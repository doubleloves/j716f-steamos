// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung S5K4H7 8MP CMOS image sensor driver
 *
 * Register sequences are based on the public MediaTek s5k4h7 driver
 * (Copyright (c) 2019 MediaTek Inc., GPL-2.0).
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define S5K4H7_MCLK_RATE		24000000UL
#define S5K4H7_DATA_LANES		4
#define S5K4H7_BITS_PER_SAMPLE		10
#define S5K4H7_LINK_FREQ		366000000LL
#define S5K4H7_PIXEL_RATE		292800000LL

#define S5K4H7_REG_MODE_SELECT		0x0100
#define S5K4H7_MODE_STANDBY		0x00
#define S5K4H7_MODE_STREAMING		0x01

#define S5K4H7_REG_CHIP_ID		0x0000
#define S5K4H7_CHIP_ID			0x487b

#define S5K4H7_REG_EXPOSURE		0x0202
#define S5K4H7_EXPOSURE_MIN		4
#define S5K4H7_EXPOSURE_STEP		1
#define S5K4H7_EXPOSURE_DEFAULT		1024

#define S5K4H7_REG_ANALOG_GAIN		0x0204
#define S5K4H7_ANA_GAIN_MIN		0x20
#define S5K4H7_ANA_GAIN_MAX		0x400
#define S5K4H7_ANA_GAIN_STEP		1
#define S5K4H7_ANA_GAIN_DEFAULT		0x40

#define S5K4H7_REG_VTS			0x0340
#define S5K4H7_VTS_DEFAULT		2530
#define S5K4H7_VTS_MAX			0xffff

#define S5K4H7_REG_HTS			0x0342
#define S5K4H7_HTS			3580

#define S5K4H7_WIDTH			1632
#define S5K4H7_HEIGHT			1224
#define S5K4H7_HBLANK			(S5K4H7_HTS - S5K4H7_WIDTH)
#define S5K4H7_VBLANK			(S5K4H7_VTS_DEFAULT - S5K4H7_HEIGHT)

struct s5k4h7_reg {
	u16 address;
	u8 val;
};

/* Sensor core initialization */
static const struct s5k4h7_reg s5k4h7_init_regs[] = {
	{ 0x0100, 0x00 },
	{ 0x0b05, 0x01 },
	{ 0x3074, 0x06 },
	{ 0x3075, 0x2f },
	{ 0x308a, 0x20 },
	{ 0x308b, 0x08 },
	{ 0x308c, 0x0b },
	{ 0x3081, 0x07 },
	{ 0x307b, 0x85 },
	{ 0x307a, 0x0a },
	{ 0x3079, 0x0a },
	{ 0x306e, 0x71 },
	{ 0x306f, 0x28 },
	{ 0x301f, 0x20 },
	{ 0x306b, 0x9a },
	{ 0x3091, 0x1f },
	{ 0x30c4, 0x06 },
	{ 0x3200, 0x09 },
	{ 0x306a, 0x79 },
	{ 0x30b0, 0xff },
	{ 0x306d, 0x08 },
	{ 0x3080, 0x00 },
	{ 0x3929, 0x3f },
	{ 0x3084, 0x16 },
	{ 0x3070, 0x0f },
	{ 0x3b45, 0x01 },
	{ 0x30c2, 0x05 },
	{ 0x3069, 0x87 },
	{ 0x3924, 0x7f },
	{ 0x3925, 0xfd },
	{ 0x3c08, 0xff },
	{ 0x3c09, 0xff },
	{ 0x3c31, 0xff },
	{ 0x3c32, 0xff },
	{ 0x0a02, 0x14 },
	{ 0x300a, 0x52 },
	{ 0x3012, 0x52 },
	{ 0x3013, 0x36 },
	{ 0x3019, 0x5f },
	{ 0x301a, 0x57 },
	{ 0x3024, 0x10 },
	{ 0x3025, 0x4e },
	{ 0x3026, 0x9a },
	{ 0x302d, 0x0b },
	{ 0x302e, 0x09 },
};

