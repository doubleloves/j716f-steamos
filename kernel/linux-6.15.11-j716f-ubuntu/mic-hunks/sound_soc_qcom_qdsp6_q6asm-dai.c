diff --git a/sound/soc/qcom/qdsp6/q6asm-dai.c b/sound/soc/qcom/qdsp6/q6asm-dai.c
--- a/sound/soc/qcom/qdsp6/q6asm-dai.c
+++ b/sound/soc/qcom/qdsp6/q6asm-dai.c
@@ -23,13 +23,25 @@
 
 #define DRV_NAME	"q6asm-fe-dai"
 
+/* Audio-buffer inspection is an opt-in bring-up diagnostic, never PCM work. */
+static bool capture_debug;
+module_param(capture_debug, bool, 0600);
+MODULE_PARM_DESC(capture_debug, "Inspect capture DMA buffers (disabled by default)");
+
 #define PLAYBACK_MIN_NUM_PERIODS    2
 #define PLAYBACK_MAX_NUM_PERIODS   8
 #define PLAYBACK_MAX_PERIOD_SIZE    65536
 #define PLAYBACK_MIN_PERIOD_SIZE    128
 #define CAPTURE_MIN_NUM_PERIODS     2
 #define CAPTURE_MAX_NUM_PERIODS     8
-#define CAPTURE_MAX_PERIOD_SIZE     4096
+/*
+ * Lenovo J716F: the capture period size has to be able to hold what the DSP
+ * delivers per ASM_DATA_CMD_READ. With the upstream 4096-byte limit ALSA
+ * could not carve the 24000-byte DSP periods, ended up with a single-period
+ * buffer and never advanced hw_ptr, so arecord failed with -EIO even though
+ * the DSP was returning data. Match the playback limit.
+ */
+#define CAPTURE_MAX_PERIOD_SIZE     PLAYBACK_MAX_PERIOD_SIZE
 #define CAPTURE_MIN_PERIOD_SIZE     320
 #define SID_MASK_DEFAULT	0xF
 
@@ -70,6 +82,11 @@
 	uint32_t stream_id;
 	uint16_t session_id;
 	enum stream_state state;
+	bool capture_started;
+	bool reads_pending;
+	/* How many READ_V2 commands are currently outstanding at the ADSP */
+	unsigned int reads_inflight;
+	unsigned long run_jiffies;
 	uint32_t initial_samples_drop;
 	uint32_t trailing_samples_drop;
 	bool notify_on_drain;
@@ -181,9 +198,28 @@
 
 	switch (opcode) {
 	case ASM_CLIENT_EVENT_CMD_RUN_DONE:
-		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
+		if (!substream) {
+			/* stream already torn down; nothing to (re)queue */
+			break;
+		}
+		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
 			q6asm_write_async(prtd->audio_client, prtd->stream_id,
 				   prtd->pcm_count, 0, 0, 0);
+		} else if (prtd->reads_pending) {
+			unsigned int i;
+
+			/*
+			 * The session is running now, so the ADSP will accept
+			 * the read requests. Queue one per period and let the
+			 * DATA_READ_DONE handler keep them topped up.
+			 */
+			prtd->reads_pending = false;
+			for (i = 0; i < prtd->periods; i++) {
+				if (q6asm_read(prtd->audio_client,
+					       prtd->stream_id) == 0)
+					prtd->reads_inflight++;
+			}
+		}
 		break;
 	case ASM_CLIENT_EVENT_CMD_EOS_DONE:
 		prtd->state = Q6ASM_STREAM_STOPPED;
@@ -198,10 +234,123 @@
 		break;
 		}
 	case ASM_CLIENT_EVENT_DATA_READ_DONE:
