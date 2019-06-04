/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the BSD Licence, GNU General Public License
 * as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */

#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <linux/freezer.h>
#include <mach/io_map.h>
#include <media/videobuf-dma-contig.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf-core.h>
#include <media/v4l2-common.h>
#include <linux/i2c.h>
#ifdef CONFIG_PM_RUNTIME_MIPI
#include <linux/pm_runtime.h>
#endif
#include "sp-mipi.h"


/* ------------------------------------------------------------------
	Constants
   ------------------------------------------------------------------*/
static const struct sp_fmt ov9281_formats[] = {
	{
		.name     = "GREY, RAW10",
		.fourcc   = V4L2_PIX_FMT_GREY,
		.width    = 1280,
		.height   = 800,
		.depth    = 8,
		.walign   = 1,
		.halign   = 1,
		.mipi_lane = 2,
		.sol_sync = SYNC_RAW10,
	},
};

static const struct sp_fmt gc0310_formats[] = {
	{
		.name     = "BAYER, RAW8",
		.fourcc   = V4L2_PIX_FMT_SRGGB8,
		.width    = 640,
		.height   = 480,
		.depth    = 8,
		.walign   = 2,
		.halign   = 2,
		.mipi_lane = 1,
		.sol_sync = SYNC_RAW8,
	},
	{
		.name     = "YUYV/YUY2, YUV422",
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.width    = 640,
		.height   = 480,
		.depth    = 16,
		.walign   = 2,
		.halign   = 1,
		.mipi_lane = 1,
		.sol_sync = SYNC_YUY2,
	},
};

static const struct sp_fmt ov9281_isp_formats[] = {
	{
		.name     = "YUYV/YUY2, YUV422",        // for SunplusIT ov9281_isp
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.width    = 1280,
		.height   = 720,
		.depth    = 16,
		.walign   = 2,
		.halign   = 1,
		.mipi_lane = 4,
		.sol_sync = SYNC_YUY2,
	},
};

static struct sp_mipi_subdev_info sp_mipi_sub_devs[] = {
	{
		.name = "ov9281",
		.grp_id = 0,
		.board_info = {
			I2C_BOARD_INFO("ov9281", 0x60),
		},
		.formats = ov9281_formats,
		.formats_size = ARRAY_SIZE(ov9281_formats),
	},
	{
		.name = "gc0310",
		.grp_id = 0,
		.board_info = {
			I2C_BOARD_INFO("gc0310", 0x21),
		},
		.formats = gc0310_formats,
		.formats_size = ARRAY_SIZE(gc0310_formats),
	},
	{
		.name = "ov9281_isp",
		.grp_id = 0,
		.board_info = {
			I2C_BOARD_INFO("ov9281_isp", 0x60),
		},
		.formats = ov9281_isp_formats,
		.formats_size = ARRAY_SIZE(ov9281_isp_formats),
	}
};

static const struct sp_mipi_config psp_mipi_cfg = {
	.i2c_adapter_id = 1,
	.sub_devs       = sp_mipi_sub_devs,
	.num_subdevs    = ARRAY_SIZE(sp_mipi_sub_devs),
};


/* ------------------------------------------------------------------
	SP7021 function
   ------------------------------------------------------------------*/
static const struct sp_fmt *get_format(const struct sp_mipi_subdev_info *sdinfo, u32 pixel_fmt)
{
	const struct sp_fmt *formats = sdinfo->formats;
	int size = sdinfo->formats_size;
	unsigned int k;

	for (k = 0; k < size; k++) {
		if (formats[k].fourcc == pixel_fmt) {
			break;
		}
	}

	if (k == size) {
		return NULL;
	}

	return &formats[k];
}

static int sp_mipi_get_register_base(struct platform_device *pdev, void **membase, const char *res_name)
{
	struct resource *r;
	void __iomem *p;

	DBG_INFO("Resource name: %s\n", res_name);

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, res_name);
	if (r == NULL) {
		MIP_ERR("platform_get_resource_byname failed!\n");
		return -ENODEV;
	}

	p = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(p)) {
		MIP_ERR("devm_ioremap_resource failed!\n");
		return PTR_ERR(p);
	}

	DBG_INFO("ioremap address = 0x%08x\n", (unsigned int)p);
	*membase = p;

	return 0;
}

static void mipicsi_init(struct sp_mipi_device *mipi)
{
	u32 val, val2;

	val = 0x8104;   // Normal mode, MSB first, Auto

	switch (mipi->cur_format->mipi_lane) {
	default:
	case 1: // 1 lane
		val |= 0;
		val2 = 0x11;
		break;
	case 2: // 2 lanes
		val |= (1<<20);
		val2 = 0x13;
		break;
	case 4: // 4 lanes
		val |= (2<<20);
		val2 = 0x1f;
		break;
	}

	switch (mipi->cur_format->sol_sync) {
	default:
	case SYNC_RAW8:	        // 8 bits
	case SYNC_YUY2:
		val |= (2<<16); break;
	case SYNC_RAW10:        // 10 bits
		val |= (1<<16); break;
	}

	writel(val, &mipi->mipicsi_regs->mipicsi_mix_cfg);
	writel(mipi->cur_format->sol_sync, &mipi->mipicsi_regs->mipicsi_sof_sol_syncword);
	writel(val2, &mipi->mipicsi_regs->mipi_analog_cfg2);
	writel(0x110, &mipi->mipicsi_regs->mipicsi_ecc_cfg);
	writel(0x1000, &mipi->mipicsi_regs->mipi_analog_cfg1);
	writel(0x1001, &mipi->mipicsi_regs->mipi_analog_cfg1);
	writel(0x1000, &mipi->mipicsi_regs->mipi_analog_cfg1);
	writel(0x1, &mipi->mipicsi_regs->mipicsi_enable);               // Enable MIPICSI
}

