#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma/xilinx_dma.h>
#include <linux/dma/xilinx_frmbuf.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/of_irq.h>
#include <linux/delay.h>
#include <linux/lcm.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include <drm/drm_fourcc.h>

#define DRIVER_NAME "hdmi-in"

struct hdmi_in_buffer {
	struct vb2_v4l2_buffer buf;
	struct list_head queue;
	struct hdmi_in_dma *dma;
};

struct hdmi_in_dma {
	struct video_device video;
	struct v4l2_pix_format format;
	struct dma_chan *dma;
	unsigned int align;
	struct mutex lock;
	struct vb2_queue queue;
	unsigned int sequence;
	struct list_head queued_bufs;
	spinlock_t queued_lock;
	struct dma_interleaved_template xt;
	struct data_chunk sgl[1];
};

struct hdmi_in_device {
	struct v4l2_device v4l2_dev;
	struct device *dev;
	struct hdmi_in_dma dma;
	void __iomem *iomem;
	struct gpio_desc *locked_gpio;
	int irq;
	struct delayed_work work;
};

static const struct v4l2_file_operations hdmi_in_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

#define to_hdmi_in_dma(vdev) container_of(vdev, struct hdmi_in_dma, video)
static int hdmi_in_querycap(struct file *file, void *fh,
											struct v4l2_capability *cap)
{
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	strlcpy(cap->driver, DRIVER_NAME, sizeof(cap->driver));
	strlcpy(cap->card, "HDIM IN Interface", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:" DRIVER_NAME);

	return 0;
}

static int hdmi_in_enum_format(struct file *file, void *fh,
											struct v4l2_fmtdesc *f)
{
	struct v4l2_fh *vfh = file->private_data;
	struct hdmi_in_dma *dma = to_hdmi_in_dma(vfh->vdev);

	if (f->index > 0) return -EINVAL;

	f->pixelformat = dma->format.pixelformat; 
	strlcpy(f->description, "24-bit RGB", sizeof(f->description));

	return 0;
}

static int hdmi_in_get_format(struct file *file, void *fh,
											struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct hdmi_in_dma *dma = to_hdmi_in_dma(vfh->vdev);

	format->fmt.pix = dma->format;

	return 0;
}

static void __hdmi_in_try_format(struct hdmi_in_dma *dma,
									struct v4l2_pix_format *pix)
{
	unsigned int min_width;
	unsigned int max_width;
	unsigned int min_bpl;
	unsigned int max_bpl;
	unsigned int width;
	unsigned int align;
	unsigned int bpl;

	/* Retrieve format information and select the default format if the
	 * requested format isn't supported.
	 */

	pix->pixelformat = dma->format.pixelformat;
	pix->field = V4L2_FIELD_NONE;

	/* The transfer alignment requirements are expressed in bytes. Compute
	 * the minimum and maximum values, clamp the requested width and convert
	 * it back to pixels.
	 */
	
	align = lcm(dma->align, 3);
	min_width = roundup(32, align);
	max_width = rounddown(7680, align);
	width = rounddown(pix->width * 3, align);

	pix->width = clamp(width, min_width, max_width) / 3;
	pix->height = clamp(pix->height, 32, 7680);

	/* Clamp the requested bytes per line value. If the maximum bytes per
	 * line value is zero, the module doesn't support user configurable
	 * line sizes. Override the requested value with the minimum in that
	 * case.
	 */

	min_bpl = pix->width * 3;
	max_bpl = rounddown(7680, dma->align);
	bpl = rounddown(pix->bytesperline, dma->align);

