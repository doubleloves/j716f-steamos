diff --git a/drivers/gpu/drm/msm/dsi/dsi_host.c b/drivers/gpu/drm/msm/dsi/dsi_host.c
--- a/drivers/gpu/drm/msm/dsi/dsi_host.c
+++ b/drivers/gpu/drm/msm/dsi/dsi_host.c
@@ -5,6 +5,7 @@
 
 #include <linux/clk.h>
 #include <linux/delay.h>
+#include <linux/debugfs.h>
 #include <linux/dma-mapping.h>
 #include <linux/err.h>
 #include <linux/interrupt.h>
@@ -109,6 +110,7 @@
 
 	void __iomem *ctrl_base;
 	phys_addr_t ctrl_size;
+
 	struct regulator_bulk_data *supplies;
 
 	int num_bus_clks;
@@ -340,6 +342,7 @@
 
 int dsi_link_clk_set_rate_6g(struct msm_dsi_host *msm_host)
 {
+	unsigned long actual_rate;
 	int ret;
 
 	DBG("Set clk rates: pclk=%lu, byteclk=%lu",
@@ -365,6 +368,27 @@
 			       __func__, ret);
 			return ret;
 		}
+	}
+
+	/*
+	 * Reuse the rates actually implemented by the clock tree. In particular,
+	 * the 7nm PHY's round_rate() can return the ideal request unchanged even
+	 * though recalc_rate() truncates the fractional PLL result. Reissuing
+	 * that ideal byte rate on every DCS command then reprograms the shared
+	 * PLL during scanout (e.g. 150037875 requested vs 150037866 implemented).
+	 * Readback makes subsequent rate requests idempotent without bypassing
+	 * the OPP vote above, which must still be restored for every transfer.
+	 */
+	actual_rate = clk_get_rate(msm_host->byte_clk);
+	if (actual_rate)
+		msm_host->byte_clk_rate = actual_rate;
+	actual_rate = clk_get_rate(msm_host->pixel_clk);
+	if (actual_rate)
+		msm_host->pixel_clk_rate = actual_rate;
+	if (msm_host->byte_intf_clk) {
+		actual_rate = clk_get_rate(msm_host->byte_intf_clk);
+		if (actual_rate)
+			msm_host->byte_intf_clk_rate = actual_rate;
 	}
 
 	return 0;
@@ -830,6 +854,7 @@
 
 	dsi_write(msm_host, REG_DSI_CTRL, data);
 
+
 	if (msm_host->cphy_mode)
 		dsi_write(msm_host, REG_DSI_CPHY_MODE_CTRL, BIT(0));
 }
@@ -981,6 +1006,7 @@
 		if (msm_host->dsc)
 			dsi_update_dsc_timing(msm_host, false);
 
+
 		dsi_write(msm_host, REG_DSI_ACTIVE_H,
 			DSI_ACTIVE_H_START(ha_start) |
 			DSI_ACTIVE_H_END(ha_end));
