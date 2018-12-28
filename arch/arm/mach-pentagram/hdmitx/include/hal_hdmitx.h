#ifndef __HAL_HDMITX_H__
#define __HAL_HDMITX_H__

/*----------------------------------------------------------------------------*
 *					INCLUDE DECLARATIONS
 *---------------------------------------------------------------------------*/
#include <linux/kernel.h>

/*----------------------------------------------------------------------------*
 *					MACRO DECLARATIONS
 *---------------------------------------------------------------------------*/
#ifdef DISABLE
#undef DISABLE
#endif

#ifdef ENABLE
#undef ENABLE
#endif

#ifdef FALSE
#undef FALSE
#endif

#ifdef TRUE
#undef TRUE
#endif

#define DISABLE (0)
#define ENABLE (1)
#define FALSE (0)
#define TRUE (1)

#define HDMITX_PTG

/*----------------------------------------------------------------------------*
 *					DATA TYPES
 *---------------------------------------------------------------------------*/
enum hal_hdmitx_interrupt0 {
	INTERRUPT0_HDP = 0,
	INTERRUPT0_RSEN,
	INTERRUPT0_TCLK_FREQ,
	INTERRUPT0_VSYNC,
	INTERRUPT0_CEC,
	INTERRUPT0_CTS,
	INTERRUPT0_AUDIO_FIFO_EMPTY,
	INTERRUPT0_AUDIO_FIFO_FULL,
	INTERRUPT0_MAX
};

enum hal_hdmitx_system_status {
	SYSTEM_STUS_RSEN_IN = 0,
	SYSTEM_STUS_HPD_IN,
	SYSTEM_STUS_PLL_READY,
	SYSTEM_STUS_TMDS_CLK_DETECT,
	SYSTEM_STUS_MAX
};

enum hal_hdmitx_timing {
	TIMING_480P = 0,
	TIMING_576P,
	TIMING_720P60,
	TIMING_1080P60,
	TIMING_MAX
};

enum hal_hdmitx_color_depth {
	COLOR_DEPTH_24BITS = 0,
	COLOR_DEPTH_30BITS,
	COLOR_DEPTH_36BITS,
	COLOR_DEPTH_48BITS,
	COLOR_DEPTH_MAX
};

enum hal_hdmitx_quantization_range {
	QUANTIZATION_RANGE_LIMITED = 0,
	QUANTIZATION_RANGE_FULL,
	QUANTIZATION_RANGE_MAX,
};

enum hal_hdmitx_pixel_format {
	PIXEL_FORMAT_RGB = 0,
	PIXEL_FORMAT_YUV444,
	PIXEL_FORMAT_YUV422,
	PIXEL_FORMAT_MAX
};


enum hal_hdmitx_audio_channel {
	AUDIO_CHL_I2S = 0,
	AUDIO_CHL_SPDIF,
	AUDIO_CHL_MAX
};

enum hal_hdmitx_audio_type {
	AUDIO_TYPE_LPCM = 0,
	AUDIO_TYPE_MAX
};

enum hal_hdmitx_audio_sample_size {
	AUDIO_SAMPLE_SIZE_16BITS = 0,
	AUDIO_SAMPLE_SIZE_24BITS,
	AUDIO_SAMPLE_SIZE_MAX
};

enum hal_hdmitx_audio_layout {
	AUDIO_LAYOUT_2CH = 0,
	AUDIO_LAYOUT_6CH,
	AUDIO_LAYOUT_8CH,
	AUDIO_LAYOUT_MAX
};

enum hal_hdmitx_audio_sample_rate {
	AUDIO_SAMPLE_RATE_32000HZ = 0,
	AUDIO_SAMPLE_RATE_48000HZ,
	AUDIO_SAMPLE_RATE_44100HZ,
	AUDIO_SAMPLE_RATE_88200HZ,
	AUDIO_SAMPLE_RATE_96000HZ,
	AUDIO_SAMPLE_RATE_176400HZ,
	AUDIO_SAMPLE_RATE_192000HZ,
	AUDIO_SAMPLE_RATE_MAX,
};

struct hal_hdmitx_video_attribute {
	enum hal_hdmitx_timing timing;
	enum hal_hdmitx_color_depth color_depth;
	enum hal_hdmitx_quantization_range input_range;
	enum hal_hdmitx_quantization_range output_range;
	enum hal_hdmitx_pixel_format input_fmt;
	enum hal_hdmitx_pixel_format output_fmt;
};

struct hal_hdmitx_audio_attribute {
	enum hal_hdmitx_audio_channel chl;
	enum hal_hdmitx_audio_type type;
	enum hal_hdmitx_audio_sample_size sample_size;
	enum hal_hdmitx_audio_layout layout;
	enum hal_hdmitx_audio_sample_rate sample_rate;
};

/*----------------------------------------------------------------------------*
 *					EXTERNAL DECLARATIONS
 *---------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------*
 *					FUNCTION DECLARATIONS
 *---------------------------------------------------------------------------*/

void hal_hdmitx_init(void);

void hal_hdmitx_deinit(void);

unsigned char hal_hdmitx_get_interrupt0_status(enum hal_hdmitx_interrupt0 intr);

void hal_hdmitx_clear_interrupt0_status(enum hal_hdmitx_interrupt0 intr);

unsigned char hal_hdmitx_get_system_status(enum hal_hdmitx_system_status sys);

void hal_hdmitx_set_hdmi_mode(unsigned char is_hdmi);

void hal_hdmitx_get_video(struct hal_hdmitx_video_attribute *video);

void hal_hdmitx_config_video(struct hal_hdmitx_video_attribute *video);

void hal_hdmitx_get_audio(struct hal_hdmitx_audio_attribute *audio);

void hal_hdmitx_config_audio(struct hal_hdmitx_audio_attribute *audio);

void hal_hdmitx_start(void);

void hal_hdmitx_stop(void);

void hal_hdmitx_enable_pattern(void);

void hal_hdmitx_disable_pattern(void);

#endif // __HAL_HDMITX_H__