	pix->bytesperline = clamp(bpl, min_bpl, max_bpl);
	pix->sizeimage = pix->bytesperline * pix->height;

}

static int hdmi_in_set_format(struct file *file, void *fh,
											struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct hdmi_in_dma *dma = to_hdmi_in_dma(vfh->vdev);
	//struct hdmi_in_device *hidev 
	//				= container_of(dma, struct hdmi_in_device, dma);

	__hdmi_in_try_format(dma, &format->fmt.pix);

	if (vb2_is_busy(&dma->queue))
		return -EBUSY;

	dma->format = format->fmt.pix;

	return 0;
}

static int hdmi_in_try_format(struct file *file, void *fh,
											struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct hdmi_in_dma *dma = to_hdmi_in_dma(vfh->vdev);

	__hdmi_in_try_format(dma, &format->fmt.pix);

	return 0;
}

static const struct v4l2_ioctl_ops hdmi_in_ioctl_ops = {
	.vidioc_querycap = hdmi_in_querycap,
	.vidioc_enum_fmt_vid_cap = hdmi_in_enum_format,
	.vidioc_g_fmt_vid_cap = hdmi_in_get_format,
	.vidioc_s_fmt_vid_cap = hdmi_in_set_format,
	.vidioc_try_fmt_vid_cap = hdmi_in_try_format,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};


static int hdmi_in_queue_setup(struct vb2_queue *q,
						unsigned int *nbuffer, unsigned int *nplanes,
						unsigned int sizes[], struct device *alloc_ctxs[])
{
	struct hdmi_in_device *hidev = vb2_get_drv_priv(q);
	struct hdmi_in_dma *dma = &hidev->dma;

	if (*nplanes)
			return sizes[0] < dma->format.sizeimage ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = dma->format.sizeimage;

	return 0;
}

static int hdmi_in_buffer_prepare(struct vb2_buffer *vb)
{
	struct hdmi_in_device *hidev = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct hdmi_in_buffer *buf = 
					container_of(vbuf, struct hdmi_in_buffer, buf);

	buf->dma = &hidev->dma;

	return 0;
}

static void hdmi_in_dma_complete(void *param)
{
	struct hdmi_in_buffer *buf = param;
	struct vb2_queue *q = buf->buf.vb2_buf.vb2_queue;
	struct hdmi_in_device *hidev = vb2_get_drv_priv(q);
	struct hdmi_in_dma *dma = buf->dma;

	spin_lock(&dma->queued_lock);
	list_del(&buf->queue);
	spin_unlock(&dma->queued_lock);

	buf->buf.field = V4L2_FIELD_NONE;
	buf->buf.sequence = dma->sequence++;
	buf->buf.vb2_buf.timestamp = ktime_get_ns();
	vb2_set_plane_payload(&buf->buf.vb2_buf, 0, dma->format.sizeimage);
	vb2_buffer_done(&buf->buf.vb2_buf, VB2_BUF_STATE_DONE);

}

static void hdmi_in_buffer_queue(struct vb2_buffer *vb)
{
	struct hdmi_in_device *hidev = vb2_get_drv_priv(vb->vb2_queue);
	struct hdmi_in_dma *dma = &hidev->dma;
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct hdmi_in_buffer *buf =
					container_of(vbuf, struct hdmi_in_buffer, buf);
	struct dma_async_tx_descriptor *desc;
	dma_addr_t addr = vb2_dma_contig_plane_dma_addr(vb, 0);
	/*
	struct xilinx_vdma_config config;

	memset(&config, 0, sizeof(config));
	config.reset = 1;
	xilinx_vdma_channel_set_config(dma->dma, &config);
	config.reset = 0;
	xilinx_vdma_channel_set_config(dma->dma, &config);
	*/
	
	xilinx_xdma_v4l2_config(dma->dma, dma->format.pixelformat);
	//xilinx_xdma_v4l2_config(dma->dma, V4L2_PIX_FMT_RGB24);

	dma->xt.dir = DMA_DEV_TO_MEM;
	dma->xt.src_sgl = false;
	dma->xt.dst_sgl = true;
	dma->xt.dst_start = addr;

	dma->xt.frame_size = 1;
	dma->sgl[0].size = dma->format.width * 3;
	dma->sgl[0].icg = dma->format.bytesperline - dma->sgl[0].size;
	dma->xt.numf = dma->format.height;

	desc = dmaengine_prep_interleaved_dma(dma->dma, &dma->xt,
						DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		dev_err(hidev->dev, "failed to prepare DMA transfer");
		vb2_buffer_done(&buf->buf.vb2_buf, VB2_BUF_STATE_ERROR);
		return;
	}
	desc->callback = hdmi_in_dma_complete;
	desc->callback_param = buf;

	spin_lock_irq(&dma->queued_lock);
	list_add_tail(&buf->queue, &dma->queued_bufs);
	spin_unlock_irq(&dma->queued_lock);

	dmaengine_submit(desc);

	if (vb2_is_streaming(&dma->queue))
		dma_async_issue_pending(dma->dma);

}

static int hdmi_in_start_streaming(struct vb2_queue *vq, unsigned int count){
	struct hdmi_in_device *hidev = vb2_get_drv_priv(vq);
	struct hdmi_in_dma *dma = &hidev->dma;

	dma->sequence = 0;

	dma_async_issue_pending(dma->dma);

	return 0;
}

static void hdmi_in_stop_streaming(struct vb2_queue *vq)
{
	struct hdmi_in_device *hidev = vb2_get_drv_priv(vq);
	struct hdmi_in_dma *dma = &hidev->dma;
	struct hdmi_in_buffer *buf, *nbuf;

	dmaengine_terminate_all(dma->dma);

	spin_lock_irq(&dma->queued_lock);
	list_for_each_entry_safe(buf, nbuf, &dma->queued_bufs, queue) {
		vb2_buffer_done(&buf->buf.vb2_buf, VB2_BUF_STATE_ERROR);
		list_del(&buf->queue);
	}
	spin_unlock_irq(&dma->queued_lock);
}

static const struct vb2_ops hdmi_in_qops = {
	.queue_setup = hdmi_in_queue_setup,
	.buf_prepare = hdmi_in_buffer_prepare,
	.buf_queue = hdmi_in_buffer_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.start_streaming = hdmi_in_start_streaming,
	.stop_streaming = hdmi_in_stop_streaming,
};

static int hdmi_in_nodes_register(struct hdmi_in_device *hidev)
{
	struct hdmi_in_dma *dma = &hidev->dma;
	struct video_device *video = &dma->video;
	struct vb2_queue *queue = &dma->queue;
	int ret;

	mutex_init(&dma->lock);
	INIT_LIST_HEAD(&dma->queued_bufs);
	spin_lock_init(&dma->queued_lock);

	video->fops = &hdmi_in_fops;
	video->v4l2_dev = &hidev->v4l2_dev;
	video->queue = queue;
	snprintf(video->name, sizeof(video->name), "HDIM IN 0");
	video->vfl_type = VFL_TYPE_GRABBER;
	video->vfl_dir = VFL_DIR_RX;
	video->release = video_device_release_empty;
	video->ioctl_ops = &hdmi_in_ioctl_ops;
	video->ctrl_handler = video->v4l2_dev->ctrl_handler;
	
	video_set_drvdata(video, dma);

	queue->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	queue->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	queue->lock = &dma->lock;
	queue->drv_priv = hidev;
	queue->buf_struct_size = sizeof(struct hdmi_in_buffer);
	queue->ops = &hdmi_in_qops;
	queue->mem_ops = &vb2_dma_contig_memops;
	queue->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC
							| V4L2_BUF_FLAG_TSTAMP_SRC_EOF;
	queue->dev = hidev->dev;

	ret = vb2_queue_init(queue);
	if (ret < 0) {
		dev_err(hidev->dev, "failed to initialize VB2 queue");
		goto error;
	}

	ret = video_register_device(video, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		dev_err(hidev->dev, "failed to register video device");
		goto error;
	}

	return 0;
error:
	if (video_is_registered(video))
		video_unregister_device(video);
	mutex_destroy(&dma->lock);

	return ret;
}

static irqreturn_t hdmi_in_irq_handler(int irq, void *data)
{
	struct hdmi_in_device *hidev = data;

	/*
	dev_info(hidev->dev, "irq_handler");
	*/

	schedule_delayed_work(&hidev->work, msecs_to_jiffies(500));

	return IRQ_HANDLED;
}

static const int hvals[] = {1080, 1024, 720, 768, 600, 480};
static void hdmi_in_check_locked(struct hdmi_in_device *hidev)
{
	struct hdmi_in_dma *dma = &hidev->dma;
	int locked;

	locked = gpiod_get_value_cansleep(hidev->locked_gpio);
	/*
	dev_info(hidev->dev, "locked = %d\n", locked);
	*/
	if (locked) {
		int tc_locked = 0;
		int i;
		int dh, dhmin, dhidx;
		iowrite32(8, hidev->iomem);
		for (i=0; i<15; i++) {
			msleep(1000);
			tc_locked = ioread32(hidev->iomem + 0x24);
			if (tc_locked & 0x1) break;
		}
		/*
		dev_info(hidev->dev, "i = %d, tc_locked = %d", i, tc_locked);
		*/
		dma->format.width = ioread32(hidev->iomem + 0x20);
		dma->format.height = (dma->format.width & 0x1FFF0000) >> 16;
		dma->format.width = dma->format.width & 0x1FFF;

		dhmin = 1000;
		for (i=0; i<ARRAY_SIZE(hvals); i++) {
			dh = abs(hvals[i]-dma->format.height);
			if (dh < dhmin) {
				dhmin = dh;
				dhidx = i;
			}
		}
		dma->format.height = hvals[dhidx];
	} else {
		dma->format.height = 0;
		dma->format.width = 0;
	}
	/*
	dev_info(hidev->dev, "width=%d, heigh=%d", dma->format.width,
					dma->format.height);
	*/
	dma->format.bytesperline = dma->format.width * 3;
	dma->format.sizeimage = dma->format.bytesperline * dma->format.height;
}

static void hdmi_in_work_handler(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work, struct delayed_work, work);
	struct hdmi_in_device *hidev = container_of(dwork, struct hdmi_in_device, work);
	hdmi_in_check_locked(hidev);

