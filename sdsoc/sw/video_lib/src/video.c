#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <drm/drm_fourcc.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "helper.h"
#include "video_int.h"
#include "log_events.h"
#include "m2m_sw_pipeline.h"
#include "mediactl_helper.h"
#include "s2m_pipeline.h"
#include "filter.h"

/* Maximum number of bytes in a log line */
#define VLIB_LOG_SIZE 256

/* number of frame buffers */
#define BUFFER_CNT_MIN		6
#define BUFFER_CNT_DEFAULT	6

/* global variables */
char vlib_errstr[VLIB_ERRSTR_SIZE];

static struct video_pipeline *video_setup;

static int vlib_filter_init(struct video_pipeline *vp)
{
	int ret;
	struct filter_tbl *ft = vp->ft;
	struct filter_s *fs;
	struct filter_init_data fid = {
		.in_width = vp->w,
		.in_height = vp->h,
		.in_fourcc = vp->in_fourcc,
		.out_width = vp->drm.overlay_plane.vlib_plane.width,
		.out_height = vp->drm.overlay_plane.vlib_plane.height,
		.out_fourcc = vp->out_fourcc,
	};

	for (size_t i=0; i<ft->size; i++) {
		fs = g_ptr_array_index(ft->filter_types, i);
		if (!fs) {
			return VLIB_ERROR_OTHER;
		}

		/* Initialize filter */
		ret = fs->ops->init(fs, &fid);
		if (ret) {
			vlib_warn("initializing filter '%s' failed\n",
					filter_type_get_display_text(fs));
			filter_type_unregister(ft, fs);
			i--;
			continue;
		}

		/* Prefetch partial binaries of filters */
		if (video_setup->flags & VLIB_CFG_FLAG_PR_ENABLE) {
			filter_type_prefetch_bin(fs);
		}
	}

	return VLIB_SUCCESS;
}

/**
 * vlib_drm_try_mode - Check if a mode with matching resolution is valid
 * @dri_card_id: DRM device ID
 * @width: Desired mode width
 * @height: Desired mode height
 * @vrefresh: Refresh rate of found mode
 *
 * Search for a mode that supports the desired @widthx@height. If a matching
 * mode is found @vrefresh is populated with the refresh rate for that mode.
 *
 * Return: 0 on success, error code otherwise.
 */
int vlib_drm_try_mode(unsigned int dri_card_id, int width, int height,
						size_t *vrefresh)
{
	size_t vr;
	struct drm_device drm_dev;

	snprintf(drm_dev.dri_card, sizeof(drm_dev.dri_card),
			"/dev/dri/card%u", dri_card_id);

	int ret = drm_try_mode(&drm_dev, width, height, &vr);
	if (vrefresh) {
		*vrefresh = vr;
	}

	return ret;
}

