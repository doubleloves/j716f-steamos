// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020, Linaro Limited

#include <dt-bindings/sound/qcom,q6afe.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <linux/soundwire/sdw.h>
#include <sound/jack.h>
#include <linux/input-event-codes.h>
#include <sound/cs35l41.h>
#include "qdsp6/q6afe.h"
#include "common.h"
#include "sdw.h"

#define DRIVER_NAME		"sm8250"
#define MI2S_BCLK_RATE		1536000

/* J716F built-in DMIC is mono; the generic fixup forced 2 channels */
static int wcd_capture_channels = 2;
module_param(wcd_capture_channels, int, 0644);

/* J716F: force-enable WCD938x DMIC output/ADC widgets so DAPM powers the
 * codec DMIC chain (and fires wcd938x_codec_enable_dmic) even though the
 * generic DAPM graph does not reach the codec from the LPASS AIF. */
static int j716f_force_dmic_pins = 1;
module_param(j716f_force_dmic_pins, int, 0644);
/* Bitmask of WCD DMIC channels (DMIC1=bit0 ... DMIC8=bit7) to force ON.
 * DSP CDC port expects channels at mask 0x3 -> use bits 0|1 (DMIC1+DMIC2). */
static int j716f_force_dmic_mask = 0x3;
module_param(j716f_force_dmic_mask, int, 0644);

/* J716F DMIC enable/select GPIO values (0/1). Stock mixer_paths says
 * "DMIC EN=Off, DMIC SEL=On"; en is ACTIVE_LOW in DT. Expose as params so
 * both polarities can be tried without rebuilding. */
static int j716f_dmic_en_val = 0;
module_param(j716f_dmic_en_val, int, 0644);
static int j716f_dmic_sel_val = 1;
module_param(j716f_dmic_sel_val, int, 0644);

struct sm8250_snd_data {
	bool stream_prepared[AFE_PORT_MAX];
	struct snd_soc_card *card;
	struct sdw_stream_runtime *sruntime[AFE_PORT_MAX];
	struct snd_soc_jack jack;
	bool jack_setup;
	struct gpio_desc *dmic_en;
	struct gpio_desc *dmic_sel;
};

static int sm8250_snd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct sm8250_snd_data *data = snd_soc_card_get_drvdata(rtd->card);

	return qcom_snd_wcd_jack_setup(rtd, &data->jack, &data->jack_setup);
}

static int sm8250_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				     struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *bits = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_SAMPLE_BITS);

	if (rtd->dai_link->name &&
	    (strstr(rtd->dai_link->name, "QUAT MI2S Capture") ||
	     strstr(rtd->dai_link->name, "TERT MI2S Capture"))) {
		/* ES7210 capture must follow the FE rate, not a fixed 48 kHz */
	} else {
		rate->min = rate->max = 48000;
	}

	if (rtd->dai_link->name &&
	    strstr(rtd->dai_link->name, "WCD Capture")) {
		channels->min = channels->max = wcd_capture_channels;
		dev_info(rtd->dev,
			 "j716f-mic: %s fixup channels=%d\n",
			 rtd->dai_link->name, wcd_capture_channels);
	} else if (rtd->dai_link->name &&
		   (strstr(rtd->dai_link->name, "QUAT MI2S Capture") ||
		    strstr(rtd->dai_link->name, "TERT MI2S Capture"))) {
		/* ES7210 4ch ADC: keep the channel count from hw_params */
	} else {
		channels->min = channels->max = 2;
	}
	bits->min = bits->max = 16;

	return 0;
}

