#ifndef VCAP_HDMI_INT_H
#define VCAP_HDMI_INT_H

#ifdef __cplusplus
extern "C"
{
#endif

struct vlib_vdev;
struct matchtable;

struct vlib_vdev *vcap_hdmi_init(const struct matchtable *mte, void *media);

#ifdef __cplusplus
}
#endif

#endif /* VCAP_HDMI_INT_H */
