#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct event;

typedef struct event *event_t;

event_t event_alloc();
void event_free(event_t event);
void event_clear(event_t event);
void event_set(event_t event);
void event_wait(event_t event);

#ifdef __cplusplus
}
#endif