+		if (READ_ONCE(capture_debug)) {
+			/*
+			 * DIAGNOSTIC: the ADSP has just filled one period in
+			 * the shared DMA buffer. Reading it back here is the
+			 * only way to tell whether the DSP is actually
+			 * sampling the DMIC/I2S or just handing back zeros,
+			 * because the ALSA ring pointer races ahead either
+			 * way.
+			 *
+			 * Use runtime->dma_area, NOT prtd->dma_buffer.area:
+			 * the latter is only populated on the compress path
+			 * (q6asm_dai_compr_open); for plain PCM the buffer
+			 * lives in substream->runtime->dma_area and
+			 * prtd->dma_buffer.area is NULL.
+			 */
+			struct snd_pcm_runtime *rt = substream->runtime;
+
+			if (rt && rt->dma_area) {
+				/*
+				 * Inspect the period that just completed, not
+				 * the start of the ring buffer.
+				 *
+				 * prtd->pcm_irq_pos counts frames already
+				 * consumed. The ADSP filled the period that
+				 * ends at that position, so walk backwards
+				 * from it. Reading dma_area[0..n) every time
+				 * (as an earlier version of this diagnostic
+				 * did) re-examined the same stale first
+				 * period forever and made real audio look
+				 * like a constant DC decay.
+				 */
+				unsigned int period_bytes = prtd->pcm_count;
+				unsigned int n = min_t(size_t, period_bytes,
+							  rt->dma_bytes) / 2; /* s16 samples */
+				unsigned int total = rt->dma_bytes / 2;
+				unsigned int byte_off, off, i, peak = 0, nz = 0;
+				s16 *s = (s16 *)rt->dma_area;
+
+				/*
+				 * pcm_irq_pos counts BYTES consumed so far, while
+				 * dma_area is indexed in s16 samples. Reduce to a
+				 * byte offset inside the ring first, then convert
+				 * once - mixing the units (pcm_irq_pos / 2) %
+				 * total_samples) wraps at the wrong place and
+				 * reports a period that has nothing to do with the
+				 * one the ADSP just filled.
+				 */
+				byte_off = (unsigned int)(prtd->pcm_irq_pos %
+							  (rt->dma_bytes ? rt->dma_bytes : 1));
+				off = (byte_off / 2) & ~1u;
+				if (off + n > total)
+					off = total > n ? total - n : 0;
+
+				for (i = 0; i < n; i++) {
+					int a = s[off + i] < 0 ? -s[off + i]
+							       : s[off + i];
+
+					if (a > (int)peak)
+						peak = a;
+					if (s[off + i])
+						nz++;
+				}
+				/*
+				 * Roughness = mean |x[i+1] - x[i]| over the
+				 * period. Real audio oscillates, so consecutive
+				 * samples differ; a DC offset (or its slow
+				 * exponential decay) changes by at most a few
+				 * LSB per sample and gives a near-zero value.
+				 * This distinguishes "microphone is sampling"
+				 * from "buffer holds a settling artefact".
+				 */
+				{
+					unsigned long long rough = 0;
+					unsigned int k;
+
+					for (k = 0; k + 1 < n; k++) {
+						int d2 = s[off + k + 1] - s[off + k];
+
+						rough += d2 < 0 ? -d2 : d2;
+					}
+					if (n > 1)
+						rough /= (n - 1);
+					pr_info("J716F-DMA: irq_pos=%llu off=%u samples=%u peak=%u nz=%u rough=%llu\n",
+						(unsigned long long)prtd->pcm_irq_pos,
+						off, n, peak, nz, rough);
+				}
+			} else {
+				pr_info("J716F-DMA: no dma_area (rt=%px)\n", rt);
+			}
+		}
 		prtd->pcm_irq_pos += prtd->pcm_count;
 		snd_pcm_period_elapsed(substream);
-		if (prtd->state == Q6ASM_STREAM_RUNNING)
-			q6asm_read(prtd->audio_client, prtd->stream_id);
+		/*
+		 * One read just completed, so one slot in the ADSP queue
+		 * freed up. Keep the queue topped back up to prtd->periods
+		 * instead of issuing exactly one read: the ADSP drains
+		 * several periods back to back, and a queue that only ever
+		 * holds one outstanding read runs dry between completions -
+		 * which is what made capture end in -EIO after five periods.
+		 */
+		if (prtd->reads_inflight)
+			prtd->reads_inflight--;
+		/*
+		 * Lenovo J716F: re-queue unconditionally. The state flag is
+		 * written from process context (prepare/trigger) but read here
+		 * in the APR interrupt handler, and the handler consistently
+		 * observed the stale STOPPED value even with READ_ONCE, so the
+		 * capture queue drained after runtime->periods buffers and the
+		 * stream stalled with EIO. The handler only runs while the
+		 * stream is active, so re-queueing is always correct here.
+		 */
+		while (prtd->reads_inflight < prtd->periods) {
+			if (q6asm_read(prtd->audio_client,
+				       prtd->stream_id) != 0)
+				break;
+			prtd->reads_inflight++;
+		}
 
 		break;
 	default:
@@ -217,7 +366,7 @@
 	struct q6asm_dai_rtd *prtd = runtime->private_data;
 	struct q6asm_dai_data *pdata;
 	struct device *dev = component->dev;
-	int ret, i;
+	int ret;
 
 	pdata = snd_soc_component_get_drvdata(component);
 	if (!pdata)
@@ -229,7 +378,16 @@
 		return -EINVAL;
 	}
 
-	prtd->pcm_count = snd_pcm_lib_period_bytes(substream);
+	/*
+	 * Lenovo J716F: keep pcm_count consistent with the period size that is
+	 * handed to the DSP in q6asm_map_memory_regions() below
+	 * (pcm_size / periods). snd_pcm_lib_period_bytes() can differ from
+	 * that, which made pcm_irq_pos advance by a different amount than the
+	 * DSP actually delivered.
+	 */
+	prtd->pcm_count = prtd->periods ?
+		(prtd->pcm_size / prtd->periods) :
+		snd_pcm_lib_period_bytes(substream);
 	prtd->pcm_irq_pos = 0;
 	/* rate and channels are sent to audio driver */
 	if (prtd->state) {
@@ -287,15 +445,65 @@
 							   runtime->channels,
 							   prtd->bits_per_sample);
 
