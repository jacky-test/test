/*
 * ov5647.c - ov5647 Image Sensor Driver
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include "ov5647.h"

static struct regval sensor_oe_enable_regs[] = {
	{0x3000, 0x0f},	{0x3001, 0xff},	{0x3002, 0xe4},
	{REG_NULL, 0x00},
};

#if 0
static struct regval sensor_oe_disable_regs[] = {
	{0x3000, 0x00},	{0x3001, 0x00},	{0x3002, 0x00},
	{REG_NULL, 0x00},
};
#endif

static struct regval ov5647_640x480[] = {
	{0x0100, 0x00},	{0x0103, 0x01},	{0x3034, 0x08},
	{0x3035, 0x21},	{0x3036, 0x46},	{0x303c, 0x11},
	{0x3106, 0xf5},	{0x3821, 0x07},	{0x3820, 0x41},
	{0x3827, 0xec},	{0x370c, 0x0f},	{0x3612, 0x59},
	{0x3618, 0x00},	{0x5000, 0x06},	{0x5001, 0x01},
	{0x5002, 0x41},	{0x5003, 0x08},	{0x5a00, 0x08},
	{0x3000, 0x00},	{0x3001, 0x00},	{0x3002, 0x00},
	{0x3016, 0x08},	{0x3017, 0xe0},	{0x3018, 0x44},
	{0x301c, 0xf8},	{0x301d, 0xf0},	{0x3a18, 0x00},
	{0x3a19, 0xf8},	{0x3c01, 0x80},	{0x3b07, 0x0c},
	{0x380c, 0x07},	{0x380d, 0x68},	{0x380e, 0x03},
	{0x380f, 0xd8},	{0x3814, 0x31},	{0x3815, 0x31},
	{0x3708, 0x64},	{0x3709, 0x52},	{0x3808, 0x02},
	{0x3809, 0x80},	{0x380a, 0x01},	{0x380b, 0xE0},
	{0x3801, 0x00},	{0x3802, 0x00},	{0x3803, 0x00},
	{0x3804, 0x0a},	{0x3805, 0x3f},	{0x3806, 0x07},
	{0x3807, 0xa1},	{0x3811, 0x08},	{0x3813, 0x02},
	{0x3630, 0x2e},	{0x3632, 0xe2},	{0x3633, 0x23},
	{0x3634, 0x44},	{0x3636, 0x06},	{0x3620, 0x64},
	{0x3621, 0xe0},	{0x3600, 0x37},	{0x3704, 0xa0},
	{0x3703, 0x5a},	{0x3715, 0x78},	{0x3717, 0x01},
	{0x3731, 0x02},	{0x370b, 0x60},	{0x3705, 0x1a},
	{0x3f05, 0x02},	{0x3f06, 0x10},	{0x3f01, 0x0a},
	{0x3a08, 0x01},	{0x3a09, 0x27},	{0x3a0a, 0x00},
	{0x3a0b, 0xf6},	{0x3a0d, 0x04},	{0x3a0e, 0x03},
	{0x3a0f, 0x58},	{0x3a10, 0x50},	{0x3a1b, 0x58},
	{0x3a1e, 0x50},	{0x3a11, 0x60},	{0x3a1f, 0x28},
	{0x4001, 0x02},	{0x4004, 0x02},	{0x4000, 0x09},
	{0x4837, 0x24},	{0x4050, 0x6e},	{0x4051, 0x8f},
	{0x0100, 0x01},	{REG_NULL, 0x00},
};

static struct regval ov5647_1280x960 [] = {
	{0x0100, 0x00},	{0x0103, 0x01},	{0x3035, 0x11}, 
	{0x303c, 0x11}, {0x370c, 0x03}, {0x5000, 0x06}, 
	{0x5003, 0x08}, {0x5a00, 0x08}, {0x3000, 0xff}, 
	{0x3001, 0xff}, {0x3002, 0xff}, {0x301d, 0xf0}, 
	{0x3a18, 0x00}, {0x3a19, 0xf8}, {0x3c01, 0x80}, 
	{0x3b07, 0x0c}, {0x3708, 0x64}, {0x3630, 0x2e}, 
	{0x3632, 0xe2}, {0x3633, 0x23}, {0x3634, 0x44}, 
	{0x3620, 0x64}, {0x3621, 0xe0}, {0x3600, 0x37}, 
	{0x3704, 0xa0}, {0x3703, 0x5a}, {0x3715, 0x78}, 
	{0x3717, 0x01}, {0x3731, 0x02}, {0x370b, 0x60}, 
	{0x3705, 0x1a}, {0x3f05, 0x02}, {0x3f06, 0x10}, 
	{0x3f01, 0x0a}, {0x3a08, 0x01}, {0x3a0f, 0x58}, 
	{0x3a10, 0x50}, {0x3a1b, 0x58}, {0x3a1e, 0x50}, 
	{0x3a11, 0x60}, {0x3a1f, 0x28}, {0x4001, 0x02}, 
	{0x4000, 0x09}, {0x3000, 0x00}, {0x3001, 0x00}, 
	{0x3002, 0x00}, {0x3017, 0xe0}, {0x301c, 0xfc}, 
	{0x3636, 0x06}, {0x3016, 0x08}, {0x3827, 0xec}, 
	{0x3018, 0x44}, {0x3035, 0x21}, {0x3106, 0xf5}, 
	{0x3034, 0x18}, {0x301c, 0xf8}, 
	/*lens setting*/ 
	{0x5000, 0x86}, {0x5800, 0x11}, {0x5801, 0x0c}, 
	{0x5802, 0x0a}, {0x5803, 0x0b}, {0x5804, 0x0d}, 
	{0x5805, 0x13}, {0x5806, 0x09}, {0x5807, 0x05}, 
	{0x5808, 0x03}, {0x5809, 0x03}, {0x580a, 0x06}, 
	{0x580b, 0x08}, {0x580c, 0x05}, {0x580d, 0x01}, 
	{0x580e, 0x00}, {0x580f, 0x00}, {0x5810, 0x02}, 
	{0x5811, 0x06}, {0x5812, 0x05}, {0x5813, 0x01}, 
	{0x5814, 0x00}, {0x5815, 0x00}, {0x5816, 0x02}, 
	{0x5817, 0x06}, {0x5818, 0x09}, {0x5819, 0x05}, 
	{0x581a, 0x04}, {0x581b, 0x04}, {0x581c, 0x06}, 
	{0x581d, 0x09}, {0x581e, 0x11}, {0x581f, 0x0c}, 
	{0x5820, 0x0b}, {0x5821, 0x0b}, {0x5822, 0x0d}, 
	{0x5823, 0x13}, {0x5824, 0x22}, {0x5825, 0x26}, 
	{0x5826, 0x26}, {0x5827, 0x24}, {0x5828, 0x24}, 
	{0x5829, 0x24}, {0x582a, 0x22}, {0x582b, 0x20}, 
	{0x582c, 0x22}, {0x582d, 0x26}, {0x582e, 0x22}, 
	{0x582f, 0x22}, {0x5830, 0x42}, {0x5831, 0x22}, 
	{0x5832, 0x02}, {0x5833, 0x24}, {0x5834, 0x22}, 
	{0x5835, 0x22}, {0x5836, 0x22}, {0x5837, 0x26}, 
	{0x5838, 0x42}, {0x5839, 0x26}, {0x583a, 0x06}, 
	{0x583b, 0x26}, {0x583c, 0x24}, {0x583d, 0xce}, 
	/* manual AWB,manual AE,close Lenc,open WBC*/ 
	{0x3503, 0x03}, {0x3501, 0x10}, {0x3502, 0x80}, 
	{0x350a, 0x00}, {0x350b, 0x7f}, {0x5001, 0x01},
	{0x5180, 0x08}, {0x5186, 0x04}, {0x5187, 0x00}, 
	{0x5188, 0x04}, {0x5189, 0x00}, {0x518a, 0x04}, 
	{0x518b, 0x00}, {0x5000, 0x06},                                                              
	/*1280*960 Reference Setting 24M MCLK 2lane 280Mbps/lane 30fps for back to preview*/
	/*Display Out setting : 0x3808~0x3809 (Width)	/0x380A~0x380B (Height)*/                                      
	{0x3035, 0x21}, {0x3036, 0x37}, {0x3821, 0x07},
	{0x3820, 0x41}, {0x3612, 0x09}, {0x3618, 0x00},
	{0x380c, 0x07}, {0x380d, 0x68}, {0x380e, 0x03},
	{0x380f, 0xd8}, {0x3814, 0x31}, {0x3815, 0x31},
	{0x3709, 0x52}, {0x3808, 0x05}, {0x3809, 0x00},
	{0x380a, 0x03}, {0x380b, 0xc0}, {0x3800, 0x00},
	{0x3801, 0x18}, {0x3802, 0x00}, {0x3803, 0x0e},
	{0x3804, 0x0a}, {0x3805, 0x27}, {0x3806, 0x07},
	{0x3807, 0x95}, {0x4004, 0x02}, 
	{0x0100, 0x01},	{REG_NULL, 0x00},
}; 