static void csiiw_init(struct sp_mipi_device *mipi)
{
	writel(0x1, &mipi->csiiw_regs->csiiw_latch_mode);               // latch mode should be enable before base address setup

	writel(0x500, &mipi->csiiw_regs->csiiw_stride);
	writel(0x3200500, &mipi->csiiw_regs->csiiw_frame_size);

	writel(0x00000100, &mipi->csiiw_regs->csiiw_frame_buf);         // set offset to trigger DRAM write

	//raw8 (0x2701); raw10 (10bit two byte space:0x2731, 8bit one byte space:0x2701)
	writel(0x32700, &mipi->csiiw_regs->csiiw_config0);              // Disable csiiw, fs_irq and fe_irq
}

irqreturn_t csiiw_fs_isr(int irq, void *dev_instance)
{
	struct sp_mipi_device *mipi = dev_instance;

	if (mipi->streaming) {
	}

	return IRQ_HANDLED;
}

irqreturn_t csiiw_fe_isr(int irq, void *dev_instance)
{
	struct sp_mipi_device *mipi = dev_instance;
	struct videobuf_buffer *next_frm;
	int addr;

	if (mipi->skip_first_int) {
		mipi->skip_first_int = 0;
		return IRQ_HANDLED;
	}

	if (mipi->streaming) {
		spin_lock(&mipi->dma_queue_lock);

		if (!list_empty(&mipi->dma_queue))
		{
			// One video frame is just being captured, if next frame
			// is available, delete the frame from queue.
			next_frm = list_entry(mipi->dma_queue.next, struct videobuf_buffer, queue);
			list_del(&next_frm->queue);

			// Set active-buffer to 'next frame'.
			next_frm->state = VIDEOBUF_ACTIVE;
			addr = videobuf_to_dma_contig(next_frm);
			writel(addr, &mipi->csiiw_regs->csiiw_base_addr);        // base address

			// Then, release current frame.
			v4l2_get_timestamp(&mipi->cur_frm->ts);
			mipi->cur_frm->state = VIDEOBUF_DONE;
			mipi->cur_frm->size = mipi->fmt.fmt.pix.sizeimage;
			wake_up_interruptible(&mipi->cur_frm->done);

			// Finally, move on.
			mipi->cur_frm = next_frm;
		}

		spin_unlock(&mipi->dma_queue_lock);
	}

	return IRQ_HANDLED;
}

static int csiiw_irq_init(struct sp_mipi_device *mipi)
{
	int ret;

	mipi->fs_irq = irq_of_parse_and_map(mipi->pdev->of_node, 0);
	ret = devm_request_irq(mipi->pdev, mipi->fs_irq, csiiw_fs_isr, 0, "csiiw_fs", mipi);
	if (ret) {
		goto err_fs_irq;
	}

	mipi->fe_irq = irq_of_parse_and_map(mipi->pdev->of_node, 1);
	ret = devm_request_irq(mipi->pdev, mipi->fe_irq, csiiw_fe_isr, 0, "csiiw_fe", mipi);
	if (ret) {
		goto err_fe_irq;
	}

	MIP_INFO("Installed csiiw interrupts (fs_irq=%d, fe_irq=%d).\n", mipi->fs_irq , mipi->fe_irq);
	return 0;

err_fe_irq:
err_fs_irq:
	MIP_ERR("request_irq failed (%d)!\n", ret);
	return ret;
}

static int buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct sp_mipi_fh *fh = vq->priv_data;
	struct sp_mipi_device *mipi = fh->mipi;

	*size = mipi->fmt.fmt.pix.sizeimage;

	if (((*count) == 0) || ((*count) >= VIDEO_MAX_FRAME)) {
		*count = VIDEO_MAX_FRAME;
	} else if ((*count) < 2) {
		*count = 2;     // At least, we need 2 video buffers.
	}

	while (((*size)*(*count)) > (vid_limit * 1024 * 1024))
		(*count)--;

	DBG_INFO("%s: count = %d, size = %d\n", __FUNCTION__, *count, *size);
	return 0;
}

static int buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct sp_mipi_fh *fh = vq->priv_data;
	struct sp_mipi_device *mipi = fh->mipi;
	unsigned long addr;
	int ret;

	/* If buffer is not initialized, initialize it */
	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		vb->width  = mipi->fmt.fmt.pix.width;
		vb->height = mipi->fmt.fmt.pix.height;
		vb->size   = mipi->fmt.fmt.pix.sizeimage;
		vb->field  = field;

		ret = videobuf_iolock(vq, vb, NULL);
		if (ret < 0) {
			MIP_ERR("videobuf_iolock failed!\n");
			return ret;
		}

		addr = videobuf_to_dma_contig(vb);

		/* Make sure user addresses are aligned to 32 bytes */
		if (!ALIGN(addr, FRAME_BUFFER_ALIGN)) {
			MIP_ERR("Physical base address of frame buffer should be %d-byte align!\n", FRAME_BUFFER_ALIGN);
			return -EINVAL;
		}

		vb->state = VIDEOBUF_PREPARED;

		DBG_INFO("%s: addr = %08lx, width = %d, height = %d, size = %ld, field = %d, state = %d\n",
			 __FUNCTION__, addr, vb->width, vb->height, vb->size, vb->field, vb->state);
	}

	return 0;
}

