/*----------------------------------------------------------------------------*
 *					INCLUDE DECLARATIONS
 *---------------------------------------------------------------------------*/
#include <linux/string.h>
#include "include/hal_hdmitx.h"
#include "include/hal_hdmitx_reg.h"
#include "include/param_cfg.inc"
#include "include/hdmi_type.h"

/*----------------------------------------------------------------------------*
 *					MACRO DECLARATIONS
 *---------------------------------------------------------------------------*/
/*about misc*/
#define mask_pack(src, mask, bit, value) (((src) & (~((mask) << (bit)))) + (((value) & (mask)) << (bit)))

/*about interrupt mask*/
#define HDMITX_INTERRUPT0_MASK_HDP (1 << INTERRUPT0_HDP)
#define HDMITX_INTERRUPT0_MASK_RSEN (1 << INTERRUPT0_RSEN)
#define HDMITX_INTERRUPT0_MASK_TCLK_FREQ (1 << INTERRUPT0_TCLK_FREQ)
#define HDMITX_INTERRUPT0_MASK_VSYNC (1 << INTERRUPT0_VSYNC)
#define HDMITX_INTERRUPT0_MASK_CEC (1 << INTERRUPT0_CEC)
#define HDMITX_INTERRUPT0_MASK_CTS (1 << INTERRUPT0_CTS)
#define HDMITX_INTERRUPT0_MASK_AUDIO_FIFO_EMPTY (1 << INTERRUPT0_AUDIO_FIFO_EMPTY)
#define HDMITX_INTERRUPT0_MASK_AUDIO_FIFO_FULL (1 << INTERRUPT0_AUDIO_FIFO_FULL)

/*about system status mask*/
#define HDMITX_SYSTEM_STUS_MASK_RSEN_IN (1 << SYSTEM_STUS_RSEN_IN)
#define HDMITX_SYSTEM_STUS_MASK_HPD_IN (1 << SYSTEM_STUS_HPD_IN)
#define HDMITX_SYSTEM_STUS_MASK_PLL_READY (1 << SYSTEM_STUS_PLL_READY)
#define HDMITX_SYSTEM_STUS_MASK_TMDS_CLK_DETECT (1 << SYSTEM_STUS_TMDS_CLK_DETECT)

/*----------------------------------------------------------------------------*
 *					DATA TYPES
 *---------------------------------------------------------------------------*/
struct hal_hdmitx_config {
	unsigned char is_hdmi;
	struct hal_hdmitx_video_attribute video;
	struct hal_hdmitx_audio_attribute audio;
};

/*----------------------------------------------------------------------------*
 *					GLOBAL VARIABLES
 *---------------------------------------------------------------------------*/
static struct hal_hdmitx_config g_hdmitx_cfg = {
	.is_hdmi = 1,
	.video = {
		.timing       = TIMING_480P,
		.color_depth  = COLOR_DEPTH_24BITS,
		.input_range  = QUANTIZATION_RANGE_LIMITED,
		.output_range = QUANTIZATION_RANGE_LIMITED,
		.input_fmt    = PIXEL_FORMAT_RGB,
		.output_fmt   = PIXEL_FORMAT_RGB,
	},

	.audio = {
		.chl         = AUDIO_CHL_I2S,
		.type        = AUDIO_TYPE_LPCM,
		.sample_size = AUDIO_SAMPLE_SIZE_16BITS,
		.layout      = AUDIO_LAYOUT_2CH,
		.sample_rate = AUDIO_SAMPLE_RATE_32000HZ,
	}
};

static unsigned char hdmi_avi_infoframe[HDMI_INFOFRAME_SIZE(AVI)] = {HDMI_INFOFRAME_TYPE_AVI, HDMI_AVI_INFOFRAME_VERSION, HDMI_AVI_INFOFRAME_SIZE, 0x00, 0x1c,
																		0x58, 0x00, 0x02, 0x00, 0x24,
																		0x00, 0x04, 0x02, 0x7a, 0x00, 0x4a, 0x03};