/* 1632x1224 2x2-binned preview mode, 30 fps, 4 lanes */
static const struct s5k4h7_reg s5k4h7_preview_regs[] = {
	{ 0x0136, 0x18 },
	{ 0x0137, 0x00 },
	{ 0x0305, 0x06 },
	{ 0x0306, 0x00 },
	{ 0x0307, 0x88 },
	{ 0x030d, 0x06 },
	{ 0x030e, 0x00 },
	{ 0x030f, 0xb7 },
	{ 0x3c1f, 0x00 },
	{ 0x3c17, 0x00 },
	{ 0x3c1c, 0x05 },
	{ 0x3c1d, 0x15 },
	{ 0x0301, 0x04 },
	{ 0x0820, 0x02 },
	{ 0x0821, 0xdc },
	{ 0x0822, 0x00 },
	{ 0x0823, 0x00 },
	{ 0x0112, 0x0a },
	{ 0x0113, 0x0a },
	{ 0x0114, 0x03 },
	{ 0x3906, 0x00 },
	{ 0x0344, 0x00 },
	{ 0x0345, 0x08 },
	{ 0x0346, 0x00 },
	{ 0x0347, 0x08 },
	{ 0x0348, 0x0c },
	{ 0x0349, 0xc7 },
	{ 0x034a, 0x09 },
	{ 0x034b, 0x97 },
	{ 0x034c, 0x06 },
	{ 0x034d, 0x60 },
	{ 0x034e, 0x04 },
	{ 0x034f, 0xc8 },
	{ 0x0900, 0x01 },
	{ 0x0901, 0x22 },
	{ 0x0381, 0x01 },
	{ 0x0383, 0x01 },
	{ 0x0385, 0x01 },
	{ 0x0387, 0x03 },
	{ 0x0101, 0x00 },
	{ 0x0340, 0x09 },
	{ 0x0341, 0xe2 },
	{ 0x0342, 0x0d },
	{ 0x0343, 0xfc },
	{ 0x0200, 0x0d },
	{ 0x0201, 0x6c },
	{ 0x0202, 0x00 },
	{ 0x0203, 0x02 },
	{ 0x3400, 0x01 },
};

struct s5k4h7 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;

	struct clk *img_clk;
	struct regulator *avdd;
	struct regulator *dovdd;
	struct regulator *dvdd;
	struct gpio_desc *reset;

	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *analog_gain;

	struct mutex mutex;
	bool streaming;
	bool identified;
};

static inline struct s5k4h7 *to_s5k4h7(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5k4h7, sd);
}

static int s5k4h7_read_reg(struct s5k4h7 *s5k4h7, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k4h7->sd);
	u8 addr[2] = { reg >> 8, reg & 0xff };
	u8 data[4] = { 0 };
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = sizeof(addr),
			.buf = addr,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = data,
		},
	};
	u32 v = 0;
	int ret, i;

	if (len > sizeof(data))
		return -EINVAL;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	for (i = 0; i < len; i++)
		v = (v << 8) | data[i];

	*val = v;
	return 0;
}

static int s5k4h7_write_reg(struct s5k4h7 *s5k4h7, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k4h7->sd);
	u8 buf[6];
	int i;

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;
	for (i = 0; i < len; i++)
		buf[2 + i] = val >> (8 * (len - 1 - i));

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int s5k4h7_write_regs(struct s5k4h7 *s5k4h7,
			     const struct s5k4h7_reg *regs, u32 len)
{
	int ret;
	u32 i;

	for (i = 0; i < len; i++) {
		ret = s5k4h7_write_reg(s5k4h7, regs[i].address, 1,
				       regs[i].val);
		if (ret) {
			dev_err(s5k4h7->sd.dev,
				"failed to write reg 0x%04x: %d\n",
				regs[i].address, ret);
			return ret;
		}
	}

	return 0;
}

