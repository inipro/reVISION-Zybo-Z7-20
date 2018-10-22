#ifndef MEDIA_CTL_H
#define MEDIA_CTL_H

struct media_device;
struct media_device_info;
struct vlib_vdev;
struct v4l2_dv_timings;

/* Display media device info */
void print_media_info(const struct media_device_info *info);
/* Retrieve the detected digital video timings */
int query_entity_dv_timings(const struct vlib_vdev *vdev, char *name,
							unsigned int path, struct v4l2_dv_timings *timings);
/* Returns the full path and name to the device node */
int get_entity_devname(struct media_device *media, char *name, char *subdev_name);
/* Set media format string */
void media_set_fmt_str(char *set_fmt, char *entity, unsigned int pad,
						const char *fmt, unsigned int width, unsigned int height);
/* Set media pad string */
void media_set_pad_str(char *set_fmt, char *entity, unsigned int pad);

#endif /* MEDIA_CTL_H */
