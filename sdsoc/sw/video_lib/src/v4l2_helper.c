#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "helper.h"
#include "mediactl_helper.h"
#include "v4l2_helper.h"

static int v4l2_set_fps(struct v4l2_dev *dev)
{
	int ret;
	struct v4l2_streamparm parm;

	/* if custom frame rate handler exists call it instead */
	if (dev->vdev->ops && dev->vdev->ops->set_frame_rate) {
		return dev->vdev->ops->set_frame_rate(dev->vdev,
								dev->setup_ptr->fps.numerator,
								dev->setup_ptr->fps.denominator);
	}

	memset(&parm, 0, sizeof(parm));
	parm.type = dev->buf_type;

	ret = ioctl(dev->fd, VIDIOC_G_PARM, &parm);
	if (ret) {
		VLIB_REPORT_ERR("VIDIOC_G_PARM not supported by device");
		vlib_dbg("%s\n", vlib_errstr);
		return VLIB_ERROR_NOT_SUPPORTED;
	}

	if (!(parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
		VLIB_REPORT_ERR("V4L2_CAP_TIMEPERFRAME not supported by device");
		return VLIB_ERROR_NOT_SUPPORTED;
	}

	/* video lib uses fps while v4l2 uses time per frame */
	parm.parm.capture.timeperframe.numerator = dev->setup_ptr->fps.denominator;
	parm.parm.capture.timeperframe.denominator = dev->setup_ptr->fps.numerator;

	ret = ioctl(dev->fd, VIDIOC_S_PARM, &parm);
	ASSERT2(!ret, "VIDIOC_S_PARM failed: %s\n", ERRSTR);

	ret = ioctl(dev->fd, VIDIOC_G_PARM, &parm);
	ASSERT2(!ret, "VIDIOC_G_PARM failed: %s\n", ERRSTR);

	vlib_info("frame rate set to: %u/%u fps\n",
			parm.parm.capture.timeperframe.denominator,
			parm.parm.capture.timeperframe.numerator);

	return 0;
}

/**
 * v4l2_has_mplane - Check capabilities for mplane support
 * @fd: File descriptor of video capture node
 *
 * Check the video device capabilities for mplane support
 *
 * Return: 0 if mplane is not supported, 1 if mplane is supported.
 */
static unsigned int v4l2_has_mplane(int fd)
{
	struct v4l2_capability cap;
	memset(&cap, 0, sizeof(cap));

	int ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
	ASSERT2(!ret, "error querying device capabilities\n");

	unsigned int caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS ?
						cap.device_caps : cap.capabilities;

	return caps & (V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE);
}

int v4l2_init(struct v4l2_dev *dev, unsigned int num_buffers)
{
	struct v4l2_format fmt;
	struct v4l2_requestbuffers rqbufs;
	int ret;

	dev->buffer_cnt = num_buffers;
	dev->vid_buf = calloc(dev->buffer_cnt, sizeof(*dev->vid_buf));
	ASSERT2(dev->vid_buf, "failed to allocate v4l2 buffer structs.\n");

	if (v4l2_has_mplane(dev->fd)) {
		dev->buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		memset(&fmt, 0, sizeof(fmt));
		fmt.type = dev->buf_type;
		ret = ioctl(dev->fd, VIDIOC_G_FMT, &fmt);
		ASSERT2(ret >= 0, "VIDIOC_G_FMT failed: %s\n", ERRSTR);
		vlib_dbg("G_FMT(start): width = %u, height = %u, bytes per line = %u, "
				"4cc = %.4s, color space = %u, mplane = true\n", fmt.fmt.pix.width,
				fmt.fmt.pix_mp.height, fmt.fmt.pix_mp.plane_fmt[0].bytesperline,
				(char*)&fmt.fmt.pix_mp.pixelformat, fmt.fmt.pix_mp.colorspace);

		fmt.fmt.pix_mp.width = dev->format.width;
		fmt.fmt.pix_mp.height = dev->format.height;
		fmt.fmt.pix_mp.pixelformat = dev->format.pixelformat;
		fmt.fmt.pix_mp.colorspace = dev->format.colorspace;
		fmt.fmt.pix_mp.num_planes = 1;
		fmt.fmt.pix_mp.plane_fmt[0].bytesperline = dev->format.bytesperline;
		fmt.fmt.pix_mp.plane_fmt[0].sizeimage = dev->format.sizeimage;

		ret = ioctl(dev->fd, VIDIOC_S_FMT, &fmt);
		ASSERT2(ret>=0, "VIDIOC_S_FMT failed: %s\n", ERRSTR);
		vlib_dbg("G_FMT(final): width = %u, height = %u, bytes per line = %u, "
				"4cc = %.4s, color space = %u, mplane = true\n", fmt.fmt.pix_mp.width,
				fmt.fmt.pix_mp.height, fmt.fmt.pix_mp.plane_fmt[0].bytesperline,
				(char*)&fmt.fmt.pix_mp.pixelformat, fmt.fmt.pix_mp.colorspace);

		// check if pixel format is supported
		if (fmt.fmt.pix_mp.pixelformat != dev->format.pixelformat) {
			// look up pixel format fourcc code
			VLIB_REPORT_ERR("Requested pixel format '%.4s' is not supported by device",
					(const char *)&dev->format.pixelformat);
			vlib_dbg("%s\n", vlib_errstr);
			ret = VLIB_ERROR_CAPTURE;
			goto err;
		}

		// check if resolution is supported
		if (fmt.fmt.pix_mp.width != dev->format.width ||
			fmt.fmt.pix_mp.height != dev->format.height) {
			VLIB_REPORT_ERR("Requested resolution '%dx%d' is not supported by device",
					dev->format.width, dev->format.height);
			vlib_dbg("%s\n", vlib_errstr);
			ret = VLIB_ERROR_CAPTURE;
			goto err;
		}

		// check if stride is supported
		if (fmt.fmt.pix_mp.plane_fmt[0].bytesperline != dev->format.bytesperline) {
			VLIB_REPORT_ERR("Requested stride '%d' is not supported by device",
					dev->format.bytesperline);
			vlib_dbg("%s\n", vlib_errstr);
			ret = VLIB_ERROR_CAPTURE;
			goto err;
		}

		dev->format.width = fmt.fmt.pix_mp.width;
		dev->format.height = fmt.fmt.pix_mp.height;
		dev->format.pixelformat = fmt.fmt.pix_mp.pixelformat;
		dev->format.colorspace = fmt.fmt.pix_mp.colorspace;
		dev->format.bytesperline = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
		dev->format.sizeimage = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
	} else {
		dev->buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		memset(&fmt, 0, sizeof(fmt));
		fmt.type = dev->buf_type;
		ret = ioctl(dev->fd, VIDIOC_G_FMT, &fmt);
		ASSERT2(ret >= 0, "VIDIOC_G_FMT failed: %s\n", ERRSTR);
		vlib_dbg("G_FMT(start): width = %u, height = %u, bytes per line = %u, "
				"4cc = %.4s, color space = %u, mplane = false\n", fmt.fmt.pix.width,
				fmt.fmt.pix.height, fmt.fmt.pix.bytesperline,
				(char*)&fmt.fmt.pix.pixelformat, fmt.fmt.pix.colorspace);

		fmt.fmt.pix = dev->format;
		ret = ioctl(dev->fd, VIDIOC_S_FMT, &fmt);
		ASSERT2(ret >= 0, "VIDIOC_S_FMT failed: %s\n", ERRSTR);
		vlib_dbg("G_FMT(final): width = %u, height = %u, bytes per line = %u, "
				"4cc = %.4s, color space = %u, mplane = false\n", fmt.fmt.pix.width,
				fmt.fmt.pix.height, fmt.fmt.pix.bytesperline,
				(char*)&fmt.fmt.pix.pixelformat, fmt.fmt.pix.colorspace);

		// check if pixel format is supported
		if (fmt.fmt.pix.pixelformat != dev->format.pixelformat) {
			// look up pixel format fourcc code
			VLIB_REPORT_ERR("Requested pixel format '%.4s' is not supported by device",
					(const char *)&dev->format.pixelformat);
			vlib_dbg("%s\n", vlib_errstr);
			ret = VLIB_ERROR_CAPTURE;
			goto err;
		}

		// check if resolution is supported
		if (fmt.fmt.pix.width != dev->format.width ||
				fmt.fmt.pix.height != dev->format.height) {
			VLIB_REPORT_ERR("Requested resolution '%dx%d' is not supported by device",
					dev->format.width, dev->format.height);
			vlib_dbg("%s\n", vlib_errstr);
			ret = VLIB_ERROR_CAPTURE;
			goto err;
		}

		// check if stride is supported
		if (fmt.fmt.pix.bytesperline != dev->format.bytesperline) {
			VLIB_REPORT_ERR("Requested stride '%d' is not supported by device",
					dev->format.bytesperline);
			vlib_dbg("%s\n", vlib_errstr);
			ret = VLIB_ERROR_CAPTURE;
			goto err;
		}

		dev->format = fmt.fmt.pix;
	}

	memset(&rqbufs, 0, sizeof(rqbufs));
	rqbufs.count = num_buffers;
	rqbufs.type = dev->buf_type;
	rqbufs.memory = dev->mem_type;
	ret = ioctl(dev->fd, VIDIOC_REQBUFS, &rqbufs);
	ASSERT2(ret >= 0, "VIDIOC_REQBUFS failed: %s\n", ERRSTR);
	ASSERT2(rqbufs.count >= num_buffers, "video node allocated only "
			"%u of %u buffers\n", rqbufs.count, num_buffers);

	/* set frame rate if specified */
	if (dev->setup_ptr->fps.denominator) {
		ret = v4l2_set_fps(dev);
		if (ret) {
			vlib_warn("setting frame rate failed: %s: %s\n",
					vlib_errstr, strerror(errno));
		}
	}

	return VLIB_SUCCESS;

err:
	return ret;
}

void v4l2_uninit(struct v4l2_dev *dev)
{
	struct v4l2_requestbuffers rqbufs;
	memset(&rqbufs, 0, sizeof(rqbufs));
	rqbufs.type = dev->buf_type;
	rqbufs.memory = dev->mem_type;
	int ret = ioctl(dev->fd, VIDIOC_REQBUFS, &rqbufs);
	ASSERT2(ret >= 0, "VIDIOC_REQBUFS failed: %s\n", ERRSTR);

	free(dev->vid_buf);
}

void v4l2_queue_buffer(struct v4l2_dev *dev, const struct buffer *buffer)
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes;
	int ret;

	memset(&buf, 0, sizeof(buf));
	buf.type = dev->buf_type;
	buf.index = buffer->index;
	buf.memory = dev->mem_type;

	if (dev->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		buf.m.fd = buffer->dbuf_fd;
	} else if (dev->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		memset(&planes, 0, sizeof(planes));
		planes.m.fd = buffer->dbuf_fd;
		buf.m.planes = &planes;
		/* Hard-code number of planes to 1 allowing packed formats only */
		buf.length = 1;
	}

	ret = ioctl(dev->fd, VIDIOC_QBUF, &buf);
	ASSERT2(!ret, "VIDIOC_QBUF(index = %d) failed: %s\n", buffer->index,
			ERRSTR);
}