static unsigned char hdmi_audio_infoframe[HDMI_INFOFRAME_SIZE(AUDIO)] = {HDMI_INFOFRAME_TYPE_AUDIO, HDMI_AUDIO_INFOFRAME_VERSION, HDMI_AUDIO_INFOFRAME_SIZE, 0x00, 0x01,
																			0x00, 0x00, 0x00, 0x00, 0x00,
																			0x00, 0x00, 0x00, 0x00};

/*----------------------------------------------------------------------------*
 *					EXTERNAL DECLARATIONS
 *---------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
 *					FUNCTION DECLARATIONS
 *---------------------------------------------------------------------------*/
static void apply_pixel_clock(void)
{
	enum pixel_clk_mode clk_mode = PIXEL_CLK_MODE_27_MHZ;

	switch (g_hdmitx_cfg.video.timing) {
		case TIMING_720P60:
			clk_mode = PIXEL_CLK_MODE_74P25_MHZ;
			break;
		case TIMING_1080P60:
			clk_mode = PIXEL_CLK_MODE_148P5_MHZ;
			break;
		case TIMING_480P:
		case TIMING_576P:
		default:
			clk_mode = PIXEL_CLK_MODE_27_MHZ;
			break;
	}

	if (g_pll_tv_cfg[clk_mode].bypass == ENABLE) {
		moon4_regs->plltv_ctl[0] = 0x80000000 | (0x1 << 15);
	} else {
		moon4_regs->plltv_ctl[0] = 0x80000000;
		moon4_regs->plltv_ctl[1] = 0x01800000 | (g_pll_tv_cfg[clk_mode].r << 7);
		moon4_regs->plltv_ctl[2] = 0x7fff0000 | (g_pll_tv_cfg[clk_mode].m << 8) | (g_pll_tv_cfg[clk_mode].n);
	}
}

void apply_phy(void)
{
	unsigned int value, mask;
	struct phy_param *phy_cfg;
	enum hal_hdmitx_timing timing;
	enum hal_hdmitx_color_depth depth;

	if (g_hdmitx_cfg.video.timing < TIMING_MAX) {
		timing = g_hdmitx_cfg.video.timing;
	} else {
		timing = TIMING_480P;
	}

	if (g_hdmitx_cfg.video.color_depth < COLOR_DEPTH_MAX) {
		depth = g_hdmitx_cfg.video.color_depth;
	} else {
		depth = COLOR_DEPTH_24BITS;
	}

	phy_cfg = &g_phy_cfg[timing][depth];

	moon5_regs->sft_cfg[4] = 0x1f70000 | ((phy_cfg->aclk_mode & 0x3) << 7) \
								| ((phy_cfg->is_data_double & 0x1) << 6) \
								| ((phy_cfg->kv_mode & 0x3) << 4) \
								| ((phy_cfg->term_mode & 0x7) << 0);

	value = hdmitx_regs->hdmi_tmdstx_ctrl1;
	mask  = 0xfff3;
	hdmitx_regs->hdmi_tmdstx_ctrl1 = (value & (~mask)) | ((phy_cfg->ectr_mode & 0xf) << 12) \
										| ((phy_cfg->is_emp & 0x1) << 11) \
										| ((phy_cfg->is_clk_detector & 0x1) << 10) \
										| ((phy_cfg->fckdv_mode & 0x3) << 8) \
										| ((phy_cfg->ckinv_mode & 0xf) << 4) \
										| ((phy_cfg->is_from_odd & 0x1) << 1) \
										| ((phy_cfg->is_clk_inv & 0x1) << 0);

	value = hdmitx_regs->hdmi_tmdstx_ctrl2;
	mask  = 0xf7f2;
	hdmitx_regs->hdmi_tmdstx_ctrl2 = (value & (~mask)) | ((phy_cfg->icp_mod_mode & 0xf) << 12) \
										| ((phy_cfg->pd_d_mode & 0x7) << 8) \
										| ((phy_cfg->cpst_mode & 0xf) << 4) \
										| ((phy_cfg->icp_mode & 0x1) << 1);

	value = hdmitx_regs->hdmi_tmdstx_ctrl3;
	mask  = 0xef1f;
	hdmitx_regs->hdmi_tmdstx_ctrl3 = (value & (~mask)) | ((phy_cfg->bgr_mode & 0x7) << 13) \
										| ((phy_cfg->sw_ctrl & 0xf) << 8) \
										| ((phy_cfg->dsel_mode & 0x1f) << 0);

	value = hdmitx_regs->hdmi_tmdstx_ctrl4;
	mask  = 0xfc3f;
	hdmitx_regs->hdmi_tmdstx_ctrl4 = (value & (~mask)) | ((phy_cfg->irt_mode & 0x3f) << 10) \
										| ((phy_cfg->dcnst_mode & 0x3f) << 0);

	value = hdmitx_regs->hdmi_tmdstx_ctrl5;
	mask  = 0x2;
	hdmitx_regs->hdmi_tmdstx_ctrl5 = (value & (~mask)) | ((phy_cfg->rv_model & 0x1) << 1);
}

