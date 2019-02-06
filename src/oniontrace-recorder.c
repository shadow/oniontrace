/*
 * See LICENSE for licensing information
 */

#include "oniontrace.h"

struct _OnionTraceRecorder {
    /* objects we don't own */
    OnionTraceTorCtl* torctl;

    /* objects/data we own */
    gchar* id;
    struct timespec nowCached;

    guint circuitCountLastBeat;
    guint streamCountLastBeat;
    gsize circuitCountTotal;
    gsize streamCountTotal;
};

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
        CircuitStatus status, gint circuitID, gchar* path) {
    g_assert(recorder);

    /* path is non-null only on EXTENDED and BUILT */

    switch(status) {
        case CIRCUIT_STATUS_ASSIGNED: {
            /* if we build a custom circuit, Tor will assign your path a
             * circuit id and emit this status event to tell us */
            break;
        }

        case CIRCUIT_STATUS_EXTENDED: {
            info("circuit %i EXTENDED for path %s", circuitID, path);
            break;
        }

        case CIRCUIT_STATUS_BUILT: {
            info("circuit %i BUILT for path %s", circuitID, path);
            break;
        }

        case CIRCUIT_STATUS_FAILED:
        case CIRCUIT_STATUS_CLOSED: {
            info("circuit %i CLOSED for path %s", circuitID, path);
            break;
        }

        case CIRCUIT_STATUS_LAUNCHED:
        case CIRCUIT_STATUS_NONE:
        default:
        {
            /* Just ignore these */
            break;
        }
    }
}

gchar* oniontracerecorder_toString(OnionTraceRecorder* recorder) {
    return NULL;
}

OnionTraceRecorder* oniontracerecorder_new(OnionTraceTorCtl* torctl) {
    OnionTraceRecorder* recorder = g_new0(OnionTraceRecorder, 1);

    recorder->torctl = torctl;

    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Recorder");
    recorder->id = g_string_free(idbuf, FALSE);

    /* we will watch status on circuits and streams asynchronously.
     * set these before we start listening for circuit and stream events. */
    oniontracetorctl_setCircuitStatusCallback(recorder->torctl,
            (OnCircuitStatusFunc)_oniontracerecorder_onCircuitStatus, recorder);
    oniontracetorctl_setStreamStatusCallback(recorder->torctl,
            (OnStreamStatusFunc)_oniontracerecorder_onStreamStatus, recorder);

    /* start watching for circuit and stream events */
    oniontracetorctl_commandEnableEvents(recorder->torctl);

    return recorder;
}

void oniontracerecorder_free(OnionTraceRecorder* recorder) {
    g_assert(recorder);

    if(recorder->id) {
        g_free(recorder->id);
    }

    g_free(recorder);
}