static int s5k4h7_power_on(struct s5k4h7 *s5k4h7)
{
	int ret;

	ret = regulator_enable(s5k4h7->dovdd);
	if (ret)
		return ret;

	usleep_range(50, 150);

	ret = regulator_enable(s5k4h7->avdd);
	if (ret)
		goto err_dovdd;

	ret = regulator_enable(s5k4h7->dvdd);
	if (ret)
		goto err_avdd;

	ret = clk_prepare_enable(s5k4h7->img_clk);
	if (ret)
		goto err_dvdd;

	gpiod_set_value_cansleep(s5k4h7->reset, 0);
	usleep_range(10000, 12000);

	dev_info(s5k4h7->sd.dev,
		 "power on: dovdd=%d avdd=%d dvdd=%d mclk=%lu\n",
		 regulator_get_voltage(s5k4h7->dovdd),
		 regulator_get_voltage(s5k4h7->avdd),
		 regulator_get_voltage(s5k4h7->dvdd),
		 clk_get_rate(s5k4h7->img_clk));

	return 0;

err_dvdd:
	regulator_disable(s5k4h7->dvdd);
err_avdd:
	regulator_disable(s5k4h7->avdd);
err_dovdd:
	regulator_disable(s5k4h7->dovdd);
	return ret;
}

static void s5k4h7_power_off(struct s5k4h7 *s5k4h7)
{
	gpiod_set_value_cansleep(s5k4h7->reset, 1);
	clk_disable_unprepare(s5k4h7->img_clk);
	regulator_disable(s5k4h7->dvdd);
	regulator_disable(s5k4h7->avdd);
	regulator_disable(s5k4h7->dovdd);
}

static int s5k4h7_identify(struct s5k4h7 *s5k4h7)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k4h7->sd);
	u32 id;
	int ret;

	if (s5k4h7->identified)
		return 0;

	ret = s5k4h7_read_reg(s5k4h7, S5K4H7_REG_CHIP_ID, 2, &id);
	if (ret) {
		dev_err(&client->dev, "failed to read chip id: %d\n", ret);
		return ret;
	}

	dev_info(&client->dev, "chip id: 0x%04x\n", id);
	if (id != S5K4H7_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: 0x%04x != 0x%04x\n",
			id, S5K4H7_CHIP_ID);
		return -EIO;
	}

	s5k4h7->identified = true;
	return 0;
}

static int s5k4h7_start_streaming(struct s5k4h7 *s5k4h7)
{
	int ret;

	ret = s5k4h7_identify(s5k4h7);
	if (ret)
		return ret;

	ret = s5k4h7_write_regs(s5k4h7, s5k4h7_init_regs,
				ARRAY_SIZE(s5k4h7_init_regs));
	if (ret)
		return ret;

	ret = s5k4h7_write_regs(s5k4h7, s5k4h7_preview_regs,
				ARRAY_SIZE(s5k4h7_preview_regs));
	if (ret)
		return ret;

	ret = __v4l2_ctrl_handler_setup(&s5k4h7->ctrl_handler);
	if (ret)
		return ret;

	ret = s5k4h7_write_reg(s5k4h7, S5K4H7_REG_MODE_SELECT, 1,
			       S5K4H7_MODE_STREAMING);
	if (ret)
		return ret;

	usleep_range(10000, 12000);
	return 0;
}

static int s5k4h7_stop_streaming(struct s5k4h7 *s5k4h7)
{
	return s5k4h7_write_reg(s5k4h7, S5K4H7_REG_MODE_SELECT, 1,
				S5K4H7_MODE_STANDBY);
}

static int s5k4h7_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct s5k4h7 *s5k4h7 = to_s5k4h7(sd);
	int ret = 0;

	mutex_lock(&s5k4h7->mutex);
	if (enable) {
		ret = s5k4h7_power_on(s5k4h7);
		if (ret)
			goto out_unlock;

		s5k4h7->streaming = true;
		ret = s5k4h7_start_streaming(s5k4h7);
		if (ret) {
			s5k4h7->streaming = false;
			s5k4h7_power_off(s5k4h7);
			goto out_unlock;
		}
	} else {
		s5k4h7_stop_streaming(s5k4h7);
		s5k4h7->streaming = false;
		s5k4h7_power_off(s5k4h7);
	}