static void buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	/* Get the file handle object and device object */
	struct sp_mipi_fh *fh = vq->priv_data;
	struct sp_mipi_device *mipi = fh->mipi;
	unsigned long flags;

	spin_lock_irqsave(&mipi->dma_queue_lock, flags);

	/* add the buffer to the DMA queue */
	list_add_tail(&vb->queue, &mipi->dma_queue);
	DBG_INFO("%s: list_add\n", __FUNCTION__);
	print_List(&mipi->dma_queue);

	/* Change state of the buffer */
	vb->state = VIDEOBUF_QUEUED;

	spin_unlock_irqrestore(&mipi->dma_queue_lock, flags);

	DBG_INFO("%s: state = %d\n", __FUNCTION__, vb->state);
}

static void buffer_release(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct sp_mipi_fh *fh = vq->priv_data;
	struct sp_mipi_device *mipi = fh->mipi;
	unsigned long flags;

	/*
	 * We need to flush the buffer from the dma queue since
	 * they are de-allocated
	 */
	spin_lock_irqsave(&mipi->dma_queue_lock, flags);
	INIT_LIST_HEAD(&mipi->dma_queue);
	DBG_INFO("%s: init_list\n", __FUNCTION__);
	print_List(&mipi->dma_queue);
	spin_unlock_irqrestore(&mipi->dma_queue_lock, flags);

	videobuf_dma_contig_free(vq, vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
	DBG_INFO("%s: state = %d\n", __FUNCTION__, vb->state);
}

static struct videobuf_queue_ops sp_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

//===================================================================================
/* ------------------------------------------------------------------
	V4L2 ioctrl operations
   ------------------------------------------------------------------*/
static int vidioc_querycap(struct file *file, void *priv, struct v4l2_capability *vcap)
{
	DBG_INFO("%s\n", __FUNCTION__);

	strlcpy(vcap->driver, "SP Video Driver", sizeof(vcap->driver));
	strlcpy(vcap->card, "SP MIPI Camera Card", sizeof(vcap->card));
	strlcpy(vcap->bus_info, "SP MIPI Camera BUS", sizeof(vcap->bus_info));

	// report capabilities
	vcap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
	vcap->capabilities = vcap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv, struct v4l2_fmtdesc *fmtdesc)
{
	struct sp_mipi_device *mipi = video_drvdata(file);
	const struct sp_fmt *fmt;

	DBG_INFO("%s: index = %d\n", __FUNCTION__, fmtdesc->index);

	if (fmtdesc->index >= mipi->current_subdev->formats_size) {
		return -EINVAL;
	}

	if (fmtdesc->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		MIP_ERR("Invalid V4L2 buffer type!\n");
		return -EINVAL;
	}

	fmt = &mipi->current_subdev->formats[fmtdesc->index];
	strlcpy(fmtdesc->description, fmt->name, sizeof(fmtdesc->description));
	fmtdesc->pixelformat = fmt->fourcc;

	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct sp_mipi_device *mipi = video_drvdata(file);
	const struct sp_fmt *fmt;
	enum v4l2_field field;

	DBG_INFO("%s\n", __FUNCTION__);

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		MIP_ERR("Invalid V4L2 buffer type!\n");
		return -EINVAL;
	}

	fmt = get_format(mipi->current_subdev, f->fmt.pix.pixelformat);
	if (fmt == NULL) {
		return -EINVAL;
	}

	field = f->fmt.pix.field;
	if (field == V4L2_FIELD_ANY) {
		field = V4L2_FIELD_INTERLACED;
	}
	f->fmt.pix.field = field;

	v4l_bound_align_image(&f->fmt.pix.width, 48, fmt->width, fmt->walign,
			      &f->fmt.pix.height, 32, fmt->height, fmt->halign, 0);

	f->fmt.pix.bytesperline = (f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage    = f->fmt.pix.height * f->fmt.pix.bytesperline;

	if ((fmt->fourcc == V4L2_PIX_FMT_YUYV) || (fmt->fourcc == V4L2_PIX_FMT_UYVY)) {
		f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	} else {
		f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	}

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct sp_mipi_device *mipi = video_drvdata(file);
	int ret;

	DBG_INFO("%s\n", __FUNCTION__);

	if (mipi->streaming) {
		MIP_ERR("Device has started streaming!\n");
		return -EBUSY;
	}

	ret = vidioc_try_fmt_vid_cap(file, mipi, f);
	if (ret != 0) {
		return ret;
	}

	mipi->fmt.type                 = f->type;
	mipi->fmt.fmt.pix.width        = f->fmt.pix.width;
	mipi->fmt.fmt.pix.height       = f->fmt.pix.height;
	mipi->fmt.fmt.pix.pixelformat  = f->fmt.pix.pixelformat; // from vidioc_try_fmt_vid_cap
	mipi->fmt.fmt.pix.field        = f->fmt.pix.field;
	mipi->fmt.fmt.pix.bytesperline = f->fmt.pix.bytesperline;
	mipi->fmt.fmt.pix.sizeimage    = f->fmt.pix.sizeimage;
	mipi->fmt.fmt.pix.colorspace   = f->fmt.pix.colorspace;

	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct sp_mipi_device *mipi = video_drvdata(file);

	DBG_INFO("%s\n", __FUNCTION__);

	memcpy(f, &mipi->fmt, sizeof(*f));

	return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv, struct v4l2_requestbuffers *req_buf)
{
	struct sp_mipi_device *mipi = video_drvdata(file);
	struct sp_mipi_fh *fh = file->private_data;

	DBG_INFO("%s\n", __FUNCTION__);

	if (mipi->streaming) {
		MIP_ERR("Device has started streaming!\n");
		return -EBUSY;
	}

	if (req_buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		MIP_ERR("Invalid V4L2 buffer type!\n");
		return -EINVAL;
	}

	mipi->memory = req_buf->memory;
	videobuf_queue_dma_contig_init(&mipi->buffer_queue,
				&sp_video_qops,
				mipi->pdev,
				&mipi->irqlock,
				req_buf->type,
				mipi->fmt.fmt.pix.field,
				sizeof(struct videobuf_buffer),
				fh, NULL);

	fh->io_allowed = 1;
	INIT_LIST_HEAD(&mipi->dma_queue);
	DBG_INFO("%s: init_list\n", __FUNCTION__);
	print_List(&mipi->dma_queue);

	return videobuf_reqbufs(&mipi->buffer_queue, req_buf);
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct sp_mipi_device *mipi = video_drvdata(file);

	DBG_INFO("%s\n", __FUNCTION__);

	if (mipi->streaming) {
		MIP_ERR("Device has started streaming!\n");
		return -EBUSY;
	}

	if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		MIP_ERR("Invalid V4L2 buffer type!\n");
		return -EINVAL;
	}

	if (mipi->memory != V4L2_MEMORY_MMAP) {
		MIP_ERR("Invalid V4L2 memory type!\n");
		return -EINVAL;
	}

	/* Call videobuf_querybuf to get information */
	return videobuf_querybuf(&mipi->buffer_queue, buf);
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct sp_mipi_device *mipi = video_drvdata(file);
	struct sp_mipi_fh *fh = file->private_data;

	DBG_INFO("%s\n", __FUNCTION__);

	if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		MIP_ERR("Invalid V4L2 buffer type!\n");
		return -EINVAL;
	}

	/*
	 * If this file handle is not allowed to do IO, return error.
	 */
	if (!fh->io_allowed) {
		MIP_ERR("IO is not allowed!\n");
		return -EACCES;
	}

	return videobuf_qbuf(&mipi->buffer_queue, buf);
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct sp_mipi_device *mipi = video_drvdata(file);

	DBG_INFO("%s\n", __FUNCTION__);

	if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		MIP_ERR("Invalid V4L2 buffer type!\n");
		return -EINVAL;
	}

	return videobuf_dqbuf(&mipi->buffer_queue, buf, file->f_flags & O_NONBLOCK);
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type buf_type)
{
	struct sp_mipi_device *mipi = video_drvdata(file);
	struct sp_mipi_fh *fh = file->private_data;
	struct sp_mipi_subdev_info *sdinfo;
	unsigned long addr;
	int ret;

	DBG_INFO("%s\n", __FUNCTION__);

	if (mipi->streaming) {
		MIP_ERR("Device has started streaming!\n");
		return -EBUSY;
	}

	if (buf_type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		MIP_ERR("Invalid V4L2 buffer type!\n");
		return -EINVAL;
	}

	/* If file handle is not allowed IO, return error. */
	if (!fh->io_allowed) {
		MIP_ERR("IO is not allowed!\n");
		return -EACCES;
	}

	sdinfo = mipi->current_subdev;
	ret = v4l2_device_call_until_err(&mipi->v4l2_dev, sdinfo->grp_id,
					video, s_stream, 1);
	if (ret && (ret != -ENOIOCTLCMD)) {
		MIP_ERR("streamon failed in subdevice!\n");
		return -EINVAL;
	}

	/* If buffer queue is empty, return error. */
	if (list_empty(&mipi->buffer_queue.stream)) {
		MIP_ERR("Buffer queue is empty!\n");
		return -EIO;
	}

	/* Call videobuf_streamon to start streaming in videobuf */
	ret = videobuf_streamon(&mipi->buffer_queue);
	if (ret) {
		return ret;
	}

	/* Get the next video-buffer from the video-buffer queue */
	mipi->cur_frm = list_entry(mipi->dma_queue.next, struct videobuf_buffer, queue);

	/* Remove current video-buffer (frame) from the video-buffer queue */
	list_del(&mipi->cur_frm->queue);
	DBG_INFO("%s: list_del\n", __FUNCTION__);
	print_List(&mipi->dma_queue);

	/* Mark state of the current video-buffer (frame) to active */
	mipi->cur_frm->state = VIDEOBUF_ACTIVE;
	addr = videobuf_to_dma_contig(mipi->cur_frm);

	writel(addr, &mipi->csiiw_regs->csiiw_base_addr);
	writel(mEXTENDED_ALIGNED(mipi->fmt.fmt.pix.bytesperline, 16), &mipi->csiiw_regs->csiiw_stride);
	writel((mipi->fmt.fmt.pix.height<<16)|mipi->fmt.fmt.pix.bytesperline, &mipi->csiiw_regs->csiiw_frame_size);

	writel(0x12701, &mipi->csiiw_regs->csiiw_config0);      // Enable csiiw and fe_irq

	mipi->streaming = 1;
	mipi->skip_first_int = 1;

	DBG_INFO("%s: cur_frm = %p, addr = %08lx\n", __FUNCTION__, mipi->cur_frm, addr);
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type buf_type)
{
	struct sp_mipi_device *mipi = video_drvdata(file);
	struct sp_mipi_fh *fh = file->private_data;
	struct sp_mipi_subdev_info *sdinfo;
	int ret;

	DBG_INFO("%s\n", __FUNCTION__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != buf_type) {
		MIP_ERR("Invalid V4L2 buffer type!\n");
		return -EINVAL;
	}

	/* If io is allowed for this file handle, return error */
	if (!fh->io_allowed) {
		MIP_ERR("IO is not allowed!\n");
		return -EACCES;
	}

	/* If streaming is not started, return error */
	if (!mipi->streaming) {
		MIP_ERR("Device has started already!\n");
		return -EINVAL;
	}

	sdinfo = mipi->current_subdev;
	ret = v4l2_device_call_until_err(&mipi->v4l2_dev, sdinfo->grp_id,
					video, s_stream, 0);
	if (ret && (ret != -ENOIOCTLCMD)) {
		MIP_ERR("streamon failed in subdevice!\n");
		return -EINVAL;
	}

	mipi->streaming = 0;

	// FW must mask irq to avoid unmap issue (for test code)
	writel(0x32700, &mipi->csiiw_regs->csiiw_config0);      // Disable csiiw, fs_irq and fe_irq

	return videobuf_streamoff(&mipi->buffer_queue);
}

