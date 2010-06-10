#include <stdio.h>
#include <stdlib.h>
#include "portal.h"
#include "windows.h"

struct portal *portals[2];

DWORD WINAPI other_thread(LPVOID lpParam)
{
    struct portal *portal = portals[1];
	
	size_t last_update = GetTickCount();
	size_t messages = 0;

	int fills = 0;

	message_t msg;

	while(true)
	{
		for(int i = 0; i < 5000; i++)
		{
			for(int b = 0; b < 20; b++)
				portal_write(portal, &msg, &fills);

			messages += 20;

			portal_notify(portal);
		}

		size_t ticks = GetTickCount();
		size_t diff = ticks - last_update;

		if(diff > 2000)
		{
			printf("messages/sec %f\n", (float)messages / diff * 1000.f);
			printf("filled buffers/sec %f, %f%%\n", (float)fills / diff * 1000.f, (float)fills * 100 / messages);

			last_update = ticks;
			messages = 0;
			fills = 0;
		}
	}

    return 0;
}

int main()
{
    portal_alloc(portals);

    DWORD thread_id;

    HANDLE thread = CreateThread(0, 0, other_thread, 0, 0, &thread_id);   // returns the thread identifier

    struct portal *portal = portals[0];
	
	size_t last_update = GetTickCount();
	size_t messages = 0;

	while(true)
	{
		portal_wait(portal);

		message_t msg;
		int pending;
		message_t *msgs;

		while(pending = portal_pending(portal, &msgs))
		{
			for(int x = 0; x < pending; x++)
			{
				msg = msgs[x];
			}

			messages += pending;
			portal_read(portal, pending);
		}
    }

    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);

    portal_free(portals);

    return 0;
}