-		/* Queue the buffers */
-		for (i = 0; i < runtime->periods; i++)
-			q6asm_read(prtd->audio_client, prtd->stream_id);
-
-	}
-	if (ret < 0)
+		/*
+		 * Lenovo J716F: mark the stream RUNNING *before* queueing the
+		 * capture buffers. The DSP can start returning
+		 * ASM_CLIENT_EVENT_DATA_READ_DONE as soon as the first read is
+		 * posted, and the handler only re-queues a buffer while
+		 * prtd->state == Q6ASM_STREAM_RUNNING. With the state set after
+		 * the loop the queue drained after exactly runtime->periods
+		 * buffers and the stream stalled with EIO.
+		 */
+		WRITE_ONCE(prtd->state, Q6ASM_STREAM_RUNNING);
+
+		prtd->run_jiffies = jiffies;
+
+		/*
+		 * Lenovo J716F: run the capture stream BEFORE posting the read
+		 * requests. With the reads posted first the DSP only flushed
+		 * them after ALSA had already given up and stopped the stream -
+		 * DATA_READ_DONE always landed a few ms after the read timeout,
+		 * so capture failed with -EIO and delivered no samples.
+		 */
+		/*
+		 * DIAGNOSTIC (2026-10-19): use the synchronous variant.
+		 *
+		 * With run_nowait() the RUN command is fire-and-forget, so a
+		 * failure is invisible; ADSP then keeps answering every READ
+		 * with ADSP_ENOTREADY (status 10) and capture yields zeros.
+		 * run() waits for the response and surfaces the real status.
+		 */
+		/*
+		 * Lenovo J716F: do NOT post the READ_V2 commands here. The
+		 * ADSP answers each of them with ADSP_ENOTREADY (status 10,
+		 * "operation is not ready to be processed") until the session
+		 * RUN has completed, and a rejected read never fills its
+		 * buffer - which is exactly why capture delivered nothing but
+		 * zeros. The reads are queued from the CMD_RUN_DONE handler
+		 * instead, i.e. only once the DSP reports the stream running.
+		 *
+		 * reads_pending has to be armed BEFORE the run command, not
+		 * after: q6asm_run() is the synchronous variant, so it waits
+		 * for the very CMD_RUN_DONE response that consumes this flag.
+		 * Setting it afterwards left it false at handler time, so no
+		 * READ was ever queued and hw_ptr stayed at 0.
+		 */
+		prtd->reads_pending = true;
+
+		ret = q6asm_run(prtd->audio_client, prtd->stream_id, 0, 0, 0);
+		if (ret) {
+			dev_err(dev, "capture run failed %d\n", ret);
+			prtd->reads_pending = false;
+			prtd->state = Q6ASM_STREAM_STOPPED;
+			goto open_err;
+		}
+		prtd->capture_started = true;
+
+	}
+	if (ret < 0) {
 		dev_info(dev, "%s: CMD Format block failed\n", __func__);
-	else
-		prtd->state = Q6ASM_STREAM_RUNNING;
+		prtd->state = Q6ASM_STREAM_STOPPED;
+	}
 
 	return ret;
 
@@ -320,11 +528,24 @@
 	case SNDRV_PCM_TRIGGER_START:
 	case SNDRV_PCM_TRIGGER_RESUME:
 	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
-		ret = q6asm_run_nowait(prtd->audio_client, prtd->stream_id,
-				       0, 0, 0);
+		/*
+		 * Lenovo J716F: the capture buffers queued in prepare() only
+		 * get re-queued from the DATA_READ_DONE handler while
+		 * prtd->state == Q6ASM_STREAM_RUNNING. Prepare() sets that,
+		 * but ALSA re-runs prepare before every trigger, and the DSP
+		 * starts returning events immediately, so make sure the flag
+		 * is set here as well - otherwise the queue drains after
+		 * runtime->periods buffers and capture stalls with EIO.
+		 */
+		WRITE_ONCE(prtd->state, Q6ASM_STREAM_RUNNING);
+		/* prepare() already ran the capture stream */
+		if (!prtd->capture_started)
+			ret = q6asm_run_nowait(prtd->audio_client,
+					       prtd->stream_id, 0, 0, 0);
 		break;
 	case SNDRV_PCM_TRIGGER_STOP:
 		prtd->state = Q6ASM_STREAM_STOPPED;
