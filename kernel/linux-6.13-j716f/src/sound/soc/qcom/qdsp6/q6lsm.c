// SPDX-License-Identifier: GPL-2.0
/*
 * Q6 LSM (Listen Stream Manager) driver for mainline Qualcomm ADSP.
 *
 * The packet layout mirrors the vendor q6lsm implementation
 * (vendor/opensource/audio-kernel, dsp/q6lsm.c + include/dsp/q6lsm.h):
 *   OPEN_TX    : { apr_hdr, app_id(u16), reserved(u16), sampling_rate(u32) }
 *   SET_PARAMS : { apr_hdr, params_hdr{payload_size,lsw,msw,mem_map_handle},
 *                  { module_id, param_id, param_size(u16), reserved(u16),
 *                    minor_version, port_id(u16), reserved(u16) } }
 * Every command waits for its APR basic response, so a failing OPEN_TX never
 * leads to SET_PARAMS on a dead session (that is what killed the DSP).
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/completion.h>
#include <linux/soc/qcom/apr.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/pcm.h>
#include <sound/soc-dapm.h>
#include "q6lsm.h"

#define Q6LSM_RSP_TIMEOUT_MS	2000

/* j716f: LSM_SESSION_CMD_OPEN_TX_V2 (0x12A8B) payload = {u32 topology_id} */
#define LSM_SESSION_CMD_OPEN_TX_V2	0x00012A8B

/*
 * j716f: the stock ADSP rejects OPEN_TX v1 for every (app_id, rate) combo.
 * OPEN_TX_V2 takes an LSM topology id; the ADSP has built-in topologies
 * (the ADM TX fluence graphs double as LSM graphs). Sweep the candidates.
 */
static int lsm_topology_id;
module_param(lsm_topology_id, int, 0644);

static int lsm_autorun;
static void lsm_autorun_work(struct work_struct *w);
static DECLARE_DELAYED_WORK(lsm_autorun_dwork, lsm_autorun_work);
static int lsm_autorun_set(const char *v, const struct kernel_param *kp);
static const struct kernel_param_ops lsm_autorun_ops = {
	.set = lsm_autorun_set,
	.get = param_get_int,
};
module_param_cb(lsm_autorun, &lsm_autorun_ops, &lsm_autorun, 0644);

static const u32 lsm_topo_candidates[] = {
	0x10F75,	/* VPM_TX_QMIC_FLUENCE  - quad mic  */
	0x10F72,	/* VPM_TX_DM_FLUENCE    - dual mic  */
	0x10F89,	/* VPM_TX_SM_ECNS_V2                */
	0x10F86,	/* VPM_TX_DM_RFECNS                 */
	0x10312,	/* NULL_COPP                        */
	0x10314,	/* DEFAULT_COPP                     */
	0x10BE4,	/* DEFAULT_POPP                     */
	0x10C68,	/* NULL_POPP                        */
};

static struct q6lsm *q6lsm_global;

struct q6lsm {
	struct device *dev;
	struct apr_device *adev;
	struct mutex lock;
	struct completion cmd_done;
	u16 session;
	u32 cmd_status;
	u32 map_handle;
	int map_rc;
	bool opened;
	bool params_set;
	bool running;
};

static inline void q6lsm_add_hdr(struct q6lsm *lsm, struct apr_hdr *hdr,
				 u32 pkt_size, bool cmd_flg, u32 token)
{
	hdr->hdr_field = APR_SEQ_CMD_HDR_FIELD;
	hdr->src_port = ((lsm->session << 8) & 0xFF00) | lsm->session;
	hdr->dest_port = ((lsm->session << 8) & 0xFF00) | lsm->session;
	hdr->pkt_size = pkt_size;
	if (cmd_flg)
		hdr->token = token;
}