static struct regval ov5647_2592x1944 [] = {
	{0x0100, 0x00},	{0x0103, 0x01},	{0x3035, 0x11}, 
	{0x303c, 0x11}, {0x370c, 0x03}, {0x5000, 0x06}, 
	{0x5003, 0x08}, {0x5a00, 0x08}, {0x3000, 0xff}, 
	{0x3001, 0xff}, {0x3002, 0xff}, {0x301d, 0xf0}, 
	{0x3a18, 0x00}, {0x3a19, 0xf8}, {0x3c01, 0x80}, 
	{0x3b07, 0x0c}, {0x3708, 0x64}, {0x3630, 0x2e}, 
	{0x3632, 0xe2}, {0x3633, 0x23}, {0x3634, 0x44}, 
	{0x3620, 0x64}, {0x3621, 0xe0}, {0x3600, 0x37}, 
	{0x3704, 0xa0}, {0x3703, 0x5a}, {0x3715, 0x78}, 
	{0x3717, 0x01}, {0x3731, 0x02}, {0x370b, 0x60}, 
	{0x3705, 0x1a}, {0x3f05, 0x02}, {0x3f06, 0x10}, 
	{0x3f01, 0x0a}, {0x3a08, 0x01}, {0x3a0f, 0x58}, 
	{0x3a10, 0x50}, {0x3a1b, 0x58}, {0x3a1e, 0x50}, 
	{0x3a11, 0x60}, {0x3a1f, 0x28}, {0x4001, 0x02}, 
	{0x4000, 0x09}, {0x3000, 0x00}, {0x3001, 0x00}, 
	{0x3002, 0x00}, {0x3017, 0xe0}, {0x301c, 0xfc}, 
	{0x3636, 0x06}, {0x3016, 0x08}, {0x3827, 0xec}, 
	{0x3018, 0x44}, {0x3035, 0x21}, {0x3106, 0xf5}, 
	{0x3034, 0x18}, {0x301c, 0xf8}, 
	/*lens setting*/ 
	{0x5000, 0x86}, {0x5800, 0x11}, {0x5801, 0x0c}, 
	{0x5802, 0x0a}, {0x5803, 0x0b}, {0x5804, 0x0d}, 
	{0x5805, 0x13}, {0x5806, 0x09}, {0x5807, 0x05}, 
	{0x5808, 0x03}, {0x5809, 0x03}, {0x580a, 0x06}, 
	{0x580b, 0x08}, {0x580c, 0x05}, {0x580d, 0x01}, 
	{0x580e, 0x00}, {0x580f, 0x00}, {0x5810, 0x02}, 
	{0x5811, 0x06}, {0x5812, 0x05}, {0x5813, 0x01}, 
	{0x5814, 0x00}, {0x5815, 0x00}, {0x5816, 0x02}, 
	{0x5817, 0x06}, {0x5818, 0x09}, {0x5819, 0x05}, 
	{0x581a, 0x04}, {0x581b, 0x04}, {0x581c, 0x06}, 
	{0x581d, 0x09}, {0x581e, 0x11}, {0x581f, 0x0c}, 
	{0x5820, 0x0b}, {0x5821, 0x0b}, {0x5822, 0x0d}, 
	{0x5823, 0x13}, {0x5824, 0x22}, {0x5825, 0x26}, 
	{0x5826, 0x26}, {0x5827, 0x24}, {0x5828, 0x24}, 
	{0x5829, 0x24}, {0x582a, 0x22}, {0x582b, 0x20}, 
	{0x582c, 0x22}, {0x582d, 0x26}, {0x582e, 0x22}, 
	{0x582f, 0x22}, {0x5830, 0x42}, {0x5831, 0x22}, 
	{0x5832, 0x02}, {0x5833, 0x24}, {0x5834, 0x22}, 
	{0x5835, 0x22}, {0x5836, 0x22}, {0x5837, 0x26}, 
	{0x5838, 0x42}, {0x5839, 0x26}, {0x583a, 0x06}, 
	{0x583b, 0x26}, {0x583c, 0x24}, {0x583d, 0xce}, 
	/* manual AWB,manual AE,close Lenc,open WBC*/ 
	{0x3503, 0x03}, {0x3501, 0x10}, {0x3502, 0x80}, 
	{0x350a, 0x00}, {0x350b, 0x7f}, {0x5001, 0x01},
	{0x5180, 0x08}, {0x5186, 0x04}, {0x5187, 0x00}, 
	{0x5188, 0x04}, {0x5189, 0x00}, {0x518a, 0x04}, 
	{0x518b, 0x00}, {0x5000, 0x06},                                                              
	/*2592x1944 Reference Setting 24M MCLK 2lane 280Mbps/lane 30fps*/ 
	/*Display Out setting
	  0x3808~0x3809 : Width	/0x380A~0x380B : Height*/
	{0x3035, 0x21}, {0x3036, 0x4f}, {0x3821, 0x06},
	{0x3820, 0x00}, {0x3612, 0x0b}, {0x3618, 0x04},
	{0x380c, 0x0a}, {0x380d, 0x8c}, {0x380e, 0x07},
	{0x380f, 0xb0}, {0x3814, 0x11}, {0x3815, 0x11},
	{0x3709, 0x12}, {0x3808, 0x0a}, {0x3809, 0x20},
	{0x380a, 0x07}, {0x380b, 0x98}, {0x3800, 0x00},
	{0x3801, 0x04}, {0x3802, 0x00}, {0x3803, 0x00},
	{0x3804, 0x0a}, {0x3805, 0x3b}, {0x3806, 0x07},
	{0x3807, 0xa3}, {0x4004, 0x04}, 
	{0x0100, 0x01},	{REG_NULL, 0x00},
}; 
static const struct ov5647_mode supported_modes[] = {
	{
		.width = 640,
		.height = 480,
		.reg_list = ov5647_640x480,
	},
	{
		.width = 1280,
		.height = 960,
		.reg_list = ov5647_1280x960,
	},
	{
		.width = 2592,
		.height = 1944,
		.reg_list = ov5647_2592x1944,
	},

};