static void apply_test_pattern(unsigned char enable)
{
	unsigned int inter_mode = 0, step = 0, is_709 = 0, hv_sycn_inv = 0, is_rgb, is_yuv444, video_mode;
	unsigned int value;
	unsigned char is_manual = 0;

	// todo: interlace timing and not defined timing

	switch (g_hdmitx_cfg.video.timing) {
		case TIMING_576P:
			is_manual   = 1;
			video_mode  = 7;
			hv_sycn_inv = 3;
			break;
		case TIMING_720P60:
			video_mode  = 1;
			hv_sycn_inv = 0;
			break;
		case TIMING_1080P60:
			video_mode  = 2;
			hv_sycn_inv = 0;
			break;
		case TIMING_480P:
		default:
			video_mode  = 0;
			hv_sycn_inv = 3;
			break;
	}

	switch (g_hdmitx_cfg.video.input_fmt) {
		case PIXEL_FORMAT_YUV444:
			is_rgb    = 0;
			is_yuv444 = 1;
			break;
		case PIXEL_FORMAT_YUV422:
			is_rgb    = 0;
			is_yuv444 = 0;
			break;
		case PIXEL_FORMAT_RGB:
		default:
			is_rgb    = 1;
			is_yuv444 = 0;
			break;
	}

	value = ((inter_mode << 11) | (video_mode << 8) | (step << 6) | (is_709 << 5) | (is_yuv444 << 4) | (is_rgb << 3) | (hv_sycn_inv << 1) | enable);
	hdmitx_regs->hdmi_video_pat_gen1 = value;

	if (is_manual) {
		hdmitx_regs->hdmi_video_pat_gen2 = 0x360;
		hdmitx_regs->hdmi_video_pat_gen3 = 0x271;
		hdmitx_regs->hdmi_video_pat_gen4 = 0x40;
		hdmitx_regs->hdmi_video_pat_gen5 = 0x4;
		hdmitx_regs->hdmi_video_pat_gen6 = 0x84;
		hdmitx_regs->hdmi_video_pat_gen7 = 0x353;
		hdmitx_regs->hdmi_video_pat_gen8 = 0x2c;
		hdmitx_regs->hdmi_video_pat_gen9 = 0x26c;
	}
}

