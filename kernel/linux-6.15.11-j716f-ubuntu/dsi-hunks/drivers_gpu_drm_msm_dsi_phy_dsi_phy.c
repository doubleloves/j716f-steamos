diff --git a/drivers/gpu/drm/msm/dsi/phy/dsi_phy.c b/drivers/gpu/drm/msm/dsi/phy/dsi_phy.c
--- a/drivers/gpu/drm/msm/dsi/phy/dsi_phy.c
+++ b/drivers/gpu/drm/msm/dsi/phy/dsi_phy.c
@@ -786,8 +786,22 @@
 	 * which will confuse clock driver and result in wrong output rate of
 	 * link clocks. Restore PLL status if its PLL is being used as clock
 	 * source.
+	 *
+	 * The bonded slave PHY must also have its state restored. Previously,
+	 * the slave was skipped under the assumption that it runs off the
+	 * master's PLL and has no post-dividers to restore. However, this
+	 * leaves the slave's CLK_CFG0 post-dividers at their reset values
+	 * while the master's are programmed, causing asymmetry:
+	 *
+	 *   dsi0 CLK_CFG0=0x61 -> bit_clk_div 1, pix_clk_div 6
+	 *   dsi1 CLK_CFG0=0xf1 -> bit_clk_div 1, pix_clk_div 15
+	 *
+	 * dsi_pll_7nm_vco_prepare() now starts the slave's own PLL as well,
+	 * so restoring its state here gives the two links matching post-dividers.
+	 * there), so restoring its state is consistent with that and gives the
+	 * two links matching post-dividers.
 	 */
-	if (phy->usecase != MSM_DSI_PHY_SLAVE) {
+	{
 		ret = msm_dsi_phy_pll_restore_state(phy);
 		if (ret) {
 			DRM_DEV_ERROR(dev, "%s: failed to restore phy state, %d\n",

