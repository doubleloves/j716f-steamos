diff --git a/sound/soc/qcom/qdsp6/q6afe.c b/sound/soc/qcom/qdsp6/q6afe.c
--- a/sound/soc/qcom/qdsp6/q6afe.c
+++ b/sound/soc/qcom/qdsp6/q6afe.c
@@ -890,6 +890,7 @@
 		case AFE_PORT_CMD_DEVICE_STOP:
 		case AFE_PORT_CMD_DEVICE_START:
 		case AFE_SVC_CMD_SET_PARAM:
+		case 0x000100FA: /* AFE_PORT_CMD_SET_PARAM_V3 (vendor) */
 			port = q6afe_find_port(afe, hdr->token);
 			if (port) {
 				port->result = *res;
@@ -1472,8 +1473,19 @@
 	dma_cfg->bit_width = cfg->bit_width;
 	dma_cfg->data_format = cfg->data_format;
 	dma_cfg->num_channels = cfg->num_channels;
-	if (!cfg->active_channels_mask)
-		dma_cfg->active_channels_mask = (1 << cfg->num_channels) - 1;
+	/*
+	 * Lenovo J716F: the original code only assigned the computed default
+	 * when cfg->active_channels_mask was 0 and never copied a non-zero
+	 * mask, so setting an explicit channel map via
+	 * q6dma_set_channel_map() left the port with 0 active channels and
+	 * the ADSP rejected the config. Copy the mask either way.
+	 */
+	dma_cfg->active_channels_mask = cfg->active_channels_mask ?
+		cfg->active_channels_mask : (1 << cfg->num_channels) - 1;
+
+	pr_debug("J716F-AFE: dma_cfg rate=%u width=%u fmt=%u ch=%u mask=%#x\n",
+		dma_cfg->sample_rate, dma_cfg->bit_width, dma_cfg->data_format,
+		dma_cfg->num_channels, dma_cfg->active_channels_mask);
 }
 EXPORT_SYMBOL_GPL(q6afe_cdc_dma_port_prepare);
 /**
@@ -1489,13 +1501,105 @@
 	struct q6afe *afe = port->afe;
 	int port_id = port->id;
 	int ret, param_id = port->cfg_type;
+
 	struct apr_pkt *pkt;
 	int pkt_size;
 	void *p;
 
-	ret  = q6afe_port_set_param_v2(port, &port->port_cfg, param_id,
-				       AFE_MODULE_AUDIO_DEV_INTERFACE,
-				       sizeof(port->port_cfg));
+	/*
+	 * Lenovo J716F: this ADSP image has instance-ID support (AVS API
+	 * v5+, as reported by the stock ACDB loader:
+	 * "acdb_loader_init_v4: ... instance_id_supported:1"). The vendor
+	 * driver then sends the CODEC DMA port config through
+	 * AFE_PORT_CMD_SET_PARAM_V3 (0x100FA), whose payload carries its own
+	 * {module_id, instance_id, param_id, param_size} header, a u32
+	 * payload_size and the full 144-byte port configuration. The V2
+	 * command (0x100EF) is rejected with error 2 for this param, so
+	 * reproduce the V3 layout exactly:
+	 *
+	 *   +20  port_id (u16)
+	 *   +36  payload_size (u32) = 16 + 144
+	 *   +40  module_id (u32), instance_id (u32),
+	 *        param_id (u32), param_size (u32)
+	 *   +56  the 144-byte CDC DMA configuration
+	 */
+	if (param_id == AFE_PARAM_ID_CODEC_DMA_CONFIG) {
+		/*
+		 * Lenovo J716F: the Kona ADSP validates the payload size of
+		 * AFE_PARAM_ID_CODEC_DMA_CONFIG and rejects the command with
+		 * error 2 (-22) unless the payload is the full 144-byte port
+		 * configuration block. The vendor driver (audio_q6.ko,
+		 * __afe_port_start) memsets a 144-byte scratch buffer, copies
+		 * the DAI config into it and sends all 144 bytes; both the
+		 * 16-byte afe_param_id_cdc_dma_cfg and the ~44-byte
+		 * afe_port_config union are refused.
+		 *
+		 * Keep this a single attempt: retrying the command while
+		 * q6afe holds its lock wedged APR and left every later mixer
+		 * access stuck in D state.
+		 */
+		u8 cfg_buf[144] = { 0 };
+		struct {
+			u32 topology_id;
+			u32 reserved;
+		} __packed topo = { 0x00010312, 0 };
+
+		/*
+		 * Lenovo J716F: the Kona ADSP also wants the port's COPP
+		 * topology id (AFE_PARAM_ID_PORT_TOPOLOGY_ID, 0x1025A) set
+		 * before the port config. The vendor driver sends it from
+		 * afe_send_port_topology_id(); without it the CDC DMA port
+		 * accepts its config but never delivers data and the DSP
+		 * signals EOS after ~600 ms.
+		 */
+		ret = q6afe_port_set_param_v2(port, &topo, 0x0001025A,
+					      AFE_MODULE_AUDIO_DEV_INTERFACE,
+					      sizeof(topo));
+		if (ret)
+			dev_warn(afe->dev,
+				 "J716F-AFE: port 0x%x topology_id=0x%x rc=%d\n",
+				 port_id, topo.topology_id, ret);
+		else
+			pr_debug("J716F-AFE: port 0x%x topology_id=0x%x rc=%d\n",
+				 port_id, topo.topology_id, ret);
+
+		/*
+		 * Lenovo J716F: the vendor's __afe_port_start() also sends
+		 * AFE_PARAM_ID_HW_DELAY (module 0x1020C, param 0x10243, 8-byte
+		 * payload carrying delay_usec) right before the port config,
+		 * and Android's log shows it succeeding ("afe_send_hw_delay:
+		 * port_id 0xb037 rate 48000 delay_usec 0 status 0") on a
+		 * capture stream that does deliver data. Send the same thing.
+		 */
+		{
+			struct {
+				u32 delay_usec;
+				u32 reserved;
+			} __packed hw_delay = { 0, 0 };
+
+			ret = q6afe_port_set_param_v2(port, &hw_delay,
+						      0x00010243,
+						      AFE_MODULE_AUDIO_DEV_INTERFACE,
+						      sizeof(hw_delay));
+			if (ret)
+				dev_warn(afe->dev,
+					 "J716F-AFE: port 0x%x hw_delay rc=%d\n",
+					 port_id, ret);
+			else
+				pr_debug("J716F-AFE: port 0x%x hw_delay rc=%d\n",
+					 port_id, ret);
+		}
+
+		memcpy(cfg_buf, &port->port_cfg.dma_cfg,
+		       sizeof(port->port_cfg.dma_cfg));
+		ret = q6afe_port_set_param_v2(port, cfg_buf, param_id,
+					      AFE_MODULE_AUDIO_DEV_INTERFACE,
+					      sizeof(cfg_buf));
+	} else {
+		ret = q6afe_port_set_param_v2(port, &port->port_cfg, param_id,
+					      AFE_MODULE_AUDIO_DEV_INTERFACE,
+					      sizeof(port->port_cfg));
+	}
 	if (ret) {
 		dev_err(afe->dev, "AFE enable for port 0x%x failed %d\n",
 			port_id, ret);
@@ -1536,6 +1640,9 @@
 	if (ret)
 		dev_err(afe->dev, "AFE enable for port 0x%x failed %d\n",
 			port_id, ret);
+	else
+		pr_debug("J716F-AFE: port 0x%x START ok (cfg_type=%#x)\n",
+			port_id, param_id);
 
 	kfree(pkt);
 	return ret;

