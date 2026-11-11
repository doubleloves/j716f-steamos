diff --git a/sound/soc/codecs/lpass-tx-macro.c b/sound/soc/codecs/lpass-tx-macro.c
--- a/sound/soc/codecs/lpass-tx-macro.c
+++ b/sound/soc/codecs/lpass-tx-macro.c
@@ -5,6 +5,7 @@
 #include <linux/init.h>
 #include <linux/clk.h>
 #include <linux/io.h>
+#include <linux/of_platform.h>
 #include <linux/platform_device.h>
 #include <linux/pm_runtime.h>
 #include <linux/regmap.h>
@@ -199,6 +200,13 @@
 #define  CF_MIN_3DB_150HZ		0x2
 #define	TX_ADC_MAX	5
 #define TX_ADC_TO_DMIC(n) ((n - TX_ADC_MAX)/2)
+
+/*
+ * Lenovo J716F: the DMIC clock generators live in the VA macro block, so the
+ * TX macro has to power them explicitly (see lpass-va-macro.c).
+ */
+extern int va_macro_set_dmic_clk_early(u32 dmic, bool enable);
+extern int va_macro_enable_micbias(u32 dmic, bool enable);
 #define NUM_DECIMATORS 8
 #define TX_NUM_CLKS_MAX	5
 #define TX_MACRO_DMIC_UNMUTE_DELAY_MS	40
