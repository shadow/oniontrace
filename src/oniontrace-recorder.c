/*
 * See LICENSE for licensing information
 */

#include "oniontrace.h"

struct _OnionTraceRecorder {
    /* objects we don't own */
    OnionTraceTorCtl* torctl;

    /* objects/data we own */
    gchar* id;
    struct timespec startTime;

    GHashTable* circuits;

    FILE* tracefile;

    gsize circuitCountTotal;
    gsize streamCountTotal;
};

typedef struct _OnionTraceRecorderCircuit OnionTraceRecorderCircuit;
struct _OnionTraceRecorderCircuit {
    gint circuitID;
    struct timespec launchTime;
    gchar* path;
    gchar* sessionID;
    guint numStreams;
};

static OnionTraceRecorderCircuit* _oniontracerecorder_newCircuit(gint circuitID) {
    OnionTraceRecorderCircuit* circuit = g_new0(OnionTraceRecorderCircuit, 1);
    circuit->circuitID = circuitID;
    clock_gettime(CLOCK_REALTIME, &circuit->launchTime);
    return circuit;
}

static void _oniontracerecorder_freeCircuit(OnionTraceRecorderCircuit* circuit) {
    g_assert(circuit);
    if(circuit->path) {
        g_free(circuit->path);
    }
    if(circuit->sessionID) {
        g_free(circuit->sessionID);
    }
    g_free(circuit);
}

static void _oniontracerecorder_recordCircuit(OnionTraceRecorder* recorder, OnionTraceRecorderCircuit* circuit) {
    if(recorder && circuit && recorder->tracefile) {
        struct timespec elapsed;
        oniontracetimer_timespecdiff(&elapsed, &recorder->startTime, &circuit->launchTime);

        GString* string = g_string_new("");

        g_string_append_printf(string, "%"G_GSIZE_FORMAT".%09"G_GSIZE_FORMAT";%s;%s\n",
                (gsize)elapsed.tv_sec, (gsize)elapsed.tv_nsec,
                circuit->sessionID ? circuit->sessionID : "NULL",
                circuit->path ? circuit->path : "NULL");

        fwrite(string->str, string->len, 1, recorder->tracefile);
        fflush(recorder->tracefile);

        g_string_free(string, TRUE);
    }
}