/* Read registers up to 4 at a time */
static int ov5647_read_reg(struct i2c_client *client, u16 reg, unsigned int len, u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret;

	if ((len > 4) || !len)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;

	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));

	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);
	return 0;
}

/* Write registers up to 4 at a time */
static int ov5647_write_reg(struct i2c_client *client, u16 reg, u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int ov5647_write_array(struct i2c_client *client, const struct regval *regs)
{
	u32 i;
	int ret = 0;

	FUNC_DEBUG();

	for (i = 0; ((ret == 0) && (regs[i].addr != REG_NULL)); i++) {
		ret = ov5647_write_reg(client, regs[i].addr,
					ov5647_REG_VALUE_08BIT,
					regs[i].val);
	}

	return ret;
}

static int __ov5647_start_stream(struct ov5647 *ov5647)
{
	int ret;
	u8 val = MIPI_CTRL00_BUS_IDLE;

	FUNC_DEBUG();

	ret = ov5647_write_reg(ov5647->client,
				 OV5647_SW_STANDBY,
				 ov5647_REG_VALUE_08BIT,
				 ov5647_MODE_STREAMING);
	if (ret < 0)
		return ret;

	#if 0
	if (ov5647->flags & V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK)
		val |= MIPI_CTRL00_CLOCK_LANE_GATE |
		       MIPI_CTRL00_LINE_SYNC_ENABLE;
	#endif
	
	ret = ov5647_write_reg(ov5647->client, OV5647_REG_MIPI_CTRL00, ov5647_REG_VALUE_08BIT, val);
	if (ret < 0)
		return ret;

	ret = ov5647_write_reg(ov5647->client, OV5647_REG_FRAME_OFF_NUMBER, ov5647_REG_VALUE_08BIT, 0x00);
	if (ret < 0)
		return ret;

	return ov5647_write_reg(ov5647->client, OV5640_REG_PAD_OUT, ov5647_REG_VALUE_08BIT, 0x00);

}

static int __ov5647_stop_stream(struct ov5647 *ov5647)
{
	int ret;
	
	FUNC_DEBUG();

	ret = ov5647_write_reg(ov5647->client,
				OV5647_SW_STANDBY,
				ov5647_REG_VALUE_08BIT,
				ov5647_MODE_SW_STANDBY);
	if (ret < 0)
		return ret;


	ret = ov5647_write_reg(ov5647->client, OV5647_REG_MIPI_CTRL00, ov5647_REG_VALUE_08BIT, 
			MIPI_CTRL00_CLOCK_LANE_GATE | MIPI_CTRL00_BUS_IDLE | MIPI_CTRL00_CLOCK_LANE_DISABLE);
	if (ret < 0)
		return ret;

	ret = ov5647_write_reg(ov5647->client, OV5647_REG_FRAME_OFF_NUMBER, ov5647_REG_VALUE_08BIT, 0x0f);
	if (ret < 0)
		return ret;

	return ov5647_write_reg(ov5647->client, OV5640_REG_PAD_OUT, ov5647_REG_VALUE_08BIT, 0x01);


}

static int ov5647_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov5647 *ov5647 = to_ov5647(sd);
	int ret = 0;

	FUNC_DEBUG();

	mutex_lock(&ov5647->mutex);
	//on = !!on;
	if (on == ov5647->streaming)
		goto unlock_and_return;

	if (on) {
		ret = __ov5647_start_stream(ov5647);
		if (ret) {
			DBG_ERR("Start streaming failed while write sensor registers!\n");
			goto unlock_and_return;
		}
	} else {
		__ov5647_stop_stream(ov5647);
	}

	ov5647->streaming = on;

unlock_and_return:
	mutex_unlock(&ov5647->mutex);

	return ret;
}