	/*
	dev_info(hidev->dev, "work_handler");
	*/

}

static int hdmi_in_probe(struct platform_device *pdev)
{
	struct hdmi_in_device *hidev;
	struct hdmi_in_dma *dma;
	struct resource *res;
	int ret;

	/*
	dev_info(&pdev->dev, "probe begin");
	*/

	hidev = devm_kzalloc(&pdev->dev, sizeof(*hidev), GFP_KERNEL);
	if (!hidev)
		return -ENOMEM;

	hidev->dev = &pdev->dev;
	platform_set_drvdata(pdev, hidev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hidev->iomem = devm_ioremap_resource(hidev->dev, res);
	if (IS_ERR(hidev->iomem))
		return PTR_ERR(hidev->iomem);

	dma = &hidev->dma;

	dma->dma = dma_request_chan(hidev->dev, "video");
	if (IS_ERR(dma->dma)) {
		ret = PTR_ERR(dma->dma);
		if (ret != -EPROBE_DEFER)
			dev_err(hidev->dev, "No Video DMA channel found in DT");
		return ret;
	}
	dma->align = 1 << dma->dma->device->copy_align;

	hidev->locked_gpio = devm_gpiod_get(&pdev->dev, "locked", GPIOD_IN);
	if (IS_ERR(hidev->locked_gpio)) {
		ret = PTR_ERR(hidev->locked_gpio);
		goto err_dma_release_channel;
	}

	dma->format.pixelformat = DRM_FORMAT_RGB888;
	dma->format.colorspace = V4L2_COLORSPACE_SRGB;
	dma->format.field = V4L2_FIELD_NONE;

	hdmi_in_check_locked(hidev);
	
	//hidev->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	hidev->irq = gpiod_to_irq(hidev->locked_gpio);
	ret = devm_request_irq(hidev->dev, hidev->irq, hdmi_in_irq_handler,
			IRQF_SHARED | IRQF_ONESHOT | IRQF_TRIGGER_RISING, "hdmi_in", hidev);

	if (ret) {
		dev_err(&pdev->dev, "unable to request IRQ %d", hidev->irq);
		goto err_dma_release_channel;
	}

	INIT_DELAYED_WORK(&hidev->work, hdmi_in_work_handler);

	ret = v4l2_device_register(hidev->dev, &hidev->v4l2_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "V4L2 device registeration failed (%d)", ret);
		goto err_irq_release;
	}

	ret = hdmi_in_nodes_register(hidev);
	if (ret < 0) {
		goto err_device_unregister;
	}

	/*
	dev_info(&pdev->dev, "probe end");
	*/

	return 0;

err_device_unregister:
	v4l2_device_unregister(&hidev->v4l2_dev);
err_irq_release:
	devm_free_irq(hidev->dev, hidev->irq, hidev);
err_dma_release_channel:
	dma_release_channel(hidev->dma.dma);
	return ret;
}