static int vlib_drm_init(struct vlib_config_data *cfg)
{
	size_t bpp;
	struct drm_device *drm_dev = &video_setup->drm;

	snprintf(drm_dev->dri_card, sizeof(drm_dev->dri_card),
			"/dev/dri/card%u", cfg->dri_card_id);
	drm_dev->overlay_plane.vlib_plane = cfg->plane;
	drm_dev->format = video_setup->out_fourcc;
	drm_dev->vrefresh = cfg->vrefresh;
	drm_dev->buffer_cnt = cfg->buffer_cnt;

	drm_dev->d_buff = calloc(drm_dev->buffer_cnt, sizeof(*drm_dev->d_buff));
	ASSERT2(drm_dev->d_buff, "failed to allocate DRM buffer structures\n");

	bpp = vlib_fourcc2bpp(drm_dev->format);
	if (!bpp) {
		VLIB_REPORT_ERR("unsupported pixel format '%s'",
						(const char *)&drm_dev->format);
		vlib_dbg("%s\n", vlib_errstr);
		return VLIB_ERROR_INVALID_PARAM;
	}

	drm_init(drm_dev, &cfg->plane);

	//if (!drm_dev->overlay_plane.drm_plane)
	//	drm_dev->overlay_plane.drm_plane = drm_dev->prim_plane.drm_plane; //

	/* Set display resolution */
	if (!cfg->height_out) {
		/* set preferred mode */
		int ret = drm_find_preferred_mode(drm_dev);
		if (ret)
			return ret;

		video_setup->h_out = drm_dev->preferred_mode->vdisplay;
		video_setup->w_out = drm_dev->preferred_mode->hdisplay;

		if (!video_setup->h) {
			video_setup->h = video_setup->h_out;
			video_setup->w = video_setup->w_out;
			video_setup->stride = video_setup->w * bpp;
		}
	} else {
		video_setup->h_out = cfg->height_out;
		video_setup->w_out = cfg->width_out;
	}

	/* if not specified on the command line make the plane fill the whole screen */
	if (!cfg->plane.width) {
		drm_dev->overlay_plane.vlib_plane.width = video_setup->w_out;
		drm_dev->overlay_plane.vlib_plane.height = video_setup->h_out;
	}

	video_setup->stride_out = drm_dev->overlay_plane.vlib_plane.width * bpp;

	drm_post_init(drm_dev, cfg->drm_background);

#if 0
	if (!(cfg->flags & VLIB_CFG_FLAG_MULTI_INSTANCE)) {
		/* Move video layer to the back and disable global alpha */
		if (drm_set_plane_prop(drm_dev,
							video_setup->drm.overlay_plane.drm_plane->plane_id,
								"zpos", 0)) {
			vlib_warn("failed to set zpos\n");
		}

		if (drm_set_plane_prop(drm_dev,
							video_setup->drm.prim_plane.drm_plane->plane_id,
								"zpos", 1)) {
			vlib_warn("failed to set zpos\n");
		}	

		if (drm_set_plane_prop(drm_dev,
							video_setup->drm.prim_plane.drm_plane->plane_id,
							"global alpha enable", 0)) {
			vlib_warn("failed to set 'global alpha'\n");
		}
	}
#endif

	vlib_dbg("vlib :: DRM Init done ..\n");

	return VLIB_SUCCESS;
}

int vlib_init(struct vlib_config_data *cfg)
{
	int ret;
	size_t bpp;

	cfg->buffer_cnt = cfg->buffer_cnt ? cfg->buffer_cnt : BUFFER_CNT_DEFAULT;
	if (cfg->buffer_cnt < BUFFER_CNT_MIN) {
		vlib_warn("buffer-count = %zu too low, using %u\n",
						cfg->buffer_cnt, BUFFER_CNT_MIN);
		cfg->buffer_cnt = BUFFER_CNT_MIN;
	}

	ret = vlib_video_src_init(cfg);
	if (ret)
		return ret;


	/* Allocate video_setup struct and zero out memory */
	video_setup = calloc(1, sizeof(*video_setup));
	video_setup->app_state = MODE_INIT;
	video_setup->in_fourcc = cfg->fmt_in ? cfg->fmt_in : INPUT_PIX_FMT;
	video_setup->out_fourcc = cfg->fmt_out ? cfg->fmt_out : OUTPUT_PIX_FMT;
	video_setup->flags = cfg->flags;
	video_setup->ft = cfg->ft;
	video_setup->buffer_cnt = cfg->buffer_cnt;

	for (size_t i=0; i<NUM_EVENTS; i++) {
		const char *event_name[] = {
			"Capture", "Display", "Filter-In", "Filter-Out",
		};
		video_setup->events[i] = levents_counter_create(event_name[i]);
		ASSERT2(video_setup->events[i], "failed to create event counter\n");
	}


	bpp = vlib_fourcc2bpp(video_setup->in_fourcc);
	if (!bpp) {
		VLIB_REPORT_ERR("unsupported pixel format '%.4s'",
						(const char *)&video_setup->in_fourcc);
		vlib_dbg("%s\n", vlib_errstr);
		return VLIB_ERROR_INVALID_PARAM;
	}

	/* Set input resolution */
	video_setup->h = cfg->height_in;
	video_setup->w = cfg->width_in;
	video_setup->stride = video_setup->w * vlib_fourcc2bpp(video_setup->in_fourcc);
	video_setup->fps.numerator = cfg->fps.numerator;
	video_setup->fps.denominator = cfg->fps.denominator;

	ret = vlib_drm_init(cfg);
	if (ret) {
		return ret;
	}

	/* allocate input buffers */
	video_setup->in_bufs = calloc(video_setup->buffer_cnt,
									sizeof(*video_setup->in_bufs));
	for (size_t i=0; i<video_setup->buffer_cnt; i++) {
		ret = drm_buffer_create(&video_setup->drm,
								video_setup->in_bufs + i,
								video_setup->w, video_setup->h,
								video_setup->stride,
								video_setup->in_fourcc);
								//video_setup->out_fourcc);

		ASSERT2(!ret, "unable to allocate frame buffer: %s",
				strerror(errno));
	}

	/* Initialize filters */
	if (video_setup->ft) {
		ret = vlib_filter_init(video_setup);
		if (ret) {
			return ret;
		}
	}

	return ret;
}

