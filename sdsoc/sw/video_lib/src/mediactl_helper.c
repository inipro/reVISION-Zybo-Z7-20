#include <string.h>
#include <mediactl/mediactl.h>
#include <mediactl/v4l2subdev.h>

#include "helper.h"
#include "mediactl_helper.h"
#include "video_int.h"

#define MEDIA_FMT "\"%s\":%d [fmt:%s/%dx%d field:none]"

void media_set_fmt_str(char *set_fmt, char *entity, unsigned int pad,
						const char *fmt, unsigned int width, unsigned int height)
{
	sprintf(set_fmt, MEDIA_FMT, entity, pad, fmt, width, height);
}

#define MEDIA_PAD "\"%s\":%d"

void media_set_pad_str(char *set_fmt, char *entity, unsigned int pad)
{
	sprintf(set_fmt, MEDIA_PAD, entity, pad);
}

/* Print media device details */
void print_media_info(const struct media_device_info *info)
{
	vlib_info("Media controller API version %u.%u.%u\n\n",
											(info->media_version << 16) & 0xff,
											(info->media_version << 8) & 0xff,
											(info->media_version << 0) & 0xff);
	vlib_info("Media device information\n"
											"------------------------\n"
											"driver                      %s\n"
											"model                       %s\n"
											"serial                      %s\n"
											"bus info            %s\n"
											"hw revision         0x%x\n"
											"driver version      %u.%u.%u\n\n",
											info->driver, info->model,
											info->serial, info->bus_info,
											info->hw_revision,
											(info->driver_version << 16) & 0xff,
											(info->driver_version << 8) & 0xff,
											(info->driver_version << 0) & 0xff);
}

/*
 * Helper function that returns the full path and name to the device node
 * corresponding to the given entity i.e (/dev/v4l-subdev*)
 */
int get_entity_devname(struct media_device *media, char *name, char *subdev_name)
{
	struct media_entity *entity;
	const char *entity_node_name;
	int ret;

	/* Enumerate entities, pads and links */
	ret = media_device_enumerate(media);
	ASSERT2(ret >= 0, "Failed to enumerate media device (%d)\n", ret);

	entity = media_get_entity_by_name(media, name, strlen(name));
	ASSERT2(entity, "Entity '%s' not found\n", name);

	entity_node_name = media_entity_get_devname(entity);
	ASSERT2(entity_node_name, "Entity '%s' has no device node name\n",
			name);

	strcpy(subdev_name, entity_node_name);
	return VLIB_SUCCESS;
}

/*
 * Helper function that retrieve the detected digital video timings for the
 * currently selected input of @entity_name and store them in the @timings
 * structure
 */
int query_entity_dv_timings(const struct vlib_vdev *vdev, char *entity_name,
							unsigned int padn, struct v4l2_dv_timings *timings)
{
	struct media_pad *pad;
	int ret;
	struct media_device *mdev = vlib_vdev_get_mdev(vdev);
	char pad_str[100];

	/* Enumerate entities, pads and links */
	ret = media_device_enumerate(mdev);
	ASSERT2(ret >= 0, "Failed to enumerate media device (%d)\n", ret);

	memset(pad_str, 0, sizeof(pad_str));
	media_set_pad_str(pad_str, entity_name, padn);
	pad = media_parse_pad(mdev, pad_str, NULL);
	ASSERT2(pad, "Pad '%s' not found\n", entity_name);

	ret = v4l2_subdev_query_dv_timings(pad->entity, timings);

	return ret;
}