static void apply_avi_inforame(void)
{
	enum hdmi_colorspace color_space;
	enum hdmi_picture_aspect aspect;
	enum hdmi_colorimetry colorimetry;
	enum hdmi_video_format_code vic;
	enum hdmi_pixel_repetition_factor pixel_repetition = HDMI_NO_PIXEL_REPETITION;
	unsigned char crc;
	int i;

	/*config packet content*/
	switch (g_hdmitx_cfg.video.output_fmt) {
		case PIXEL_FORMAT_YUV444:
			color_space = HDMI_COLORSPACE_YUV444;
			break;
		case PIXEL_FORMAT_YUV422:
			color_space = HDMI_COLORSPACE_YUV422;
			break;
		case PIXEL_FORMAT_RGB:
		default:
			color_space = HDMI_COLORSPACE_RGB;
			break;
	}

	switch (g_hdmitx_cfg.video.timing) {
		case TIMING_576P:
			aspect      = HDMI_PICTURE_ASPECT_4_3;
			colorimetry = HDMI_COLORIMETRY_ITU_601;
			vic         = HDMI_VIDEO_FORMAT_CODE_576P;
			break;
		case TIMING_720P60:
			aspect      = HDMI_PICTURE_ASPECT_16_9;
			colorimetry = HDMI_COLORIMETRY_ITU_709;
			vic         = HDMI_VIDEO_FORMAT_CODE_720P60;
			break;
		case TIMING_1080P60:
			aspect      = HDMI_PICTURE_ASPECT_16_9;
			colorimetry = HDMI_COLORIMETRY_ITU_709;
			vic         = HDMI_VIDEO_FORMAT_CODE_1080P60;
			break;
		case TIMING_480P:
		default:
			aspect      = HDMI_PICTURE_ASPECT_4_3;
			colorimetry = HDMI_COLORIMETRY_ITU_601;
			vic         = HDMI_VIDEO_FORMAT_CODE_480P;
			break;
	}

	hdmi_avi_infoframe[4] = mask_pack(hdmi_avi_infoframe[4], 0x3, 5, color_space);
	hdmi_avi_infoframe[5] = mask_pack(hdmi_avi_infoframe[5], 0x3, 4, aspect);
	hdmi_avi_infoframe[5] = mask_pack(hdmi_avi_infoframe[5], 0x3, 6, colorimetry);
	hdmi_avi_infoframe[7] = mask_pack(hdmi_avi_infoframe[7], 0x7f, 0, vic);
	hdmi_avi_infoframe[8] = mask_pack(hdmi_avi_infoframe[8], 0xf, 0, pixel_repetition);

	/*apply*/
	crc = hdmi_avi_infoframe[0] + hdmi_avi_infoframe[1] + hdmi_avi_infoframe[2];

	for (i = 4; i < HDMI_INFOFRAME_SIZE(AVI); i++) {
		crc += hdmi_avi_infoframe[i];
	}

	crc = 0x100 - crc;

	hdmitx_regs->hdmi_avi_infoframe01   = crc + (hdmi_avi_infoframe[4] << 8);
	hdmitx_regs->hdmi_avi_infoframe23   = hdmi_avi_infoframe[5] + (hdmi_avi_infoframe[6]<<8);
	hdmitx_regs->hdmi_avi_infoframe45   = hdmi_avi_infoframe[7] + (hdmi_avi_infoframe[8]<<8);
	hdmitx_regs->hdmi_avi_infoframe67   = hdmi_avi_infoframe[9] + (hdmi_avi_infoframe[10]<<8);
	hdmitx_regs->hdmi_avi_infoframe89   = hdmi_avi_infoframe[11] + (hdmi_avi_infoframe[12]<<8);
	hdmitx_regs->hdmi_avi_infoframe1011 = hdmi_avi_infoframe[13] + (hdmi_avi_infoframe[14]<<8);
	hdmitx_regs->hdmi_avi_infoframe1213 = hdmi_avi_infoframe[15] + (hdmi_avi_infoframe[16]<<8);
}