int vlib_get_active_height(void)
{
	return video_setup->h_out;
}

int vlib_get_active_width(void)
{
	return video_setup->w_out;
}

static int vlib_pipeline_term_threads(struct video_pipeline *vp)
{
	int ret = 0;

	/*
	int ret_i = pthread_cancel(video_setup->fps_thread);
	if (ret_i) {
		vlib_warn("failed to cancel fps thread (%d)\n", ret);
		ret |= ret_i;
	}
	*/
	video_setup->fps_thread_quit = 1;
	int ret_i = pthread_join(video_setup->fps_thread, NULL);
	if (ret_i) {
		vlib_warn("failed to join fps thread (%d)\n", ret);
		ret |= ret_i;
	}
	/*
	ret_i = pthread_cancel(video_setup->eventloop);
	if (ret_i) {
		vlib_warn("failed to cancel eventloop(%d)\n", ret);
		ret |= ret_i;
	}
	*/
	video_setup->process_thread_quit = 1;
	ret_i = pthread_join(video_setup->eventloop, NULL);
	if (ret_i) {
		vlib_warn("failed to join eventloop (%d)\n", ret);
		ret |= ret_i;
	}

	video_setup->eventloop = 0;

	return ret;
}

int vlib_pipeline_stop(void)
{
	int ret = 0;

	/* Add cleanup code */
	if (video_setup->eventloop) {
		/* Set application state */
		video_setup->app_state = MODE_EXIT;
		/* Stop previous running mode if any */
		ret |= vlib_pipeline_term_threads(video_setup);
		levents_counter_clear(video_setup->events[DISPLAY]);
	}
	if (!(video_setup->flags & VLIB_CFG_FLAG_MULTI_INSTANCE)) {
		/* Disable video layer on pipeline stop */
		ret |= drm_set_plane_state(&video_setup->drm,
							video_setup->drm.overlay_plane.drm_plane->plane_id,
							0);
	}

	return ret;
}

static int vlib_filter_uninit(struct filter_s *fs)
{
	if (!fs)
		return VLIB_ERROR_OTHER;

	/* free buffers for partial bitstreams */
	if (video_setup->flags & VLIB_CFG_FLAG_PR_ENABLE)
		filter_type_free_bin(fs);

	return VLIB_SUCCESS;
}