static int q6lsm_callback(struct apr_device *adev, struct apr_resp_pkt *data)
{
	struct q6lsm *lsm = dev_get_drvdata(&adev->dev);
	struct apr_hdr *hdr = &data->hdr;

	if (hdr->opcode == APR_BASIC_RSP_RESULT && data->payload_size) {
		struct aprv2_ibasic_rsp_result_t *res = data->payload;

		dev_info(lsm->dev, "j716f-mic: rsp cmd=0x%x status=0x%x\n",
			 res->opcode, res->status);
		lsm->cmd_status = res->status;
		complete(&lsm->cmd_done);
	} else if (hdr->opcode == LSM_SESSION_CMDRSP_SHARED_MEM_MAP_REGIONS) {
		/* payload = { u32 mem_map_handle } */
		if (data->payload_size >= 4) {
			lsm->map_handle = le32_to_cpup(data->payload);
			lsm->map_rc = 0;
		} else {
			lsm->map_rc = -EINVAL;
		}
		dev_info(lsm->dev, "j716f-mic: map rsp handle=0x%x size=%d\n",
			 lsm->map_handle, data->payload_size);
		complete(&lsm->cmd_done);
	} else {
		/* LSM events (READ_DONE etc.) are not handled yet */
		dev_info(lsm->dev, "j716f-mic: event 0x%x size=%d\n",
			 hdr->opcode, data->payload_size);
	}

	return 0;
}

/*
 * Send one command and wait for its basic response. Returns 0 on success,
 * negative errno otherwise (the ADSP status is logged either way).
 */
static int q6lsm_send_cmd(struct q6lsm *lsm, void *pkt, u32 size)
{
	struct apr_hdr *hdr = pkt;
	int rc;

	reinit_completion(&lsm->cmd_done);
	lsm->cmd_status = 0xffffffff;

	rc = apr_send_pkt(lsm->adev, pkt);
	if (rc < 0) {
		dev_err(lsm->dev, "j716f-mic: send 0x%x failed rc=%d\n",
			hdr->opcode, rc);
		return rc;
	}

	if (!wait_for_completion_timeout(&lsm->cmd_done,
					 msecs_to_jiffies(Q6LSM_RSP_TIMEOUT_MS))) {
		dev_err(lsm->dev, "j716f-mic: cmd 0x%x timed out\n",
			hdr->opcode);
		return -ETIMEDOUT;
	}

	dev_info(lsm->dev, "j716f-mic: cmd 0x%x status=0x%x\n",
		 hdr->opcode, lsm->cmd_status);

	return lsm->cmd_status ? -EIO : 0;
}

static int q6lsm_close_cmd(struct q6lsm *lsm);
static int q6lsm_open_cmd(struct q6lsm *lsm);

static int q6lsm_open_v2_sweep(struct q6lsm *lsm)
{
	struct {
		struct apr_hdr hdr;
		u32 topology_id;
	} __packed pkt = {0};
	u32 cand[16];
	u32 ncand = 0;
	int i, rc = -EINVAL;

	if (lsm_topology_id) {
		cand[ncand++] = (u32)lsm_topology_id;
	} else {
		for (i = 0; i < ARRAY_SIZE(lsm_topo_candidates); i++)
			cand[ncand++] = lsm_topo_candidates[i];
	}

	for (i = 0; i < ncand; i++) {
		memset(&pkt, 0, sizeof(pkt));
		q6lsm_add_hdr(lsm, &pkt.hdr, sizeof(pkt), true, lsm->session);
		pkt.hdr.opcode = LSM_SESSION_CMD_OPEN_TX_V2;
		pkt.topology_id = cand[i];
		rc = q6lsm_send_cmd(lsm, &pkt, sizeof(pkt));
		dev_info(lsm->dev, "j716f-mic: open_v2 topo=0x%x rc=%d\n",
			 cand[i], rc);
		if (!rc) {
			lsm->opened = true;
			/* 关闭会话, 允许重复扫描 */
			q6lsm_close_cmd(lsm);
			return 0;
		}
	}
	return rc;
}

/* ---- j716f: LSM_CMD_ADD_TOPOLOGIES path (custom topology registration) ---- */
#define LSM_CMD_ADD_TOPOLOGIES		0x00012A8C
#define ADSP_MEMORY_MAP_SHMEM8_4K_POOL	3

static char *lsm_fw;
module_param(lsm_fw, charp, 0644);