out_unlock:
	mutex_unlock(&s5k4h7->mutex);
	return ret;
}

static int s5k4h7_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5k4h7 *s5k4h7 = container_of(ctrl->handler,
					     struct s5k4h7, ctrl_handler);
	int ret = 0;

	if (!s5k4h7->streaming)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = s5k4h7_write_reg(s5k4h7, S5K4H7_REG_EXPOSURE, 2,
				       ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = s5k4h7_write_reg(s5k4h7, S5K4H7_REG_ANALOG_GAIN, 2,
				       ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = s5k4h7_write_reg(s5k4h7, S5K4H7_REG_VTS, 2,
				       S5K4H7_HEIGHT + ctrl->val);
		break;
	default:
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops s5k4h7_ctrl_ops = {
	.s_ctrl = s5k4h7_set_ctrl,
};

static const s64 s5k4h7_link_freq_menu[] = {
	S5K4H7_LINK_FREQ,
};

static int s5k4h7_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = v4l2_subdev_state_get_format(fh->state, 0);
	fmt->width = S5K4H7_WIDTH;
	fmt->height = S5K4H7_HEIGHT;
	fmt->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	fmt->field = V4L2_FIELD_NONE;

	return 0;
}

static int s5k4h7_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	return 0;
}

static int s5k4h7_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index)
		return -EINVAL;

	fse->min_width = S5K4H7_WIDTH;
	fse->max_width = S5K4H7_WIDTH;
	fse->min_height = S5K4H7_HEIGHT;
	fse->max_height = S5K4H7_HEIGHT;

	return 0;
}

static int s5k4h7_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *mf;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		mf = v4l2_subdev_state_get_format(state, fmt->pad);
	else
		mf = v4l2_subdev_state_get_format(state, fmt->pad);

	fmt->format = *mf;
	return 0;
}

static int s5k4h7_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *mf;

	mf = v4l2_subdev_state_get_format(state, fmt->pad);
	mf->width = S5K4H7_WIDTH;
	mf->height = S5K4H7_HEIGHT;
	mf->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	mf->field = V4L2_FIELD_NONE;
	mf->colorspace = V4L2_COLORSPACE_RAW;
	mf->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	mf->quantization = V4L2_QUANTIZATION_DEFAULT;
	mf->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	fmt->format = *mf;
	return 0;
}

static const struct v4l2_subdev_video_ops s5k4h7_video_ops = {
	.s_stream = s5k4h7_set_stream,
};

static const struct v4l2_subdev_pad_ops s5k4h7_pad_ops = {
	.enum_mbus_code = s5k4h7_enum_mbus_code,
	.enum_frame_size = s5k4h7_enum_frame_size,
	.get_fmt = s5k4h7_get_fmt,
	.set_fmt = s5k4h7_set_fmt,
};

static const struct v4l2_subdev_ops s5k4h7_subdev_ops = {
	.video = &s5k4h7_video_ops,
	.pad = &s5k4h7_pad_ops,
};

static const struct media_entity_operations s5k4h7_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops s5k4h7_internal_ops = {
	.open = s5k4h7_open,
};

