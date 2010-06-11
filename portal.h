#pragma once

#ifdef __cplusplus
extern "C" {
#else
	#define false 0
	#define true 1
	typedef unsigned char bool;
#endif

struct portal;

struct message
{
	size_t data[4];
};

/*
 * Dummy typedefs for messages.
 */
typedef struct message message_t;

/*
 * portal_alloc()
 *  Allocate a new portal for each part.
 */
void portal_alloc(struct portal *portals[]);

/*
 * portal_alloc()
 *  Free the portals for each part.
 */
void portal_free(struct portal *portals[]);

void portal_write(struct portal *portal, message_t *msg, int *test);
void portal_write_and_notify(struct portal *portal, message_t *msg);
void portal_notify(struct portal *portal);
void portal_read_msg(message_t *queue, message_t *target);
void portal_wait(struct portal *portal);
size_t portal_pending(struct portal *portal, message_t **msgs);
void portal_read(struct portal *portal, size_t count);
void portal_sync_query(struct portal *portal, message_t *msg, size_t msg_id);
void portal_sync_reply(struct portal *portal, message_t *msg, size_t msg_id);

#ifdef __cplusplus
}
#endif