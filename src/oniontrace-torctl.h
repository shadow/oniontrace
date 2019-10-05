/*
 * See LICENSE for licensing information
 */

#ifndef SRC_ONIONTRACE_TORCTL_H_
#define SRC_ONIONTRACE_TORCTL_H_

#include <glib.h>

#include "oniontrace-event-manager.h"

typedef enum _StreamStatus StreamStatus;
enum _StreamStatus {
    STREAM_STATUS_NONE,
    STREAM_STATUS_NEW,       /* New request to connect */
    STREAM_STATUS_SUCCEEDED, /* Received a reply; stream established */
    STREAM_STATUS_DETACHED,  /* Detached from circuit; still retriable */
    STREAM_STATUS_FAILED,    /* Stream failed and not retriable */
    STREAM_STATUS_CLOSED     /* Stream closed */
};

typedef enum _CircuitStatus CircuitStatus;
enum _CircuitStatus {
    CIRCUIT_STATUS_NONE,
    CIRCUIT_STATUS_ASSIGNED, /* special status from '250 EXTENDED 3' sync event */
    CIRCUIT_STATUS_LAUNCHED, /* circuit ID assigned to new circuit */
    CIRCUIT_STATUS_BUILT,    /* all hops finished, can now accept streams */
    CIRCUIT_STATUS_EXTENDED, /* one more hop has been completed */
    CIRCUIT_STATUS_FAILED,   /* circuit closed (was not built) */
    CIRCUIT_STATUS_CLOSED    /* circuit closed (was built) */
};

typedef struct _OnionTraceTorCtl OnionTraceTorCtl;

typedef void (*OnConnectedFunc)(gpointer userData);
typedef void (*OnAuthenticatedFunc)(gpointer userData);
typedef void (*OnBootstrappedFunc)(gpointer userData);

typedef void (*OnDescriptorsReceivedFunc)(gpointer userData, GQueue* descriptorLines);

typedef void (*OnCircuitStatusFunc)(gpointer userData, CircuitStatus status, gint circuitID, gchar* path);
typedef void (*OnStreamStatusFunc)(gpointer userData, StreamStatus status, gint circuitID, gint streamID, gchar* username);

typedef void (*OnLineReceivedFunc)(gpointer userData, gchar* line);

OnionTraceTorCtl* oniontracetorctl_new(OnionTraceEventManager* manager, in_port_t controlPort,
        OnConnectedFunc onConnected, gpointer onConnectedArg);
void oniontracetorctl_free(OnionTraceTorCtl* torctl);

in_port_t oniontracetorctl_getControlClientPort(OnionTraceTorCtl* torctl);

/* set the callbacks for torctl status updates */
void oniontracetorctl_setCircuitStatusCallback(OnionTraceTorCtl* torctl,
        OnCircuitStatusFunc onCircuitStatus, gpointer onCircuitStatusArg);
void oniontracetorctl_setStreamStatusCallback(OnionTraceTorCtl* torctl,
        OnStreamStatusFunc onStreamStatus, gpointer onStreamStatusArg);
void oniontracetorctl_setLineReceivedCallback(OnionTraceTorCtl* torctl,
        OnLineReceivedFunc onLineReceived, gpointer onLineReceivedArg);

/* controller commands with callbacks when they complete */
void oniontracetorctl_commandAuthenticate(OnionTraceTorCtl* torctl,
        OnAuthenticatedFunc onAuthenticated, gpointer onAuthenticatedArg);
void oniontracetorctl_commandGetBootstrapStatus(OnionTraceTorCtl* torctl,
        OnBootstrappedFunc onBootstrapped, gpointer onBootstrappedArg);
void oniontracetorctl_commandGetDescriptorInfo(OnionTraceTorCtl* torctl,
        OnDescriptorsReceivedFunc onDescriptorsReceived, gpointer onDescriptorsReceivedArg);

/* controller commands without callbacks */
void oniontracetorctl_commandSetupTorConfig(OnionTraceTorCtl* torctl);
void oniontracetorctl_commandEnableEvents(OnionTraceTorCtl* torctl, const gchar* spaceDelimitedEvents);
void oniontracetorctl_commandDisableEvents(OnionTraceTorCtl* torctl);

void oniontracetorctl_commandBuildNewCircuit(OnionTraceTorCtl* torctl, const gchar* path);
void oniontracetorctl_commandAttachStreamToCircuit(OnionTraceTorCtl* torctl, gint streamID, gint circuitID);

void oniontracetorctl_commandCloseCircuit(OnionTraceTorCtl* torctl, gint crcuitID);
void oniontracetorctl_commandCloseStream(OnionTraceTorCtl* torctl, gint streamID);

void oniontracetorctl_commandGetAllCircuitStatus(OnionTraceTorCtl* torctl);
void oniontracetorctl_commandGetAllCircuitStatusCleanup(OnionTraceTorCtl* torctl);

#endif /* SRC_ONIONTRACE_TORCTL_H_ */