static int s5k4h7_init_controls(struct s5k4h7 *s5k4h7)
{
	struct v4l2_ctrl_handler *hdl = &s5k4h7->ctrl_handler;
	const s64 pixel_rate = S5K4H7_PIXEL_RATE;
	s64 exposure_max = S5K4H7_VTS_DEFAULT - 8;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 8);
	if (ret)
		return ret;

	hdl->lock = &s5k4h7->mutex;

	s5k4h7->link_freq = v4l2_ctrl_new_int_menu(hdl, &s5k4h7_ctrl_ops,
						   V4L2_CID_LINK_FREQ, 0, 0,
						   s5k4h7_link_freq_menu);
	if (s5k4h7->link_freq)
		s5k4h7->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	s5k4h7->pixel_rate = v4l2_ctrl_new_std(hdl, &s5k4h7_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       pixel_rate, pixel_rate, 1,
					       pixel_rate);
	if (s5k4h7->pixel_rate)
		s5k4h7->pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	s5k4h7->vblank = v4l2_ctrl_new_std(hdl, &s5k4h7_ctrl_ops,
					   V4L2_CID_VBLANK, S5K4H7_VBLANK,
					   S5K4H7_VTS_MAX - S5K4H7_HEIGHT, 1,
					   S5K4H7_VBLANK);

	s5k4h7->hblank = v4l2_ctrl_new_std(hdl, &s5k4h7_ctrl_ops,
					   V4L2_CID_HBLANK, S5K4H7_HBLANK,
					   S5K4H7_HBLANK, 1, S5K4H7_HBLANK);
	if (s5k4h7->hblank)
		s5k4h7->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	s5k4h7->exposure = v4l2_ctrl_new_std(hdl, &s5k4h7_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     S5K4H7_EXPOSURE_MIN, exposure_max,
					     S5K4H7_EXPOSURE_STEP,
					     S5K4H7_EXPOSURE_DEFAULT);

	s5k4h7->analog_gain = v4l2_ctrl_new_std(hdl, &s5k4h7_ctrl_ops,
						V4L2_CID_ANALOGUE_GAIN,
						S5K4H7_ANA_GAIN_MIN,
						S5K4H7_ANA_GAIN_MAX,
						S5K4H7_ANA_GAIN_STEP,
						S5K4H7_ANA_GAIN_DEFAULT);

	if (hdl->error) {
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		return ret;
	}

	s5k4h7->sd.ctrl_handler = hdl;
	return 0;
}

static int s5k4h7_check_hwcfg(struct s5k4h7 *s5k4h7)
{
	struct device *dev = s5k4h7->sd.dev;
	struct v4l2_fwnode_endpoint ep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct fwnode_handle *endpoint;
	u32 ext_clk;
	int ret;

	if (!fwnode)
		return -ENXIO;

	endpoint = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!endpoint) {
		dev_warn(dev, "no endpoint, probe in standalone mode\n");
		return 0;
	}

	ret = fwnode_property_read_u32(fwnode, "clock-frequency", &ext_clk);
	if (ret) {
		dev_err(dev, "missing clock-frequency\n");
		goto out;
	}

	if (ext_clk != S5K4H7_MCLK_RATE) {
		dev_err(dev, "unsupported clock-frequency %u\n", ext_clk);
		ret = -EINVAL;
		goto out;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep);
	if (ret)
		goto out;

	if (ep.bus.mipi_csi2.num_data_lanes != S5K4H7_DATA_LANES) {
		dev_err(dev, "unsupported lane count %u\n",
			ep.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto out_free;
	}

	if (!ep.nr_of_link_frequencies) {
		dev_err(dev, "no link frequency\n");
		ret = -EINVAL;
		goto out_free;
	}

	if (ep.link_frequencies[0] != S5K4H7_LINK_FREQ) {
		dev_err(dev, "unsupported link frequency %lld\n",
			ep.link_frequencies[0]);
		ret = -EINVAL;
		goto out_free;
	}

	ret = 0;
out_free:
	v4l2_fwnode_endpoint_free(&ep);
out:
	fwnode_handle_put(endpoint);
	return ret;
}