static void apply_audio_inforame(void)
{
	enum hdmi_audio_channel_count chl_cnt;
	unsigned char crc;
	int i;

	/*config packet content*/
	switch (g_hdmitx_cfg.audio.layout) {
		case AUDIO_LAYOUT_6CH:
			chl_cnt = HDMI_AUDIO_CHANNEL_COUNT_6;
			break;
		case AUDIO_LAYOUT_8CH:
			chl_cnt = HDMI_AUDIO_CHANNEL_COUNT_8;
			break;
		case AUDIO_LAYOUT_2CH:
		default:
			chl_cnt = HDMI_AUDIO_CHANNEL_COUNT_2;
			break;
	}

	hdmi_audio_infoframe[4] = mask_pack(hdmi_audio_infoframe[4], 0x7, 0, chl_cnt);

	/*apply*/
	crc = hdmi_audio_infoframe[0] + hdmi_audio_infoframe[1] + hdmi_audio_infoframe[2];

	for (i = 4; i < HDMI_INFOFRAME_SIZE(AUDIO); i++) {
		crc += hdmi_audio_infoframe[i];
	}

	crc = 0x100 - crc;

	hdmitx_regs->hdmi_audio_infoframe01   = crc + (hdmi_audio_infoframe[4]<<8);
	hdmitx_regs->hdmi_audio_infoframe23   = hdmi_audio_infoframe[5] + (hdmi_audio_infoframe[6] << 8);
	hdmitx_regs->hdmi_audio_infoframe45   = hdmi_audio_infoframe[7] + (hdmi_audio_infoframe[8] << 8);
	hdmitx_regs->hdmi_audio_infoframe67   = hdmi_audio_infoframe[9] + (hdmi_audio_infoframe[10] << 8);
	hdmitx_regs->hdmi_audio_infoframe89   = hdmi_audio_infoframe[11] + (hdmi_audio_infoframe[12] << 8);
	hdmitx_regs->hdmi_audio_infoframe1011 = hdmi_audio_infoframe[13];
}

static void apply_color_space_conversion(void)
{
	unsigned int value, mode;
	unsigned char is_up, is_down;
	enum color_space in_cs, out_cs;

	switch (g_hdmitx_cfg.video.input_fmt) {
		case PIXEL_FORMAT_YUV444:
			if (g_hdmitx_cfg.video.input_range == QUANTIZATION_RANGE_LIMITED) {
				in_cs = COLOR_SPACE_LIMITED_YUV;
			} else {
				in_cs = COLOR_SPACE_FULL_YUV;
			}
			is_up = FALSE;
			break;
		case PIXEL_FORMAT_YUV422:
			if (g_hdmitx_cfg.video.input_range == QUANTIZATION_RANGE_LIMITED) {
				in_cs = COLOR_SPACE_LIMITED_YUV;
			} else {
				in_cs = COLOR_SPACE_FULL_YUV;
			}
			is_up = TRUE;
			break;
		case PIXEL_FORMAT_RGB:
		default:
			if (g_hdmitx_cfg.video.input_range == QUANTIZATION_RANGE_LIMITED) {
				in_cs = COLOR_SPACE_LIMITED_RGB;
			} else {
				in_cs = COLOR_SPACE_FULL_RGB;
			}
			is_up = FALSE;
			break;
	}

	switch (g_hdmitx_cfg.video.output_fmt) {
		case PIXEL_FORMAT_YUV444:
			if (g_hdmitx_cfg.video.output_range == QUANTIZATION_RANGE_LIMITED) {
				out_cs = COLOR_SPACE_LIMITED_YUV;
			} else {
				out_cs = COLOR_SPACE_FULL_YUV;
			}
			is_down = FALSE;
			break;
		case PIXEL_FORMAT_YUV422:
			if (g_hdmitx_cfg.video.output_range == QUANTIZATION_RANGE_LIMITED) {
				out_cs = COLOR_SPACE_LIMITED_YUV;
			} else {
				out_cs = COLOR_SPACE_FULL_YUV;
			}
			is_down = TRUE;
			break;
		case PIXEL_FORMAT_RGB:
		default:
			if (g_hdmitx_cfg.video.output_range == QUANTIZATION_RANGE_LIMITED) {
				out_cs = COLOR_SPACE_LIMITED_RGB;
			} else {
				out_cs = COLOR_SPACE_FULL_RGB;
			}
			is_down = FALSE;
			break;
	}

	value = hdmitx_regs->hdmi_video_ctrl1 & (~((0x3 << 11) | (0x3f << 4)));

	mode = ((g_csc_cfg[in_cs][out_cs].ycc_range & 0x1)
				| ((g_csc_cfg[in_cs][out_cs].rgb_range & 0x1) << 1)
				| ((g_csc_cfg[in_cs][out_cs].conversion_type & 0x1) << 2)
				| ((g_csc_cfg[in_cs][out_cs].colorimetry & 0x1) << 3));

	value |= (((mode & 0xf) << 4)
				| ((g_csc_cfg[in_cs][out_cs].on_off & 0x1) << 8)
				| ((g_csc_cfg[in_cs][out_cs].manual & 0x1) << 9));

	value |= ((is_up << 11) | (is_down << 12));

	hdmitx_regs->hdmi_video_ctrl1 = value;

	if (g_csc_cfg[in_cs][out_cs].manual)
	{
		// todo: need to config coefficient
		// limited RGB to full RGB
		// limited YUV to full YUV
		// full RGB to limited RGB
		// full YUV to limited YUV
	}
}

