#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>

struct vlib_plane {
	size_t id;	/* plane ID */
	size_t width;
	size_t height;
	size_t xoffs;	/* x offset */
	size_t yoffs;	/* y offset */
};

#endif /* COMMON_H */
