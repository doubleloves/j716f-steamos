diff --git a/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder.c b/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder.c
--- a/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder.c
+++ b/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder.c
@@ -1246,7 +1246,11 @@
 			return;
 		}
 
-		phys->hw_ctl = i < num_ctl ? to_dpu_hw_ctl(hw_ctl[i]) : NULL;
+		/* Use first (and only) CTL if active CTLs are supported */
+		if (num_ctl == 1)
+			phys->hw_ctl = to_dpu_hw_ctl(hw_ctl[0]);
+		else
+			phys->hw_ctl = i < num_ctl ? to_dpu_hw_ctl(hw_ctl[i]) : NULL;
 		if (!phys->hw_ctl) {
 			DPU_ERROR_ENC(dpu_enc,
 				"no ctl block assigned at idx: %d\n", i);

