#ifndef __FILTER_H__
#define __FILTER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

struct filter_s {
	const char *display_text;
	const char *dt_comp_string;
	const char *pr_file_name;
	char *pr_buf;
	int fd;
	size_t mode;
	struct filter_ops *ops;
	void *data;				/* pointer to pass data to filter init / private data pointer */
	size_t num_modes;
	const char **modes;
};

/* TODO: Remove once CR-986477 is fixed */
#ifdef __SDSVHLS__
#define _GLIB_TEST_OVERFLOW_FALLBACK
#include <glib.h>
#undef _GLIB_TEST_OVERFLOW_FALLBACK
#else
#include <glib.h>
#endif

struct filter_tbl {
	unsigned int size;
	GPtrArray *filter_types;
};

struct filter_init_data {
	size_t in_width;
	size_t in_height;
	uint32_t in_fourcc;
	size_t out_width;
	size_t out_height;
	uint32_t out_fourcc;
};

struct filter_ops {
	int (*init)(struct filter_s *fs, const struct filter_init_data *data);
	void (*func)(struct filter_s *fs,
				unsigned char *frm_data_in, unsigned char *frm_data_out,
				int height_in, int width_in, int stride_in,
				int height_out, int width_out, int stride_out);
	void (*func2)(struct filter_s *fs, 
			unsigned char *frame_prev, unsigned char *frame_curr,
		   	unsigned char *frame_out,
			int height_in, int width_in, int stride_in,
			int height_out, int width_out, int stride_out);
};

/* Helper functions */
int filter_type_register(struct filter_tbl *ft, struct filter_s *fs);
int filter_type_unregister(struct filter_tbl *ft, struct filter_s *fs);
struct filter_s *filter_type_get_obj(struct filter_tbl *ft, unsigned int i);
int filter_type_match(struct filter_s *fs, const char *str);
int filter_type_set_mode(struct filter_s *fs, size_t mode);
const char *filter_type_get_display_text(const struct filter_s *fs);

static inline size_t filter_type_get_num_modes(const struct filter_s *fs)
{
	return fs->num_modes;
}

static inline const char *filter_type_get_mode(const struct filter_s *fs,
												size_t m)
{
	if (m >= filter_type_get_num_modes(fs)) {
		return NULL;
	}

	return fs->modes[m];
}

/* Partial reconfig functions */
int filter_type_prefetch_bin(struct filter_s *fs);
int filter_type_free_bin(struct filter_s *fs);
int filter_type_config_bin(struct filter_s *fs);

#ifdef __cplusplus
}
#endif

#endif /* __FILTER_H__ */
