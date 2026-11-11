diff --git a/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder_phys_vid.c b/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder_phys_vid.c
--- a/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder_phys_vid.c
+++ b/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder_phys_vid.c
@@ -302,6 +302,8 @@
 	if (phys_enc->hw_cdm)
 		intf_cfg.cdm = phys_enc->hw_cdm->idx;
 	intf_cfg.intf = phys_enc->hw_intf->idx;
+	if (phys_enc->split_role == ENC_ROLE_MASTER)
+		intf_cfg.intf_master = phys_enc->hw_intf->idx;
 	intf_cfg.intf_mode_sel = DPU_CTL_MODE_SEL_VID;
 	intf_cfg.stream_sel = 0; /* Don't care value for video mode */
 	intf_cfg.mode_3d = dpu_encoder_helper_get_3d_blend_mode(phys_enc);