static int q6lsm_map_blob(struct q6lsm *lsm, dma_addr_t paddr, u32 size,
			  u32 *handle)
{
	struct {
		struct apr_hdr hdr;
		u16 mem_pool_id;
		u16 num_regions;
		u32 property_flag;
		struct {
			u32 shm_addr_lsw;
			u32 shm_addr_msw;
			u32 mem_size_bytes;
		} __packed region;
	} __packed pkt = {0};
	int rc;

	q6lsm_add_hdr(lsm, &pkt.hdr, sizeof(pkt), true, lsm->session);
	/* vendor q6lsm_add_mmaphdr: src_port=0, dest_port=session,
	 * token=(session<<8) -- NOT the (session<<8|session) used elsewhere. */
	pkt.hdr.src_port = 0;
	pkt.hdr.dest_port = lsm->session;
	pkt.hdr.token = lsm->session << 8;
	pkt.hdr.opcode = LSM_SESSION_CMD_SHARED_MEM_MAP_REGIONS;
	pkt.mem_pool_id = ADSP_MEMORY_MAP_SHMEM8_4K_POOL;
	pkt.num_regions = 1;
	pkt.property_flag = 0;
	pkt.region.shm_addr_lsw = lower_32_bits(paddr);
	pkt.region.shm_addr_msw = upper_32_bits(paddr);
	pkt.region.mem_size_bytes = size;

	reinit_completion(&lsm->cmd_done);
	lsm->map_rc = -ETIMEDOUT;
	lsm->map_handle = 0;
	rc = apr_send_pkt(lsm->adev, (struct apr_pkt *)&pkt);
	if (rc < 0)
		return rc;
	if (!wait_for_completion_timeout(&lsm->cmd_done,
					 msecs_to_jiffies(Q6LSM_RSP_TIMEOUT_MS)))
		return -ETIMEDOUT;
	if (lsm->map_rc)
		return lsm->map_rc;
	*handle = lsm->map_handle;
	return 0;
}

/* LSM topology ids known to elicit a response (no hang): the ACDB LSM
 * custom-topology family extracted from the device ACDB. After
 * ADD_TOPOLOGIES registers them, one of these should open. */
static const u32 lsm_cust_topo_ids[] = {
	0x12E1B,	/* ACDB 0x130D9 result for acdb_id=100/app=1 */
	0x1002A000,	/* custom topo family in LsmTopologyData/ctop blobs */
	0x1002A001,
	0x1002A002,
	0x10030FFF,
};

static int q6lsm_add_topologies(struct q6lsm *lsm, dma_addr_t paddr, u32 size,
				u32 handle)
{
	struct {
		struct apr_hdr hdr;
		u32 data_payload_addr_lsw;
		u32 data_payload_addr_msw;
		u32 mem_map_handle;
		u32 buffer_size;
	} __packed pkt = {0};

	q6lsm_add_hdr(lsm, &pkt.hdr, sizeof(pkt), true, lsm->session);
	pkt.hdr.opcode = LSM_CMD_ADD_TOPOLOGIES;
	pkt.hdr.dest_port = 0;	/* vendor: ADD_TOPOLOGIES dest_port must be 0 */
	pkt.data_payload_addr_lsw = lower_32_bits(paddr);
	pkt.data_payload_addr_msw = upper_32_bits(paddr);
	pkt.mem_map_handle = handle;
	pkt.buffer_size = size;
	return q6lsm_send_cmd(lsm, &pkt, sizeof(pkt));
}

