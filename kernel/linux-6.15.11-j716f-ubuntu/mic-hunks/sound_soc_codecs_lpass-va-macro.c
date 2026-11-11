diff --git a/sound/soc/codecs/lpass-va-macro.c b/sound/soc/codecs/lpass-va-macro.c
--- a/sound/soc/codecs/lpass-va-macro.c
+++ b/sound/soc/codecs/lpass-va-macro.c
@@ -17,6 +17,11 @@
 #include <sound/tlv.h>
 
 #include "lpass-macro-common.h"
+
+/* Optional register inspection must not add MMIO reads to normal capture. */
+static bool register_debug;
+module_param(register_debug, bool, 0600);
+MODULE_PARM_DESC(register_debug, "Inspect DMIC registers (disabled by default)");
 
 /* VA macro registers */
 #define CDC_VA_CLK_RST_CTRL_MCLK_CONTROL	(0x0000)
@@ -37,6 +42,32 @@
 #define CDC_VA_TOP_CSR_DMIC3_CTL		(0x0090)
 #define CDC_VA_DMIC_EN_MASK			BIT(0)
 #define CDC_VA_DMIC_ENABLE			BIT(0)
+/*
+ * Lenovo J716F: DMICn_CTL bit 7 is a second, mandatory enable.
+ *
+ * Recovered from the vendor va_macro_dlkm.ko. Its DMIC enable path does,
+ * in this order (0x29f0..0x2a24):
+ *
+ *   mov w1, w22          ; w22 = DMICn_CTL (0x84/0x88/0x8c/0x90)
+ *   mov w2, #0x80        ; mask  = BIT(7)
+ *   mov w3, #0x80        ; value = BIT(7)      <- SET bit 7
+ *   bl  regmap_update_bits
+ *   ...
+ *   mov w1, #0x3094      ; global bolero register
+ *   mov w2, #0x80        ; mask  = BIT(7)
+ *   mov w3, wzr          ; value = 0           <- CLEAR bit 7
+ *   bl  regmap_update_bits
+ *
+ * mainline only ever touched bit 0 of DMICn_CTL, which left the
+ * generator half-enabled: the pad was driven (OE=1, static high) but no
+ * clock ever appeared on it, and every capture returned silence.
+ *
+ * 0x3094 lives in the bolero codec block, outside this macro's 0x1000
+ * register window, so mainline cannot reach it at all. Bit 7 of
+ * DMICn_CTL is the part of that handshake we *can* do.
+ */
+#define CDC_VA_DMIC_EN2_MASK			BIT(7)
+#define CDC_VA_DMIC_EN2_ENABLE			BIT(7)
 #define CDC_VA_DMIC_CLK_SEL_MASK		GENMASK(3, 1)
 #define CDC_VA_DMIC_CLK_SEL_SHFT		1
 #define CDC_VA_DMIC_CLK_SEL_DIV0		0x0
@@ -206,12 +237,41 @@
 	int dec_mode[VA_MACRO_NUM_DECIMATORS];
 	struct regmap *regmap;
 	struct clk *mclk;
+	/*
+	 * Lenovo J716F: the VA macro's DMIC clock generators run off the
+	 * 2x core MCLK, which is a separate LPASS clock ID
+	 * (LPASS_CLK_ID_VA_CORE_2X_MCLK). mainline only ever gets and enables
+	 * "mclk" (LPASS_CLK_ID_VA_CORE_MCLK), so the 2x clock stayed at
+	 * enable_count 0 and the DMIC clock generators produced nothing -
+	 * which is exactly what probing the LPI pads showed (0 transitions
+	 * on every DMIC pin no matter which register bits were set).
+	 */
+	struct clk *mclk2x;
 	struct clk *npl;
 	struct clk *macro;
 	struct clk *dcodec;
 	struct clk *fsgen;
 	struct clk_hw hw;
 	struct lpass_macro *pds;
+	/*
+	 * Lenovo J716F: the MEMS microphones only produce data while their
+	 * bias rail is powered. mainline models "vdd-micb" purely as a
+	 * SND_SOC_DAPM_REGULATOR_SUPPLY widget, which the DAPM core only
+	 * switches on when a route references it - and even then it never
+	 * sets a voltage or current limit. On this board that left the rail
+	 * at 0 mV ("3370000.codec-vdd-micb  0 users  0mV") and every capture
+	 * returned digital silence.
+	 *
+	 * The vendor driver instead grabs the supply directly and drives it
+	 * with regulator_set_voltage() / regulator_set_current_limit() /
+	 * regulator_enable() from va_macro_enable_micbias(), with the
+	 * values taken from qcom,va-vdd-micb-voltage / -current. Do the
+	 * same here.
+	 */
+	struct regulator *micb_reg;
+	unsigned int micb_voltage;
+	unsigned int micb_current;
+	bool micb_enabled;
 
 	s32 dmic_0_1_clk_cnt;
 	s32 dmic_2_3_clk_cnt;
