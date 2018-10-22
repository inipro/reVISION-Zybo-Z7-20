#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>

#include "common.h"
#include "log_events.h"
#include "video_int.h"

#define SAMPLE_WINDOW	5

struct levents_counter {
	size_t sampled_val[SAMPLE_WINDOW];
	size_t counter_val;
	size_t cur_sample;
	char *nm;
	uint8_t flags;
	uint8_t valid_samples;
};
#define LEVENTS_COUNTER_FLAG_ACTIVE		BIT(0)

static GSList *levents_active_counter;
static pthread_t levents_thread;
static int levents_counter_thread_quit;

static int levents_counter_is_active(struct levents_counter *counter)
{
	return !!(counter->flags & LEVENTS_COUNTER_FLAG_ACTIVE);
}

struct levents_counter *levents_counter_create(const char *name)
{
	struct levents_counter *counter;

	counter = calloc(1, sizeof(*counter));
	if (!counter) {
		return NULL;
	}

	ASSERT2(SAMPLE_WINDOW <= sizeof(counter->valid_samples) * 8, "invalid sample window\n");

	counter->nm = strdup(name);
	if (!counter->nm) {
		vlib_warn("%s\n", strerror(errno));
	}

	return counter;
}

void levents_counter_destroy(struct levents_counter *counter)
{
	ASSERT2(counter, "invalid counter\n");

	if (levents_counter_is_active(counter)) {
		levents_counter_stop(counter);
	}

	free(counter->nm);
	free(counter);
}

static void levents_counter_sample(struct levents_counter *counter)
{
	ASSERT2(counter, "invalid counter\n");

	counter->sampled_val[counter->cur_sample] = counter->counter_val;
	counter->cur_sample++;
	counter->cur_sample %= SAMPLE_WINDOW;
	counter->counter_val = 0;
	counter->valid_samples <<= 1;
	counter->valid_samples |= 1;
}

static void *levents_event_thread(void *ptr)
{
	while (1) {
		//vlib_dbg("-----------\n");
		vlib_log(VLIB_LOG_LEVEL_INFO, "-----------\n");
		for (GSList *e = levents_active_counter; e; e = g_slist_next(e)) {
			struct levents_counter *c = e->data;

			levents_counter_sample(c);

			//vlib_dbg("%s :: %.2f \n", levents_counter_get_name(c),
			vlib_log(VLIB_LOG_LEVEL_INFO, "%s :: %.2f \n", levents_counter_get_name(c),
					levents_counter_get_value(c));
		}

		sleep(1);
		if (levents_counter_thread_quit == 1) break;
	}

	return NULL;
}

void levents_counter_start(struct levents_counter *counter)
{
	ASSERT2(counter, "invalid counter\n");

	/* reset counter values */
	levents_counter_clear(counter);

	if (!levents_active_counter) {
		/* start event thread */
		levents_counter_thread_quit = 0;
		int ret = pthread_create(&levents_thread, NULL,
						levents_event_thread, NULL);
		ASSERT2(ret >= 0, "failed to create event thread\n");
	}

	levents_active_counter = g_slist_prepend(levents_active_counter, counter);

	counter->flags |= LEVENTS_COUNTER_FLAG_ACTIVE;
}

void levents_counter_stop(struct levents_counter *counter)
{
	ASSERT2(counter, "invalid counter\n");

	levents_active_counter = g_slist_remove(levents_active_counter, counter);
	counter->flags &= ~LEVENTS_COUNTER_FLAG_ACTIVE;

	if (!levents_active_counter) {
		levents_counter_thread_quit = 1;
		int ret = pthread_join(levents_thread, NULL);
		ASSERT2(ret >= 0, "failed to terminate event thread\n");
	}
}

void levents_capture_event(struct levents_counter *counter)
{
	ASSERT2(counter, "invalid counter\n");

	counter->counter_val++;
}

float levents_counter_get_value(struct levents_counter *counter)
{
	ASSERT2(counter, "invalid counter\n");

	size_t i, ret = 0;

	for (i=0; i<SAMPLE_WINDOW; i++) {
		if (!(counter->valid_samples & (1 << i))) {
			break;
		}
		ret += counter->sampled_val[i];
	}

	return (float)ret / i;
} 

const char *levents_counter_get_name(struct levents_counter *counter)
{
	ASSERT2(counter, "invalid counter\n");

	return counter->nm;
}

void levents_counter_clear(struct levents_counter *counter)
{
	ASSERT2(counter, "invalid counter\n");

	counter->cur_sample = 0;
	counter->counter_val = 0;
	counter->valid_samples = 0;
	for (size_t i=0; i<SAMPLE_WINDOW; i++) {
		counter->sampled_val[i] = 0;
	}
}