static int sm8250_snd_startup(struct snd_pcm_substream *substream)
{
	unsigned int fmt = SND_SOC_DAIFMT_BP_FP;
	unsigned int codec_dai_fmt = SND_SOC_DAIFMT_BC_FC;
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int i, ret;

	/* j716f: vote the LPASS macro (tx macro / DMIC) on for capture.
	 * Stock does this via the "lpass_audio_hw_vote" clock; mainline never
	 * voted, leaving the tx macro unclocked (MMIO reads hang) and DMIC
	 * dead. Without this the captured PCM is always zero. */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		static u32 vote_handle;
		int vret;

		vret = q6afe_vote_lpass_core_hw(cpu_dai->dev,
				Q6AFE_LPASS_CORE_HW_MACRO_BLOCK, "j716f",
				&vote_handle);
		dev_info(rtd->dev,
			 "j716f: lpass macro vote ret=%d handle=%u\n",
			 vret, vote_handle);
	}

	switch (cpu_dai->id) {
	case PRIMARY_MI2S_RX:
		codec_dai_fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_I2S;
		ret = snd_soc_dai_set_sysclk(cpu_dai,
			Q6AFE_LPASS_CLK_ID_PRI_MI2S_IBIT,
			MI2S_BCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			dev_err(rtd->dev, "Primary MI2S clock setup failed: %d\n",
				ret);
		snd_soc_dai_set_fmt(cpu_dai, fmt);

		/*
		 * J716F has four CS35L41 amps on the same Primary MI2S bus.
		 * Configure every codec, not just the first one: each amp needs
		 * its ASP format and PLL source/rate programmed.
		 */
		for_each_rtd_codec_dais(rtd, i, codec_dai) {
			ret = snd_soc_dai_set_fmt(codec_dai, codec_dai_fmt);
			if (ret)
				dev_err(rtd->dev, "codec%d fmt failed: %d\n",
					i, ret);

			ret = snd_soc_dai_set_sysclk(codec_dai, 0,
						     MI2S_BCLK_RATE,
						     SNDRV_PCM_STREAM_PLAYBACK);
			if (ret)
				dev_err(rtd->dev, "codec%d sysclk failed: %d\n",
					i, ret);

			ret = snd_soc_component_set_sysclk(codec_dai->component,
							   CS35L41_CLKID_SCLK,
							   0, MI2S_BCLK_RATE, 0);
			if (ret)
				dev_err(rtd->dev, "codec%d PLL setup failed: %d\n",
					i, ret);
		}
		break;
	case SECONDARY_MI2S_RX:
		codec_dai_fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_I2S;
		snd_soc_dai_set_sysclk(cpu_dai,
			Q6AFE_LPASS_CLK_ID_SEC_MI2S_IBIT,
			MI2S_BCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);
		snd_soc_dai_set_fmt(cpu_dai, fmt);
		snd_soc_dai_set_fmt(codec_dai, codec_dai_fmt);
		break;
	case TERTIARY_MI2S_RX:
		codec_dai_fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_I2S;
		snd_soc_dai_set_sysclk(cpu_dai,
			Q6AFE_LPASS_CLK_ID_TER_MI2S_IBIT,
			MI2S_BCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);
		snd_soc_dai_set_fmt(cpu_dai, fmt);
		snd_soc_dai_set_fmt(codec_dai, codec_dai_fmt);
		break;
	case TX_CODEC_DMA_TX_3: {
		struct sm8250_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
		dev_info(rtd->dev, "j716f-dmic: startup en=%p sel=%p (en_val=%d sel_val=%d)\n",
			 data->dmic_en, data->dmic_sel, j716f_dmic_en_val, j716f_dmic_sel_val);
		if (data->dmic_en)
			gpiod_set_value(data->dmic_en, j716f_dmic_en_val);
		if (data->dmic_sel)
			gpiod_set_value(data->dmic_sel, j716f_dmic_sel_val);
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE &&
		    j716f_force_dmic_pins) {
			struct snd_soc_dai *cd;
			int ci, k;
			for_each_rtd_codec_dais(rtd, ci, cd) {
				if (!cd->component ||
				    (!strstr(cd->name, "wcd938x") &&
				     !strstr(cd->component->name, "wcd938x") &&
				     !strstr(cd->component->name, "audio-codec")))
					continue;
				for (k = 1; k <= 8; k++) {
					char pin[24];
					if (!(j716f_force_dmic_mask & BIT(k - 1)))
						continue;
					snprintf(pin, sizeof(pin), "DMIC%d", k);
					snd_soc_dapm_force_enable_pin(
						&cd->component->dapm, pin);
					snprintf(pin, sizeof(pin), "DMIC%d_MIXER", k);
					snd_soc_dapm_force_enable_pin(
						&cd->component->dapm, pin);
					snprintf(pin, sizeof(pin), "DMIC%d_OUTPUT", k);
					snd_soc_dapm_force_enable_pin(
						&cd->component->dapm, pin);
				}
				dev_info(rtd->dev,
					 "j716f: forced WCD DMIC outputs (pins)\n");
			}
		}
		break;
	}
	case TERTIARY_MI2S_TX: {
		/* j716f: ES7210 might actually be wired to the TLMM mi2s2
		 * (gpio133/134/135) rather than the LPASS quaternary MI2S.
		 * AFE is master here too, so use the internal bit clock. */
		int r1, r2, r3, r4, r5;

		r1 = snd_soc_dai_set_sysclk(cpu_dai,
			Q6AFE_LPASS_CLK_ID_TER_MI2S_IBIT,
			MI2S_BCLK_RATE, SNDRV_PCM_STREAM_CAPTURE);
		r2 = 0;
		r3 = snd_soc_dai_set_sysclk(cpu_dai,
			Q6AFE_LPASS_CLK_ID_TX_CORE_MCLK,
			19200000, SNDRV_PCM_STREAM_CAPTURE);
		r4 = snd_soc_dai_set_fmt(cpu_dai,
			SND_SOC_DAIFMT_BP_FP | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_I2S);
		r5 = snd_soc_dai_set_fmt(codec_dai,
			SND_SOC_DAIFMT_BC_FC | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_I2S);
		dev_info(rtd->dev,
			 "j716f-mic: tert startup ibit=%d txcore=%d fmt_cpu=%d fmt_codec=%d\n",
			 r1, r3, r4, r5);
		break;
	}
	case QUATERNARY_MI2S_TX: {
		int r1, r2, r3, r4, r5;
		/* j716f: AFE is the bit-clock/frame master here (see set_fmt below),
		 * so it must use the INTERNAL bit clock (IBIT), matching the stock
		 * machine driver (mi2s_clk table = 0x100-range IBIT ids). Using
		 * EBIT (external, codec-master) leaves nobody driving BCLK/WS. */
		r1 = snd_soc_dai_set_sysclk(cpu_dai,
			Q6AFE_LPASS_CLK_ID_QUAD_MI2S_IBIT,
			MI2S_BCLK_RATE * 2, SNDRV_PCM_STREAM_CAPTURE);
		/* MCLK is provided by the codec driver via the clock framework
		 * (devm_clk_get("mclk") -> q6afecc); stock does not touch it here,
		 * and the per-port MCLK_1 vote is rejected by the ADSP (status 2). */
		r2 = 0;
		r3 = snd_soc_dai_set_sysclk(cpu_dai,
			Q6AFE_LPASS_CLK_ID_TX_CORE_MCLK,
			19200000, SNDRV_PCM_STREAM_CAPTURE);
		r4 = snd_soc_dai_set_fmt(cpu_dai,
			SND_SOC_DAIFMT_BP_FP | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_I2S);
		r5 = snd_soc_dai_set_fmt(codec_dai,
			SND_SOC_DAIFMT_BC_FC | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_I2S);
		dev_info(rtd->dev,
			 "j716f-mic: quat startup ebit=%d mclk1=%d txcore=%d fmt_cpu=%d fmt_codec=%d\n",
			 r1, r2, r3, r4, r5);
		break;
	}
	default:
		break;
	}

	return qcom_snd_sdw_startup(substream);
}

