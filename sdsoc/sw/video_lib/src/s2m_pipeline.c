#include <glib.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "helper.h"
#include "log_events.h"
#include "mediactl_helper.h"
#include "s2m_pipeline.h"
#include "video.h"

#define S2M_PIPELINE_DELAY_SRC2SINK			1
#define S2M_PIPELINE_DELAY_SINK2SRC			1

const struct stream_handle *s2m_pipeline_init(struct video_pipeline *s)
{
	int ret;
	const struct vlib_vdev *vdev = s->vid_src;
	ASSERT2(vdev, "invalid video source\n");

	struct stream_handle *sh = calloc(1, sizeof(*sh));
	if (!sh) {
		return NULL;
	}

	sh->vp = s;

	/* Configure media pipeline */
	if (vdev->ops && vdev->ops->set_media_ctrl) {
		ret = vdev->ops->set_media_ctrl(s, vdev);
		ASSERT2(!ret, "failed to configure media pipeline\n");
	}

	if (vdev->ops && vdev->ops->prepare) {
		ret = vdev->ops->prepare(s, vdev);
	}

	if (video_src_is_v4l2(vdev)) {
		/* Set v4l2 device name */
		ret = vlib_pipeline_v4l2_init(sh, s);
		if (ret) {
			vlib_err("init v4l2 pipeline failed\n");
			free(sh);
			return NULL;
		}
	}

	return sh;
}

static void s2m_v4l2_cleanup(void *arg)
{
	struct stream_handle *sh = arg;

	g_queue_free(sh->buffer_q_src2filter);
	g_queue_free(sh->buffer_q_sink2src);
}

static void s2m_v4l2_process_loop(struct stream_handle *sh)
{
	int ret;
	struct video_pipeline *v_pipe = sh->vp;
	/* TOTO: Remove once image sensor subdevice driver becomes available */
	const struct vlib_vdev *vdev = v_pipe->vid_src;
	ASSERT2(vdev, "invalid video source\n");

	/* Assigning buffer index and set exported buff handle */
	for (size_t i=0; i<v_pipe->buffer_cnt; i++) {
		sh->video_in.vid_buf[i].index = i;
		/* Assign DRM buffer buff-sharing handle */
		sh->video_in.vid_buf[i].dbuf_fd = v_pipe->drm.d_buff[i].dbuf_fd;
		/* Queue buffers to video device using DMA buff sharing */
		v4l2_queue_buffer(&sh->video_in, &sh->video_in.vid_buf[i]);
	}

	/* Start streaming */
	ret = v4l2_device_on(&sh->video_in);
	ASSERT2(ret >= 0, "v4l2_device_on [video_in] failed: %s\n",
			strerror(errno));
	vlib_dbg("vlib :: Video Capture Pipeline started\n");

	struct pollfd fds[] = {
		{.fd = sh->video_in.fd, .events = POLLIN},
	};

	/*
	 * NOTE: VDMA doesn't issue EOR interrupt, as a result even on the
	 * first frame done interrupt, it is still updating it. The current
	 * solution is to skip the first frame done notification
	 */

	/* wait for poll events and pass buffers */
	sh->buffer_q_src2filter = g_queue_new();
	ASSERT2(sh->buffer_q_src2filter, "unable to create buffer queue\n");
	sh->buffer_q_sink2src = g_queue_new();
	ASSERT2(sh->buffer_q_sink2src, "unable to create buffer queue\n");

	while (poll(fds, ARRAY_SIZE(fds), POLL_TIMEOUT_MSEC) > 0) {
		if (fds[0].revents & POLLIN) {
			levents_capture_event(v_pipe->events[CAPTURE]);

			struct buffer *b = v4l2_dequeue_buffer(&sh->video_in,
													sh->video_in.vid_buf);
			g_queue_push_head(sh->buffer_q_src2filter, b);
			if (g_queue_get_length(sh->buffer_q_src2filter) < 
					(S2M_PIPELINE_DELAY_SRC2SINK + 1)) {
				continue;
			}

			b = g_queue_pop_tail(sh->buffer_q_src2filter);
			ret = drm_set_plane(&v_pipe->drm, b->index);
			if (ret < 0) {
				/* If the flip failed, requeue the buffer on the V4L2 side
				 * immediately.
				 */
				v4l2_queue_buffer(&sh->video_in, b);
			} else {
				/*
				 * If the flip succeeded, the previous buffer
				 * is now released, Requeue it on the V4L2 side,
				 * and store the index of the new buffer.
				 */
				if (!v_pipe->pflip_pending) {
					v_pipe->pflip_pending = 1;
					drm_wait_vblank(&v_pipe->drm, v_pipe);
				}

				g_queue_push_head(sh->buffer_q_sink2src, b);
				if (g_queue_get_length(sh->buffer_q_sink2src) >
						S2M_PIPELINE_DELAY_SINK2SRC + 1) {
					v4l2_queue_buffer(&sh->video_in,
							g_queue_pop_tail(sh->buffer_q_sink2src));
				}
			}
		}
		if (v_pipe->process_thread_quit == 1) { 
			s2m_v4l2_cleanup(sh);
			break;
		}
	}
}