@@ -714,16 +774,270 @@
 	return 0;
 }
 
+/*
+ * Lenovo J716F: the digital-microphone clock generators (and the MCLK that
+ * feeds them) live in the VA macro block, not in the TX macro. The TX macro's
+ * DMIC capture path therefore has to power this block explicitly, otherwise
+ * the DMIC clock never starts and the DSP reports EOS with no data.
+ *
+ * Called by lpass-tx-macro.c with the DAPM component of the VA macro.
+ */
+/* Lenovo J716F: the TX macro needs to start our DMIC clocks. */
+static struct snd_soc_component *va_macro_component;
+static struct device *va_macro_dev;
+
+struct snd_soc_component *va_macro_get_component(void)
+{
+	/*
+	 * The TX macro's DAPM events can run before our ASoC component probe
+	 * (both macros are part of the same card). Fall back to looking the
+	 * component up from the platform device we saved during probe.
+	 */
+	if (!va_macro_component && va_macro_dev)
+		va_macro_component =
+			snd_soc_lookup_component_nolocked(va_macro_dev,
+							  "va_macro");
+
+	return va_macro_component;
+}
+EXPORT_SYMBOL_GPL(va_macro_get_component);
+
+
+/*
+ * Lenovo J716F: power the microphone bias rail.
+ *
+ * The vendor's va_macro_enable_micbias() does exactly this and its strings
+ * give the full sequence:
+ *
+ *   "%s:regulator not provided in dtsi"
+ *   "%s: Setting voltage failed, err = %d"
+ *   "%s: Setting current failed, err = %d"
+ *   "%s: regulator enable failed, err = %d"
+ *   "%s: regulator disable failed, err = %d"
+ *
+ * The values come from qcom,va-vdd-micb-voltage / -current. Without this
+ * the rail sits at 0 mV and the microphones cannot output anything, no
+ * matter how the DMIC clocks and decimators are programmed.
+ */
+int va_macro_enable_micbias(u32 dmic, bool enable)
+{
+	struct va_macro *va = va_macro_dev ? dev_get_drvdata(va_macro_dev) : NULL;
+	int ret;
+
+	if (!va || !va->micb_reg)
+		return -ENODEV;
+
+	if (enable) {
+		if (va->micb_enabled)
+			return 0;
+
+		if (va->micb_voltage) {
+			ret = regulator_set_voltage(va->micb_reg,
+						    va->micb_voltage,
+						    va->micb_voltage);
+			if (ret)
+				dev_err(va->dev,
+					"J716F-VA: micb set voltage %u failed %d\n",
+					va->micb_voltage, ret);
+		}
+		if (va->micb_current) {
+			ret = regulator_set_current_limit(va->micb_reg,
+							  va->micb_current,
+							  va->micb_current);
+			if (ret)
+				dev_err(va->dev,
+					"J716F-VA: micb set current %u failed %d\n",
+					va->micb_current, ret);
+		}
+		ret = regulator_enable(va->micb_reg);
+		if (ret) {
+			dev_err(va->dev, "J716F-VA: micb enable failed %d\n", ret);
+			return ret;
+		}
+		va->micb_enabled = true;
+		dev_dbg(va->dev, "J716F-VA: micbias on (%u uV, %u uA)\n",
+			 va->micb_voltage, va->micb_current);
+	} else {
+		if (!va->micb_enabled)
+			return 0;
+		ret = regulator_disable(va->micb_reg);
+		if (ret)
+			dev_err(va->dev, "J716F-VA: micb disable failed %d\n", ret);
+		va->micb_enabled = false;
+		dev_dbg(va->dev, "J716F-VA: micbias off\n");
+	}
+
+	return 0;
+}
+EXPORT_SYMBOL_GPL(va_macro_enable_micbias);
+
+/*
+ * Lenovo J716F: the TX macro's DAPM power-up path needs the VA macro's DMIC
+ * clock generators running, but it can run before this driver's ASoC
+ * component is registered, so this helper works purely on the platform
+ * device's regmap (available since va_macro_probe).
+ */
+int va_macro_set_dmic_clk_early(u32 dmic, bool enable)
+{
+	struct va_macro *va = va_macro_dev ? dev_get_drvdata(va_macro_dev) : NULL;
+	u16 dmic_clk_reg;
+	int ret;
+
+	if (!va || !va->regmap)
+		return -ENODEV;
+
+	/* DMIC0/1 -> 0x84, DMIC2/3 -> 0x88, DMIC4/5 -> 0x8c, ... */
+	dmic_clk_reg = CDC_VA_TOP_CSR_DMIC0_CTL + (dmic / 2) * 4;
+
+	if (enable) {
+		/*
+		 * Lenovo J716F: va_macro_mclk_enable() only touches the
+		 * macro's register block; the LPASS VA_CORE_MCLK itself is
+		 * prepared/enabled by the DAPM path only when the macro acts
+		 * as a SoundWire master. This board wires the DMICs directly,
+		 * so without an explicit enable here VA_CORE_MCLK stays at
+		 * enable_count 0 and the DMIC clock never runs at its
+		 * programmed rate - capture then delivered data at roughly
+		 * 1/8 of the requested 48 kHz.
+		 */
+		ret = clk_prepare_enable(va->mclk);
+		if (ret) {
+			dev_err(va->dev, "mclk enable failed %d\n", ret);
+			return ret;
+		}
+		ret = clk_set_rate(va->mclk, 2 * VA_MACRO_MCLK_FREQ);
+		if (ret)
+			dev_warn(va->dev, "mclk set_rate failed %d\n", ret);
+
+		/*
+		 * Lenovo J716F: enable the 2x core MCLK as well. It is a
+		 * distinct LPASS clock ID and stays at enable_count 0 unless
+		 * something asks for it, and the DMIC clock generators do not
+		 * run without it.
+		 */
+		if (va->mclk2x) {
+			ret = clk_set_rate(va->mclk2x, 2 * VA_MACRO_MCLK_FREQ);
+			if (ret)
+				dev_warn(va->dev,
+					 "mclk2x set_rate failed %d\n", ret);
+			ret = clk_prepare_enable(va->mclk2x);
+			if (ret)
+				dev_err(va->dev,
+					"mclk2x enable failed %d\n", ret);
+			else
+				dev_dbg(va->dev,
+					 "J716F-VA: mclk2x enabled at %lu Hz\n",
+					 clk_get_rate(va->mclk2x));
+		}
+
+		/*
+		 * va_macro_mclk_enable() ends with regcache_sync_region(),
+		 * which writes the cached value of CDC_VA_TOP_CSR_DMIC_CFG
+		 * back - and its reset default is 0x80 (RESET_ALL_DMICS
+		 * asserted). Release the reset *after* that sync so the DMIC
+		 * clock generators actually start.
+		 */
+		va_macro_mclk_enable(va, true);
+
+		/*
+		 * Order matters, and the earlier version of this helper got
+		 * it wrong.
+		 *
+		 * va_macro_mclk_enable() calls regcache_sync_region(0, VA_MAX)
+		 * *after* va_clk_rsc_fs_gen_request() set the MCLK/FS bits, so
+		 * the sync can write stale cache entries back over them. It
+		 * also means anything written before it is overwritten.
+		 *
+		 * So: release the DMIC reset and program the divider/ENABLE
+		 * *after* the sync, and then re-assert the clock-resource
+		 * bits once more so they are definitely the last thing that
+		 * hits the hardware. A DMIC pad with the register bit set but
+		 * no FS/MCLK request simply sits idle - which is exactly what
+		 * probing the pad showed (0 transitions on both clk and data).
+		 */
+		regmap_update_bits(va->regmap, CDC_VA_TOP_CSR_DMIC_CFG,
+				   CDC_VA_RESET_ALL_DMICS_MASK,
+				   CDC_VA_RESET_ALL_DMICS_DISABLE);
+		/*
+		 * Lenovo J716F: TOP_CFG0 bit 0 is the global DMIC enable.
+		 *
+		 * The per-generator enable in DMICn_CTL (bit 0) is not enough:
+		 * with it set but TOP_CFG0 bit 0 clear the pads stay silent.
+		 * Measured with the VA macro up and a capture running:
+		 *
+		 *   TOP_CFG0   = 0x00000002   bit0 clear  <- no DMIC clock
+		 *   DMIC23_CTL = 0x00000005   bit0 set
+		 *   DMIC_CFG   = 0x00000000   reset released
+		 *
+		 * and all six LPI DMIC pads showed 0 transitions. TOP_CFG0 bit 1
+		 * (FS broadcast) was already set by va_macro_mclk_enable(); bit 0
+		 * was simply never touched anywhere in this driver.
+		 */
+		regmap_update_bits(va->regmap, CDC_VA_TOP_CSR_TOP_CFG0,
+				   CDC_VA_DMIC_EN_MASK, CDC_VA_DMIC_ENABLE);
+		regmap_update_bits(va->regmap, dmic_clk_reg,
+				   CDC_VA_DMIC_CLK_SEL_MASK,
+				   va->dmic_clk_div << CDC_VA_DMIC_CLK_SEL_SHFT);
+		regmap_update_bits(va->regmap, dmic_clk_reg,
+				   CDC_VA_DMIC_EN_MASK,
+				   CDC_VA_DMIC_ENABLE);
+		/*
+		 * Second enable, see CDC_VA_DMIC_EN2_MASK above: the vendor
+		 * driver sets bit 7 of the same register as part of the same
+		 * handshake. Without it the pad is driven but never toggles.
+		 */
+		regmap_update_bits(va->regmap, dmic_clk_reg,
+				   CDC_VA_DMIC_EN2_MASK,
+				   CDC_VA_DMIC_EN2_ENABLE);
+
+		/* make sure the clock resource bits survive any cache sync */
+		va_clk_rsc_fs_gen_request(va, true);
+		if (READ_ONCE(register_debug)) {
+			unsigned int v080 = 0, v084 = 0, v094 = 0;
+
+			regmap_read(va->regmap, CDC_VA_TOP_CSR_TOP_CFG0, &v080);
+			regmap_read(va->regmap, dmic_clk_reg, &v084);
+			regmap_read(va->regmap, CDC_VA_TOP_CSR_DMIC_CFG, &v094);
+			dev_info(va->dev,
+				 "J716F-VA: dmic%d reg=%#x top_cfg0=%#x dmic_ctl=%#x dmic_cfg=%#x div=%u\n",
+				 dmic, dmic_clk_reg, v080, v084, v094,
+				 va->dmic_clk_div);
+		}
+	} else {
+		regmap_update_bits(va->regmap, dmic_clk_reg,
+				   CDC_VA_DMIC_EN_MASK, 0);
+	}
+
+	return 0;
+}
+EXPORT_SYMBOL_GPL(va_macro_set_dmic_clk_early);
+
 static int va_macro_enable_dmic(struct snd_soc_dapm_widget *w,
 				struct snd_kcontrol *kcontrol, int event)
 {
 	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
 	unsigned int dmic = w->shift;
 
+	pr_debug("J716F-VA: enable_dmic dmic=%u event=%d\n", dmic, event);
+
 	switch (event) {
-	case SND_SOC_DAPM_PRE_PMU:
-		va_dmic_clk_enable(comp, dmic, true);
-		break;
+	case SND_SOC_DAPM_PRE_PMU: {
+		int __rc = va_dmic_clk_enable(comp, dmic, true);
+
+		if (__rc)
+			dev_warn(comp->dev, "J716F-VA: clk_enable failed %d\n", __rc);
+		if (READ_ONCE(register_debug)) {
+			unsigned int __ctl, __cfg;
+
+			__cfg = snd_soc_component_read(comp, CDC_VA_TOP_CSR_DMIC_CFG);
+			__ctl = snd_soc_component_read(comp,
+				CDC_VA_TOP_CSR_DMIC0_CTL + (dmic / 2) * 4);
+			pr_info("J716F-VA: clk_enable rc=%d DMIC_CFG=%#x CTL(0x%x)=%#x\n",
+				__rc, __cfg, CDC_VA_TOP_CSR_DMIC0_CTL + (dmic / 2) * 4,
+				__ctl);
+		}
+		break;
+	}
 	case SND_SOC_DAPM_POST_PMD:
 		va_dmic_clk_enable(comp, dmic, false);
 		break;
@@ -1324,6 +1638,11 @@
 
 	snd_soc_component_init_regmap(component, va->regmap);
 
+	/* Lenovo J716F: publish ourselves so the TX macro can start our
+	 * DMIC clock generators (see va_macro_set_dmic_clk()).
+	 */
+	va_macro_component = component;
+
 	return 0;
 }
 
@@ -1523,6 +1842,20 @@
 	if (IS_ERR(va->mclk))
 		return dev_err_probe(dev, PTR_ERR(va->mclk), "unable to get mclk clock\n");
 
+	/*
+	 * Lenovo J716F: optional 2x core MCLK. The DMIC clock generators in
+	 * this macro need it; mainline never asks for it, which is why the
+	 * DMIC pads stayed completely idle.
+	 */
+	va->mclk2x = devm_clk_get_optional(dev, "mclk2x");
+	if (IS_ERR(va->mclk2x)) {
+		dev_info(dev, "J716F-VA: no mclk2x clock (%ld)\n",
+			 PTR_ERR(va->mclk2x));
+		va->mclk2x = NULL;
+	} else if (va->mclk2x) {
+		dev_dbg(dev, "J716F-VA: got mclk2x clock\n");
+	}
+
 	va->pds = lpass_macro_pds_init(dev);
 	if (IS_ERR(va->pds))
 		return PTR_ERR(va->pds);
@@ -1540,6 +1873,38 @@
 		}
 	}
 