static int hdmi_in_remove(struct platform_device *pdev)
{
	struct hdmi_in_device *hidev = platform_get_drvdata(pdev);
	struct hdmi_in_dma *dma = &hidev->dma;
	struct video_device *video = &dma->video;

	/*
	dev_info(&pdev->dev, "remove");
	*/

	if (video_is_registered(video))
		video_unregister_device(video);
	mutex_destroy(&dma->lock);
	v4l2_device_unregister(&hidev->v4l2_dev);
	devm_free_irq(hidev->dev, hidev->irq, hidev);
	dma_release_channel(hidev->dma.dma);
	return 0;
}

static struct of_device_id hdmi_in_of_id_table[] = {
	{ .compatible = "inipro,hdmi-in-1.00", },
	{ }
};
MODULE_DEVICE_TABLE(of, hdmi_in_of_id_table);

static struct platform_driver hdmi_in_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME,
		.of_match_table = hdmi_in_of_id_table,
	},
	.probe = hdmi_in_probe,
	.remove = hdmi_in_remove,
};
module_platform_driver(hdmi_in_driver);

MODULE_AUTHOR("Hyunok Kim <hokim@inipro.net>");
MODULE_DESCRIPTION("HDMI IN Driver");
MODULE_LICENSE("GPL v2");
