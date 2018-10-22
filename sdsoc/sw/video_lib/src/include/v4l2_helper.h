#ifndef V4L2_HELPER_H
#define V4L2_HELPER_H

#include <linux/videodev2.h>

#include "common.h"
#include "common_int.h"

struct video_pipeline;
struct vlib_vdev;

struct buffer {
	unsigned int index;
	int dbuf_fd;
	unsigned char *v4l2_buff;
	unsigned int v4l2_buff_length;
	struct drm_buffer *drm_buf;
};

/* video device */
struct v4l2_dev {
	int fd;							/* device node fd */
	enum v4l2_buf_type buf_type;	/* type of buffer */
	enum v4l2_memory mem_type;		/* type of memory */
	struct v4l2_pix_format format;
	struct buffer *vid_buf;
	size_t buffer_cnt;
	struct video_pipeline *setup_ptr;
	const struct vlib_vdev *vdev;
};

#include "video_int.h"

/* Initialize v4l2 video device */
int v4l2_init(struct v4l2_dev *dev, unsigned int num_buffers);
void v4l2_uninit(struct v4l2_dev *dev);
/* Queue buffer to video device */
void v4l2_queue_buffer(struct v4l2_dev *, const struct buffer *);
/* Dequeue buffer from video device */
struct buffer *v4l2_dequeue_buffer(struct v4l2_dev *, struct buffer *);
/* Start the capture or output process during streaming */
int v4l2_device_off(struct v4l2_dev *);
/* Stop the capture or output process during streaming */
int v4l2_device_on(struct v4l2_dev *);
/* Set subdevice control */
int v4l2_set_ctrl(const struct vlib_vdev *vsrc, char *name, int id, int value);

#endif /* V4L2_HELPER_H */
