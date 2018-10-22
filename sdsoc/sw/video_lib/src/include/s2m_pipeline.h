#ifndef S2M_PIPELINE_H
#define S2M_PIPELINE_H

#include "v4l2_helper.h"

struct stream_handle;
struct video_pipeline;

/* Initialize stream to memory video input pipeline */
const struct stream_handle *s2m_pipeline_init(struct video_pipeline *s);
/* Handles processing for s2m pipeline */
void *s2m_process_event_loop(void *ptr);

#endif /* S2M_PIPELINE_H */