static int s5k4h7_probe(struct i2c_client *client)
{
	struct s5k4h7 *s5k4h7;
	int ret;

	s5k4h7 = devm_kzalloc(&client->dev, sizeof(*s5k4h7), GFP_KERNEL);
	if (!s5k4h7)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&s5k4h7->sd, client, &s5k4h7_subdev_ops);
	mutex_init(&s5k4h7->mutex);

	s5k4h7->img_clk = devm_clk_get(&client->dev, NULL);
	if (IS_ERR(s5k4h7->img_clk))
		return dev_err_probe(&client->dev, PTR_ERR(s5k4h7->img_clk),
				     "failed to get mclk\n");

	ret = clk_set_rate(s5k4h7->img_clk, S5K4H7_MCLK_RATE);
	if (ret)
		return dev_err_probe(&client->dev, ret, "failed to set mclk\n");

	s5k4h7->reset = devm_gpiod_get(&client->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(s5k4h7->reset))
		return dev_err_probe(&client->dev, PTR_ERR(s5k4h7->reset),
				     "failed to get reset gpio\n");

	s5k4h7->avdd = devm_regulator_get(&client->dev, "avdd");
	if (IS_ERR(s5k4h7->avdd))
		return dev_err_probe(&client->dev, PTR_ERR(s5k4h7->avdd),
				     "failed to get avdd\n");

	s5k4h7->dovdd = devm_regulator_get(&client->dev, "dovdd");
	if (IS_ERR(s5k4h7->dovdd))
		return dev_err_probe(&client->dev, PTR_ERR(s5k4h7->dovdd),
				     "failed to get dovdd\n");

	s5k4h7->dvdd = devm_regulator_get(&client->dev, "dvdd");
	if (IS_ERR(s5k4h7->dvdd))
		return dev_err_probe(&client->dev, PTR_ERR(s5k4h7->dvdd),
				     "failed to get dvdd\n");

	ret = s5k4h7_check_hwcfg(s5k4h7);
	if (ret)
		return dev_err_probe(&client->dev, ret, "failed to check hwcfg\n");

	ret = s5k4h7_init_controls(s5k4h7);
	if (ret)
		return ret;

	ret = s5k4h7_power_on(s5k4h7);
	if (ret) {
		dev_err(&client->dev, "failed to power on\n");
		goto err_ctrl;
	}

	ret = s5k4h7_identify(s5k4h7);
	s5k4h7_power_off(s5k4h7);
	if (ret) {
		dev_err(&client->dev, "sensor not found\n");
		goto err_ctrl;
	}

	s5k4h7->sd.internal_ops = &s5k4h7_internal_ops;
	s5k4h7->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	s5k4h7->sd.entity.ops = &s5k4h7_entity_ops;
	s5k4h7->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	s5k4h7->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&s5k4h7->sd.entity, 1, &s5k4h7->pad);
	if (ret)
		goto err_ctrl;

	ret = v4l2_subdev_init_finalize(&s5k4h7->sd);
	if (ret)
		goto err_entity;

	ret = v4l2_async_register_subdev_sensor(&s5k4h7->sd);
	if (ret)
		goto err_subdev;

	return 0;

err_subdev:
	v4l2_subdev_cleanup(&s5k4h7->sd);
err_entity:
	media_entity_cleanup(&s5k4h7->sd.entity);
err_ctrl:
	v4l2_ctrl_handler_free(&s5k4h7->ctrl_handler);
	mutex_destroy(&s5k4h7->mutex);
	return ret;
}

static void s5k4h7_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k4h7 *s5k4h7 = to_s5k4h7(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&s5k4h7->ctrl_handler);
	mutex_destroy(&s5k4h7->mutex);
}

static const struct of_device_id s5k4h7_of_match[] = {
	{ .compatible = "samsung,s5k4h7" },
	{ }
};
MODULE_DEVICE_TABLE(of, s5k4h7_of_match);

static struct i2c_driver s5k4h7_i2c_driver = {
	.driver = {
		.name = "s5k4h7",
		.of_match_table = s5k4h7_of_match,
	},
	.probe = s5k4h7_probe,
	.remove = s5k4h7_remove,
};
module_i2c_driver(s5k4h7_i2c_driver);

MODULE_AUTHOR("J716F Linux bring-up");
MODULE_DESCRIPTION("Samsung S5K4H7 sensor driver");
MODULE_LICENSE("GPL v2");