+	/*
+	 * Lenovo J716F: microphone bias rail.
+	 *
+	 * The vendor driver reads qcom,va-vdd-micb-voltage / -current and
+	 * drives the supply itself; mainline only has a DAPM supply widget
+	 * that never programs a voltage, which left the rail at 0 mV and the
+	 * microphones silent. Accept both the vendor property names and the
+	 * generic ones so either style of device tree works.
+	 */
+	va->micb_reg = devm_regulator_get_optional(dev, "va-vdd-micb");
+	if (IS_ERR(va->micb_reg)) {
+		if (PTR_ERR(va->micb_reg) == -ENODEV) {
+			va->micb_reg = NULL;
+		} else {
+			dev_info(dev, "J716F-VA: no va-vdd-micb supply (%ld)\n",
+				 PTR_ERR(va->micb_reg));
+			va->micb_reg = NULL;
+		}
+	}
+	if (va->micb_reg) {
+		if (!of_property_read_u32(dev->of_node,
+					  "qcom,va-vdd-micb-voltage",
+					  &va->micb_voltage))
+			dev_dbg(dev, "J716F-VA: micb voltage %u uV\n",
+				 va->micb_voltage);
+		if (!of_property_read_u32(dev->of_node,
+					  "qcom,va-vdd-micb-current",
+					  &va->micb_current))
+			dev_dbg(dev, "J716F-VA: micb current %u uA\n",
+				 va->micb_current);
+	}
+
 	base = devm_platform_ioremap_resource(pdev, 0);
 	if (IS_ERR(base)) {
 		ret = PTR_ERR(base);
@@ -1553,6 +1918,9 @@
 	}
 
 	dev_set_drvdata(dev, va);
