/*
 * See LICENSE for licensing information
 */

#include "oniontrace.h"

typedef enum _OnionTraceRecorderState OnionTraceRecorderState;
enum _OnionTraceRecorderState {
    ONIONTRACE_RECORDER_IDLE,
    ONIONTRACE_RECORDER_CONNECTING,
    ONIONTRACE_RECORDER_AUTHENTICATING,
    ONIONTRACE_RECORDER_BOOTSTRAPPING,
    ONIONTRACE_RECORDER_RECORDING,
};

struct _OnionTraceRecorder {
    /* objects we don't own */
    OnionTraceConfig* config;
    OnionTraceEventManager* manager;

    /* objects/data we own */
    OnionTraceRecorderState state;
    gchar* id;
    OnionTraceTorCtl* torctl;
    OnionTraceTimer* heartbeatTimer;
    struct timespec nowCached;

    guint circuitCountLastBeat;
    guint streamCountLastBeat;
    gsize circuitCountTotal;
    gsize streamCountTotal;
};

static void _oniontracerecorder_genericTimerReadable(OnionTraceTimer* timer, OnionTraceEventFlag type) {
    g_assert(timer);
    g_assert(type & ONIONTRACE_EVENT_READ);

    /* if the timer triggered, this will call the timer callback function */
    gboolean calledNotify = oniontracetimer_check(timer);
    if(!calledNotify) {
        warning("Authority unable to execute timer callback function. "
                "The timer might trigger again since we did not delete it.");
    }
}

static void _oniontracerecorder_heartbeat(OnionTraceRecorder* recorder, gpointer unused) {
    g_assert(recorder);

    clock_gettime(CLOCK_REALTIME, &recorder->nowCached);

    /* log some generally useful info as a status update */
    message("%s: recorder-heartbeat-current: circuits=%u streams=%u",
            recorder->id,
            recorder->circuitCountLastBeat,
            recorder->streamCountLastBeat);

    message("%s: recorder-heartbeat-total: circuits=%zu streams=%zu",
            recorder->id,
            recorder->circuitCountTotal,
            recorder->streamCountTotal);

    recorder->circuitCountLastBeat = 0;
    recorder->streamCountLastBeat = 0;
}

static void _oniontracerecorder_registerHeartbeat(OnionTraceRecorder* recorder) {
    g_assert(recorder);

    if(recorder->heartbeatTimer) {
        oniontracetimer_free(recorder->heartbeatTimer);
    }

    /* log heartbeat message every 1 second */
    recorder->heartbeatTimer = oniontracetimer_new((GFunc)_oniontracerecorder_heartbeat, recorder, NULL);
    oniontracetimer_arm(recorder->heartbeatTimer, 1, 1);

    gint timerFD = oniontracetimer_getFD(recorder->heartbeatTimer);
    oniontraceeventmanager_register(recorder->manager, timerFD, ONIONTRACE_EVENT_READ,
            (OnionTraceOnEventFunc)_oniontracerecorder_genericTimerReadable, recorder->heartbeatTimer);
}

static void _oniontracerecorder_onStreamStatus(OnionTraceRecorder* recorder,
        StreamStatus status, gint circuitID, gint streamID, in_port_t sourcePort) {
    g_assert(recorder);

    /* note: the sourcePort is only valid for STREAM_STATUS_NEW, otherwise its 0 */

    switch(status) {
        case STREAM_STATUS_NEW: {
            /* note: the provided circuitID is 0 here */

            break;
        }

        case STREAM_STATUS_SUCCEEDED: {

            break;
        }

        case STREAM_STATUS_DETACHED:
        case STREAM_STATUS_FAILED:
        case STREAM_STATUS_CLOSED: {

            break;
        }

        case STREAM_STATUS_NONE:
        default:
        {
            /* Just ignore these */
            break;
        }
    }
}

static void _oniontracerecorder_onCircuitStatus(OnionTraceRecorder* recorder,
        CircuitStatus status, gint circuitID) {
    g_assert(recorder);

    switch(status) {
        case CIRCUIT_STATUS_ASSIGNED: {

            break;
        }

        case CIRCUIT_STATUS_BUILT: {

            break;
        }

        case CIRCUIT_STATUS_FAILED:
        case CIRCUIT_STATUS_CLOSED: {

            break;
        }

        case CIRCUIT_STATUS_LAUNCHED:
        case CIRCUIT_STATUS_EXTENDED:
        case CIRCUIT_STATUS_NONE:
        default:
        {
            /* Just ignore these */
            break;
        }
    }
}