static void lsm_addtopo_work(struct q6lsm *lsm)
{
	const struct firmware *fw;
	const char *name = lsm_fw ? lsm_fw : "j716f-lsm-topo.bin";
	void *buf;
	dma_addr_t paddr;
	size_t sz;
	u32 handle = 0;
	int rc;

	rc = request_firmware(&fw, name, lsm->dev);
	if (rc) {
		dev_err(lsm->dev, "j716f-mic: request_fw %s rc=%d\n", name, rc);
		return;
	}
	sz = fw->size;
	dev_info(lsm->dev, "j716f-mic: blob %s size=%zu\n", name, sz);

	/* Find the q6asm dai device: it already has iommus=<apps_smmu 0x1801>
	 * and working DMA ops for ADSP audio buffers. Allocating our blob on
	 * that device yields an IOVA the ADSP SMMU can translate, with no dts
	 * change needed. */
	{
		struct device_node *np = of_find_node_by_name(NULL, "dais");
		struct device *dma_dev = NULL;
		while (np && !dma_dev) {
			if (of_property_read_bool(np, "iommus")) {
				struct platform_device *pd = of_find_device_by_node(np);
				if (pd)
					dma_dev = &pd->dev;
			}
			np = of_find_node_by_name(np, "dais");
		}
		dev_info(lsm->dev, "j716f-mic: dma_dev=%p\n", dma_dev);
		if (dma_dev)
			buf = dma_alloc_coherent(dma_dev, roundup(sz, 4096),
						 &paddr, GFP_KERNEL);
		else
			buf = NULL;
	}
	if (!buf) {
		release_firmware(fw);
		return;
	}
	memcpy(buf, fw->data, sz);
	release_firmware(fw);
	dev_info(lsm->dev, "j716f-mic: blob paddr=0x%llx\n", (u64)paddr);

	rc = q6lsm_map_blob(lsm, paddr, roundup(sz, 4096), &handle);
	dev_info(lsm->dev, "j716f-mic: map rc=%d handle=0x%x\n", rc, handle);
	if (!rc) {
		rc = q6lsm_add_topologies(lsm, paddr, sz, handle);
		dev_info(lsm->dev, "j716f-mic: add_topologies rc=%d\n", rc);
	}
	/* buffer stays mapped for the ADSP; sweep the LSM custom-topology ids */
	if (!rc) {
		int i;

		for (i = 0; i < ARRAY_SIZE(lsm_cust_topo_ids); i++) {
			struct {
				struct apr_hdr hdr;
				u32 topology_id;
			} __packed pkt = {0};

			q6lsm_add_hdr(lsm, &pkt.hdr, sizeof(pkt), true,
				      lsm->session);
			pkt.hdr.opcode = LSM_SESSION_CMD_OPEN_TX_V2;
			pkt.topology_id = lsm_cust_topo_ids[i];
			rc = q6lsm_send_cmd(lsm, &pkt, sizeof(pkt));
			dev_info(lsm->dev,
				 "j716f-mic: open_v2(cust) topo=0x%x rc=%d\n",
				 lsm_cust_topo_ids[i], rc);
			if (!rc) {
				lsm->opened = true;
				break;
			}
		}
	}
}

static void lsm_autorun_work(struct work_struct *w)
{
	struct q6lsm *lsm = q6lsm_global;

	if (!lsm) {
		pr_err("j716f-mic: autorun: no lsm\n");
		return;
	}
	int val = lsm_autorun;
	mutex_lock(&lsm->lock);
	if (val == 2) {
		int rc = q6lsm_open_cmd(lsm);
		pr_info("j716f-mic: autorun v1 sweep rc=%d\n", rc);
		q6lsm_close_cmd(lsm);
	} else if (val == 3) {
		lsm_addtopo_work(lsm);
	} else {
		q6lsm_open_v2_sweep(lsm);
	}
	mutex_unlock(&lsm->lock);
}

static int lsm_autorun_set(const char *v, const struct kernel_param *kp)
{
	int val = 0;
	int ret = kstrtoint(v, 0, &val);
	if (ret) return ret;
	*(int *)kp->arg = val;
	schedule_delayed_work(&lsm_autorun_dwork, msecs_to_jiffies(500));
	return 0;
}

static int q6lsm_open_cmd(struct q6lsm *lsm)
{
	struct {
		struct apr_hdr hdr;
		u16 app_id;
		u16 reserved;
		u32 sampling_rate;
	} __packed pkt = {0};
	static const struct {
		u16 app_id;
		u32 rate;
	} combos[] = {
		{ LSM_VOICE_WAKEUP_APP_ID_V2, 16000 },
		{ LSM_VOICE_WAKEUP_APP_ID_V2, 48000 },
		{ LSM_VOICE_WAKEUP_APP_ID,    16000 },
		{ LSM_VOICE_WAKEUP_APP_ID,    48000 },
	};
	int i, rc = -EINVAL;

	if (lsm->opened)
		return 0;

	/* j716f: try v2 (topology) first (skip in v1 debug mode) */
	if (lsm_autorun != 2 && !q6lsm_open_v2_sweep(lsm))
		return 0;

	q6lsm_add_hdr(lsm, &pkt.hdr, sizeof(pkt), true, lsm->session);
	pkt.hdr.opcode = LSM_SESSION_CMD_OPEN_TX;

	/*
	 * Probe which (app_id, sample rate) pair the stock ADSP accepts.
	 * Vendor only shows LSM_VOICE_WAKEUP_APP_ID_V2 / 16 kHz, but this
	 * firmware rejects it, so sweep the documented combinations once
	 * per boot and keep the answer in dmesg.
	 */
	for (i = 0; i < ARRAY_SIZE(combos); i++) {
		pkt.app_id = combos[i].app_id;
		pkt.sampling_rate = combos[i].rate;
		rc = q6lsm_send_cmd(lsm, &pkt, sizeof(pkt));
		dev_info(lsm->dev, "j716f-mic: open app_id=%u rate=%u rc=%d\n",
			 pkt.app_id, pkt.sampling_rate, rc);
		if (!rc)
			break;
	}

	if (!rc)
		lsm->opened = true;

	return rc;
}

