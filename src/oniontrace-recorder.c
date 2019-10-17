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

    OnionTraceFile* otfile;

    gsize circuitCountTotal;
    gsize streamCountTotal;
};

static void _oniontracerecorder_onStreamStatus(OnionTraceRecorder* recorder,
        StreamStatus status, gint circuitID, gint streamID, gchar* username) {
    g_assert(recorder);

    switch(status) {
        case STREAM_STATUS_SUCCEEDED: {
            info("%s: stream SUCCEEDED for session name %s: stream %i on circuit %i",
                    recorder->id, username, streamID, circuitID);
            recorder->streamCountTotal++;

            OnionTraceCircuit* circuit = g_hash_table_lookup(recorder->circuits, &circuitID);

            if(circuit) {
                info("%s: circuit %i for session name %s will be recorded",
                    recorder->id, circuitID, username);

                oniontracecircuit_incrementStreamCounter(circuit);

                if(username) {
                    const gchar* sessionID = oniontracecircuit_getSessionID(circuit);

                    /* store the username as the session ID for this circuit.
                     * if we got a second username, make sure they match */
                    if(!sessionID) {
                        info("%s: storing username %s as the session name for circuit %i",
                                recorder->id, username, circuitID);
                        oniontracecircuit_setSessionID(circuit, username);
                    } else {
                        if(g_ascii_strcasecmp(sessionID, username)) {
                            /* this means they do NOT match */
                            warning("%s: circuit %i with session ID %s was assigned a stream with username %s",
                                    recorder->id, circuitID, sessionID, username);
                        }
                    }
                }
            } else {
                info("%s: circuit %i for session name %s is not recorded. closing now so we get "
                        "a new circuit for the session and then we can record it",
                        recorder->id, circuitID, username);

                /* a stream was built on a circuit we did not record. close the circuit
                 * so that Tor will use a new one that we will record. */
                oniontracetorctl_commandCloseCircuit(recorder->torctl, circuitID);
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
                info("%s: storing new circuit %i", recorder->id, circuitID);

                OnionTraceCircuit* circuit = oniontracecircuit_new();

                struct timespec launchTime;
                clock_gettime(CLOCK_REALTIME, &launchTime);
                oniontracecircuit_setLaunchTime(circuit, &launchTime);
                oniontracecircuit_setCircuitID(circuit, circuitID);

                if(status == CIRCUIT_STATUS_EXTENDED && path != NULL) {
                    /* if we got a second path... this will keep the latest one */
                    info("%s: storing path %s for EXTENDED circuit %i", recorder->id, path, circuitID);
                    oniontracecircuit_setPath(circuit, path);
                }

                g_hash_table_replace(recorder->circuits, oniontracecircuit_getID(circuit), circuit);
            }

            break;
        }

        case CIRCUIT_STATUS_BUILT: {
            info("%s: circuit %i BUILT with path %s", recorder->id, circuitID, path);
            recorder->circuitCountTotal++;

            /* now that we know the circuit was successfully built, we can store the path */
            if(path) {
                OnionTraceCircuit* circuit = g_hash_table_lookup(recorder->circuits, &circuitID);

                if(circuit && path) {
                    /* if we got a second path... this will keep the latest one */
                    info("%s: storing path %s for BUILT circuit %i", recorder->id, path, circuitID);
                    oniontracecircuit_setPath(circuit, path);
                }
            }
            break;
        }

        case CIRCUIT_STATUS_FAILED:
        case CIRCUIT_STATUS_CLOSED: {
            info("%s: circuit %i done for path %s", recorder->id, circuitID, path);

            OnionTraceCircuit* circuit = g_hash_table_lookup(recorder->circuits, &circuitID);
            if(circuit) {
                /* only write the circuit if it was build and we have a path for it */
                if(oniontracecircuit_getPath(circuit) != NULL) {
                    /* write the circuit record to disk */
                    oniontracefile_writeCircuit(recorder->otfile, circuit, &recorder->startTime);
                }

                /* remove it from our table, which will also free the circuit memory */
                info("%s: removing and freeing circuit %i", recorder->id, circuitID);
                g_hash_table_remove(recorder->circuits, &circuitID);
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

void oniontracerecorder_cleanup(OnionTraceRecorder* recorder) {
    oniontracetorctl_commandGetAllCircuitStatusCleanup(recorder->torctl);
}

/* returns a status string for the heartbeat message */
gchar* oniontracerecorder_toString(OnionTraceRecorder* recorder) {
    guint circuitCountActive = 0, streamCountActive = 0;
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, recorder->circuits);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        OnionTraceCircuit* circuit = value;
        if(circuit) {
            circuitCountActive++;
            streamCountActive += oniontracecircuit_getStreamCounter(circuit);
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
    OnionTraceFile* otfile = oniontracefile_newWriter(filename);
    if(!otfile) {
        return NULL;
    }

    OnionTraceRecorder* recorder = g_new0(OnionTraceRecorder, 1);

    recorder->torctl = torctl;
    recorder->otfile = otfile;

    clock_gettime(CLOCK_REALTIME, &recorder->startTime);

    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Recorder");
    recorder->id = g_string_free(idbuf, FALSE);

    recorder->circuits = g_hash_table_new_full(g_int_hash, g_int_equal, NULL,
            (GDestroyNotify)oniontracecircuit_free);

    /* we will watch status on circuits and streams asynchronously.
     * set these before we start listening for circuit and stream events. */
    oniontracetorctl_setCircuitStatusCallback(recorder->torctl,
            (OnCircuitStatusFunc)_oniontracerecorder_onCircuitStatus, recorder);
    oniontracetorctl_setStreamStatusCallback(recorder->torctl,
            (OnStreamStatusFunc)_oniontracerecorder_onStreamStatus, recorder);

    /* start watching for circuit and stream events */
    oniontracetorctl_commandEnableEvents(recorder->torctl, "CIRC STREAM");

    /* record all existing circuits.
     * this will call our circuit status callback for all existing circuits
     * that were built before we started listening. */
    oniontracetorctl_commandGetAllCircuitStatus(recorder->torctl);

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
            OnionTraceCircuit* circuit = value;
            /* only write the circuit if it was build and we have a path for it */
            if(circuit && oniontracecircuit_getPath(circuit) != NULL) {
                oniontracefile_writeCircuit(recorder->otfile, circuit, &recorder->startTime);
            }
        }

        /* free all of the circuits */
        g_hash_table_destroy(recorder->circuits);
    }

    if(recorder->otfile) {
        oniontracefile_free(recorder->otfile);
    }

    if(recorder->id) {
        g_free(recorder->id);
    }

    g_free(recorder);
}
