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
                    info("%s: assigning stream %i on session %s to circuit %i",
                            player->id, streamID, username, sessionCircuitID);
                    oniontracetorctl_commandAttachStreamToCircuit(player->torctl, streamID, sessionCircuitID);
                } else {
                    /* let Tor do the assignment */
                    warning("%s: could not find circuit for stream %i on session %s, "
                            "letting Tor assign the stream to a circuit instead",
                            player->id, streamID, username);
                    oniontracetorctl_commandAttachStreamToCircuit(player->torctl, streamID, 0);
                }
            } else {
                /* let Tor do the assignment */
                info("%s: letting Tor assign stream %i without a session to any circuit",
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
             * circuit id and emit this status event to tell us */
            OnionTraceCircuit* circuit = g_queue_pop_head(player->pendingCircuits);
            if(circuit) {
                oniontracecircuit_setCircuitID(circuit, circuitID);

                const gchar* circuitPath = oniontracecircuit_getPath(circuit);
                info("%s: id %i ASSIGNED to circuit with path %s", player->id, circuitID, circuitPath);

                /* add all circuits to the active table */
                gint* id = oniontracecircuit_getID(circuit);
                g_hash_table_replace(player->activeCircuits, id, circuit);

                /* also track circuits with sessions so we can assign streams later */
                const gchar* sessionID = oniontracecircuit_getSessionID(circuit);
                if(sessionID) {
                    message("%s: id %i assigned to circuit on session %s with path %s",
                            player->id, circuitID, sessionID, circuitPath);
                    g_hash_table_replace(player->circuitsWithSessions, (gpointer)sessionID, circuit);
                }
            }

            break;
        }

        case CIRCUIT_STATUS_FAILED:
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
 * returns the time in reltime.
 * returns TRUE if we have more circuits to build, FALSE otherwise.
 */
gboolean oniontraceplayer_getNextCircuitLaunchTime(OnionTracePlayer* player, struct timespec* reltime) {
    g_assert(player);

    OnionTraceCircuit* circuit = g_queue_peek_head(player->futureCircuits);
    if(!circuit) {
        return FALSE;
    }

    /* the circuit launch time is relative to when we started */
    struct timespec* targetElapsed = oniontracecircuit_getLaunchTime(circuit);

    struct timespec now;
    struct timespec nowElapsed;
    memset(&now, 0, sizeof(struct timespec));
    memset(&nowElapsed, 0, sizeof(struct timespec));

    /* how much time has elapsed from when we started until now */
    clock_gettime(CLOCK_REALTIME, &now);
    oniontracetimer_timespecdiff(&nowElapsed, &player->startTime, &now);

    /* how much remaining elapsed time until the target */
    struct timespec remainingElapsed;
    memset(&remainingElapsed, 0, sizeof(struct timespec));
    gint neg = oniontracetimer_timespecdiff(&remainingElapsed, &nowElapsed, targetElapsed);

    /* only output if we have a non-NULL return variable */
    if (reltime) {
        if (neg) {
            /* just build now */
            memset(reltime, 0, sizeof(struct timespec));
        } else {
            /* build soon */
            *reltime = remainingElapsed;
        }
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
        clock_gettime(CLOCK_REALTIME, &now);
        struct timespec elapsed;
        oniontracetimer_timespecdiff(&elapsed, &player->startTime, &now);

        struct timespec* launchtime = oniontracecircuit_getLaunchTime(circuit);

        message("%s: at "
                "%"G_GSIZE_FORMAT".%09"G_GSIZE_FORMAT" "
                "launched circuit with offset "
                "%"G_GSIZE_FORMAT".%09"G_GSIZE_FORMAT" "
                "and path %s",
                player->id,
                (gsize)elapsed.tv_sec, (gsize)elapsed.tv_nsec,
                (gsize)launchtime->tv_sec, (gsize)launchtime->tv_nsec,
                path);
    }
}

gchar* oniontraceplayer_toString(OnionTracePlayer* player) {
    return NULL;
}

OnionTracePlayer* oniontraceplayer_new(OnionTraceTorCtl* torctl, const gchar* filename) {
    /* open the csv file containing the circuits */
    OnionTraceFile* otfile = oniontracefile_newReader(filename);
    if(!otfile) {
        critical("Error opening circuit file, cannot proceed");
        return NULL;
    }

    /* parse the circuit list */
    GQueue* circuits = oniontracefile_parseCircuits(otfile);
    oniontracefile_free(otfile);
    if(!circuits) {
        critical("Error parsing circuit file, cannot proceed");
        return NULL;
    }

    OnionTracePlayer* player = g_new0(OnionTracePlayer, 1);

    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Player");
    player->id = g_string_free(idbuf, FALSE);

    message("%s: successfully parsed %i circuits from tracefile %s",
            player->id, g_queue_get_length(circuits), filename);

    player->torctl = torctl;

    clock_gettime(CLOCK_REALTIME, &player->startTime);

    /* free circuits when they are removed from the active table */
    player->activeCircuits = g_hash_table_new_full(g_int_hash, g_int_equal, NULL,
            (GDestroyNotify)oniontracecircuit_free);
    /* don't free circuits when they are removed from the session table */
    player->circuitsWithSessions = g_hash_table_new(g_str_hash, g_str_equal);

    player->futureCircuits = circuits;
    player->pendingCircuits = g_queue_new();

    /* we will watch status on circuits and streams asynchronously.
     * set this before we tell Tor to stop attaching streams for us. */
    oniontracetorctl_setCircuitStatusCallback(player->torctl,
            (OnCircuitStatusFunc)_oniontraceplayer_onCircuitStatus, player);
    oniontracetorctl_setStreamStatusCallback(player->torctl,
            (OnStreamStatusFunc)_oniontraceplayer_onStreamStatus, player);

    /* set the config for Tor so streams stay unattached */
    oniontracetorctl_commandSetupTorConfig(player->torctl);

    /* start watching for circuit and stream events */
    oniontracetorctl_commandEnableEvents(player->torctl);

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