static void apply_video(void)
{
	unsigned int value, cd, hv_pol;


	apply_color_space_conversion();

	switch (g_hdmitx_cfg.video.color_depth) {
		case COLOR_DEPTH_30BITS:
			cd = 5;
			break;
		case COLOR_DEPTH_36BITS:
			cd = 6;
			break;
		case COLOR_DEPTH_48BITS:
			cd = 7;
			break;
		case COLOR_DEPTH_24BITS:
		default:
			cd = 4;
			break;
	}

	switch (g_hdmitx_cfg.video.timing) {
		case TIMING_576P:
			hv_pol = 3;
			break;
		case TIMING_720P60:
			hv_pol = 0;
			break;
		case TIMING_1080P60:
			hv_pol = 0;
			break;
		case TIMING_480P:
		default:
			hv_pol = 3;
			break;
	}

	value = ((cd << 4) | hv_pol);
	hdmitx_regs->hdmi_video_format = value;

	value = ((1 << 12) | g_hdmitx_cfg.is_hdmi);
	hdmitx_regs->hdmi_system_ctrl1 = value;

	hdmitx_regs->hdmi_infoframe_ctrl2 = 0x1013;
}

static void apply_audio(void)
{
	unsigned int value, chl_en;
	unsigned short chl_sts, cts_n;
	unsigned char is_spdif;

	if (g_hdmitx_cfg.audio.chl == AUDIO_CHL_I2S) {
		chl_en   = 0x1;
		is_spdif = FALSE;
	} else {
		chl_en   = 0xf;
		is_spdif = TRUE;
	}

	value  = ((3 << 14) | (2 << 12) | (1 << 10) | (chl_en << 4) | 1);

	switch (g_hdmitx_cfg.audio.sample_rate) {
		case AUDIO_SAMPLE_RATE_48000HZ:
			chl_sts = 0x200;
			cts_n   = 0x1800;
			break;
		case AUDIO_SAMPLE_RATE_44100HZ:
			chl_sts = 0x0;
			cts_n   = 0x1880;
			break;
		case AUDIO_SAMPLE_RATE_88200HZ:
			chl_sts = 0x800;
			cts_n   = 0x3100;
			break;
		case AUDIO_SAMPLE_RATE_96000HZ:
			chl_sts = 0xa00;
			cts_n   = 0x3000;
			break;
		case AUDIO_SAMPLE_RATE_176400HZ:
			chl_sts = 0xc00;
			cts_n   = 0x6200;
			break;
		case AUDIO_SAMPLE_RATE_192000HZ:
			chl_sts = 0xe00;
			cts_n   = 0x6000;
			break;
		case AUDIO_SAMPLE_RATE_32000HZ:
		default:
			chl_sts = 0x300;
			cts_n   = 0x1000;
			break;
	}

	hdmitx_regs->hdmi_audio_ctrl1      = value;
	hdmitx_regs->hdmi_audio_ctrl2      = 0x01b5;
	hdmitx_regs->hdmi_audio_chnl_sts2  = chl_sts;
	hdmitx_regs->hdmi_audio_spdif_ctrl = ((0x3f << 1) | is_spdif) ;
	hdmitx_regs->hdmi_arc_config1      = 0x1;
	hdmitx_regs->hdmi_arc_n_value1     = cts_n;
}