static struct v4l2_subdev_video_ops ov5647_subdev_video_ops = {
	.s_stream       = ov5647_s_stream,
};
static struct v4l2_subdev_ops ov5647_subdev_ops = {
	.video          = &ov5647_subdev_video_ops,
};

static int ov5647_check_sensor_id(struct ov5647 *ov5647, struct i2c_client *client)
{
	u32 val = 0;
	int ret;

	ret = ov5647_read_reg(client, ov5647_REG_CHIP_ID,
			      ov5647_REG_VALUE_16BIT, &val);
	if ((ret != 0) || (val != CHIP_ID)) {
		DBG_ERR("Unexpected sensor (id = 0x%04x, ret = %d)!\n", val, ret);
		return -1;
	}
	DBG_INFO("Check sensor id success (id = 0x%04x).\n", val);

	return 0;
}

static int ov5647_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ov5647 *ov5647;
	struct v4l2_subdev *sd;
	int ret;
	u32 resetval, rdval;

	FUNC_DEBUG();

	ov5647 = devm_kzalloc(dev, sizeof(*ov5647), GFP_KERNEL);
	if (!ov5647) {
		DBG_ERR("Failed to allocate memory for \'ov5647\'!\n");
		return -ENOMEM;
	}

	ov5647->client = client;
	ov5647->sensor_data.mode = 0;


