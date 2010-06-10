#include <stdio.h>
#include <stdlib.h>
#include "event.h"
#include "portal.h"
#ifdef WIN32
    #include <windows.h>
#else
    #include <sys/time.h>
    #include <pthread.h>
    #define WINAPI
#endif

struct portal *portals[2];

int get_ticks()
{
    #ifdef WIN32
        return GetTickCount();
    #else
        struct timeval tv;

        if(gettimeofday(&tv, 0) != 0)
            return 0;

        return tv.tv_sec * 1000 + tv.tv_usec / 1000;
    #endif
}

int WINAPI other_thread(void *param)
{
    struct portal *portal = portals[1];

	size_t last_update = get_ticks();
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

		size_t ticks = get_ticks();
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

    #ifdef WIN32
    DWORD thread_id;

    HANDLE thread = CreateThread(0, 0, other_thread, 0, 0, &thread_id);   // returns the thread identifier
    #else
    pthread_t thread;
    pthread_create(&thread, 0, (void *(*)(void *))other_thread, 0);
    #endif
    struct portal *portal = portals[0];

	while(true)
	{
		portal_wait(portal);

		message_t msg;
		int pending;
		message_t *msgs;

		while((pending = portal_pending(portal, &msgs)))
		{
			for(int x = 0; x < pending; x++)
			{
				msg = msgs[x];
			}

			portal_read(portal, pending);
		}
    }

    #ifdef WIN32
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    #else
        pthread_join(thread, 0);
        pthread_exit(0);
    #endif

    portal_free(portals);

    return 0;
}
