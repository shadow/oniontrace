/*
 * See LICENSE for licensing information
 */

#ifndef SRC_ONIONTRACE_EVENT_MANAGER_H_
#define SRC_ONIONTRACE_EVENT_MANAGER_H_

#include <glib.h>

typedef enum _OnionTraceEventFlag OnionTraceEventFlag;
enum _OnionTraceEventFlag {
    ONIONTRACE_EVENT_NONE = 0,
    ONIONTRACE_EVENT_READ = 1 << 0,
    ONIONTRACE_EVENT_WRITE = 1 << 1,
};

/* function signature for the callback function that will get called by the
 * event manager when I/O occurs on registered descriptors. */
typedef void (*OnionTraceOnEventFunc)(gpointer onEventArg, OnionTraceEventFlag eventType);

/* Opaque internal struct for the manager object */
typedef struct _OnionTraceEventManager OnionTraceEventManager;

/* returns a new instance of an event manager that will watch file descriptors
 * for read/write events and notify components when the specified I/O occurs. */
OnionTraceEventManager* oniontraceeventmanager_new();

/* deallocates all memory associated with an event manager previously created
 * with the new() function. */
void oniontraceeventmanager_free(OnionTraceEventManager* manager);

/* monitor a new file descriptor for I/O events of the given type, and register
 * a callback function and arguments to execute when I/O occurs.
 * returns TRUE if the registration was successful, false otherwise. */
gboolean oniontraceeventmanager_register(OnionTraceEventManager* manager,
        gint descriptor, OnionTraceEventFlag eventType,
        OnionTraceOnEventFunc onEvent, gpointer onEventArg);

/* stops monitoring I/O for the given file descriptor and deregisters previously
 * registered callback functions.
 * returns TRUE if the descriptor was previously registered, FALSE otherwise. */
gboolean oniontraceeventmanager_deregister(OnionTraceEventManager* manager, gint descriptor);

/* instructs the event manager to start waiting for events from all registered descriptors.
 * when events occur, the registered callback functions will be executed. */
gboolean oniontraceeventmanager_runMainLoop(OnionTraceEventManager* manager);

/* instructs the event manager to break out of the main loop asap.
 * this will probably stop the program. */
void oniontraceeventmanager_stopMainLoop(OnionTraceEventManager* manager);

#endif /* SRC_ONIONTRACE_EVENT_MANAGER_H_ */