void hal_hdmitx_init(void)
{
	/*enable clock*/
	moon0_regs->clken[5]  = (0x10000) | (0x1);
	moon0_regs->gclken[5] = (0x10000) | (0x1);
	moon0_regs->reset[5]  = (0x10000) | (0x0);

	/*apply phy general configures*/

	/*enable all power on bits*/
	hdmitx_regs->hdmi_pwr_ctrl |= 0x1f;

	/*apply software reset*/
	hdmitx_regs->hdmi_sw_reset = 0;
	hdmitx_regs->hdmi_sw_reset = 0xff;

	/*enable interrupt*/
	hdmitx_regs->hdmi_intr0_unmask |= (HDMITX_INTERRUPT0_MASK_HDP | HDMITX_INTERRUPT0_MASK_RSEN);
}

void hal_hdmitx_deinit(void)
{
	/*disable interrupt*/
	hdmitx_regs->hdmi_intr0_unmask &= (~(HDMITX_INTERRUPT0_MASK_HDP | HDMITX_INTERRUPT0_MASK_RSEN));

	/*disable all power on bits*/
	hdmitx_regs->hdmi_pwr_ctrl &= (~0x1f);

	/*apply software reset*/
	hdmitx_regs->hdmi_sw_reset = 0;

	/*disable clock*/
	moon0_regs->clken[5]  = (0x10000) | (0x0);
	moon0_regs->gclken[5] = (0x10000) | (0x0);
	moon0_regs->reset[5]  = (0x10000) | (0x1);
}

unsigned char hal_hdmitx_get_interrupt0_status(enum hal_hdmitx_interrupt0 intr)
{
	unsigned char stus;
	unsigned int value;

	value = hdmitx_regs->hdmi_intr0_sts;

	switch (intr) {
		case INTERRUPT0_HDP:
			value &= HDMITX_INTERRUPT0_MASK_HDP;
			break;
		case INTERRUPT0_RSEN:
			value &= HDMITX_INTERRUPT0_MASK_RSEN;
			break;
		case INTERRUPT0_TCLK_FREQ:
			value &= HDMITX_INTERRUPT0_MASK_TCLK_FREQ;
			break;
		case INTERRUPT0_VSYNC:
			value &= HDMITX_INTERRUPT0_MASK_VSYNC;
			break;
		case INTERRUPT0_CEC:
			value &= HDMITX_INTERRUPT0_MASK_CEC;
			break;
		case INTERRUPT0_CTS:
			value &= HDMITX_INTERRUPT0_MASK_CTS;
			break;
		case INTERRUPT0_AUDIO_FIFO_EMPTY:
			value &= HDMITX_INTERRUPT0_MASK_AUDIO_FIFO_EMPTY;
			break;
		case INTERRUPT0_AUDIO_FIFO_FULL:
			value &= HDMITX_INTERRUPT0_MASK_AUDIO_FIFO_FULL;
			break;
		default:
			value = 0;
			break;
	}

	stus = value ? 1 : 0;

	return stus;
}

void hal_hdmitx_clear_interrupt0_status(enum hal_hdmitx_interrupt0 intr)
{
	unsigned int value;

	value = hdmitx_regs->hdmi_intr0_sts;

	switch (intr) {
		case INTERRUPT0_HDP:
			value &= (~HDMITX_INTERRUPT0_MASK_HDP);
			break;
		case INTERRUPT0_RSEN:
			value &= (~HDMITX_INTERRUPT0_MASK_RSEN);
			break;
		case INTERRUPT0_TCLK_FREQ:
			value &= (~HDMITX_INTERRUPT0_MASK_TCLK_FREQ);
			break;
		case INTERRUPT0_VSYNC:
			value &= (~HDMITX_INTERRUPT0_MASK_VSYNC);
			break;
		case INTERRUPT0_CEC:
			value &= (~HDMITX_INTERRUPT0_MASK_CEC);
			break;
		case INTERRUPT0_CTS:
			value &= (~HDMITX_INTERRUPT0_MASK_CTS);
			break;
		case INTERRUPT0_AUDIO_FIFO_EMPTY:
			value &= (~HDMITX_INTERRUPT0_MASK_AUDIO_FIFO_EMPTY);
			break;
		case INTERRUPT0_AUDIO_FIFO_FULL:
			value &= (~HDMITX_INTERRUPT0_MASK_AUDIO_FIFO_FULL);
			break;
		default:
			break;
	}

	hdmitx_regs->hdmi_intr0_sts = value;
}

