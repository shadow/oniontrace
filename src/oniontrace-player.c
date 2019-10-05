/*
 * See LICENSE for licensing information
 */

#include "oniontrace.h"

struct _OnionTracePlayer {
    /* objects we don't own */
    OnionTraceTorCtl* torctl;

    /* objects/data we own */
    gchar* id;
    struct timespec startTime;

    GQueue* futureCircuits;
    GQueue* pendingCircuits;
    GHashTable* activeCircuits;
    GHashTable* circuitsWithSessions;
};

static void _oniontraceplayer_onStreamStatus(OnionTracePlayer* player,
        StreamStatus status, gint circuitID, gint streamID, gchar* username) {
    g_assert(player);

    /* note: the sourcePort is only valid for STREAM_STATUS_NEW, otherwise its 0 */

    switch(status) {
        case STREAM_STATUS_NEW: {
            /* note: the provided circuitID is 0 here, because it doesn't have a circuit yet.
             * we need to figure out how to assign it. */
            if(username) {
                OnionTraceCircuit* circuit = g_hash_table_lookup(player->circuitsWithSessions, username);
                if(circuit) {
                    /* assign the stream to the session's circuit */
                    gint sessionCircuitID = oniontracecircuit_getCircuitID(circuit);

                    info("%s: circuit found for session name %s; "
                            "asking Tor to attach stream %i to circuit %i",
                            player->id, username, streamID, sessionCircuitID);

                    oniontracetorctl_commandAttachStreamToCircuit(player->torctl, streamID, sessionCircuitID);
                } else {
                    /* let Tor do the assignment */
                    warning("%s: circuit missing for session name %s; "
                            "asking Tor to choose how to attach stream %i to a circuit",
                            player->id, username, streamID);

                    oniontracetorctl_commandAttachStreamToCircuit(player->torctl, streamID, 0);
                }
            } else {
                /* let Tor do the assignment */
                info("%s: stream %i has no session; asking Tor to choose how to attach it to a circuit",
                            player->id, streamID);

                oniontracetorctl_commandAttachStreamToCircuit(player->torctl, streamID, 0);
            }

            break;
        }

        case STREAM_STATUS_SUCCEEDED:
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

static void _oniontraceplayer_onCircuitStatus(OnionTracePlayer* player,
        CircuitStatus status, gint circuitID, gchar* path) {
    g_assert(player);

    /* path is non-null only on EXTENDED and BUILT */

    switch(status) {
        case CIRCUIT_STATUS_ASSIGNED: {
            /* if we build a custom circuit, Tor will assign your path a
             * circuit id and emit this status event to tell us the circuit id */
            OnionTraceCircuit* circuit = g_queue_pop_head(player->pendingCircuits);
            if(circuit) {
                oniontracecircuit_setCircuitID(circuit, circuitID);

                const gchar* circuitPath = oniontracecircuit_getPath(circuit);
                info("%s: circuit id %i ASSIGNED to circuit with path %s", player->id, circuitID, circuitPath);

                /* add all circuits to the active table */
                gint* id = oniontracecircuit_getID(circuit);
                g_hash_table_replace(player->activeCircuits, id, circuit);

                /* also track circuits with sessions so we can assign streams later */
                const gchar* sessionID = oniontracecircuit_getSessionID(circuit);
                if(sessionID) {
                    message("%s: circuit id %i ASSIGNED to circuit on session %s with path %s",
                            player->id, circuitID, sessionID, circuitPath);
                    g_hash_table_replace(player->circuitsWithSessions, (gpointer)sessionID, circuit);
                }
            }

            break;
        }

        case CIRCUIT_STATUS_FAILED:
            info("circuit %i FAILED for path %s", circuitID, path);

            /* try again if it's one of our circuits */
            OnionTraceCircuit* circuit = g_hash_table_lookup(player->activeCircuits, &circuitID);
            if(circuit) {
                info("trying path %s again on new circuit since previous circuit build failed", path);

                /* the new circuit will get a new circuit id */
                OnionTraceCircuit* circuitCopy = oniontracecircuit_copy(circuit);
                oniontracecircuit_setCircuitID(circuitCopy, 0);

                /* create the circuit and wait for the assigned circuit id */
                const gchar* circuitCopyPath = oniontracecircuit_getPath(circuitCopy);
                oniontracetorctl_commandBuildNewCircuit(player->torctl, circuitCopyPath);
                g_queue_push_tail(player->pendingCircuits, circuitCopy);
            }

            /* no break - continue to clear the original circuit */

        case CIRCUIT_STATUS_CLOSED: {
            info("circuit %i CLOSED for path %s", circuitID, path);

            OnionTraceCircuit* circuit = g_hash_table_lookup(player->activeCircuits, &circuitID);
            if(circuit) {
                const gchar* sessionID = oniontracecircuit_getSessionID(circuit);
                if(sessionID) {
                    g_hash_table_remove(player->circuitsWithSessions, sessionID);
                }

                /* this will free the circuit */
                g_hash_table_remove(player->activeCircuits, &circuitID);
            }
            break;
        }

        case CIRCUIT_STATUS_EXTENDED:
        case CIRCUIT_STATUS_BUILT:
        case CIRCUIT_STATUS_LAUNCHED:
        case CIRCUIT_STATUS_NONE:
        default:
        {
            /* Just ignore these */
            break;
        }
    }
}

/* gets the elapsed time from now until we should build another circuit.
 * returns the relative time in reltime.
 * returns TRUE if we have more circuits to build, FALSE otherwise.
 */
gboolean oniontraceplayer_getNextCircuitLaunchTime(OnionTracePlayer* player, struct timespec* reltime) {
    g_assert(player);
    g_assert(reltime);

    OnionTraceCircuit* circuit = g_queue_peek_head(player->futureCircuits);
    if(!circuit) {
        return FALSE;
    }

    /* what time is it now */
    struct timespec now;
    memset(&now, 0, sizeof(struct timespec));
    clock_gettime(CLOCK_REALTIME, &now);

    /* the absolute circuit launch time */
    struct timespec* launchtime = oniontracecircuit_getLaunchTime(circuit);

    /* lets launch it 1 second early so its ready when we need it */
    if(launchtime->tv_sec >= 1){
        launchtime->tv_sec--;
    }

    if(launchtime->tv_sec < now.tv_sec ||
            (launchtime->tv_sec == now.tv_sec && launchtime->tv_nsec <= now.tv_nsec)){
        /* the circuit should have been launched in the past or now */
        memset(reltime, 0, sizeof(struct timespec));
        /* make sure its not 0, or else the timer will disarm */
        reltime->tv_nsec = 1;
    } else {
        /* compute how much time we have from now until the circuit should launch */
        oniontracetimer_timespecsubtract(reltime, &now, launchtime);
    }

    return TRUE;
}

void oniontraceplayer_launchNextCircuit(OnionTracePlayer* player) {
    g_assert(player);

    OnionTraceCircuit* circuit = g_queue_pop_head(player->futureCircuits);
    if(circuit) {
        const gchar* path = oniontracecircuit_getPath(circuit);
        oniontracetorctl_commandBuildNewCircuit(player->torctl, path);
        g_queue_push_tail(player->pendingCircuits, circuit);

        struct timespec now;
        memset(&now, 0, sizeof(struct timespec));
        clock_gettime(CLOCK_REALTIME, &now);

        struct timespec elapsed;
        memset(&elapsed, 0, sizeof(struct timespec));
        oniontracetimer_timespecsubtract(&elapsed, &player->startTime, &now);

        message("%s: launched new circuit at "
                "%"G_GSIZE_FORMAT".%09"G_GSIZE_FORMAT" "
                "using launch offset "
                "%"G_GSIZE_FORMAT".%09"G_GSIZE_FORMAT" "
                "from start "
                "%"G_GSIZE_FORMAT".%09"G_GSIZE_FORMAT" "
                "circuit path is %s",
                player->id,
                (gsize)now.tv_sec, (gsize)now.tv_nsec,
                (gsize)elapsed.tv_sec, (gsize)elapsed.tv_nsec,
                (gsize)player->startTime.tv_sec, (gsize)player->startTime.tv_nsec,
                path);
    }
}

gchar* oniontraceplayer_toString(OnionTracePlayer* player) {
    return NULL;
}

//static void _oniontraceplayer_logCircuit(OnionTraceCircuit* circuit, OnionTracePlayer* player) {
//    GString* circString = oniontracecircuit_toCSV(circuit, &player->startTime);
//    info("circuit: %s", circString->str);
//    g_string_free(circString, TRUE);
//}

OnionTracePlayer* oniontraceplayer_new(OnionTraceTorCtl* torctl, const gchar* filename) {
    OnionTracePlayer* player = g_new0(OnionTracePlayer, 1);

    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Player");
    player->id = g_string_free(idbuf, FALSE);

    clock_gettime(CLOCK_REALTIME, &player->startTime);

    /* open the csv file containing the circuits */
    OnionTraceFile* otfile = oniontracefile_newReader(filename);

    if(!otfile) {
        critical("Error opening circuit file, cannot proceed");
        oniontraceplayer_free(player);
        return NULL;
    }

    /* parse the circuit list */
    player->futureCircuits = oniontracefile_parseCircuits(otfile, &player->startTime);
    oniontracefile_free(otfile);

    if(!player->futureCircuits) {
        oniontraceplayer_free(player);
        critical("Error parsing circuit file, cannot proceed");
        return NULL;
    }

    message("%s: successfully parsed %i circuits from tracefile %s",
            player->id, g_queue_get_length(player->futureCircuits), filename);

    player->torctl = torctl;

    /* free circuits when they are removed from the active table */
    player->activeCircuits = g_hash_table_new_full(g_int_hash, g_int_equal, NULL,
            (GDestroyNotify)oniontracecircuit_free);
    /* don't free circuits when they are removed from the session table */
    player->circuitsWithSessions = g_hash_table_new(g_str_hash, g_str_equal);

    player->pendingCircuits = g_queue_new();

//    info("%s: here are the parsed circuits in order");
//    g_queue_foreach(player->futureCircuits, (GFunc)_oniontraceplayer_logCircuit, player);

    /* we will watch status on circuits and streams asynchronously.
     * set this before we tell Tor to stop attaching streams for us. */
    oniontracetorctl_setCircuitStatusCallback(player->torctl,
            (OnCircuitStatusFunc)_oniontraceplayer_onCircuitStatus, player);
    oniontracetorctl_setStreamStatusCallback(player->torctl,
            (OnStreamStatusFunc)_oniontraceplayer_onStreamStatus, player);

    /* set the config for Tor so streams stay unattached */
    oniontracetorctl_commandSetupTorConfig(player->torctl);

    /* start watching for circuit and stream events */
    oniontracetorctl_commandEnableEvents(player->torctl, "CIRC STREAM");

    /* the driver will drive the timer process so that we build
     * circuits according to the trace file */

    return player;
}

void oniontraceplayer_free(OnionTracePlayer* player) {
    g_assert(player);

    if(player->futureCircuits) {
        g_queue_free_full(player->futureCircuits, (GDestroyNotify)oniontracecircuit_free);
    }

    if(player->pendingCircuits) {
        g_queue_free_full(player->pendingCircuits, (GDestroyNotify)oniontracecircuit_free);
    }

    if(player->circuitsWithSessions) {
        g_hash_table_destroy(player->circuitsWithSessions);
    }

    if(player->activeCircuits) {
        g_hash_table_destroy(player->activeCircuits);
    }

    if(player->id) {
        g_free(player->id);
    }

    g_free(player);
}