static void sm8250_snd_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct sm8250_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	data->sruntime[cpu_dai->id] = NULL;
	sdw_release_stream(sruntime);
}

static int sm8250_snd_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct sm8250_snd_data *pdata = snd_soc_card_get_drvdata(rtd->card);

	int rc = qcom_snd_sdw_hw_params(substream, params,
				      &pdata->sruntime[cpu_dai->id]);
	dev_info(rtd->dev, "j716f-mic: card hw_params dai=%d rc=%d\n",
		 cpu_dai->id, rc);
	return rc;
}

static int sm8250_snd_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct sm8250_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	return qcom_snd_sdw_prepare(substream, sruntime,
				    &data->stream_prepared[cpu_dai->id]);
}

static int sm8250_snd_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sm8250_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	return qcom_snd_sdw_hw_free(substream, sruntime,
				    &data->stream_prepared[cpu_dai->id]);
}

static const struct snd_soc_ops sm8250_be_ops = {
	.startup = sm8250_snd_startup,
	.shutdown = sm8250_snd_shutdown,
	.hw_params = sm8250_snd_hw_params,
	.hw_free = sm8250_snd_hw_free,
	.prepare = sm8250_snd_prepare,
};

/* j716f: the WCD938x DMIC ADC outputs feed the LPASS tx-macro's DMIC
 * inputs. Without these cross-component routes the DAPM graph stops at
 * the tx-macro "TX DMICn" INPUT widgets, the codec's DMICn widget never
 * powers up, wcd938x_codec_enable_dmic() is never called, and the DMIC
 * clock stays off -> captured PCM is all zeros. */
