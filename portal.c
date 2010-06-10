#include <string.h>
#include <malloc.h>
#include <assert.h>
#include "event.h"
#include "portal.h"
#include "windows.h"

#ifdef _MSC_VER
	#include <intrin.h>
#endif

/*
 * A function that ensures compiler and hardware read/write order, also known as a memory barrier.
 */
void memory_barrier()
{
#ifdef _MSC_VER
	_ReadWriteBarrier();
#else
	__sync_synchronize();
#endif
}

/*
 * Portal is an implementation of a pipe where two threads can safely write and read from. It does not support multiple writers or readers.
 *  TODO: Implement a silding window?
 */

#define PORTAL_BUFFER_COUNT 1
#define PORTAL_BUFFER_MASK (PORTAL_BUFFER_COUNT - 1)

/*
 * Part is a structure belonging to one of the threads.
 */
struct part {
	/*
	 * The number of reads this part has done.
	 */
	size_t read_count;

	/*
	 * The number of writes the other part has done.
	 */
	size_t write_count;

	/*
	 * An event which if preset will be used to signal new messages.
	 */
	event_t event;

	/*
	 * An event which if preset will be used to signal a reply to a synchronious message.
	 */
	event_t msg_event;

	/*
	 * The id of the send synchronious message.
	 */
	size_t msg_id;

	/*
	 * A buffer containg messages to be read.
	 *  TODO: This should be moved out of here!
	 */
	message_t *buffer;
};

struct portal {
	/*
	 * The part belonging to the other thread.
	 */
	struct part *remote;

	/*
	 * The part belonging to the local thread.
	 */
	struct part *local;

	/*
	 * Event used for synchronizing.
	 */
	event_t event;
};

/*
 * part_alloc()
 *  Allocate a new part.
 */
struct part *part_alloc()
{
    struct part *part = malloc(sizeof(struct part));

    memset(part, 0, sizeof(struct part));

    part->buffer = malloc(sizeof(message_t) * PORTAL_BUFFER_COUNT);

	return part;
}

/*
 * part_free()
 *  Destroy an existing part.
 */
void part_free(struct part *part)
{
    free(part->buffer);
    free(part);
}

/*
 * part_write()
 *  Write a message to a part.
 */
bool part_write(volatile struct part *part, message_t *msg, int *test)
{
	size_t read_count = part->read_count;
	size_t write_count = part->write_count;
	size_t used_count = write_count - read_count;

	if(used_count >= PORTAL_BUFFER_COUNT)
	{
		/*
		 * Check if there is a pending waiting event, if so, trigger it.
		 */
		event_t event = part->event;

		if(event)
			event_set(event);

		*test = *test + 1;

		while(write_count - read_count >= PORTAL_BUFFER_COUNT)
		{
			SwitchToThread();

			read_count = part->read_count;
		}
		//assert(0);

		//return false;
	}

	part->buffer[write_count & PORTAL_BUFFER_MASK] = *msg;

	memory_barrier();

	part->write_count++;

	memory_barrier();

	return true;
}

/*
 * part_read()
 *  Read a message from a part.
 */
void part_read(struct part *part, size_t count)
{
	memory_barrier();

	part->read_count += count;
}

/*
 * part_pending()
 *  Check if there is pending messages in the part.
 */
bool part_pending_msgs(struct part *part)
{
	return part->read_count != part->write_count;
}

/*
 * portal_alloc()
 *  Allocate a new portal for each part.
 */
void portal_alloc(struct portal *portals[])
{
    struct part *remote = part_alloc();
    struct part *local = part_alloc();

    portals[0] = malloc(sizeof(struct portal));
    portals[0]->remote = remote;
    portals[0]->local = local;
    portals[0]->event = event_alloc();

    portals[1] = malloc(sizeof(struct portal));
    portals[1]->remote = local;
    portals[1]->local = remote;
    portals[1]->event = event_alloc();
}

/*
 * portal_alloc()
 *  Free the portals for each part.
 */
void portal_free(struct portal *portals[])
{
    part_free(portals[0]->remote);
    part_free(portals[0]->local);

    event_free(portals[0]->event);
    event_free(portals[1]->event);

    free(portals[0]);
    free(portals[1]);
}

void portal_write(struct portal *portal, message_t *msg, int *test)
{
	part_write(portal->remote, msg, test);
}

void portal_write_and_notify(struct portal *portal, message_t *msg)
{
	event_t event;
	int a;
	part_write(portal->remote, msg, &a);

	/*
	 * Check if there is a pending waiting event, if so, trigger it.
	 */
	event = portal->remote->event;

	if(event)
		event_set(event);
}

void portal_notify(struct portal *portal)
{
	/*
	 * Check if there is a pending waiting event and waiting messages, if so, trigger it.
	 */
	event_t event = portal->remote->event;

	if(event && part_pending_msgs(portal->remote))
		event_set(event);
}

void portal_wait(struct portal *portal)
{
	/*
	 * Notify the other part if needed before waiting.
	 */
	portal_notify(portal);

	/*
	 * Setup the waiting event.
	 */
	event_clear(portal->event);
	portal->local->event = portal->event;

	/*
	 * Make sure there are no new messages, then wait.
	 */
	if(!part_pending_msgs(portal->local))
		event_wait(portal->event);

	/*
	 * Clear the waiting event.
	 */
	portal->local->event = 0;
}

size_t portal_pending(struct portal *portal, message_t **msgs)
{
	size_t read_count = portal->local->read_count;
	size_t write_count = portal->local->write_count;

	size_t offset = read_count & PORTAL_BUFFER_MASK;
	size_t max_read = min(write_count - read_count, PORTAL_BUFFER_COUNT - offset);

	*msgs = &portal->local->buffer[offset];

	return max_read;
}

void portal_read(struct portal *portal, size_t count)
{
	/*
	 * Store the message and return.
	 */
	part_read(portal->local, count);
}

void portal_sync_query(struct portal *portal, message_t *msg, size_t msg_id)
{
	/*
	 * Setup the message waiting event.
	 */
	portal->local->msg_id = 0;

	memory_barrier();

	event_clear(portal->event);
	portal->local->msg_event = portal->event;

	/*
	 * Send the message and wait for the reply.
	 */
	portal_write_and_notify(portal, msg);
	event_wait(portal->event);

	/*
	 * Clear the message waiting event.
	 */
	portal->local->msg_event = 0;
}

void portal_sync_reply(struct portal *portal, message_t *msg, size_t msg_id)
{
	/*
	 * Check if the other side is waiting for this message.
	 */
	event_t msg_event = portal->remote->msg_event;
	size_t remote_msg_id;

	memory_barrier();

	remote_msg_id = portal->remote->msg_id;

	if(msg_event && remote_msg_id == msg_id)
	{
		/*
		 * Send the message and trigger the event.
		 */int a;
		part_write(portal->remote, msg, &a);
		event_set(msg_event);
	}
	else
	{
		/*
		 * The other side is not waiting for this message, send it using the regular notify method.
		 */
		portal_write_and_notify(portal, msg);
	}
}