+
+	/* Lenovo J716F: let the TX macro reach our DMIC clock generators. */
+	va_macro_dev = dev;
 
 	data = of_device_get_match_data(dev);
 	va->has_swr_master = data->has_swr_master;
@@ -1571,20 +1939,37 @@
 		clk_set_rate(va->npl, 2 * VA_MACRO_MCLK_FREQ);
 	}
 
+	/*
+	 * Lenovo J716F: the LPASS hardware-vote clocks (macro/dcodec) are
+	 * granted by an APR command to the ADSP. Early in boot the ADSP can
+	 * still be starting up, in which case the vote times out (-110) and
+	 * the whole probe fails - after which no fsgen clock exists, the TX
+	 * macro defers past its timeout and the sound card never registers.
+	 * Turn timeouts into -EPROBE_DEFER so the driver core retries until
+	 * the ADSP is actually ready.
+	 */
 	ret = clk_prepare_enable(va->macro);
+	if (ret == -ETIMEDOUT)
+		ret = -EPROBE_DEFER;
 	if (ret)
 		goto err;
 
 	ret = clk_prepare_enable(va->dcodec);
+	if (ret == -ETIMEDOUT)
+		ret = -EPROBE_DEFER;
 	if (ret)
 		goto err_dcodec;
 
 	ret = clk_prepare_enable(va->mclk);
+	if (ret == -ETIMEDOUT)
+		ret = -EPROBE_DEFER;
 	if (ret)
 		goto err_mclk;
 
 	if (va->has_npl_clk) {
 		ret = clk_prepare_enable(va->npl);
+		if (ret == -ETIMEDOUT)
+			return -EPROBE_DEFER;
 		if (ret)
 			goto err_npl;
 	}

