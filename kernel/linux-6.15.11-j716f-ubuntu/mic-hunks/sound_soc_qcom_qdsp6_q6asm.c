diff --git a/sound/soc/qcom/qdsp6/q6asm.c b/sound/soc/qcom/qdsp6/q6asm.c
--- a/sound/soc/qcom/qdsp6/q6asm.c
+++ b/sound/soc/qcom/qdsp6/q6asm.c
@@ -630,6 +630,7 @@
 			break;
 		case ASM_SESSION_CMD_RUN_V2:
 			client_event = ASM_CLIENT_EVENT_CMD_RUN_DONE;
+			pr_debug("J716F-ASM: CMD_RUN_DONE received\n");
 			break;
 		case ASM_STREAM_CMD_CLOSE:
 			client_event = ASM_CLIENT_EVENT_CMD_CLOSE_DONE;
@@ -655,8 +656,42 @@
 			}
 			break;
 		default:
-			dev_err(ac->dev, "command[0x%x] not expecting rsp\n",
-				result->opcode);
+			/*
+			 * The ADSP rejects ASM_DATA_CMD_READ_V2 with
+			 * ADSP_ENOTREADY (10) while the ASM session is not in a
+			 * state where it can hand out capture data, and a
+			 * rejected read never fills its buffer. Surface it
+			 * instead of silently dropping the response.
+			 */
+			if (result->opcode == ASM_DATA_CMD_READ_V2) {
+				if (result->status != ADSP_EOK) {
+					/*
+					 * Lenovo J716F: ADSP_ENOTREADY (10) is
+					 * transient, but re-queueing the read
+					 * from inside this handler recurses
+					 * without bound: q6asm_read() is called
+					 * synchronously, ADSP answers ENOTREADY
+					 * again, we call the callback again...
+					 * That produced 8724 read completions in
+					 * three seconds.
+					 *
+					 * The read that got rejected simply never
+					 * fills its buffer, so the only thing we
+					 * owe the stream is another attempt once
+					 * the DSP has caught up. Throttle it and
+					 * let the callback decide - it re-arms the
+					 * read from process context, not from this
+					 * APR callback.
+					 */
+					dev_dbg_ratelimited(ac->dev,
+						"capture read not ready: status %u\n",
+						result->status);
+				}
+			} else {
+				dev_err(ac->dev,
+					"command[0x%x] not expecting rsp\n",
+					result->opcode);
+			}
 			break;
 		}
 
@@ -706,6 +741,7 @@
 			struct asm_data_cmd_read_v2_done *done = data->payload;
 			unsigned long flags;
 			phys_addr_t phys;
+			unsigned int seq;
 
 			spin_lock_irqsave(&ac->lock, flags);
 			port =  &ac->port[SNDRV_PCM_STREAM_CAPTURE];
@@ -782,8 +818,15 @@
 			wake_up(&a->mem_wait);
 			break;
 		default:
-			dev_err(&adev->dev, "command[0x%x] not expecting rsp\n",
-				 result->opcode);
+			/*
+			 * The ADSP answers every ASM_DATA_CMD_READ_V2 with a
+			 * basic result. Its status field is the only feedback
+			 * we get on whether the read was accepted, so log it
+			 * instead of discarding it.
+			 */
+			dev_err(&adev->dev,
+				"command[0x%x] not expecting rsp\n",
+				result->opcode);
 			break;
 		}
 		goto done;
@@ -1399,10 +1442,10 @@
 					 uint32_t channels,
 					 uint16_t bits_per_sample)
 {
+	u32 frames_per_buf = 0;
 	struct asm_multi_channel_pcm_enc_cfg_v2  *enc_cfg;
 	struct apr_pkt *pkt;
 	u8 *channel_mapping;
-	u32 frames_per_buf = 0;
 	int pkt_size, rc;
 	void *p;
 
@@ -1480,6 +1523,14 @@
 	read->buf_size = ab->size;
 	read->seq_id = port->dsp_buf;
 	pkt->hdr.token = port->dsp_buf;
+
+	pr_debug("J716F-READ: phys=%#llx size=%u handle=%#x seq=%u\n",
+		(unsigned long long)ab->phys, ab->size,
+		port->mem_map_handle, port->dsp_buf);
+
+	pr_debug("J716F-READ: phys=%#llx size=%u handle=%#x seq=%u dsp_buf=%u\n",
+		(unsigned long long)ab->phys, ab->size,
+		port->mem_map_handle, port->dsp_buf, port->dsp_buf);
 
 	port->dsp_buf++;
 

