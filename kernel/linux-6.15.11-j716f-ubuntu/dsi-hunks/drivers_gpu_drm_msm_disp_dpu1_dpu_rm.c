diff --git a/drivers/gpu/drm/msm/disp/dpu1/dpu_rm.c b/drivers/gpu/drm/msm/disp/dpu1/dpu_rm.c
--- a/drivers/gpu/drm/msm/disp/dpu1/dpu_rm.c
+++ b/drivers/gpu/drm/msm/disp/dpu1/dpu_rm.c
@@ -52,6 +52,8 @@
 
 	/* Clear, setup lists */
 	memset(rm, 0, sizeof(*rm));
+
+	rm->has_legacy_ctls = (cat->mdss_ver->core_major_ver < 5);
 
 	/* Interrogate HW catalog and create tracking items for hw blocks */
 	for (i = 0; i < cat->mixer_count; i++) {
@@ -434,20 +436,19 @@
 	int i = 0, j, num_ctls;
 	bool needs_split_display;
 
-	/*
-	 * For non-CWB mode, each hw_intf needs its own hw_ctl to program its
-	 * control path.
-	 *
-	 * Hardcode num_ctls to 1 if CWB is enabled because in CWB, both the
-	 * writeback and real-time encoders must be driven by the same control
-	 * path
-	 */
-	if (top->cwb_enabled)
+	if (rm->has_legacy_ctls) {
+		/*
+		 * TODO: check if there is a need for special handling if
+		 * DPU < 5.0 get CWB support.
+		 */
+		num_ctls = top->num_intf;
+
+		needs_split_display = _dpu_rm_needs_split_display(top);
+	} else {
+		/* use single CTL */
 		num_ctls = 1;
-	else
-		num_ctls = top->num_intf;
-
-	needs_split_display = _dpu_rm_needs_split_display(top);
+		needs_split_display = false;
+	}
 
 	for (j = 0; j < ARRAY_SIZE(rm->ctl_blks); j++) {
 		const struct dpu_hw_ctl *ctl;