static int q6lsm_set_port(struct q6lsm *lsm)
{
	struct {
		struct apr_hdr hdr;
		struct {
			u32 payload_size;
			u32 payload_addr_lsw;
			u32 payload_addr_msw;
			u32 mem_map_handle;
		} __packed params_hdr;
		struct {
			u32 module_id;
			u32 param_id;
			u16 param_size;
			u16 reserved;
			u32 minor_version;
			u16 port_id;
			u16 reserved2;
		} __packed connect;
	} __packed pkt = {0};
	int rc;

	if (!lsm->opened)
		return -EINVAL;
	if (lsm->params_set)
		return 0;

	q6lsm_add_hdr(lsm, &pkt.hdr, sizeof(pkt), true, lsm->session);
	pkt.hdr.opcode = LSM_SESSION_CMD_SET_PARAMS;

	pkt.params_hdr.payload_size = sizeof(pkt.connect);
	pkt.params_hdr.mem_map_handle = 0;

	pkt.connect.module_id = LSM_MODULE_ID_VOICE_WAKEUP;
	pkt.connect.param_id = LSM_PARAM_ID_CONNECT_TO_PORT;
	pkt.connect.param_size = sizeof(pkt.connect) - 12;
	pkt.connect.minor_version = 1;
	pkt.connect.port_id = 0x1007;	/* AFE_PORT_ID_QUATERNARY_MI2S_TX */

	rc = q6lsm_send_cmd(lsm, &pkt, sizeof(pkt));
	if (!rc)
		lsm->params_set = true;

	return rc;
}

static int q6lsm_start_cmd(struct q6lsm *lsm)
{
	struct apr_hdr hdr = {0};
	int rc;

	if (lsm->running)
		return 0;
	if (!lsm->params_set)
		return -EINVAL;

	q6lsm_add_hdr(lsm, &hdr, sizeof(hdr), true, lsm->session);
	hdr.opcode = LSM_SESSION_CMD_START;

	rc = q6lsm_send_cmd(lsm, &hdr, sizeof(hdr));
	if (!rc)
		lsm->running = true;

	return rc;
}

static int q6lsm_stop_cmd(struct q6lsm *lsm)
{
	struct apr_hdr hdr = {0};

	if (!lsm->running)
		return 0;

	q6lsm_add_hdr(lsm, &hdr, sizeof(hdr), true, lsm->session);
	hdr.opcode = LSM_SESSION_CMD_STOP;
	lsm->running = false;

	return apr_send_pkt(lsm->adev, (struct apr_pkt *)&hdr);
}

static int q6lsm_close_cmd(struct q6lsm *lsm)
{
	struct apr_hdr hdr = {0};
	int rc;

	if (!lsm->opened)
		return 0;

	q6lsm_add_hdr(lsm, &hdr, sizeof(hdr), true, lsm->session);
	hdr.opcode = LSM_SESSION_CMD_CLOSE_TX;

	rc = q6lsm_send_cmd(lsm, &hdr, sizeof(hdr));
	if (!rc) {
		lsm->opened = false;
		lsm->params_set = false;
	}

	return rc;
}

static int q6lsm_pcm_open(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream)
{
	struct q6lsm *lsm = snd_soc_component_get_drvdata(component);
	int rc;

	mutex_lock(&lsm->lock);
	rc = q6lsm_open_cmd(lsm);
	mutex_unlock(&lsm->lock);

	dev_info(lsm->dev, "j716f-mic: pcm open rc=%d\n", rc);
	return rc;
}

static int q6lsm_pcm_hw_params(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params)
{
	struct q6lsm *lsm = snd_soc_component_get_drvdata(component);
	int rc;

	mutex_lock(&lsm->lock);
	rc = q6lsm_set_port(lsm);
	mutex_unlock(&lsm->lock);

	dev_info(lsm->dev, "j716f-mic: hw_params rate=%u ch=%u rc=%d\n",
		 params_rate(params), params_channels(params), rc);
	return rc;
}