static void _oniontracerecorder_onStreamStatus(OnionTraceRecorder* recorder,
        StreamStatus status, gint circuitID, gint streamID, gchar* username) {
    g_assert(recorder);

    switch(status) {
        case STREAM_STATUS_SUCCEEDED: {
            info("stream %i SUCCEEDED on circuit %i with username %s", streamID, circuitID, username);
            recorder->streamCountTotal++;

            OnionTraceRecorderCircuit* circuit = g_hash_table_lookup(recorder->circuits, &circuitID);

            if(circuit) {
                circuit->numStreams++;

                if(username) {
                    /* store the username as the session ID for this circuit.
                     * if we got a second username, make sure they match */
                    if(!circuit->sessionID) {
                        info("storing username %s as session ID for circuit %i", username, circuitID);
                        circuit->sessionID = g_strdup(username);
                    } else {
                        if(g_ascii_strcasecmp(circuit->sessionID, username)) {
                            /* this means they do NOT match */
                            warning("circuit %i with session ID %s was assigned a stream with username %s",
                                    circuitID, circuit->sessionID, username);
                        }
                    }
                }
            }

            break;
        }

        case STREAM_STATUS_NEW:
        case STREAM_STATUS_DETACHED:
        case STREAM_STATUS_FAILED:
        case STREAM_STATUS_CLOSED:
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
        /* if we build a custom circuit, Tor will assign your path a
         * circuit id and emit an ASSIGNED status event to tell us.
         * otherwise we first get an EXTENDED event for a new circuit. */
        case CIRCUIT_STATUS_ASSIGNED:
        case CIRCUIT_STATUS_LAUNCHED:
        case CIRCUIT_STATUS_EXTENDED: {
            /* create the new circuit struct and record the start time.
             * we will get multiple extended events, so only do this if we don't
             * already know about this circuit. */
            if(!g_hash_table_lookup(recorder->circuits, &circuitID)) {
                info("storing new circuit %i", circuitID);
                OnionTraceRecorderCircuit* circuit = _oniontracerecorder_newCircuit(circuitID);
                g_hash_table_replace(recorder->circuits, &circuit->circuitID, circuit);
            }

            break;
        }

        case CIRCUIT_STATUS_BUILT: {
            info("circuit %i BUILT with path %s", circuitID, path);
            recorder->circuitCountTotal++;

            /* now that we know the circuit was successfully built, we can store the path */
            if(path) {
                OnionTraceRecorderCircuit* circuit = g_hash_table_lookup(recorder->circuits, &circuitID);

                if(circuit) {
                    /* if we got a second path... keep the latest one */
                    if(circuit->path) {
                        g_free(circuit->path);
                    }

                    info("storing path %s for circuit %i", path, circuitID);
                    circuit->path = g_strdup(path);
                }
            }
            break;
        }

        case CIRCUIT_STATUS_FAILED:
        case CIRCUIT_STATUS_CLOSED: {
            info("circuit %i done for path %s", circuitID, path);

            OnionTraceRecorderCircuit* circuit = g_hash_table_lookup(recorder->circuits, &circuitID);
            if(circuit) {
                /* write the circuit record to disk */
                _oniontracerecorder_recordCircuit(recorder, circuit);

                /* remove it from our table, which will also free the circuit memory */
                info("removing and freeing circuit %i", circuitID);
                g_hash_table_remove(recorder->circuits, &circuit->circuitID);
            }

            break;
        }

        case CIRCUIT_STATUS_NONE:
        default:
        {
            /* Just ignore these */
            break;
        }
    }
}

/* returns a status string for the heartbeat message */
gchar* oniontracerecorder_toString(OnionTraceRecorder* recorder) {
    guint circuitCountActive = 0, streamCountActive = 0;
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, recorder->circuits);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        OnionTraceRecorderCircuit* circuit = value;
        if(circuit) {
            circuitCountActive++;
            streamCountActive += circuit->numStreams;
        }
    }

    GString* string = g_string_new("");
    g_string_append_printf(string,
            "n_circs_act=%u n_strms_act=%u n_circs_tot=%zu n_strms_tot=%zu",
            circuitCountActive, streamCountActive,
            recorder->circuitCountTotal, recorder->streamCountTotal);
    return g_string_free(string, FALSE);
}

OnionTraceRecorder* oniontracerecorder_new(OnionTraceTorCtl* torctl, const gchar* filename) {
    FILE* tracefile = fopen(filename, "w");
    if(!tracefile) {
        warning("Failed to open tracefile for writing using path %s: error %i, %s",
                filename, errno, g_strerror(errno));
        return NULL;
    }

    OnionTraceRecorder* recorder = g_new0(OnionTraceRecorder, 1);

    recorder->torctl = torctl;
    recorder->tracefile = tracefile;

    clock_gettime(CLOCK_REALTIME, &recorder->startTime);

    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Recorder");
    recorder->id = g_string_free(idbuf, FALSE);

    recorder->circuits = g_hash_table_new_full(g_int_hash, g_int_equal, NULL,
            (GDestroyNotify)_oniontracerecorder_freeCircuit);

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

    if(recorder->circuits) {
        /* record any leftover circuits */
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init(&iter, recorder->circuits);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            OnionTraceRecorderCircuit* circuit = value;
            if(circuit) {
                _oniontracerecorder_recordCircuit(recorder, circuit);
            }
        }

        /* free all of the circuits */
        g_hash_table_destroy(recorder->circuits);
    }

    if(recorder->id) {
        g_free(recorder->id);
    }

    g_free(recorder);
}