static const struct v4l2_ioctl_ops sp_mipi_ioctl_ops = {
	.vidioc_querycap                = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap        = vidioc_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap         = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap           = vidioc_s_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap           = vidioc_g_fmt_vid_cap,
	.vidioc_reqbufs                 = vidioc_reqbufs,
	.vidioc_querybuf                = vidioc_querybuf,
	.vidioc_qbuf                    = vidioc_qbuf,
	.vidioc_dqbuf                   = vidioc_dqbuf,
	.vidioc_streamon                = vidioc_streamon,
	.vidioc_streamoff               = vidioc_streamoff,
};

//===================================================================================
/* ------------------------------------------------------------------
	V4L2 file operations
   ------------------------------------------------------------------*/
static int sp_mipi_open(struct file *file)
{
	struct sp_mipi_device *mipi = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);
	struct sp_mipi_fh *fh;

	DBG_INFO("%s\n", __FUNCTION__);

#ifdef CONFIG_PM_RUNTIME_MIPI
	if (pm_runtime_get_sync(mipi->pdev) < 0)
		goto out;
#endif

	/* Allocate memory for the file handle object */
	fh = kmalloc(sizeof(struct sp_mipi_fh), GFP_KERNEL);
	if (!fh) {
		MIP_ERR("Failed to allocate memory for file handle object!\n");
		return -ENOMEM;
	}

	/* store pointer to fh in private_data member of file */
	file->private_data = fh;
	fh->mipi = mipi;
	v4l2_fh_init(&fh->fh, vdev);

	/* Set io_allowed member to false */
	fh->io_allowed = 0;
	v4l2_fh_add(&fh->fh);
	return 0;

