#include "v4l2_helper.h"
#include "video_int.h"
#include <sys/ioctl.h>

struct vlib_vdev *vcap_hdmi_init(const struct matchtable *mte, void *data)
{
	int ret;
	int fd = (uintptr_t)data;

	struct v4l2_capability vcap;
	ret = ioctl(fd, VIDIOC_QUERYCAP, &vcap);
	if (ret) {
		return NULL;
	}

	if (!(vcap.capabilities & V4L2_CAP_DEVICE_CAPS)) {
		return NULL;
	}

	if (!(vcap.device_caps & V4L2_CAP_VIDEO_CAPTURE)) {
		return NULL;
	}

	if (!(vcap.device_caps & V4L2_CAP_STREAMING)) {
		return NULL;
	}

	struct vlib_vdev *vd = calloc(1, sizeof(*vd));
	if (!vd) {
		return NULL;
	}

	vd->vsrc_type = VSRC_TYPE_V4L2;
	vd->data.v4l2.vnode = fd;
	vd->vsrc_class = VLIB_VCLASS_HDMII;
	vd->display_text = "HDMI Input";
	vd->entity_name = mte->s;

	return vd;
}