int vlib_uninit(void)
{
	struct filter_s *fs;
	int ret;

	/* free input buffers */
	for (size_t i=0; i<video_setup->buffer_cnt; i++) {
		drm_buffer_destroy(video_setup->drm.fd,
							video_setup->in_bufs + i);
	}
	free(video_setup->in_bufs);

	drm_uninit(&video_setup->drm);

	vlib_video_src_uninit();

	/* Uninitialize filters */
	if (video_setup->ft) {
		for (size_t i=0; i<video_setup->ft->size; i++) {
			fs = filter_type_get_obj(video_setup->ft, i);
			ret = vlib_filter_uninit(fs);
			if (ret)
				return ret;
		}
		if (video_setup->ft->size) {
			g_ptr_array_free(video_setup->ft->filter_types, TRUE);
		}
	}

	for (size_t i=0; i<NUM_EVENTS; i++) {
		levents_counter_destroy(video_setup->events[i]);
	}

	free(video_setup);

	return ret;
}

static void drm_event_handler(int fd __attribute__((__unused__)),
		unsigned int frame __attribute__((__unused__)),
		unsigned int sec __attribute__((__unused__)),
		unsigned int usec __attribute__((__unused__)),
		void *data)
{
	struct video_pipeline *v_pipe = (struct video_pipeline *)data;

	ASSERT2(v_pipe, " %s :: argument NULL ", __func__);
	v_pipe->pflip_pending = 0;
	/* Count number of VBLANK events */
	levents_capture_event(v_pipe->events[DISPLAY]);
}

static void *fps_count_thread(void *arg)
{
	struct video_pipeline *s = arg;

	struct pollfd fds[] = {
		{.fd = s->drm.fd, .events = POLLIN},
	};

	/* setup drm event context */
	drmEventContext evctx;
	memset(&evctx, 0, sizeof(evctx));
	evctx.version = DRM_EVENT_CONTEXT_VERSION;
	evctx.vblank_handler = drm_event_handler;

	while (poll(fds, ARRAY_SIZE(fds), POLL_TIMEOUT_MSEC) > 0) {
		if (fds[0].revents & POLLIN) {
			/* Processes outstanding DRM events on the DRM file-descriptor */
			int ret = drmHandleEvent(s->drm.fd, &evctx);
			ASSERT2(!ret, "drmHandleEvent failed: %s\n", ERRSTR);
		}
		if (s->fps_thread_quit == 1) break;
	}

	pthread_exit(NULL);
}