@@ -1031,8 +1057,29 @@
 static void dsi_sw_reset(struct msm_dsi_host *msm_host)
 {
 	u32 ctrl;
+	bool reenable;
 
 	ctrl = dsi_read(msm_host, REG_DSI_CTRL);
+
+	/*
+	 * The controller must be re-enabled after the reset whenever the driver
+	 * still considers it logically on. Deciding that from the CTRL readback
+	 * alone is not enough: a reset entered while CTRL already reads 0 leaves
+	 * the controller permanently disabled, because both the disable and the
+	 * restore below are skipped.
+	 *
+	 * That is exactly what happens on the Lenovo TB-J716F: the slave DSI
+	 * link raises MDP_FIFO_UNDERFLOW, dsi_err_worker() resets it, and the
+	 * link never comes back -- the right half of the 2560x1600 panel stays
+	 * black while the master link keeps streaming (verified by reading the
+	 * registers back: master CTRL=0x000001f3, slave CTRL=0x00000000 with
+	 * mdp_fifo_underflow=1).
+	 *
+	 * So combine the readback with msm_host->enabled, and remember whether
+	 * we disabled it ourselves so the restore always pairs with the
+	 * disable.
+	 */
+	reenable = !!(ctrl & DSI_CTRL_ENABLE) || msm_host->enabled;
 
 	if (ctrl & DSI_CTRL_ENABLE) {
 		dsi_write(msm_host, REG_DSI_CTRL, ctrl & ~DSI_CTRL_ENABLE);
@@ -1052,10 +1099,24 @@
 	dsi_write(msm_host, REG_DSI_RESET, 0);
 	wmb(); /* controller out of reset */
 
-	if (ctrl & DSI_CTRL_ENABLE) {
-		dsi_write(msm_host, REG_DSI_CTRL, ctrl);
+	if (reenable) {
+		u32 restore = ctrl;
+
+		/*
+		 * If CTRL read 0 on entry (the failure case), rebuild the
+		 * enable state from the driver's own configuration instead of
+		 * restoring a stale zero.
+		 */
+		if (!(restore & DSI_CTRL_ENABLE)) {
+			restore = DSI_CTRL_ENABLE | DSI_CTRL_CLK_EN;
+			restore |= ((DSI_CTRL_LANE0 << msm_host->lanes) -
+				    DSI_CTRL_LANE0);
+		}
+
+		dsi_write(msm_host, REG_DSI_CTRL, restore);
 		wmb();	/* make sure dsi controller enabled again */
 	}
+
 }
 
 static void dsi_op_mode_config(struct msm_dsi_host *msm_host,
@@ -1473,8 +1534,29 @@
 	u32 status = msm_host->err_work_state;
 
 	pr_err_ratelimited("%s: status=%x\n", __func__, status);
-	if (status & DSI_ERR_STATE_MDP_FIFO_UNDERFLOW)
+	if (status & DSI_ERR_STATE_MDP_FIFO_UNDERFLOW) {
+		u32 ctrl;
+
 		dsi_sw_reset(msm_host);
+
+		/*
+		 * Safety net for the Lenovo TB-J716F: after recovering from an
+		 * underflow the controller must be running again, otherwise the
+		 * link is dead for the rest of the session (the panel half
+		 * driven by it stays black). dsi_sw_reset() restores the enable
+		 * state, but verify it here so a future regression cannot
+		 * silently leave the link off.
+		 */
+		ctrl = dsi_read(msm_host, REG_DSI_CTRL);
+		if (msm_host->enabled && !(ctrl & DSI_CTRL_ENABLE)) {
+			u32 restore = DSI_CTRL_ENABLE | DSI_CTRL_CLK_EN;
+
+			restore |= ((DSI_CTRL_LANE0 << msm_host->lanes) -
+				    DSI_CTRL_LANE0);
+			dsi_write(msm_host, REG_DSI_CTRL, restore);
+			wmb();
+		}
+	}
 
 	/* It is safe to clear here because error irq is disabled. */
 	msm_host->err_work_state = 0;
@@ -1527,13 +1609,42 @@
 
 static void dsi_fifo_status(struct msm_dsi_host *msm_host)
 {
+	/*
+	 * Only the underflow/overflow bits are errors. DSI_FIFO_STATUS also
+	 * carries status bits that are set during normal operation -- notably
+	 * DLN0_LP_FIFO_EMPTY (0x1000) and DLN0_HS_FIFO_EMPTY (0x10000), which
+	 * simply report that the low-power / high-speed FIFOs are empty -- plus
+	 * the CMD_DMA watermark bits. Testing the whole register for non-zero
+	 * therefore reports a fault on a healthy link.
+	 *
+	 * That is exactly what happens on the Lenovo TB-J716F: the slave DSI
+	 * link reads back 0x00001010 (LP_FIFO_EMPTY set), this function flags
+	 * DSI_ERR_STATE_FIFO, dsi_err_worker() resets the controller, and the
+	 * error re-arms immediately -- an endless reset loop that leaves the
+	 * half of the panel behind the slave link flickering and then dark.
+	 *
+	 * Mask down to the real error bits.
+	 */
+	const u32 err_mask = DSI_FIFO_STATUS_VIDEO_MDP_FIFO_OVERFLOW |
+			     DSI_FIFO_STATUS_VIDEO_MDP_FIFO_UNDERFLOW |
+			     DSI_FIFO_STATUS_CMD_MDP_FIFO_UNDERFLOW |
+			     DSI_FIFO_STATUS_CMD_DMA_FIFO_UNDERFLOW |
+			     DSI_FIFO_STATUS_DLN0_LP_FIFO_OVERFLOW |
+			     DSI_FIFO_STATUS_DLN0_HS_FIFO_OVERFLOW |
+			     DSI_FIFO_STATUS_DLN0_HS_FIFO_UNDERFLOW |
+			     DSI_FIFO_STATUS_DLN1_HS_FIFO_OVERFLOW |
+			     DSI_FIFO_STATUS_DLN1_HS_FIFO_UNDERFLOW |
+			     DSI_FIFO_STATUS_DLN2_HS_FIFO_OVERFLOW |
+			     DSI_FIFO_STATUS_DLN2_HS_FIFO_UNDERFLOW |
+			     DSI_FIFO_STATUS_DLN3_HS_FIFO_OVERFLOW |
+			     DSI_FIFO_STATUS_DLN3_HS_FIFO_UNDERFLOW;
 	u32 status;
 
 	status = dsi_read(msm_host, REG_DSI_FIFO_STATUS);
 
-	/* fifo underflow, overflow */
-	if (status) {
-		dsi_write(msm_host, REG_DSI_FIFO_STATUS, status);
+	if (status & err_mask) {
+		/* clear the error bits that fired */
+		dsi_write(msm_host, REG_DSI_FIFO_STATUS, status & err_mask);
 		msm_host->err_work_state |= DSI_ERR_STATE_FIFO;
 		if (status & DSI_FIFO_STATUS_CMD_MDP_FIFO_UNDERFLOW)
 			msm_host->err_work_state |=
@@ -2312,6 +2423,7 @@
 {
 	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
 
+
 	dsi_op_mode_config(msm_host,
 		!!(msm_host->mode_flags & MIPI_DSI_MODE_VIDEO), true);
 
@@ -2325,6 +2437,7 @@
 	 * }
 	 */
 	msm_host->enabled = true;
+
 	return 0;
 }
 
@@ -2403,6 +2516,7 @@
 			__func__, ret);
 		goto fail_disable_clk;
 	}
+
 
 	dsi_timing_setup(msm_host, is_bonded_dsi);
 	dsi_sw_reset(msm_host);

