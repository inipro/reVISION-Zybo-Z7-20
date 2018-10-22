#ifndef COMMON_INT_H_
#define COMMON_INT_H_

/* Common defines / utils for video_lib */
#define DEV_NAME_LEN       32

struct drm_buffer {
	unsigned int index;
	unsigned int bo_handle;
	unsigned int fb_handle;
	int dbuf_fd;			/* DRM kernel buffer FD */
	unsigned char *drm_buff;
	unsigned int dumb_buff_length;
};

#endif
