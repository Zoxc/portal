#include <string.h>
#include <malloc.h>
#include <assert.h>

#ifdef WIN32
    #include <windows.h>

    #ifdef _MSC_VER
        #include <intrin.h>
    #endif
#else
    #include <sched.h>
    #define min(a, b) (a > b ? b : a)
#endif

#ifdef __SSE2__
	#include <emmintrin.h>
#endif

#ifdef __ARM_NEON__
	#include <arm_neon.h>
#endif

#include "event.h"
#include "portal.h"

/*
 * A function that ensures that both read and write operations are complete.
 */
void memory_fence()
{
	#ifndef PORTAL_UNIPROCESSOR
		#if defined(_M_IX86) || defined(_X86_)
			#ifdef _MSC_VER
				_ReadWriteBarrier();
				__asm {
					mfence;
				}
			#else
				__asm("mfence");
			#endif
		#endif

		#ifdef ARMv7
			__asm("dmb");
		#endif
	#endif

	#ifdef _MSC_VER
			_ReadWriteBarrier();
	#endif

	#ifdef __GNUC__
        __sync_synchronize();
	#endif
}

/*
 * A function that ensures that all read operations are complete.
 */
void read_fence()
{
	#ifndef PORTAL_UNIPROCESSOR
		#if defined(_M_IX86) || defined(_X86_)
			#ifdef _MSC_VER
				__asm
				{
					lfence;
				}
			#else
				__asm("lfence");
			#endif
		#endif

		#ifdef ARMv7
			__asm("dmb");
		#endif
	#endif

	#ifdef _MSC_VER
        _ReadBarrier();
	#endif

	#ifdef __GNUC__
        __sync_synchronize();
	#endif
}

/*
 * A function that ensures that all write operations are complete.
 */
void write_fence()
{
	#ifndef PORTAL_UNIPROCESSOR
		#if defined(_M_IX86) || defined(_X86_)
			#ifdef _MSC_VER
				__asm
				{
					sfence;
				}
			#else
				__asm("sfence");
			#endif
		#endif

		#ifdef ARMv7
			__asm("dmb st");
		#endif
	#endif

	#ifdef _MSC_VER
        _WriteBarrier();
	#endif

	#ifdef __GNUC__
        __sync_synchronize();
	#endif
}

/*
 * Portal is an implementation of a pipe where two threads can safely write and read from. It does not support multiple writers or readers.
 *  TODO: Implement a silding window?
 */

#define PORTAL_BUFFER_COUNT 512
#define PORTAL_BUFFER_MASK (PORTAL_BUFFER_COUNT - 1)

/*
 * struct part is a shared structure belonging to one of the threads.
 */
struct part {
	/*
	 * The number of reads this part has done.
	 */
	volatile size_t read_count;

	/*
	 * The number of writes the other part has done.
	 */
	volatile size_t write_count;

	/*
	 * An event which if preset will be used to signal new messages.
	 */
	volatile event_t event;

	/*
	 * An event which if preset will be used to signal a reply to a synchronious message.
	 */
	volatile event_t msg_event;

	/*
	 * The id of the send synchronious message.
	 */
	volatile size_t msg_id;

	/*
	 * A buffer containg messages to be read.
	 *  TODO: This should be moved out of here!
	 */
	message_t *buffer;
};

/*
 * struct portal is a local structure only one of the parts should have access to. Each part will have one of these.
 */
struct portal {
	/*
	 * The part belonging to the other thread.
	 */
	struct part *remote;

	/*
	 * The part belonging to the local thread.
	 */
	struct part *local;
	
	#ifndef PORTAL_UNIPROCESSOR
		/*
		 * A pending updated write count which the other part don't know about yet.
		 *  This is only used to prevent a memory barrier. No need for that on a uniprocessor system.
		 */
		size_t pending_write_count;
	#endif

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
	struct part *part = (struct part *)malloc(sizeof(struct part));

    memset(part, 0, sizeof(struct part));