#ifdef CONFIG_PM_RUNTIME_MIPI
out:
	pm_runtime_mark_last_busy(mipi->pdev);
	pm_runtime_put_autosuspend(mipi->pdev);
	return -ENOMEM;
#endif
}

static int sp_mipi_release(struct file *file)
{
	struct sp_mipi_device *mipi = video_drvdata(file);
	struct sp_mipi_fh *fh = file->private_data;
	struct sp_mipi_subdev_info *sdinfo;
	int ret;

	DBG_INFO("%s\n", __FUNCTION__);

	/* if this instance is doing IO */
	if (fh->io_allowed) {
		if (mipi->streaming) {
			sdinfo = mipi->current_subdev;
			ret = v4l2_device_call_until_err(&mipi->v4l2_dev, sdinfo->grp_id,
							 video, s_stream, 0);
			if (ret && (ret != -ENOIOCTLCMD)) {
				MIP_ERR("streamon failed in subdevice!\n");
				return -EINVAL;
			}

			mipi->streaming = 0;
			writel(0x32700, &mipi->csiiw_regs->csiiw_config0);      // Disable csiiw, fs_irq and fe_irq

			videobuf_streamoff(&mipi->buffer_queue);
		}

		videobuf_stop(&mipi->buffer_queue);
		videobuf_mmap_free(&mipi->buffer_queue);
	}

	/* Decrement device usrs counter */
	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);
	file->private_data = NULL;