static void _oniontracerecorder_onBootstrapped(OnionTraceRecorder* recorder) {
    g_assert(recorder);

    in_port_t clientPort = oniontracetorctl_getControlClientPort(recorder->torctl);

    message("%s: successfully bootstrapped client port %u", recorder->id, clientPort);

    recorder->state = ONIONTRACE_RECORDER_RECORDING;

    /* we will watch status on circuits and streams asynchronously.
     * set this before we tell Tor to stop attaching streams for us. */
    oniontracetorctl_setCircuitStatusCallback(recorder->torctl,
            (OnCircuitStatusFunc)_oniontracerecorder_onCircuitStatus, recorder);
    oniontracetorctl_setStreamStatusCallback(recorder->torctl,
            (OnStreamStatusFunc)_oniontracerecorder_onStreamStatus, recorder);

    /* start watching for circuit and stream events */
    oniontracetorctl_commandEnableEvents(recorder->torctl);

    /* set the config for Tor so streams stay unattached */
    oniontracetorctl_commandSetupTorConfig(recorder->torctl);
}

static void _oniontracerecorder_onAuthenticated(OnionTraceRecorder* recorder) {
    g_assert(recorder);

    in_port_t clientPort = oniontracetorctl_getControlClientPort(recorder->torctl);

    message("%s: successfully authenticated client port %u", recorder->id, clientPort);

    message("%s: bootstrapping on client port %u", recorder->id, clientPort);

    oniontracetorctl_commandGetBootstrapStatus(recorder->torctl,
            (OnBootstrappedFunc)_oniontracerecorder_onBootstrapped, recorder);
    recorder->state = ONIONTRACE_RECORDER_BOOTSTRAPPING;
}

static void _oniontracerecorder_onConnected(OnionTraceRecorder* recorder) {
    g_assert(recorder);

    in_port_t clientPort = oniontracetorctl_getControlClientPort(recorder->torctl);

    message("%s: connection attempt finished on client port %u to Tor control server port %u",
            recorder->id, clientPort, oniontraceconfig_getTorControlPort(recorder->config));

    message("%s: attempting to authenticate on client port %u", recorder->id, clientPort);

    oniontracetorctl_commandAuthenticate(recorder->torctl,
            (OnAuthenticatedFunc)_oniontracerecorder_onAuthenticated, recorder);
    recorder->state = ONIONTRACE_RECORDER_AUTHENTICATING;
}

gboolean oniontracerecorder_start(OnionTraceRecorder* recorder) {
    g_assert(recorder);

    if(recorder->state != ONIONTRACE_RECORDER_IDLE) {
        message("%s: can't start recorder because it is not idle", recorder->id);
        return FALSE;
    }

    message("%s: creating control client to connect to Tor", recorder->id);

    /* set up our torctl instance to get the descriptors before starting attack */
    in_port_t controlPort = oniontraceconfig_getTorControlPort(recorder->config);

    recorder->torctl = oniontracetorctl_new(recorder->manager, controlPort,
            (OnConnectedFunc)_oniontracerecorder_onConnected, recorder);

    if(recorder->torctl == NULL) {
        message("%s: error creating tor controller instance", recorder->id);
        return FALSE;
    }

    message("%s: created tor controller instance, connecting to port %u",
            recorder->id, controlPort);
    recorder->state = ONIONTRACE_RECORDER_CONNECTING;

    /* now set up the heartbeat so we can log progress over time */
    _oniontracerecorder_registerHeartbeat(recorder);

    return TRUE;
}

gboolean oniontracerecorder_stop(OnionTraceRecorder* recorder) {
    g_assert(recorder);

    if(recorder->state == ONIONTRACE_RECORDER_IDLE) {
        message("%s: can't stop recorder because it is already idle", recorder->id);
        return FALSE;
    }

    if(recorder->heartbeatTimer) {
        oniontracetimer_free(recorder->heartbeatTimer);
        recorder->heartbeatTimer = NULL;
    }

    if(recorder->torctl) {
        oniontracetorctl_free(recorder->torctl);
        recorder->torctl = NULL;
    }

    recorder->state = ONIONTRACE_RECORDER_IDLE;

    // TODO write any file output?
    return TRUE;
}

OnionTraceRecorder* oniontracerecorder_new(OnionTraceConfig* config, OnionTraceEventManager* manager) {
    OnionTraceRecorder* recorder = g_new0(OnionTraceRecorder, 1);

    recorder->manager = manager;
    recorder->config = config;

    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Recorder");
    recorder->id = g_string_free(idbuf, FALSE);

    recorder->state = ONIONTRACE_RECORDER_IDLE;

    return recorder;
}

void oniontracerecorder_free(OnionTraceRecorder* recorder) {
    g_assert(recorder);

    if(recorder->heartbeatTimer) {
        oniontracetimer_free(recorder->heartbeatTimer);
    }

    if(recorder->torctl) {
        oniontracetorctl_free(recorder->torctl);
    }

    if(recorder->id) {
        g_free(recorder->id);
    }

    g_free(recorder);
}
