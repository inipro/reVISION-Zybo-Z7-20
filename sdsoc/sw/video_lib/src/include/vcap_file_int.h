#ifndef VCAP_FILE_INT_H
#define VCAP_FILE_INT_H

#ifdef __cplusplus
extern "C"
{
#endif

struct vlib_vdev;
struct matchtable;

struct vlib_vdev *vcap_file_init(const struct matchtable *mte, const void *filename);

#ifdef __cplusplus
}
#endif

#endif /* VCAP_FILE_INT_H */