@@ -618,6 +626,18 @@
 
 	if (mclk_enable) {
 		if (tx->tx_mclk_users == 0) {
+			/*
+			 * Lenovo J716F: sync the cache BEFORE enabling the
+			 * clocks. regcache_sync() writes every cached register
+			 * back, and the reset defaults for FREQ_MCLK (0x090),
+			 * MCLK_CONTROL (0x000) and FS_CNT_CONTROL (0x004) are
+			 * all 0x00 - so running it after the enables turned
+			 * the TX macro MCLK straight back off and the
+			 * decimators produced digital silence.
+			 */
+			regcache_mark_dirty(regmap);
+			regcache_sync(regmap);
+
 			/* 9.6MHz MCLK, set value 0x00 if other frequency */
 			regmap_update_bits(regmap, CDC_TX_TOP_CSR_FREQ_MCLK, 0x01, 0x01);
 			regmap_update_bits(regmap, CDC_TX_CLK_RST_CTRL_MCLK_CONTROL,
@@ -626,8 +646,6 @@
 			regmap_update_bits(regmap, CDC_TX_CLK_RST_CTRL_FS_CNT_CONTROL,
 					   CDC_TX_FS_CNT_EN_MASK,
 					   CDC_TX_FS_CNT_ENABLE);
-			regcache_mark_dirty(regmap);
-			regcache_sync(regmap);
 		}
 		tx->tx_mclk_users++;
 	} else {
@@ -843,10 +861,34 @@
 	}
 
 	if (val != 0) {
-		if (widget->shift) /* MSM DMIC */
+		/*
+		 * Lenovo J716F: widget->shift is the DAPM shift of the MUX
+		 * widget, which is 4 for the "TX DMIC MUXn" widgets in this
+		 * driver - but the vendor's check used a different field and
+		 * mainline ends up taking the SWR branch (bit7 cleared) here,
+		 * so the DMIC clock was never enabled. These MUXes only ever
+		 * select digital microphones on this board, so always take the
+		 * MSM DMIC path.
+		 */
+		if (true) { /* MSM DMIC */
+			/*
+			 * Lenovo J716F: tx_macro_enable_dec() only powers the
+			 * DMIC clock when the low nibble of
+			 * CDC_TX_INP_MUX_ADC_MUXn_CFG0 (the "adc_n" field) is
+			 * >= TX_ADC_MAX (5); TX_ADC_TO_DMIC() then maps
+			 * adc_n -> dmic as (adc_n - 5) / 2. Setting the MUX to
+			 * "DMIC2" only writes the enum field at bit 4..6 and
+			 * leaves adc_n at 0, so the DMIC clock was never
+			 * enabled and capture produced no data (the DSP
+			 * signalled EOS). Program adc_n as well:
+			 * 5 + 2 * (dmic index).
+			 */
 			snd_soc_component_write_field(component, mic_sel_reg,
 						      CDC_TXn_ADC_DMIC_SEL_MASK, 1);
-		else if (tx->data->ver <= LPASS_VER_9_0_0)
+			snd_soc_component_write_field(component, e->reg,
+						      GENMASK(3, 0),
+						      TX_ADC_MAX + 2 * (val - 1));
+		} else if (tx->data->ver <= LPASS_VER_9_0_0)
 			tx_macro_update_smic_sel_v9(component, widget, tx,
 						    mic_sel_reg, val);
 		else
@@ -928,19 +970,54 @@
 
 	switch (event) {
 	case SND_SOC_DAPM_PRE_PMU:
+		/*
+		 * Lenovo J716F: upstream only runs this block for the SWR
+		 * microphone path (CFG1 bit0). The MSM DMIC path
+		 * ("TX DECn MUX" = MSM_DMIC, the mode the stock Android
+		 * recorder uses) sets that bit to 0 and therefore never
+		 * programmed the DMIC clock select, so the physical
+		 * microphone got no clock and the DSP sent EOS with no data.
+		 * Run the block whenever the decimator's CFG0 adc_n field
+		 * selects a digital microphone, regardless of which of the two
+		 * DMIC sources is selected.
+		 */
 		adc_mux_reg = CDC_TX_INP_MUX_ADC_MUXn_CFG1(decimator);
-		if (snd_soc_component_read(component, adc_mux_reg) & SWR_MIC) {
-			adc_reg = CDC_TX_INP_MUX_ADC_MUXn_CFG0(decimator);
-			adc_n = snd_soc_component_read(component, adc_reg) &
-				CDC_TX_MACRO_SWR_MIC_MUX_SEL_MASK;
-			if (adc_n >= TX_ADC_MAX) {
+		adc_reg = CDC_TX_INP_MUX_ADC_MUXn_CFG0(decimator);
+		adc_n = snd_soc_component_read(component, adc_reg) &
+			CDC_TX_MACRO_SWR_MIC_MUX_SEL_MASK;
+		if (adc_n >= TX_ADC_MAX) {
 				dmic = TX_ADC_TO_DMIC(adc_n);
 				dmic_clk_reg = CDC_TX_TOP_CSR_SWR_DMICn_CTL(dmic);
 
 				snd_soc_component_write_field(component, dmic_clk_reg,
 							CDC_TX_SWR_DMIC_CLK_SEL_MASK,
 							CDC_TX_SWR_MIC_CLK_DEFAULT);
-			}
+
+				/*
+				 * Lenovo J716F: the DMIC clock *generators*
+				 * live in the VA macro block; the TX macro
+				 * only routes the decimator input. Without
+				 * starting the VA macro's DMIC clock the
+				 * physical microphone gets no clock and the
+				 * AFE port delivers nothing.
+				 *
+				 * This runs from DAPM power-up, not from a
+				 * kcontrol write, so the APR vote inside the
+				 * VA macro helper cannot deadlock the control
+				 * path.
+				 */
+				va_macro_set_dmic_clk_early(dmic, true);
+				/*
+				 * Lenovo J716F: a MEMS microphone needs its
+				 * bias rail before it will drive the data
+				 * line. mainline's "vdd-micb" DAPM supply
+				 * widget stays Off (0 users, 0 mV) unless
+				 * something explicitly programs the
+				 * regulator, so power it here alongside the
+				 * DMIC clock - this is where the vendor's
+				 * va_macro_enable_micbias() runs too.
+				 */
+				va_macro_enable_micbias(dmic, true);
 		}
 		snd_soc_component_write_field(component, dec_cfg_reg,
 					      CDC_TXn_ADC_MODE_MASK,
@@ -950,6 +1027,19 @@
 					      CDC_TXn_PGA_MUTE_MASK, 0x1);
 		break;
 	case SND_SOC_DAPM_POST_PMU:
+		/*
+		 * Lenovo J716F: make sure the TX macro MCLK is actually
+		 * running once the decimator is powered. tx_macro_mclk_event()
+		 * enables it in its PRE_PMU, but that runs before the DAPM
+		 * register writes of this widget, and any later
+		 * regcache_sync()/write can drop MCLK_CONTROL (0x000) back to
+		 * its 0x00 default. Without the MCLK the decimator shifts out
+		 * silence.
+		 */
+		snd_soc_component_update_bits(component,
+					      CDC_TX_CLK_RST_CTRL_MCLK_CONTROL,
+					      CDC_TX_MCLK_EN_MASK,
+					      CDC_TX_MCLK_ENABLE);
 		snd_soc_component_write_field(component, tx_vol_ctl_reg,
 					     CDC_TXn_CLK_EN_MASK, 0x1);
 		if (!is_amic_enabled(component, tx, decimator)) {
@@ -1160,10 +1250,22 @@
 		return -EINVAL;
 	}
 
-	for_each_set_bit(decimator, &tx->active_ch_mask[dai->id], TX_MACRO_DEC_MAX)
+	for_each_set_bit(decimator, &tx->active_ch_mask[dai->id], TX_MACRO_DEC_MAX) {
 		snd_soc_component_update_bits(component, CDC_TXn_TX_PATH_CTL(decimator),
 					      CDC_TXn_PCM_RATE_MASK,
 					      tx_fs_rate);
+		/*
+		 * Lenovo J716F: hw_params() runs after the DAPM POST_PMU that
+		 * sets CLK_EN, and the regmap update above is a
+		 * read-modify-write of the cached value. When the cache still
+		 * held the pre-power-up value the write cleared CLK_EN again,
+		 * leaving TX PATH_CTL at 0x04 (rate only) instead of 0x24, and
+		 * the decimator produced silence. Re-assert CLK_EN here.
+		 */
+		snd_soc_component_update_bits(component, CDC_TXn_TX_PATH_CTL(decimator),
+					      CDC_TXn_CLK_EN_MASK,
+					      CDC_TXn_CLK_EN_MASK);
+	}
 	return 0;
 }
 
@@ -1199,14 +1301,26 @@
 
 	decimator = tx->active_decimator[dai->id];
 
+	/*
+	 * Lenovo J716F: the decimator clock must stay enabled while the
+	 * stream runs. Touching PATH_CTL here is a read-modify-write of the
+	 * regmap cache; if the cached value predates the DAPM power-up the
+	 * write drops CLK_EN again and the decimator goes silent. Re-assert
+	 * both bits explicitly.
+	 */
 	if (mute)
-		snd_soc_component_write_field(component,
+		snd_soc_component_update_bits(component,
 					      CDC_TXn_TX_PATH_CTL(decimator),
-					      CDC_TXn_PGA_MUTE_MASK, 0x1);
+					      CDC_TXn_PGA_MUTE_MASK |
+					      CDC_TXn_CLK_EN_MASK,
+					      CDC_TXn_PGA_MUTE_MASK |
+					      CDC_TXn_CLK_EN_MASK);
 	else
 		snd_soc_component_update_bits(component,
 					      CDC_TXn_TX_PATH_CTL(decimator),
-					      CDC_TXn_PGA_MUTE_MASK, 0x0);
+					      CDC_TXn_PGA_MUTE_MASK |
+					      CDC_TXn_CLK_EN_MASK,
+					      CDC_TXn_CLK_EN_MASK);
 
 	return 0;
 }
@@ -2248,6 +2362,7 @@
 	void __iomem *base;
 	int ret, reg;
 
+
 	tx = devm_kzalloc(dev, sizeof(*tx), GFP_KERNEL);
 	if (!tx)
 		return -ENOMEM;
@@ -2319,23 +2434,39 @@
 	clk_set_rate(tx->mclk, MCLK_FREQ);
 	clk_set_rate(tx->npl, MCLK_FREQ);
 
+	/*
+	 * Lenovo J716F: see the matching comment in lpass-va-macro.c - the
+	 * LPASS hardware-vote clocks are granted by the ADSP, which can still
+	 * be starting up during early boot. Time out into a probe deferral
+	 * instead of failing the probe outright.
+	 */
 	ret = clk_prepare_enable(tx->macro);
+	if (ret == -ETIMEDOUT)
+		ret = -EPROBE_DEFER;
 	if (ret)
 		goto err;
 
 	ret = clk_prepare_enable(tx->dcodec);
+	if (ret == -ETIMEDOUT)
+		ret = -EPROBE_DEFER;
 	if (ret)
 		goto err_dcodec;
 
 	ret = clk_prepare_enable(tx->mclk);
+	if (ret == -ETIMEDOUT)
+		ret = -EPROBE_DEFER;
 	if (ret)
 		goto err_mclk;
 
 	ret = clk_prepare_enable(tx->npl);
+	if (ret == -ETIMEDOUT)
+		ret = -EPROBE_DEFER;
 	if (ret)
 		goto err_npl;
 
 	ret = clk_prepare_enable(tx->fsgen);
+	if (ret == -ETIMEDOUT)
+		ret = -EPROBE_DEFER;
 	if (ret)
 		goto err_fsgen;
 
@@ -2356,8 +2487,9 @@
 	ret = devm_snd_soc_register_component(dev, &tx_macro_component_drv,
 					      tx_macro_dai,
 					      ARRAY_SIZE(tx_macro_dai));
-	if (ret)
+	if (ret) {
 		goto err_clkout;
+	}
 
 	pm_runtime_set_autosuspend_delay(dev, 3000);
 	pm_runtime_use_autosuspend(dev);