#ifdef CONFIG_PM_RUNTIME_MIPI
	pm_runtime_put(mipi->pdev);		// Starting count timeout.
#endif

	/* Free memory allocated to file handle object */
	kfree(fh);
	return 0;
}

static int sp_mipi_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct sp_mipi_device *mipi = video_drvdata(file);
	int ret;

	DBG_INFO("%s\n", __FUNCTION__);

	ret = videobuf_mmap_mapper(&mipi->buffer_queue, vma);

	DBG_INFO("vma_start = 0x%08lx, size=%ld, ret=%d\n",
		vma->vm_start, vma->vm_end-vma->vm_start, ret);

	return ret;
}

static unsigned int sp_mipi_poll(struct file *file, poll_table *wait)
{
	struct sp_mipi_device *mipi = video_drvdata(file);

	DBG_INFO("%s\n", __FUNCTION__);

	if (mipi->streaming)
		return videobuf_poll_stream(file, &mipi->buffer_queue, wait);

	return 0;
}

static const struct v4l2_file_operations sp_mipi_fops = {
	.owner          = THIS_MODULE,
	.open           = sp_mipi_open,
	.release        = sp_mipi_release,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = sp_mipi_mmap,
	.poll           = sp_mipi_poll,
};

//===================================================================================
/* ------------------------------------------------------------------
	SP-MIPI driver probe
   ------------------------------------------------------------------*/
