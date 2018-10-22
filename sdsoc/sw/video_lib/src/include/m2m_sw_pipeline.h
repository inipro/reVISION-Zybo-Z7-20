#ifndef M2M_SW_PIPELINE_H
#define M2M_SW_PIPELINE_H

#include "v4l2_helper.h"

struct filter_s;
struct stream_handle;

/* Initialize memory to memory sw pipeline */
const struct stream_handle *m2m_sw_pipeline_init(struct video_pipeline *s,
												struct filter_s *fs);

/* Handles m2m sw pipeline event loop */
void *m2m_sw_process_event_loop(void *ptr);

#endif /* M2M_SW_PIPELINE_H */
