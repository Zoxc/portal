#include "event.h"
#include "malloc.h"
#ifdef WIN32
    #include <windows.h>
#else
    #include <semaphore.h>
#endif

struct event
{
    #ifdef WIN32
        HANDLE handle;
    #else
        sem_t sem;
    #endif
};

event_t event_alloc()
{
    event_t event = malloc(sizeof(struct event));

    #ifdef WIN32
        event->handle = CreateEvent(0, TRUE, 0, 0);
    #else
        sem_init(&event->sem, 0, 0);
    #endif

    return event;
}

void event_free(event_t event)
{
    #ifdef WIN32
        CloseHandle(event->handle);
    #else
        sem_destroy(&event->sem);
    #endif
}

void event_clear(event_t event)
{
    #ifdef WIN32
        ResetEvent(event->handle);
    #endif
}

void event_set(event_t event)
{
    #ifdef WIN32
        SetEvent(event->handle);
    #else
        sem_post(&event->sem);
    #endif
}

void event_wait(event_t event)
{
    #ifdef WIN32
        WaitForSingleObject(event->handle, INFINITE);
    #else
        sem_wait(&event->sem);
    #endif
}
