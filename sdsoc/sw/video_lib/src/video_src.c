#include <fcntl.h>
#include <glib.h>
#include <glob.h>
#include <mediactl/mediactl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "common.h"
#include "helper.h"
#include "mediactl_helper.h"
#include "vcap_file_int.h"
#include "vcap_hdmi_int.h"
#include "video_int.h"

static GPtrArray *video_srcs;

const char *vlib_video_src_get_display_text(const struct vlib_vdev *vsrc)
{
	if (!vsrc) {
		return NULL;
	}

	return vsrc->display_text;
}

int vlib_video_src_get_vnode(const struct vlib_vdev *vsrc)
{
	if (!vsrc) {
		return -1;
	}

	switch (vsrc->vsrc_type) {
		case VSRC_TYPE_MEDIA:
			return vsrc->data.media.vnode;
		case VSRC_TYPE_V4L2:
			return vsrc->data.v4l2.vnode;
		default:
			break;
	}

	return -1;
}

const struct vlib_vdev *vlib_video_src_get(size_t id)
{
	if (id >= video_srcs->len) {
		return NULL;
	}

	return g_ptr_array_index(video_srcs, id);
}

struct media_device *vlib_vdev_get_mdev(const struct vlib_vdev *vdev)
{
	if (vdev->vsrc_type != VSRC_TYPE_MEDIA) {
		return NULL;
	}

	return vdev->data.media.mdev;
}

static void vlib_vsrc_vdev_free(struct vlib_vdev *vd)
{
	switch (vd->vsrc_type) {
		case VSRC_TYPE_MEDIA:
			media_device_unref(vd->data.media.mdev);
			close(vd->data.media.vnode);
			break;
		case VSRC_TYPE_V4L2:
			close(vd->data.v4l2.vnode);
			break;
		default:
			break;
	}

	free(vd->priv);
	free(vd);
}

static void vlib_vsrc_table_free_func(void *e)
{
	struct vlib_vdev *vd = e;

	vlib_vsrc_vdev_free(vd);
}

static const struct matchtable mt_drivers_v4l2[] = {
	{
		.s = "hdmi-in", .init = vcap_hdmi_init,
	},
};

int vlib_video_src_init(struct vlib_config_data *cfg)
{
	int ret;
	glob_t pglob;

	video_srcs = g_ptr_array_new_with_free_func(vlib_vsrc_table_free_func);
	if (!video_srcs) {
		return VLIB_ERROR_OTHER;
	}

	ret = glob("/dev/video*", 0, NULL, &pglob);
	if (ret && ret != GLOB_NOMATCH) {
		ret = VLIB_ERROR_OTHER;
		goto error;
	}

	ret = VLIB_SUCCESS;

	for (size_t i = 0; i < pglob.gl_pathc; i++) {
		int fd = open(pglob.gl_pathv[i], O_RDWR);
		if (fd < 0) {
			ret = VLIB_ERROR_OTHER;
			goto error;
		}

		struct v4l2_capability vcap;
		ret = ioctl(fd, VIDIOC_QUERYCAP, &vcap);
		if (ret) {
			close(fd);
			continue;
		}

		size_t j;
		for (j=0; j<ARRAY_SIZE(mt_drivers_v4l2); j++) {
			if (strcmp(mt_drivers_v4l2[j].s, (char *)vcap.driver)) {
				continue;
			}

			struct vlib_vdev *vd =
				mt_drivers_v4l2[j].init(&mt_drivers_v4l2[j],
										(void *)(uintptr_t)fd);
			if (vd) {
				vlib_dbg("found video source '%s (%s)'\n",
						vd->display_text, pglob.gl_pathv[i]);
				g_ptr_array_add(video_srcs, vd);
				break;
			}
		}

		if (j == ARRAY_SIZE(mt_drivers_v4l2)) {
			close(fd);
		}
	}

	if (cfg->vcap_file_fn) {
		struct vlib_vdev *vd = vcap_file_init(NULL, (void *)cfg->vcap_file_fn);
		if (vd) {
			vlib_dbg("found video source '%s (%s)'\n",
					vd->display_text, cfg->vcap_file_fn);
			g_ptr_array_add(video_srcs, vd);
		}
	}

error:
	globfree(&pglob);
	return ret;
}

void vlib_video_src_uninit(void)
{
	g_ptr_array_free(video_srcs, TRUE);
}

size_t vlib_video_src_cnt_get(void)
{
	return video_srcs->len;
}