struct buffer *v4l2_dequeue_buffer(struct v4l2_dev *dev, struct buffer *buffers)
{
	struct v4l2_buffer buf;
	int ret;

	memset(&buf, 0, sizeof(buf));
	buf.type = dev->buf_type;
	buf.memory = dev->mem_type;

	if (dev->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		struct v4l2_plane planes;
		memset(&planes, 0, sizeof(planes));
		buf.m.planes = &planes;
		/* Hard-code number of planes to 1 allowing packed formats only */
		buf.length = 1;
	}

	ret = ioctl(dev->fd, VIDIOC_DQBUF, &buf);
	ASSERT2(!ret, "VIDIOC_DQBUF failed: %s\n", ERRSTR);

	if (buf.flags & V4L2_BUF_FLAG_ERROR) {
		vlib_warn("%s: corrupted buffer\n", __func__);
	}

	if (dev->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
			buf.bytesused != dev->setup_ptr->w * dev->setup_ptr->h *
							vlib_fourcc2bpp(dev->setup_ptr->in_fourcc)) {
		vlib_warn("%s: incomplete frame\n", __func__);
	} else if (dev->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
			buf.m.planes->bytesused != dev->setup_ptr->w * dev->setup_ptr->h *
							vlib_fourcc2bpp(dev->setup_ptr->in_fourcc)) {
		vlib_warn("%s: incomplete frame\n", __func__);
	}