#ifdef CONFIG_OV5647_640x480
	ov5647->sensor_data.mode = 0;
#else
	#ifdef CONFIG_OV5647_1280x960
		ov5647->sensor_data.mode = 1;
	#else
		ov5647->sensor_data.mode = 2;
	#endif
#endif


	ov5647->sensor_data.fourcc = V4L2_PIX_FMT_SRGGB8;
	ov5647->cur_mode = &supported_modes[ov5647->sensor_data.mode];

	mutex_init(&ov5647->mutex);

	sd = &ov5647->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov5647_subdev_ops);
	DBG_INFO("Initialized V4L2 I2C subdevice.\n");

	ret = ov5647_check_sensor_id(ov5647, client);
	if (ret) {
		return ret;
	}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov5647_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif

	ret = v4l2_async_register_subdev(sd);
	if (ret) {
		DBG_ERR("V4L2 async register subdevice failed.\n");
		goto err_clean_entity;
	}
	DBG_INFO("Registered V4L2 sub-device successfully.\n");

	v4l2_set_subdev_hostdata(sd, &ov5647->sensor_data);

	// Set Ov5647 register
	ret = ov5647_write_array(ov5647->client, sensor_oe_enable_regs);

	if (ret < 0) {
		DBG_ERR("Write sensor_oe_enable_regs error\n");
		return ret;
	}

	ret = ov5647_read_reg(ov5647->client, OV5647_SW_STANDBY,
			ov5647_REG_VALUE_08BIT, &rdval);
	if (ret < 0)
		return ret;

	ret = ov5647_write_array(ov5647->client, ov5647->cur_mode->reg_list);
	
	if (ret < 0) {
		DBG_ERR("Write sensor default regs error\n");
		return ret;
	}

	ret = ov5647_read_reg(ov5647->client, OV5647_SW_STANDBY,
			ov5647_REG_VALUE_08BIT, &resetval);
	if (ret < 0)
		return ret;

	#if 0
	if (!(resetval & 0x01)) {
		DBG_ERR("Device was in SW standby");
		ret = ov5647_write_reg(ov5647->client, OV5647_SW_STANDBY, ov5647_REG_VALUE_08BIT, ov5647_MODE_STREAMING);
		if (ret < 0)
			return ret;
	}
	#endif

	// stream off to make the clock lane into LP-11 state.
	return __ov5647_stop_stream(ov5647);

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif

	return ret;
}

static int ov5647_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5647 *ov5647 = to_ov5647(sd);

	FUNC_DEBUG();

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&priv->subdev->entity);
#endif
	v4l2_ctrl_handler_free(&ov5647->ctrl_handler);
	mutex_destroy(&ov5647->mutex);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov5647_of_match[] = {
	{ .compatible = "ovti,ov5647" },
	{},
};
MODULE_DEVICE_TABLE(of, ov5647_of_match);
#endif

static const struct i2c_device_id ov5647_match_id[] = {
	{ "ov5647", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, ov5647_match_id);

static struct i2c_driver ov5647_i2c_driver = {
	.probe          = ov5647_probe,
	.remove         = ov5647_remove,
	.id_table       = ov5647_match_id,
	.driver = {
		.owner = THIS_MODULE,
		.name = "ov5647",
		.of_match_table = of_match_ptr(ov5647_of_match),
	},
};


static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov5647_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov5647_i2c_driver);
}

module_init(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Sunplus ov5647 sensor driver");
MODULE_LICENSE("GPL v2");
