#include "event.h"
#include "windows.h"

struct event
{
    HANDLE handle;
};

event_t event_alloc()
{
    event_t event = malloc(sizeof(struct event));

    event->handle = CreateEvent(0, TRUE, 0, 0);
	
    return event;
}

void event_free(event_t event)
{
    CloseHandle(event->handle);

    free(event);
}

void event_clear(event_t event)
{
    ResetEvent(event->handle);
}

void event_set(event_t event)
{
    SetEvent(event->handle);
}

void event_wait(event_t event)
{
    WaitForSingleObject(event->handle, INFINITE);
}
