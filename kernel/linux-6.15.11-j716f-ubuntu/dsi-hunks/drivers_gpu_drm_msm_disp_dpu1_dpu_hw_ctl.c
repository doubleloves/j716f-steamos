diff --git a/drivers/gpu/drm/msm/disp/dpu1/dpu_hw_ctl.c b/drivers/gpu/drm/msm/disp/dpu1/dpu_hw_ctl.c
--- a/drivers/gpu/drm/msm/disp/dpu1/dpu_hw_ctl.c
+++ b/drivers/gpu/drm/msm/disp/dpu1/dpu_hw_ctl.c
@@ -563,6 +563,7 @@
 	u32 wb_active = 0;
 	u32 cwb_active = 0;
 	u32 mode_sel = 0;
+	u32 merge_3d_active = 0;
 
 	/* CTL_TOP[31:28] carries group_id to collate CTL paths
 	 * per VM. Explicitly disable it until VM support is
@@ -578,6 +579,7 @@
 	wb_active = DPU_REG_READ(c, CTL_WB_ACTIVE);
 	cwb_active = DPU_REG_READ(c, CTL_CWB_ACTIVE);
 	dsc_active = DPU_REG_READ(c, CTL_DSC_ACTIVE);
+	merge_3d_active = DPU_REG_READ(c, CTL_MERGE_3D_ACTIVE);
 
 	if (cfg->intf)
 		intf_active |= BIT(cfg->intf - INTF_0);
@@ -590,16 +592,19 @@
 
 	if (cfg->dsc)
 		dsc_active |= cfg->dsc;
+
+	if (cfg->merge_3d)
+		merge_3d_active |= BIT(cfg->merge_3d - MERGE_3D_0);
 
 	DPU_REG_WRITE(c, CTL_TOP, mode_sel);
 	DPU_REG_WRITE(c, CTL_INTF_ACTIVE, intf_active);
 	DPU_REG_WRITE(c, CTL_WB_ACTIVE, wb_active);
 	DPU_REG_WRITE(c, CTL_CWB_ACTIVE, cwb_active);
 	DPU_REG_WRITE(c, CTL_DSC_ACTIVE, dsc_active);
-
-	if (cfg->merge_3d)
-		DPU_REG_WRITE(c, CTL_MERGE_3D_ACTIVE,
-			      BIT(cfg->merge_3d - MERGE_3D_0));
+	DPU_REG_WRITE(c, CTL_MERGE_3D_ACTIVE, merge_3d_active);
+
+	if (cfg->intf_master)
+		DPU_REG_WRITE(c, CTL_INTF_MASTER, BIT(cfg->intf_master - INTF_0));
 
 	if (cfg->cdm)
 		DPU_REG_WRITE(c, CTL_CDM_ACTIVE, cfg->cdm);
@@ -643,6 +648,7 @@
 {
 	struct dpu_hw_blk_reg_map *c = &ctx->hw;
 	u32 intf_active = 0;
+	u32 intf_master = 0;
 	u32 wb_active = 0;
 	u32 cwb_active = 0;
 	u32 merge3d_active = 0;
@@ -670,6 +676,14 @@
 		intf_active = DPU_REG_READ(c, CTL_INTF_ACTIVE);
 		intf_active &= ~BIT(cfg->intf - INTF_0);
 		DPU_REG_WRITE(c, CTL_INTF_ACTIVE, intf_active);
+
+		intf_master = DPU_REG_READ(c, CTL_INTF_MASTER);
+
+		/* Unset this intf as master, if it is the current master */
+		if (intf_master == BIT(cfg->intf - INTF_0)) {
+			DPU_DEBUG_DRIVER("Unsetting INTF_%d master\n", cfg->intf - INTF_0);
+			DPU_REG_WRITE(c, CTL_INTF_MASTER, 0);
+		}
 	}
 
 	if (cfg->cwb) {