int vlib_change_mode(struct vlib_config *config)
{
	int ret;
	struct filter_s *fs = NULL;
	void *(*process_thread_fptr)(void *);

	/* Print requested config */
	vlib_dbg("config: src=%zu, type=%d, mode=%zu\n", config->vsrc,
			config->type, config->mode);

	if (config->vsrc >= vlib_video_src_cnt_get()) {
		VLIB_REPORT_ERR("invalid video source '%zu'",
						config->vsrc);
		vlib_dbg("%s\n", vlib_errstr);
		return VLIB_ERROR_INVALID_PARAM;
	}

	/* filter is required when output resolution != input resolution */
	if ((video_setup->w != video_setup->drm.overlay_plane.vlib_plane.width ||
		video_setup->h != video_setup->drm.overlay_plane.vlib_plane.height) &&
		!config->type) {
		VLIB_REPORT_ERR("invalid filter 'pass through' for selected input/output resolutions");
		vlib_dbg("%s\n", vlib_errstr);
		return VLIB_ERROR_INVALID_PARAM;
	}

	/* Stop processing loop */
	if (video_setup->eventloop) {
		/* Set application state */
		video_setup->app_state = MODE_CHANGE;

		/* Stop previous running mode if any */
		ret = vlib_pipeline_term_threads(video_setup);
	}

	const struct vlib_vdev *vdev = vlib_video_src_get(config->vsrc);
	if (!vdev) {
		return VLIB_ERROR_INVALID_PARAM;
	}

	/* Set video source */
	video_setup->vid_src = vdev;

	if (vdev->ops && vdev->ops->change_mode) {
		ret = vdev->ops->change_mode(video_setup, config);
		if (ret) {
			return ret;
		}
	}

	/* Initialize filter mode */
	if (config->type > 0) {
		fs = filter_type_get_obj(video_setup->ft, config->type - 1);
		if (!fs) {
			config->mode = 0;
		}
	}

	if (fs && config->mode >= filter_type_get_num_modes(fs)) {
		vlib_warn("invalid filter mode '%zu' for filter '%s'\n",
				config->mode, filter_type_get_display_text(fs));
		config->mode = 0;
	}

	const struct stream_handle *sh;
	switch(config->type) {
		case 0:
			sh = s2m_pipeline_init(video_setup);
			if (!sh) {
				return VLIB_ERROR_CAPTURE;
			}
			process_thread_fptr = s2m_process_event_loop;
			break;
		default:
			filter_type_set_mode(fs, config->mode); /* 0 is pass through */
			sh = m2m_sw_pipeline_init(video_setup, fs);
			if (!sh) {
				return VLIB_ERROR_CAPTURE;
			}
			process_thread_fptr = m2m_sw_process_event_loop;
			break;
	}

	/* start fps counter thread */
	video_setup->fps_thread_quit = 0;
	ret = pthread_create(&video_setup->fps_thread, NULL, fps_count_thread,
						video_setup);
	if (ret) {
		vlib_warn("failed to create FPS count thread\n");
	}

	/* Start the processing loop */
	video_setup->process_thread_quit = 0;
	ret = pthread_create(&video_setup->eventloop, NULL, process_thread_fptr,
						(void *)sh);
	ASSERT2(ret >= 0, "thread creation failed \n");

	return VLIB_SUCCESS;
}

int vlib_drm_set_layer0_state(int enable_state)
{
	/* Map primary-plane coordinates into CRTC using drmModeSetPlane */
	drm_set_plane_state(&video_setup->drm,
						video_setup->drm.prim_plane.drm_plane->plane_id,
						enable_state);
	return VLIB_SUCCESS;
}

#define DRM_MAX_ALPHA 		255
#define DRM_ALPHA_PROP       "transparency"
int vlib_drm_set_layer0_transparency(int transparency)
{
	/* Set Layer Alpha for graphics layer */
	drm_set_plane_prop(&video_setup->drm,
						video_setup->drm.prim_plane.drm_plane->plane_id,
						DRM_ALPHA_PROP, (DRM_MAX_ALPHA-transparency));
	return VLIB_SUCCESS;
}

int vlib_drm_set_layer0_position(int x, int y)
{
	drm_set_prim_plane_pos(&video_setup->drm, x, y);
	return VLIB_SUCCESS;
}

/* Set event-log state */
int vlib_set_event_log(int state)
{
	video_setup->enable_log_event = state;
	return VLIB_SUCCESS;
}

/**
 * vlib_get_event_cnt - Retrieve normalized event counter
 * @event: Event counter to return
 *
 * Return: Normalized event count for @event.
 */
float vlib_get_event_cnt(pipeline_event event)
{
	if (video_setup->enable_log_event && event < NUM_EVENTS) {
		return levents_counter_get_value(video_setup->events[event]);
	}

	return VLIB_ERROR_OTHER;
}

/** This function returns a constant NULL-terminated string with the ASCII name of a vlib
 *  error. The caller must not free() the returned string.
 *
 *  \param error_code The \ref vlib_error to return the name of.
 *  \returns The error name, or the string **UNKNOWN** if the value of
 *  error_code is not a known error.
 */
const char *vlib_error_name(vlib_error error_code)
{
	switch (error_code) {
	case VLIB_ERROR_INTERNAL:
		return "VLIB Internal Error";
	case VLIB_ERROR_CAPTURE:
		return "VLIB Capture Error";
	case VLIB_ERROR_INVALID_PARAM:
		return "VLIB Invalid Parameter Error";
	case VLIB_ERROR_FILE_IO:
		return "VLIB File I/O Error";
	case VLIB_ERROR_NOT_SUPPORTED:
		return "VLIB Not Supported Error";
	case VLIB_ERROR_OTHER:
		return "VLIB Other Error";
	case VLIB_SUCCESS:
		return "VLIB Success";
	default:
		return "VLIB Unknown Error";
	}
}

/** This function returns a string with a short description of the given error code.
 *  This description is intended for displaying to the end user.
 *
 *  The messages always start with a capital letter and end without any dot.
 *  The caller must not free() the returned string.
 *
 *  \returns a short description of the error code in UTF-8 encoding
 */
char *vlib_strerror(void)
{
	return vlib_errstr;
}

/* This function returns a string with a log-information w.r.t to the input log-level */
static void vlib_log_str(vlib_log_level level, const char *str)
{
	fputs(str, stderr);
	UNUSED(level);
}

void vlib_log_v(vlib_log_level level, const char *format, va_list args)
{
	const char *prefix = "";
	char buf[VLIB_LOG_SIZE];
	int header_len, text_len;

	switch (level) {
	case VLIB_LOG_LEVEL_INFO:
		prefix = "[vlib info] ";
		break;
	case VLIB_LOG_LEVEL_WARNING:
		prefix = "[vlib warning] ";
		break;
	case VLIB_LOG_LEVEL_ERROR:
		prefix = "[vlib error] ";
		break;
	case VLIB_LOG_LEVEL_DEBUG:
		prefix = "[vlib debug] ";
		break;
	case VLIB_LOG_LEVEL_NONE:
	default:
		return;
	}

	header_len = snprintf(buf, sizeof(buf), "%s", prefix);
	if (header_len < 0 || header_len >= (int)sizeof(buf)) {
		/* Somehow snprintf failed to write to the buffer,
		 * remove the header so something useful is output. */
		header_len = 0;
	}
	/* Make sure buffer is NULL terminated */
	buf[header_len] = '\0';

	text_len = vsnprintf(buf + header_len, sizeof(buf) - header_len, format, args);
	if (text_len < 0 || text_len + header_len >= (int)sizeof(buf)) {
		/* Truncated log output. On some platforms a -1 return value means
		 * that the output was truncated. */
		text_len = sizeof(buf) - header_len;
	}

	if (header_len + text_len >= sizeof(buf)) {
		/* Need to truncate the text slightly to fit on the terminator. */
		text_len -= (header_len + text_len) - sizeof(buf);
	}

	vlib_log_str(level, buf);
}

void vlib_log(vlib_log_level level, const char *format, ...)
{
	va_list args;

	va_start (args, format);
	vlib_log_v(level, format, args);
	va_end (args);
}

int vlib_pipeline_v4l2_init(struct stream_handle *sh, struct video_pipeline *s)
{
	/* Initialize v4l2 video input device */
	sh->video_in.fd = vlib_video_src_get_vnode(s->vid_src);
	sh->video_in.format.pixelformat = s->in_fourcc;
	sh->video_in.format.width = s->w;
	sh->video_in.format.height = s->h;
	sh->video_in.format.bytesperline = s->stride;
	sh->video_in.format.colorspace = V4L2_COLORSPACE_SRGB;
	sh->video_in.mem_type = V4L2_MEMORY_DMABUF;
	sh->video_in.setup_ptr = s;
	sh->video_in.vdev = s->vid_src;
	return v4l2_init(&sh->video_in, s->buffer_cnt);
}

/**
 * vlib_fourcc2bpp - Get bytes per pixel
 * @fourcc: Fourcc pixel format code
 *
 * Return: Number of bytes per pixel for @fourcc or 0.
 */
size_t vlib_fourcc2bpp(uint32_t fourcc)
{
	size_t bpp;

	/* look up bits per pixel */
	switch (fourcc) {
		case V4L2_PIX_FMT_RGB332:
		case V4L2_PIX_FMT_HI240:
		case V4L2_PIX_FMT_HM12:
		case DRM_FORMAT_RGB332:
		case DRM_FORMAT_BGR233:
			bpp = 8;
			break;
		case V4L2_PIX_FMT_YVU410:
		case V4L2_PIX_FMT_YUV410:
			bpp = 9;
			break;
		case V4L2_PIX_FMT_YVU420:
		case V4L2_PIX_FMT_YUV420:
		case V4L2_PIX_FMT_M420:
		case V4L2_PIX_FMT_Y41P:
			bpp = 12;
			break;
		case V4L2_PIX_FMT_RGB444:
		case V4L2_PIX_FMT_ARGB444:
		case V4L2_PIX_FMT_XRGB444:
		case V4L2_PIX_FMT_RGB555:
		case V4L2_PIX_FMT_ARGB555:
		case V4L2_PIX_FMT_XRGB555:
		case V4L2_PIX_FMT_RGB565:
		case V4L2_PIX_FMT_RGB555X:
		case V4L2_PIX_FMT_ARGB555X:
		case V4L2_PIX_FMT_XRGB555X:
		case V4L2_PIX_FMT_RGB565X:
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_YYUV:
		case V4L2_PIX_FMT_YVYU:
		case V4L2_PIX_FMT_UYVY:
		case V4L2_PIX_FMT_VYUY:
		case V4L2_PIX_FMT_YUV422P:
		case V4L2_PIX_FMT_YUV411P:
		case V4L2_PIX_FMT_YUV444:
		case V4L2_PIX_FMT_YUV555:
		case V4L2_PIX_FMT_YUV565:
		case DRM_FORMAT_XBGR4444:
		case DRM_FORMAT_RGBX4444:
		case DRM_FORMAT_BGRX4444:
		case DRM_FORMAT_ABGR4444:
		case DRM_FORMAT_RGBA4444:
		case DRM_FORMAT_BGRA4444:
		case DRM_FORMAT_XBGR1555:
		case DRM_FORMAT_RGBX5551:
		case DRM_FORMAT_BGRX5551:
		case DRM_FORMAT_ABGR1555:
		case DRM_FORMAT_RGBA5551:
		case DRM_FORMAT_BGRA5551:
		case DRM_FORMAT_RGB565:
		case DRM_FORMAT_BGR565:
			bpp = 16;
			break;
		case V4L2_PIX_FMT_BGR666:
			bpp = 18;
			break;
		case V4L2_PIX_FMT_BGR24:
		case V4L2_PIX_FMT_RGB24:
		case DRM_FORMAT_RGB888:
		case DRM_FORMAT_BGR888:
			bpp = 24;
			break;
		case V4L2_PIX_FMT_BGR32:
		case V4L2_PIX_FMT_ABGR32:
		case V4L2_PIX_FMT_XBGR32:
		case V4L2_PIX_FMT_RGB32:
		case V4L2_PIX_FMT_ARGB32:
		case V4L2_PIX_FMT_XRGB32:
		case V4L2_PIX_FMT_YUV32:
		case DRM_FORMAT_XBGR8888:
		case DRM_FORMAT_RGBX8888:
		case DRM_FORMAT_ABGR8888:
		case DRM_FORMAT_RGBA8888:
		case DRM_FORMAT_XRGB2101010:
		case DRM_FORMAT_XBGR2101010:
		case DRM_FORMAT_RGBX1010102:
		case DRM_FORMAT_BGRX1010102:
		case DRM_FORMAT_ARGB2101010:
		case DRM_FORMAT_ABGR2101010:
		case DRM_FORMAT_RGBA1010102:
		case DRM_FORMAT_BGRA1010102:
			bpp = 32;
			break;
		default:
			return 0;
	}

	/* return bytes required to hold one pixel */
	return (bpp + 7) >> 3;
}