static void s2m_file_process_loop(struct video_pipeline *v_pipe,
								const struct vlib_vdev *vdev)
{
	int ret;
	size_t cur_drm_buf = 0;
	size_t frame_sz, bpp = vlib_fourcc2bpp(v_pipe->in_fourcc);
	struct timespec sleep_time = {
		.tv_sec = 0,
		.tv_nsec = 1000000000 * v_pipe->fps.denominator / v_pipe->fps.numerator,
	};

	ASSERT2(bpp, "invalid pixel format '%.4s'\n", (const char *)&v_pipe->in_fourcc);

	frame_sz = v_pipe->h * v_pipe->w * bpp;

	while (1) {
		uint8_t *in_buf = vdev->data.file.get_frame(vdev, v_pipe);
		ASSERT2(in_buf, "no input data\n");
		memcpy(v_pipe->drm.d_buff[cur_drm_buf].drm_buff, in_buf,
				frame_sz);

		ret = drm_set_plane(&v_pipe->drm, cur_drm_buf);
		if (ret) {
			vlib_warn("buffer flip failed\n");
		}

		cur_drm_buf ^= 1;
		if (v_pipe->fps.denominator && v_pipe->fps.numerator) {
			while (nanosleep(&sleep_time, &sleep_time))
				;
		} else {
			pause();
		}
		if (v_pipe->process_thread_quit == 1) break;
	}
}

/* Un-init s2m pipeline ->stop the video stream and close video device */
static void s2m_pipeline_uninit(void *ptr)
{
	int ret;
	struct stream_handle *sh = ptr;
	struct video_pipeline *v_pipe = sh->vp;
	const struct vlib_vdev *vdev = v_pipe->vid_src;
	ASSERT2(vdev, "invalid video source\n");

	/* Set display to last buffer index */
	ret = drm_set_plane(&v_pipe->drm, v_pipe->buffer_cnt - 1);

	if (video_src_is_v4l2(vdev)) {
		ret = v4l2_device_off(&sh->video_in);
		ASSERT2(ret >= 0, "video_in :: stream-off failed\n");

		v4l2_uninit(&sh->video_in);
	}

	if (vdev->ops && vdev->ops->unprepare) {
		ret = vdev->ops->unprepare(v_pipe, vdev);
	}

	if (v_pipe->enable_log_event) {
		//levents_counter_stop(v_pipe->events[DISPLAY]);
		levents_counter_stop(v_pipe->events[CAPTURE]);
	}

	free(sh);
}

/*
 * Process s2m pipeline input/output events.
 * TPG/External --> DRM
 * Its uses DMABUF framework for sharing buffers between multiple devices.
 * DRM allocate framebuffer memory, its exported to get file handle
 * which is then imported by video input device to put video frames directly
 * into display memory
 */
void *s2m_process_event_loop(void *ptr)
{
	struct stream_handle *sh = ptr;
	struct video_pipeline *v_pipe = sh->vp;
	const struct vlib_vdev *vdev = v_pipe->vid_src;
	ASSERT2(vdev, "invalid video source\n");

	if (v_pipe->enable_log_event) {
		//levents_counter_start(v_pipe->events[DISPLAY]);
		levents_counter_start(v_pipe->events[CAPTURE]);
	}

	if (video_src_is_v4l2(vdev)) {
		s2m_v4l2_process_loop(sh);
	} else if (video_src_is_file(vdev)) {
		s2m_file_process_loop(v_pipe, vdev);
	}

	s2m_pipeline_uninit(sh);

	pthread_exit(NULL);
}
