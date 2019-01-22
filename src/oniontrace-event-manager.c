/*
 * See LICENSE for licensing information
 */

#include "oniontrace.h"

struct _OnionTraceEventManager {
    gint epollDescriptor;
    gboolean shouldStopLoop;
    GHashTable* watches;
};

typedef struct _OnionTraceWatch OnionTraceWatch;
struct _OnionTraceWatch {
    gint descriptor;
    OnionTraceEventFlag type;
    OnionTraceOnEventFunc onEvent;
    gpointer onEventArg;
};

OnionTraceEventManager* oniontraceeventmanager_new() {
    OnionTraceEventManager* manager = g_new0(OnionTraceEventManager, 1);

    /* we need to watch all of the descriptors in our main loop
     * so we know when we can wait on any of them without blocking. */
    manager->epollDescriptor = epoll_create(1);
    if(manager->epollDescriptor < 0) {
        critical("Error in main epoll_create");
        close(manager->epollDescriptor);
        oniontraceeventmanager_free(manager);
        return NULL;
    }

    manager->watches = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, g_free);

    return manager;
}

void oniontraceeventmanager_free(OnionTraceEventManager* manager) {
    g_assert(manager);

    if(manager->watches) {
        /* this will call g_free on all of the watch objects */
        g_hash_table_destroy(manager->watches);
    }

    g_free(manager);
}

gboolean oniontraceeventmanager_register(OnionTraceEventManager* manager,
        gint descriptor, OnionTraceEventFlag eventType,
        OnionTraceOnEventFunc onEvent, gpointer onEventArg) {
    g_assert(manager);

    struct epoll_event epev;
    memset(&epev, 0, sizeof(struct epoll_event));

    if(eventType & ONIONTRACE_EVENT_READ) {
        epev.events |= EPOLLIN;
    }
    if(eventType & ONIONTRACE_EVENT_WRITE) {
        epev.events |= EPOLLOUT;
    }

    if(epev.events == 0 || descriptor <= 0) {
        /* not registered */
        return FALSE;
    }

    epev.data.fd = descriptor;

    /* if it's already registered, we have to remove it first */
    if(g_hash_table_lookup(manager->watches, &descriptor) != NULL) {
        oniontraceeventmanager_deregister(manager, descriptor);
    }

    /* tell epoll to watch it */
    int result = epoll_ctl(manager->epollDescriptor, EPOLL_CTL_ADD, epev.data.fd, &epev);
    if(result < 0) {
        warning("epoll_ctl failed to add descriptor %i", descriptor);
        return FALSE;
    }

    /* now store the variables we need to take action when events occur */
    OnionTraceWatch* watch = g_new0(OnionTraceWatch, 1);
    watch->descriptor = descriptor;
    watch->type = eventType;
    watch->onEvent = onEvent;
    watch->onEventArg = onEventArg;

    g_hash_table_replace(manager->watches, &(watch->descriptor), watch);

    /* successfully registered */
    return TRUE;
}

gboolean oniontraceeventmanager_deregister(OnionTraceEventManager* manager, gint descriptor) {
    g_assert(manager);

    /* de-register the epoll descriptor */
    OnionTraceWatch* watch = g_hash_table_lookup(manager->watches, &descriptor);
    if(watch == NULL) {
        /* couldn't find a registered watch, so nothing was deregistered */
        return FALSE;
    }

    struct epoll_event epev;
    memset(&epev, 0, sizeof(struct epoll_event));

    epev.data.fd = descriptor;

    int result = epoll_ctl(manager->epollDescriptor, EPOLL_CTL_DEL, epev.data.fd, &epev);
    if(result < 0) {
        warning("epoll_ctl failed to delete descriptor %i", descriptor);
        return FALSE;
    }

    /* this will call g_free on the watch object */
    g_hash_table_remove(manager->watches, &descriptor);

    /* successfully deregistered */
    return TRUE;
}

static void _oniontraceventmanager_processEvent(OnionTraceEventManager* manager, gint descriptor, OnionTraceEventFlag event) {
    g_assert(manager);

    debug("started processing event %s for descriptor %i",
            (event == ONIONTRACE_EVENT_READ) ? "READ" :
            (event == ONIONTRACE_EVENT_WRITE) ? "WRITE" :
            (event == ONIONTRACE_EVENT_READ|ONIONTRACE_EVENT_WRITE) ? "READ|WRITE" :
            "NONE", descriptor);

    OnionTraceWatch* watch = g_hash_table_lookup(manager->watches, &descriptor);
    if(watch == NULL) {
        /* couldn't find a registered watch, so nothing was deregistered */
        warning("missing watch object to handle event %s for descriptor %i",
                    (event == ONIONTRACE_EVENT_READ) ? "READ" :
                    (event == ONIONTRACE_EVENT_WRITE) ? "WRITE" :
                    (event == ONIONTRACE_EVENT_READ|ONIONTRACE_EVENT_WRITE) ? "READ|WRITE" :
                    "NONE", descriptor);
        return;
    }

    if(watch->onEvent) {
        /* call the registered callback to handle the event. make sure to pass the events
         * that *occurred*, not the events that are *registered* in the watch. */
        watch->onEvent(watch->onEventArg, event);
    }

    debug("finished processing event %s for descriptor %i",
                (event == ONIONTRACE_EVENT_READ) ? "READ" :
                (event == ONIONTRACE_EVENT_WRITE) ? "WRITE" :
                (event == ONIONTRACE_EVENT_READ|ONIONTRACE_EVENT_WRITE) ? "READ|WRITE" :
                "NONE", descriptor);
}

gboolean oniontraceeventmanager_runMainLoop(OnionTraceEventManager* manager) {
    g_assert(manager);

    /* main loop - wait for events from the descriptors */
    struct epoll_event events[100];
    gint nReadyFDs;
    message("entering main loop to watch descriptors");

    while(1) {
        /* wait for some events */
        debug("waiting for events");
        nReadyFDs = epoll_wait(manager->epollDescriptor, events, 100, -1);
        if(nReadyFDs == -1) {
            critical("Error in client epoll_wait in main loop");
            return FALSE;
        }

        /* process every descriptor that's ready */
        for(gint i = 0; i < nReadyFDs; i++) {
            gint descriptor = events[i].data.fd;

            OnionTraceEventFlag eventFlag = ONIONTRACE_EVENT_NONE;
            if(events[i].events & EPOLLIN) {
                eventFlag |= ONIONTRACE_EVENT_READ;
            }
            if(events[i].events & EPOLLOUT) {
                eventFlag |= ONIONTRACE_EVENT_WRITE;
            }

            _oniontraceventmanager_processEvent(manager, descriptor, eventFlag);
        }

        /* break out if done */
        if(manager->shouldStopLoop) {
            break;
        }
    }

    message("exited main loop");
    return TRUE;
}

void oniontraceeventmanager_stopMainLoop(OnionTraceEventManager* manager) {
    g_assert(manager);

    manager->shouldStopLoop = TRUE;
}