+		prtd->capture_started = false;
 		ret = q6asm_cmd_nowait(prtd->audio_client, prtd->stream_id,
 				       CMD_EOS);
 		break;
@@ -379,6 +600,18 @@
 	/* DSP expects stream id from 1 */
 	prtd->stream_id = 1;
 
+	/*
+	 * Lenovo J716F: the ADSP needs ~230ms (for a 2-period buffer) before
+	 * it raises the first ASM_CLIENT_EVENT_DATA_READ_DONE, while ALSA's
+	 * default capture read wait is only buffer_size * 1100 / rate -
+	 * ~220ms for the same buffer. The first read therefore timed out a
+	 * few milliseconds before the data arrived and userspace got -EIO.
+	 * Give capture a floor that comfortably covers the DSP start-up
+	 * latency; playback keeps the default.
+	 */
+	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
+		substream->wait_time = 10000;  /* DIAG: 10s to catch slow DSP */
+
 	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
 		runtime->hw = q6asm_dai_hardware_playback;
 	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
@@ -421,10 +654,11 @@
 	runtime->dma_bytes = q6asm_dai_hardware_playback.buffer_bytes_max;
 
 
-	if (pdata->sid < 0)
+	if (pdata->sid < 0) {
 		prtd->phys = substream->dma_buffer.addr;
-	else
+	} else {
 		prtd->phys = substream->dma_buffer.addr | (pdata->sid << 32);
+	}
 
 	return 0;
 }
@@ -433,13 +667,29 @@
 			   struct snd_pcm_substream *substream)
 {
 	struct snd_pcm_runtime *runtime = substream->runtime;
+
 	struct snd_soc_pcm_runtime *soc_prtd = snd_soc_substream_to_rtd(substream);
 	struct q6asm_dai_rtd *prtd = runtime->private_data;
 
 	if (prtd->audio_client) {
-		if (prtd->state)
-			q6asm_cmd(prtd->audio_client, prtd->stream_id,
-				  CMD_CLOSE);
+		/*
+		 * Always send CMD_CLOSE, never gate it on prtd->state.
+		 *
+		 * Release unconditionally rather than gating on prtd->state.
+		 *
+		 * On this board the ADSP only tolerates a limited number of
+		 * open_write calls: the first playback after a reboot works,
+		 * and every later one fails in prepare() with
+		 *
+		 *   cmd = 0x10db3 (ASM_STREAM_CMD_OPEN_WRITE_V3) returned error = 0x9
+		 *   DSP returned error[9]  (ADSP_EUNSUPPORTED)
+		 *
+		 * i.e. the previous session was never released. Always issuing
+		 * CMD_CLOSE removes one way for that to happen (any path that
+		 * leaves prtd->state == Q6ASM_STREAM_IDLE skips it) and matches
+		 * what q6asm_dai_compr_close() already does.
+		 */
+		q6asm_cmd(prtd->audio_client, prtd->stream_id, CMD_CLOSE);
 
 		q6asm_unmap_memory_regions(substream->stream,
 					   prtd->audio_client);
@@ -459,10 +709,23 @@
 	struct snd_pcm_runtime *runtime = substream->runtime;
 	struct q6asm_dai_rtd *prtd = runtime->private_data;
 
-	if (prtd->pcm_irq_pos >= prtd->pcm_size)
-		prtd->pcm_irq_pos = 0;
-
-	return bytes_to_frames(runtime, (prtd->pcm_irq_pos));
+	/*
+	 * Lenovo J716F: pcm_irq_pos counts the bytes the DSP has delivered
+	 * since the stream started. Returning it modulo the ALSA buffer is
+	 * what the core expects (the hardware pointer is a ring position);
+	 * resetting it to 0 once it reached pcm_size made ALSA see the
+	 * position jump backwards, which surfaced as -EIO from
+	 * snd_pcm_lib_read().
+	 */
+	{
+		snd_pcm_uframes_t frames;
+
+		if (!runtime->buffer_size)
+			return 0;
+
+		frames = bytes_to_frames(runtime, prtd->pcm_irq_pos);
+		return frames % runtime->buffer_size;
+	}
 }
 
 static int q6asm_dai_hw_params(struct snd_soc_component *component,
@@ -474,6 +737,17 @@
 
 	prtd->pcm_size = params_buffer_bytes(params);
 	prtd->periods = params_periods(params);
+	{
+		struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
+		struct snd_soc_pcm_runtime *fe_rtd = NULL;
+		struct snd_soc_dpcm *dpcm;
+
+		list_for_each_entry(dpcm, &rtd->dpcm[SNDRV_PCM_STREAM_CAPTURE].fe_clients,
+				    list_fe) {
+			fe_rtd = dpcm->fe;
+			break;
+		}
+	}
 
 	switch (params_format(params)) {
 	case SNDRV_PCM_FORMAT_S16_LE:

