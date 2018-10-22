#ifndef LOG_EVENTS_H_
#define LOG_EVENTS_H_

struct levents_counter *levents_counter_create(const char *name);
void levents_counter_destroy(struct levents_counter *counter);
void levents_counter_start(struct levents_counter *counter);
void levents_counter_stop(struct levents_counter *counter);
void levents_capture_event(struct levents_counter *counter);
float levents_counter_get_value(struct levents_counter *counter);
const char *levents_counter_get_name(struct levents_counter *counter);
void levents_counter_clear(struct levents_counter *counter);

#endif /* LOG_EVENTS_H_ */