unsigned char hal_hdmitx_get_system_status(enum hal_hdmitx_system_status sys)
{
	unsigned char stus;
	unsigned int value;

	value = hdmitx_regs->hdmi_system_status;

	switch (sys) {
		case SYSTEM_STUS_RSEN_IN:
			value &= HDMITX_SYSTEM_STUS_MASK_RSEN_IN;
			break;
		case SYSTEM_STUS_HPD_IN:
			value &= HDMITX_SYSTEM_STUS_MASK_HPD_IN;
			break;
		case SYSTEM_STUS_PLL_READY:
			value &= HDMITX_SYSTEM_STUS_MASK_PLL_READY;
			break;
		case SYSTEM_STUS_TMDS_CLK_DETECT:
			value &= HDMITX_SYSTEM_STUS_MASK_TMDS_CLK_DETECT;
			break;
		default:
			value = 0;
			break;
	}

	stus = value ? 1 : 0;

	return stus;
}

void hal_hdmitx_set_hdmi_mode(unsigned char is_hdmi)
{
	g_hdmitx_cfg.is_hdmi = is_hdmi;
}

void hal_hdmitx_get_video(struct hal_hdmitx_video_attribute *video)
{
	if (!video) {
		return;
	}

	memcpy(video, &g_hdmitx_cfg.video, sizeof(struct hal_hdmitx_video_attribute));
}

void hal_hdmitx_config_video(struct hal_hdmitx_video_attribute *video)
{
	if (!video) {
		return;
	}

	memcpy(&g_hdmitx_cfg.video, video, sizeof(struct hal_hdmitx_video_attribute));
}

void hal_hdmitx_get_audio(struct hal_hdmitx_audio_attribute *audio)
{
	if (!audio) {
		return;
	}

	memcpy(audio, &g_hdmitx_cfg.audio, sizeof(struct hal_hdmitx_audio_attribute));
}

void hal_hdmitx_config_audio(struct hal_hdmitx_audio_attribute *audio)
{
	if (!audio) {
		return;
	}

	memcpy(&g_hdmitx_cfg.audio, audio, sizeof(struct hal_hdmitx_audio_attribute));
}

void hal_hdmitx_start(void)
{
	// apply clock
	apply_pixel_clock();

	// apply phy
	apply_phy();

	// apply test pattern
#ifdef HDMITX_PTG
	apply_test_pattern(ENABLE);
#endif

	// apply AVI infoframe
	apply_avi_inforame();

	// apply Audio infoframe
	apply_audio_inforame();

	// apply video configurations
	apply_video();

	// apply audio configurations
	apply_audio();

	hdmitx_regs->hdmi_infoframe_ctrl1 |= (0x1b1b);
	hdmitx_regs->hdmi_tmdstx_ctrl3 = ((1 << 8) | (1 << 4) | (1 << 2) | 1);
}

void hal_hdmitx_stop(void)
{
	hdmitx_regs->hdmi_infoframe_ctrl1 = 0x1010;
	hdmitx_regs->hdmi_infoframe_ctrl2 = 0x1010;
}

void hal_hdmitx_enable_pattern(void)
{
	apply_test_pattern(ENABLE);
}

void hal_hdmitx_disable_pattern(void)
{
	apply_test_pattern(DISABLE);
}