static int q6lsm_pcm_prepare(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	struct q6lsm *lsm = snd_soc_component_get_drvdata(component);
	int rc;

	mutex_lock(&lsm->lock);
	rc = q6lsm_start_cmd(lsm);
	mutex_unlock(&lsm->lock);

	dev_info(lsm->dev, "j716f-mic: prepare/start rc=%d\n", rc);
	return rc;
}

static int q6lsm_pcm_trigger(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream, int cmd)
{
	return 0;
}

static snd_pcm_uframes_t q6lsm_pcm_pointer(struct snd_soc_component *component,
					   struct snd_pcm_substream *substream)
{
	return 0;
}

static int q6lsm_pcm_new(struct snd_soc_component *component,
			 struct snd_soc_pcm_runtime *rtd)
{
	return snd_pcm_set_managed_buffer_all(rtd->pcm, SNDRV_DMA_TYPE_DEV,
					      rtd->dev, 0, 64 * 1024);
}

static int q6lsm_pcm_close(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	struct q6lsm *lsm = snd_soc_component_get_drvdata(component);

	mutex_lock(&lsm->lock);
	q6lsm_stop_cmd(lsm);
	q6lsm_close_cmd(lsm);
	mutex_unlock(&lsm->lock);

	dev_info(lsm->dev, "j716f-mic: pcm close done\n");
	return 0;
}

static const struct snd_soc_dapm_widget q6lsm_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_OUT("LSM Capture", "LSM Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_MIXER("MultiMedia1 Mixer LSM Capture", SND_SOC_NOPM, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route q6lsm_dapm_routes[] = {
	{"MultiMedia1 Mixer", NULL, "MultiMedia1 Mixer LSM Capture"},
	{"MultiMedia1 Mixer LSM Capture", NULL, "LSM Capture"},
};

static const struct snd_soc_component_driver q6lsm_component = {
	.name = "q6lsm",
	.open = q6lsm_pcm_open,
	.close = q6lsm_pcm_close,
	.hw_params = q6lsm_pcm_hw_params,
	.prepare = q6lsm_pcm_prepare,
	.trigger = q6lsm_pcm_trigger,
	.pointer = q6lsm_pcm_pointer,
	.pcm_construct = q6lsm_pcm_new,
	.legacy_dai_naming = 1,
	.dapm_widgets = q6lsm_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(q6lsm_dapm_widgets),
	.dapm_routes = q6lsm_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(q6lsm_dapm_routes),
};

static struct snd_soc_dai_driver q6lsm_dai = {
	.name = "LSM",
	.capture = {
		.stream_name = "LSM Capture",
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
		.channels_min = 1,
		.channels_max = 4,
		.rate_min = 8000,
		.rate_max = 48000,
	},
};

static int q6lsm_probe(struct apr_device *adev)
{
	struct q6lsm *lsm;

	lsm = devm_kzalloc(&adev->dev, sizeof(*lsm), GFP_KERNEL);
	if (!lsm)
		return -ENOMEM;
	lsm->dev = &adev->dev;
	lsm->adev = adev;
	lsm->session = 1;
	mutex_init(&lsm->lock);
	init_completion(&lsm->cmd_done);
	dev_set_drvdata(&adev->dev, lsm);
	q6lsm_global = lsm;
	dev_info(lsm->dev, "j716f-mic: q6lsm probe\n");
	schedule_delayed_work(&lsm_autorun_dwork, msecs_to_jiffies(20000));

	return devm_snd_soc_register_component(&adev->dev, &q6lsm_component,
					       &q6lsm_dai, 1);
}

static const struct of_device_id q6lsm_device_id[] = {
	{ .compatible = "qcom,q6lsm" },
	{},
};
MODULE_DEVICE_TABLE(of, q6lsm_device_id);

static struct apr_driver qcom_q6lsm_driver = {
	.probe = q6lsm_probe,
	.callback = q6lsm_callback,
	.driver = {
		.name = "qcom-q6lsm",
		.of_match_table = of_match_ptr(q6lsm_device_id),
	},
};
module_apr_driver(qcom_q6lsm_driver);

MODULE_DESCRIPTION("Q6 LSM driver");
MODULE_LICENSE("GPL");