static int sp_mipi_probe(struct platform_device *pdev)
{
	struct sp_mipi_device *mipi;
	struct video_device *vfd;
	struct device *dev = &pdev->dev;
	struct sp_mipi_config *sp_mipi_cfg;
	struct sp_mipi_subdev_info *sdinfo;
	struct i2c_adapter *i2c_adap;
	struct v4l2_subdev *subdev;
	struct sp_subdev_sensor_data *sensor_data;
	const struct sp_fmt *cur_fmt;
	int num_subdevs = 0;
	int ret, i;

	DBG_INFO("%s\n", __FUNCTION__);

	// Allocate memory for 'sp_mipi_device'.
	mipi = kzalloc(sizeof(struct sp_mipi_device), GFP_KERNEL);
	if (!mipi) {
		MIP_ERR("Failed to allocate memory for \'sp_mipi_device\'!\n");
		ret = -ENOMEM;
		goto err_alloc;
	}
	mipi->pdev = &pdev->dev;

	/* set the driver data in platform device */
	platform_set_drvdata(pdev, mipi);

	/* set driver private data */
	video_set_drvdata(&mipi->video_dev, mipi);

	// Get and set 'mipicsi' register base.
	ret = sp_mipi_get_register_base(pdev, (void**)&mipi->mipicsi_regs, MIPICSI_REG_NAME);
	if (ret) {
		return ret;
	}

	// Get and set 'csiiw' register base.
	ret = sp_mipi_get_register_base(pdev, (void**)&mipi->csiiw_regs, CSIIW_REG_NAME);
	if (ret) {
		return ret;
	}
	MIP_INFO("%s: mipicsi_regs = 0x%p, csiiw_regs = 0x%p\n",
		__FUNCTION__, mipi->mipicsi_regs, mipi->csiiw_regs);

	// Get clock resource 'clk_mipicsi'.
	mipi->mipicsi_clk = devm_clk_get(dev, "clk_mipicsi");
	if (IS_ERR(mipi->mipicsi_clk)) {
		ret = PTR_ERR(mipi->mipicsi_clk);
		MIP_ERR("Failed to retrieve clock resource \'clk_mipicsi\'!\n");
		goto err_get_mipicsi_clk;
	}

	// Get clock resource 'clk_csiiw'.
	mipi->csiiw_clk = devm_clk_get(dev, "clk_csiiw");
	if (IS_ERR(mipi->csiiw_clk)) {
		ret = PTR_ERR(mipi->csiiw_clk);
		MIP_ERR("Failed to retrieve clock resource \'clk_csiiw\'!\n");
		goto err_get_csiiw_clk;
	}

	// Get reset controller resource 'rstc_mipicsi'.
	mipi->mipicsi_rstc = devm_reset_control_get(&pdev->dev, "rstc_mipicsi");
	if (IS_ERR(mipi->mipicsi_rstc)) {
		ret = PTR_ERR(mipi->mipicsi_rstc);
		dev_err(&pdev->dev, "Failed to retrieve reset controller 'rstc_mipicsi\'!\n");
		goto err_get_mipicsi_rstc;
	}

	// Get reset controller resource 'rstc_csiiw'.
	mipi->csiiw_rstc = devm_reset_control_get(&pdev->dev, "rstc_csiiw");
	if (IS_ERR(mipi->csiiw_rstc)) {
		ret = PTR_ERR(mipi->csiiw_rstc);
		dev_err(&pdev->dev, "Failed to retrieve reset controller 'rstc_csiiw\'!\n");
		goto err_get_csiiw_rstc;
	}

	// Get i2c id.
	ret = of_property_read_u32(pdev->dev.of_node, "i2c-id", &mipi->i2c_id);
	if (ret) {
		MIP_ERR("Failed to retrieve \'i2c-id\'!\n");
		goto err_get_i2c_id;
	}

	// Enable 'mipicsi' clock.
	ret = clk_prepare_enable(mipi->mipicsi_clk);
	if (ret) {
		MIP_ERR("Failed to enable \'mipicsi\' clock!\n");
		goto err_en_mipicsi_clk;
	}

	// Enable 'csiiw' clock.
	ret = clk_prepare_enable(mipi->csiiw_clk);
	if (ret) {
		MIP_ERR("Failed to enable \'csiiw\' clock!\n");
		goto err_en_csiiw_clk;
	}

	// De-assert 'mipicsi' reset controller.
	ret = reset_control_deassert(mipi->mipicsi_rstc);
	if (ret) {
		MIP_ERR("Failed to deassert 'mipicsi' reset controller!\n");
		goto err_deassert_mipicsi_rstc;
	}

	// De-assert 'csiiw' reset controller.
	ret = reset_control_deassert(mipi->csiiw_rstc);
	if (ret) {
		MIP_ERR("Failed to deassert 'csiiw' reset controller!\n");
		goto err_deassert_csiiw_rstc;
	}

	// Register V4L2 device.
	ret = v4l2_device_register(&pdev->dev, &mipi->v4l2_dev);
	if (ret) {
		MIP_ERR("Unable to register V4L2 device!\n");
		goto err_v4l2_register;
	}
	MIP_INFO("Registered V4L2 device.\n");

	/* Initialize field of video device */
	vfd = &mipi->video_dev;
	vfd->release    = video_device_release_empty;
	vfd->fops       = &sp_mipi_fops;
	vfd->ioctl_ops  = &sp_mipi_ioctl_ops;
	//vfd->tvnorms  = 0;
	vfd->v4l2_dev   = &mipi->v4l2_dev;
	strlcpy(vfd->name, MIPI_CSI_RX_NAME, sizeof(vfd->name));

	spin_lock_init(&mipi->irqlock);
	spin_lock_init(&mipi->dma_queue_lock);
	mutex_init(&mipi->lock);

	vfd->lock = &mipi->lock;
	vfd->minor = -1;

	// Register video device.
	ret = video_register_device(&mipi->video_dev, VFL_TYPE_GRABBER, -1);
	if (ret) {
		MIP_ERR("Unable to register video device!\n");
		vfd->minor = -1;
		ret = -ENODEV;
		goto err_video_register;
	}
	MIP_INFO("Registered video device \'/dev/video%d\'.\n", vfd->minor);

	// Get i2c_info for sub-device.
	sp_mipi_cfg = kmalloc(sizeof(*sp_mipi_cfg), GFP_KERNEL);
	if (!sp_mipi_cfg) {
		MIP_ERR("Failed to allocate memory for \'sp_mipi_cfg\'!\n");
		ret = -ENOMEM;
		goto err_alloc_mipi_cfg;
	}
	memcpy (sp_mipi_cfg, &psp_mipi_cfg, sizeof(*sp_mipi_cfg));
	sp_mipi_cfg->i2c_adapter_id = mipi->i2c_id;
	num_subdevs = sp_mipi_cfg->num_subdevs;
	mipi->cfg = sp_mipi_cfg;

	// Get i2c adapter.
	i2c_adap = i2c_get_adapter(sp_mipi_cfg->i2c_adapter_id);
	if (!i2c_adap) {
		MIP_ERR("Failed to get i2c adapter #%d!\n", sp_mipi_cfg->i2c_adapter_id);
		ret = -ENODEV;
		goto err_i2c_get_adapter;
	}
	mipi->i2c_adap = i2c_adap;
	MIP_INFO("Got i2c adapter #%d.\n", sp_mipi_cfg->i2c_adapter_id);

	for (i = 0; i < num_subdevs; i++) {
		sdinfo = &sp_mipi_cfg->sub_devs[i];

		/* Load up the subdevice */
		subdev = v4l2_i2c_new_subdev_board(&mipi->v4l2_dev,
						    i2c_adap,
						    &sdinfo->board_info,
						    NULL);
		if (subdev) {
			MIP_INFO("Registered V4L2 subdevice \'%s\'.\n", sdinfo->name);
			break;
		}
	}
	if (i == num_subdevs) {
		MIP_ERR("Failed to register V4L2 subdevice!\n");
		ret = -ENXIO;
		goto err_subdev_register;
	}

	/* set current sub device */
	mipi->current_subdev = &sp_mipi_cfg->sub_devs[i];
	mipi->v4l2_dev.ctrl_handler = subdev->ctrl_handler;
	sensor_data = v4l2_get_subdevdata(subdev);
	mipi->cur_mode = sensor_data->mode;

	cur_fmt = get_format(mipi->current_subdev, sensor_data->fourcc);
	if (cur_fmt == NULL) {
		goto err_get_format;
	}
	mipi->cur_format = cur_fmt;

	mipicsi_init(mipi);
	csiiw_init(mipi);

	ret = csiiw_irq_init(mipi);
	if (ret) {
		goto err_csiiw_irq_init;
	}

	// Initialize video format (V4L2_format).
	mipi->fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	mipi->fmt.fmt.pix.width = cur_fmt->width;
	mipi->fmt.fmt.pix.height = cur_fmt->height;
	mipi->fmt.fmt.pix.pixelformat = cur_fmt->fourcc;
	mipi->fmt.fmt.pix.field = V4L2_FIELD_NONE;
	mipi->fmt.fmt.pix.bytesperline = (mipi->fmt.fmt.pix.width * cur_fmt->depth) >> 3;
	mipi->fmt.fmt.pix.sizeimage = mipi->fmt.fmt.pix.height * mipi->fmt.fmt.pix.bytesperline;
	mipi->fmt.fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
	mipi->fmt.fmt.pix.priv = 0;

#ifdef CONFIG_PM_RUNTIME_MIPI
	pm_runtime_set_autosuspend_delay(&pdev->dev,5000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
#endif
	return 0;

err_get_format:
err_csiiw_irq_init:
err_subdev_register:
	i2c_put_adapter(i2c_adap);

err_i2c_get_adapter:
	kfree(sp_mipi_cfg);

err_alloc_mipi_cfg:
	video_unregister_device(&mipi->video_dev);

err_video_register:
	v4l2_device_unregister(&mipi->v4l2_dev);

err_v4l2_register:
err_deassert_csiiw_rstc:
err_deassert_mipicsi_rstc:
	clk_disable_unprepare(mipi->csiiw_clk);

err_en_csiiw_clk:
	clk_disable_unprepare(mipi->mipicsi_clk);

err_en_mipicsi_clk:
err_get_i2c_id:
err_get_csiiw_rstc:
err_get_mipicsi_rstc:
err_get_csiiw_clk:
err_get_mipicsi_clk:
err_alloc:
	kfree(mipi);
	return ret;
}

static int sp_mipi_remove(struct platform_device *pdev)
{
	struct sp_mipi_device *mipi = platform_get_drvdata(pdev);

	DBG_INFO("%s\n", __FUNCTION__);

	i2c_put_adapter(mipi->i2c_adap);
	kfree(mipi->cfg);

	video_unregister_device(&mipi->video_dev);
	v4l2_device_unregister(&mipi->v4l2_dev);

	clk_disable_unprepare(mipi->csiiw_clk);
	clk_disable_unprepare(mipi->mipicsi_clk);

	kfree(mipi);

#ifdef CONFIG_PM_RUNTIME_MIPI
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
#endif

	return 0;
}

static int sp_mipi_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct sp_mipi_device *mipi = platform_get_drvdata(pdev);

	MIP_INFO("MIPI suspend.\n");

	// Disable 'mipicsi' and 'csiiw' clock.
	clk_disable(mipi->mipicsi_clk);
	clk_disable(mipi->csiiw_clk);

	return 0;
}