	return &buffers[buf.index];
}

/* turn off video device */
int v4l2_device_off(struct v4l2_dev *d)
{
	return ioctl(d->fd, VIDIOC_STREAMOFF, &d->buf_type);
}

/* turn on video device */
int v4l2_device_on(struct v4l2_dev *d)
{
	return ioctl(d->fd, VIDIOC_STREAMON, &d->buf_type);
}

/* set subdevice control */
int v4l2_set_ctrl(const struct vlib_vdev *vsrc, char *name, int id, int value)
{
	int fd, ret;
	char subdev_name[DEV_NAME_LEN];
	struct v4l2_queryctrl query;
	struct v4l2_control ctrl;

	if (!vsrc) {
		return VLIB_ERROR_INVALID_PARAM;
	}

	//get_entity_devname(vlib_vdev_get_mdev(vsrc), name, subdev_name); //check_again

	fd = open(subdev_name, O_RDWR);
	ASSERT2(fd >= 0, "failed to open %s: %s\n", subdev_name, ERRSTR);

	memset(&query, 0, sizeof(query));
	query.id = id;
	ret = ioctl(fd, VIDIOC_QUERYCTRL, &query);
	ASSERT2(ret >= 0, "VIDIOC_QUERYCTL failed: %s\n", ERRSTR);

	if (query.flags & V4L2_CTRL_FLAG_DISABLED) {
		vlib_info("V4L2_CID_%d is disabled\n", id);
	} else {
		memset(&ctrl, 0, sizeof(ctrl));
		ctrl.id = query.id;
		ctrl.value = value;
		ret = ioctl(fd, VIDIOC_S_CTRL, &ctrl);
		ASSERT2(ret >= 0, "VIDIOC_S_CTRL failed: %s\n", ERRSTR);
	}

	close(fd);
	return VLIB_SUCCESS;
}
