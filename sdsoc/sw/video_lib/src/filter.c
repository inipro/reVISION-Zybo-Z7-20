#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "filter.h"
#include "helper.h"
#include "video_int.h"

#define FILTER_PR_BIN_SIZE	753848

/**
 * filter_type_register - register a filter with vlib
 * @ft: Pointer to filter table
 * @fs: Pointer to filter struct to register
 *
 * Return: 0 on success, error code otherwise.
 */
int filter_type_register(struct filter_tbl *ft, struct filter_s *fs)
{
	if (!ft || !fs)
		return VLIB_ERROR_INVALID_PARAM;

	if (!ft->filter_types) {
		ft->filter_types = g_ptr_array_new();
		if (!ft->filter_types) {
			return VLIB_ERROR_OTHER;
		}
	}

	g_ptr_array_add(ft->filter_types, fs);

	vlib_info("Filter %s registered successfully!\n",
			filter_type_get_display_text(fs));

	ft->size = ft->filter_types->len;

	return 0;
}

int filter_type_unregister(struct filter_tbl *ft, struct filter_s *fs)
{
	if (!ft || !fs)
		return -1;

	g_ptr_array_remove_fast(ft->filter_types, fs);
	ft->size = ft->filter_types->len;

	if (!ft->size) {
		g_ptr_array_free(ft->filter_types, TRUE);
	}

	vlib_info("Filter %s unregistered successfully!\n", filter_type_get_display_text(fs));

	return ft->size;
}

struct filter_s *filter_type_get_obj(struct filter_tbl *ft, unsigned int i)
{
	if (ft && i < ft->size)
		return g_ptr_array_index(ft->filter_types, i);

	return NULL;
}

int filter_type_match(struct filter_s *fs, const char *str)
{
	if (fs && !strcmp(filter_type_get_display_text(fs), str))
		return 1;
	else
		return 0;
}

int filter_type_set_mode(struct filter_s *fs, size_t mode)
{
	if (!fs || mode >= filter_type_get_num_modes(fs)) {
		return VLIB_ERROR_INVALID_PARAM;
	}

	fs->mode = mode;

	return 0;
}

const char *filter_type_get_display_text(const struct filter_s *fs)
{
	ASSERT2(fs, " %s :: argument NULL\n", __func__);
	return fs->display_text;
}

int filter_type_prefetch_bin(struct filter_s *fs)
{
	char file_name[128];
	char *buf;
	int fd;
	int ret;

	if (fs && fs->pr_file_name[0] != '\0') {
		// compose file name
		sprintf(file_name, "/media/card/partial/%s", fs->pr_file_name);

		// open partial bitfile
		fd = open(file_name, O_RDONLY);
		if (fd < 0) {
			VLIB_REPORT_ERR("failed to open partial bitfile '%s': %s",
							file_name, strerror(errno));
			vlib_dbg("%s\n", vlib_errstr);
			return VLIB_ERROR_FILE_IO;
		}

		// allocate buffer and read partial bitfile into buffer
		buf = (char *)malloc(FILTER_PR_BIN_SIZE);
		ret = read(fd, buf, FILTER_PR_BIN_SIZE);
		if (ret < 0) {
			VLIB_REPORT_ERR("failed to read partial bitfile '%s': %s",
					file_name, strerror(errno));
			vlib_dbg("%s\n", vlib_errstr);
			close(fd);
			return VLIB_ERROR_FILE_IO;
		}

		// store buffer address and close file handle
		fs->pr_buf = buf;
		close(fd);
	}

	return 0;
}

int filter_type_free_bin(struct filter_s *fs)
{
	/* In case arg to free is NULL, no action occurs */
	if (fs)
		free(fs->pr_buf);

	return 0;
}

int filter_type_config_bin(struct filter_s *fs)
{
	if (!(fs && fs->pr_buf)) {
		return 0;
	}

	// Set is_partial_bitfile device attribute
	int fd = open("/sys/devices/soc0/amba/f8007000.devcfg/is_partial_bitstream", O_RDWR);
	if (fd < 0) {
		VLIB_REPORT_ERR("failed to set xdevcfg attribute 'is_partial_bitstream': %s", strerror(errno));
		vlib_dbg("%s\n", vlib_errstr);
		return VLIB_ERROR_FILE_IO;

	}

	int ret = write(fd, "1", 2);
	if (ret != 2) {
		VLIB_REPORT_ERR("failed to set xdevcfg attribute 'is_partial_bitstream': %s", strerror(errno));
		vlib_dbg("%s\n", vlib_errstr);
		ret = VLIB_ERROR_FILE_IO;
		goto err_close;
	}
	close(fd);

	// Write partial bitfile to xdevcfg device
	fd = open("/dev/xdevcfg", O_RDWR);
	if (fd < 0) {
		VLIB_REPORT_ERR("failed to open '/dev/xdevcfg' : %s",
						strerror(errno));
		vlib_dbg("%s\n", vlib_errstr);
		return VLIB_ERROR_FILE_IO;	
	}
	ret = write(fd, fs->pr_buf, FILTER_PR_BIN_SIZE);
	if (ret != FILTER_PR_BIN_SIZE) {
		vlib_err("failed to open '/dev/xdevcfg': %s", strerror(errno));
		ret = VLIB_ERROR_FILE_IO;
		goto err_close;
	}

err_close:
	close(fd);

	return ret;
}