static int sp_mipi_resume(struct platform_device *pdev)
{
	struct sp_mipi_device *mipi = platform_get_drvdata(pdev);
	int ret;

	MIP_INFO("MIPI resume.\n");

	// Enable 'mipicsi' clock.
	ret = clk_prepare_enable(mipi->mipicsi_clk);
	if (ret) {
		MIP_ERR("Failed to enable \'mipicsi\' clock!\n");
	}

	// Enable 'csiiw' clock.
	ret = clk_prepare_enable(mipi->csiiw_clk);
	if (ret) {
		MIP_ERR("Failed to enable \'csiiw\' clock!\n");
	}

	return 0;
}

#ifdef CONFIG_PM_RUNTIME_MIPI
static int sp_mipi_runtime_suspend(struct device *dev)
{
	struct sp_mipi_device *mipi = dev_get_drvdata(dev);

	MIP_INFO("MIPI runtime suspend.\n");

	// Disable 'mipicsi' and 'csiiw' clock.
	clk_disable(mipi->mipicsi_clk);
	clk_disable(mipi->csiiw_clk);

	return 0;
}

static int sp_mipi_runtime_resume(struct device *dev)
{
	struct sp_mipi_device *mipi = dev_get_drvdata(dev);
	int ret;

	MIP_INFO("MIPI runtime resume.\n");

	// Enable 'mipicsi' clock.
	ret = clk_prepare_enable(mipi->mipicsi_clk);
	if (ret) {
		MIP_ERR("Failed to enable \'mipicsi\' clock!\n");
	}

	// Enable 'csiiw' clock.
	ret = clk_prepare_enable(mipi->csiiw_clk);
	if (ret) {
		MIP_ERR("Failed to enable \'csiiw\' clock!\n");
	}

	return 0;
}

static const struct dev_pm_ops sp7021_mipicsi_rx_pm_ops = {
	.runtime_suspend = sp_mipi_runtime_suspend,
	.runtime_resume  = sp_mipi_runtime_resume,
};
#endif

static const struct of_device_id sp_mipicsi_rx_of_match[] = {
	{ .compatible = "sunplus,sp7021-mipicsi-rx", },
	{}
};

static struct platform_driver sp_mipicsi_rx_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = MIPI_CSI_RX_NAME,
		.of_match_table = sp_mipicsi_rx_of_match,
#ifdef CONFIG_PM_RUNTIME_MIPI
		.pm = &sp7021_mipicsi_rx_pm_ops,
#endif
	},
	.probe = sp_mipi_probe,
	.remove = sp_mipi_remove,
	.suspend = sp_mipi_suspend,
	.resume	= sp_mipi_resume,
};

module_platform_driver(sp_mipicsi_rx_driver);

MODULE_DESCRIPTION("Sunplus MIPI/CSI-RX driver");
MODULE_LICENSE("GPL");