static const struct snd_soc_dapm_route sm8250_snd_dapm_routes[] = {
	{ "TX DMIC0", NULL, "DMIC1" },
	{ "TX DMIC1", NULL, "DMIC2" },
	{ "TX DMIC2", NULL, "DMIC3" },
	{ "TX DMIC3", NULL, "DMIC4" },
	{ "TX DMIC4", NULL, "DMIC5" },
	{ "TX DMIC5", NULL, "DMIC6" },
	{ "TX DMIC6", NULL, "DMIC7" },
	{ "TX DMIC7", NULL, "DMIC8" },
};

static void sm8250_add_be_ops(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *link;
	int i;

	for_each_card_prelinks(card, i, link) {
		if (link->no_pcm == 1) {
			link->init = sm8250_snd_init;
			link->be_hw_params_fixup = sm8250_be_hw_params_fixup;
			link->ops = &sm8250_be_ops;
		}
	}
}

static int sm8250_platform_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct sm8250_snd_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->owner = THIS_MODULE;
	/* Allocate the private data */
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	card->dev = dev;
	card->dapm_routes = sm8250_snd_dapm_routes;
	card->num_dapm_routes = ARRAY_SIZE(sm8250_snd_dapm_routes);
	dev_set_drvdata(dev, card);
	data->dmic_en = devm_gpiod_get_optional(dev, "qcom,msm-dmic-en", GPIOD_OUT_HIGH);
	data->dmic_sel = devm_gpiod_get_optional(dev, "qcom,msm-dmic-sel", GPIOD_OUT_LOW);
	dev_info(dev, "j716f-dmic: probe en=%p sel=%p\n",
		 data->dmic_en, data->dmic_sel);
	snd_soc_card_set_drvdata(card, data);
	ret = qcom_snd_parse_of(card);
	if (ret)
		return ret;
	{
		struct snd_soc_dai_link *l;
		int i;
		for_each_card_prelinks(card, i, l)
			dev_info(dev, "j716f-mic: link[%d] %s no_pcm=%d\n",
				 i, l->name, l->no_pcm);
	}

	card->driver_name = DRIVER_NAME;
	sm8250_add_be_ops(card);
	{
		struct snd_soc_dai_link *l;
		int i;
		for_each_card_prelinks(card, i, l)
			dev_info(dev, "j716f-mic: post link[%d] %s no_pcm=%d\n",
				 i, l->name, l->no_pcm);
	}
	return devm_snd_soc_register_card(dev, card);
}

static const struct of_device_id snd_sm8250_dt_match[] = {
	{.compatible = "qcom,sm8250-sndcard"},
	{.compatible = "qcom,qrb4210-rb2-sndcard"},
	{.compatible = "qcom,qrb5165-rb5-sndcard"},
	{}
};

MODULE_DEVICE_TABLE(of, snd_sm8250_dt_match);

static struct platform_driver snd_sm8250_driver = {
	.probe  = sm8250_platform_probe,
	.driver = {
		.name = "snd-sm8250",
		.of_match_table = snd_sm8250_dt_match,
	},
};
module_platform_driver(snd_sm8250_driver);
MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@linaro.org");
MODULE_DESCRIPTION("SM8250 ASoC Machine Driver");
MODULE_LICENSE("GPL");
