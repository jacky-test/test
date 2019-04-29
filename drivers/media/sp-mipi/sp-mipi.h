#ifndef __SP_MIPI_H__
#define __SP_MIPI_H__

#include <media/v4l2-dev.h>
#include <linux/videodev2.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>


#define VOUT_NAME                       "sp_vout"
#define MIPICSI_REG_NAME                "mipicsi"
#define CSIIW_REG_NAME                  "csiiw"

#define norm_maxw()                     1280
#define norm_maxh()                     800

#define mEXTENDED_ALIGNED(w,n)          (w%n)? ((w/n)*n+n): (w)
#define FRAME_BUFFER_ALIGN              256


#if 0
#define DBG_INFO(fmt, args ...)         printk(KERN_INFO "[MIPI] " fmt, ## args)
#else
#define DBG_INFO(fmt, args ...)
#endif
#define MIP_INFO(fmt, args ...)         printk(KERN_INFO "[MIPI] " fmt, ## args)
#define MIP_ERR(fmt, args ...)          printk(KERN_ERR "[MIPI] ERR: " fmt, ## args)


static void print_List(struct list_head *head){
	struct list_head *listptr;
	struct videobuf_buffer *entry;

	DBG_INFO("*********************************************************************************\n");
	DBG_INFO("(HEAD addr =  %p, next = %p, prev = %p)\n", head, head->next, head->prev);
	list_for_each(listptr, head) {
		entry = list_entry(listptr, struct videobuf_buffer, stream);
		DBG_INFO("list addr = %p | next = %p | prev = %p\n", &entry->stream, entry->stream.next,
			 entry->stream.prev);
	}
	DBG_INFO("*********************************************************************************\n");
}

static unsigned int vid_limit = 16;     //16M

typedef enum {
	CAM_Capture_NONE = 0,
	CAM_Capture_BEGIN = 1,
	CAM_Capture_ACTIVE = 2,
	CAM_Capture_DONE = 3,
} DRV_CAMERA_Capture_Staute;

struct sp_vout_subdev_info {
	char                            name[32];               /* Sub device name */
	int                             grp_id;                 /* Sub device group id */
	struct i2c_board_info           board_info;             /* i2c subdevice board info */
};

struct sp_vout_config {
	int                             num_subdevs;            /* Number of sub devices connected to vpfe */
	int                             i2c_adapter_id;         /* i2c bus adapter no */
	struct sp_vout_subdev_info      *sub_devs;              /* information about each subdev */
};

struct mipicsi_reg {
	volatile unsigned int mipicsi_ststus;                   /* 00 (mipicsi) */
	volatile unsigned int mipi_debug0;                      /* 01 (mipicsi) */
	volatile unsigned int mipi_wc_lpf;                      /* 02 (mipicsi) */
	volatile unsigned int mipi_analog_cfg0;                 /* 03 (mipicsi) */
	volatile unsigned int mipi_analog_cfg1;                 /* 04 (mipicsi) */
	volatile unsigned int mipicsi_fsm_rst;                  /* 05 (mipicsi) */
	volatile unsigned int mipi_analog_cfg2;                 /* 06 (mipicsi) */
	volatile unsigned int mipicsi_enable;                   /* 07 (mipicsi) */
	volatile unsigned int mipicsi_mix_cfg;                  /* 08 (mipicsi) */
	volatile unsigned int mipicsi_delay_ctl;                /* 09 (mipicsi) */
	volatile unsigned int mipicsi_packet_size;              /* 10 (mipicsi) */
	volatile unsigned int mipicsi_sot_syncword;             /* 11 (mipicsi) */
	volatile unsigned int mipicsi_sof_sol_syncword;         /* 12 (mipicsi) */
	volatile unsigned int mipicsi_eof_eol_syncword;         /* 13 (mipicsi) */
	volatile unsigned int mipicsi_reserved_a14;             /* 14 (mipicsi) */
	volatile unsigned int mipicsi_reserved_a15;             /* 15 (mipicsi) */
	volatile unsigned int mipicsi_ecc_error;                /* 16 (mipicsi) */
	volatile unsigned int mipicsi_crc_error;                /* 17 (mipicsi) */
	volatile unsigned int mipicsi_ecc_cfg;                  /* 18 (mipicsi) */
	volatile unsigned int mipi_analog_cfg3;                 /* 19 (mipicsi) */
	volatile unsigned int mipi_analog_cfg4;                 /* 20 (mipicsi) */
};

struct csiiw_reg {
	volatile unsigned int csiiw_latch_mode;                 /* 00 (csiiw) */
	volatile unsigned int csiiw_config0;                    /* 01 (csiiw) */
	volatile unsigned int csiiw_base_addr;                  /* 02 (csiiw) */
	volatile unsigned int csiiw_stride;                     /* 03 (csiiw) */
	volatile unsigned int csiiw_frame_size;                 /* 04 (csiiw) */
	volatile unsigned int csiiw_frame_buf;                  /* 05 (csiiw) */
	volatile unsigned int csiiw_config1;                    /* 06 (csiiw) */
	volatile unsigned int csiiw_frame_size_ro;              /* 07 (csiiw) */
};

struct sp_vout_device {
	struct mipicsi_reg              *mipicsi_regs;
	struct csiiw_reg                *csiiw_regs;
	struct clk                      *mipicsi_clk;
	struct clk                      *csiiw_clk;
	struct reset_control            *mipicsi_rstc;
	struct reset_control            *csiiw_rstc;
	u32                             i2c_id;

	struct device                   *pdev;                  /* parent device */
	struct video_device             video_dev;
	struct videobuf_buffer          *cur_frm;               /* Pointer pointing to current v4l2_buffer */
	struct videobuf_buffer          *next_frm;              /* Pointer pointing to next v4l2_buffer */
	struct videobuf_queue           buffer_queue;           /* Buffer queue used in video-buf */
	struct list_head                dma_queue;              /* Queue of filled frames */

	struct v4l2_device              v4l2_dev;
	struct v4l2_format              fmt;                    /* Used to store pixel format */
	struct v4l2_rect                crop;
	struct v4l2_rect                win;
	struct v4l2_control             ctrl;
	enum v4l2_buf_type              type;
	enum v4l2_memory                memory;
	struct v4l2_subdev              **sd;
	struct sp_vout_subdev_info      *current_subdev;        /* ptr to currently selected sub device */
	struct sp_vout_config           *cfg;
	struct i2c_adapter              *i2c_adap;

	spinlock_t                      irqlock;                /* Used in video-buf */
	spinlock_t                      dma_queue_lock;         /* IRQ lock for DMA queue */
	struct                          mutex lock;             /* lock used to access this structure */

	int                             baddr;
	int                             fs_irq;
	int                             fe_irq;
	u32                             io_usrs;                /* number of users performing IO */
	u8                              started;                /* Indicates whether streaming started */
	u8                              capture_status;
//      u32                             usrs;                   /* number of open instances of the channel */
//      u8                              initialized;            /* flag to indicate whether decoder is initialized */
};

/* File handle structure */
struct sp_vout_fh {
	struct v4l2_fh          fh;
	struct sp_vout_device   *vout;
	u8                      io_allowed;                     /* Indicates whether this file handle is doing IO */
};

struct sp_fmt {
	char    *name;
	u32     fourcc;                                         /* v4l2 format id */
	int     depth;
};

#endif