	#if defined(__SSE2__) || defined(__ARM_NEON__)
		part->buffer = (message_t *)_mm_malloc(sizeof(message_t) * PORTAL_BUFFER_COUNT, 16);
	#else
		part->buffer = (message_t *)malloc(sizeof(message_t) * PORTAL_BUFFER_COUNT);
	#endif

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
 * part_pending()
 *  Check if there is pending messages in the part.
 */
bool part_pending_msgs(struct part *part)
{
	return part->read_count != part->write_count;
}

/*
 * portal_write()
 *  Write a message to a portal.
 */
void portal_write(struct portal *portal, message_t *msg, int *test)
{
	struct part *part = portal->remote;

	#ifdef PORTAL_UNIPROCESSOR
		size_t write_count = part->write_count;
	#else
		size_t write_count = portal->pending_write_count;
	#endif
	
	size_t read_count = part->read_count;
	size_t used_count = write_count - read_count;

	if(used_count >= PORTAL_BUFFER_COUNT)
	{
		event_t event;

		read_fence();

		/*
		 * Check if there is a pending waiting event, if so, trigger it.
		 */
		event = part->event;

		if(event)
		{
			event_set(event);
		}

		(*test)++;

		while(write_count - read_count >= PORTAL_BUFFER_COUNT)
		{
		    #ifdef WIN32
                SwitchToThread();
            #else
                sched_yield();
            #endif

			read_count = part->read_count;
		}
	}
	
	#ifdef  __SSE2__
		__m128i reg;
		
		reg = _mm_loadu_si128((__m128i *)msg);
		
		_mm_store_si128((__m128i *)&part->buffer[write_count & PORTAL_BUFFER_MASK], reg);
	#elif __ARM_NEON__
		uint32x4_t reg;

		reg = vld1q_u32((uint32_t *)msg, reg);

		vst1q_u32((uint32_t *)&part->buffer[write_count & PORTAL_BUFFER_MASK]);
	#else	
		part->buffer[write_count & PORTAL_BUFFER_MASK] = *msg;
	#endif
	
	#ifdef PORTAL_UNIPROCESSOR
		part->write_count++;
	#else
		portal->pending_write_count = write_count + 1;
	#endif
}

void portal_read_msg(message_t *queue, message_t *target)
{
	#ifdef  __SSE2__
		__m128i reg;
		
		reg = _mm_load_si128((__m128i *)queue);
		
		_mm_storeu_si128((__m128i *)target, reg);
	#elif __ARM_NEON__
		uint32x4_t reg;

		reg = vld1q_u32((uint32_t *)queue);

		vst1q_u32((uint32_t *)target, reg);
	#else	
		*target = *queue;
	#endif
}

/*
 * portal_alloc()
 *  Allocate a new portal for each part.
 */
void portal_alloc(struct portal *portals[])
{
    struct part *remote = part_alloc();
    struct part *local = part_alloc();

    portals[0] = (struct portal *)malloc(sizeof(struct portal));
    portals[0]->remote = remote;
    portals[0]->local = local;
    portals[0]->event = event_alloc();
	

    portals[1] = (struct portal *)malloc(sizeof(struct portal));
    portals[1]->remote = local;
    portals[1]->local = remote;
    portals[1]->event = event_alloc();
	
	#ifndef PORTAL_UNIPROCESSOR
		portals[1]->pending_write_count = 0;
		portals[0]->pending_write_count = 0;
	#endif
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

void portal_write_and_notify(struct portal *portal, message_t *msg)
{
	event_t event;
	int dummy;
	portal_write(portal, msg, &dummy);

	/*
	 * Check if there is a pending waiting event, if so, trigger it.
	 */
	event = portal->remote->event;

	if(event)
	{
		write_fence();
		event_set(event);
	}
}

void portal_flush(struct portal *portal)
{
	#ifndef PORTAL_UNIPROCESSOR
		/*
		 * Flush the write count if we have any pending writes.
		 */
		if(portal->pending_write_count != portal->remote->write_count)
		{
			write_fence();
			portal->remote->write_count = portal->pending_write_count;
		}
	#endif
}

void portal_notify(struct portal *portal)
{
	portal_flush(portal);

	/*
	 * Check if there is a pending waiting event and waiting messages, if so, trigger it.
	 */
	event_t event = portal->remote->event;

	if(event && part_pending_msgs(portal->remote))
	{
		write_fence();
		event_set(event);
	}
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

	memory_fence();

	/*
	 * Make sure there are no new messages, then wait.
	 */
	if(!part_pending_msgs(portal->local))
	{
		event_wait(portal->event);
	}

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
	 * Simply update the read count after all the reads are complete.
	 */
	read_fence();

	portal->local->read_count += count;
}

void portal_sync_query(struct portal *portal, message_t *msg, size_t msg_id)
{
	/*
	 * Setup the message waiting event.
	 */
	portal->local->msg_id = 0;

	write_fence();

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

	read_fence();

	remote_msg_id = portal->remote->msg_id;

	if(msg_event && remote_msg_id == msg_id)
	{
		/*
		 * Send the message and trigger the event.
		 */
		int dummy;
		portal_write(portal, msg, &dummy);
		portal_flush(portal);
		write_fence();
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
