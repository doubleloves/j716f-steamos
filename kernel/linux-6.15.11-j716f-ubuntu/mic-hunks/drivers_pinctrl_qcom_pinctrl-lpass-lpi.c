diff --git a/drivers/pinctrl/qcom/pinctrl-lpass-lpi.c b/drivers/pinctrl/qcom/pinctrl-lpass-lpi.c
--- a/drivers/pinctrl/qcom/pinctrl-lpass-lpi.c
+++ b/drivers/pinctrl/qcom/pinctrl-lpass-lpi.c
@@ -469,6 +469,18 @@
 		return ret;
 
 	ret = clk_bulk_prepare_enable(MAX_LPI_NUM_CLKS, pctrl->clks);
+	if (ret == -ETIMEDOUT) {
+		/*
+		 * Lenovo J716F: the LPI clocks are granted by the ADSP through
+		 * an APR vote. Early in boot the ADSP may still be starting
+		 * up, so the vote times out (-110). Failing the probe here
+		 * cascades: the VA macro needs this pin controller, the TX
+		 * macro needs the VA macro's fsgen clock, and the sound card
+		 * then reports "codec dai not found". Defer instead so the
+		 * driver core retries once the ADSP is up.
+		 */
+		return -EPROBE_DEFER;
+	}
 	if (ret)
 		return dev_err_probe(dev, ret, "Can't enable clocks\n");
 

