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

    GHashTable* circuitsBySession;

    struct {
        guint streamsAssigning;
        guint streamsAssigned;
        guint streamsSucceeded;
        guint streamsFailed;
        guint streamsDetached;
        guint circuitsBuilding;
        guint circuitsBuilt;
        guint circuitsFailed;
    } counts;
};

static void _oniontraceplayer_onStreamStatus(OnionTracePlayer* player,
        StreamStatus status, gint circuitID, gint streamID, gchar* username) {
    g_assert(player);

    /* note: the sourcePort is only valid for STREAM_STATUS_NEW, otherwise its 0 */

    switch(status) {
        case STREAM_STATUS_DETACHED:
            if(username) {
                OnionTraceCircuit* circuit = g_hash_table_lookup(player->circuitsBySession, username);
                if(circuit) {
                    player->counts.streamsDetached++;
                }
            }
            // no break, continue into the NEW case
        case STREAM_STATUS_NEW: {
            /* note: the provided circuitID is 0 here, because it doesn't have a circuit yet.
             * we need to figure out how to assign it. */
            if(!username) {
                /* let Tor do the assignment */
                info("%s: stream %i doesn't have a session id; let Tor attach it to a circuit",
                            player->id, streamID);

                oniontracetorctl_commandAttachStreamToCircuit(player->torctl, streamID, 0);

                break;
            }

            OnionTraceCircuit* circuit = g_hash_table_lookup(player->circuitsBySession, username);
            if(!circuit) {
                /* let Tor do the assignment */
                warning("%s: circuit missing for session %s; "
                        "asking Tor to choose how to attach stream %i to a circuit",
                        player->id, username, streamID);

                oniontracetorctl_commandAttachStreamToCircuit(player->torctl, streamID, 0);

                break;
            }

            /* now we have a circuit */
            const gchar* sessionID = oniontracecircuit_getSessionID(circuit);
            const gchar* circuitPath = oniontracecircuit_getPath(circuit);
            circuitID = oniontracecircuit_getCircuitID(circuit);

            info("%s: stream %i wants to attach to circuit %i for session %s on path %s",
                    player->id, streamID, circuitID, sessionID, circuitPath);

            CircuitStatus status = oniontracecircuit_getCircuitStatus(circuit);

            if(status == CIRCUIT_STATUS_NONE || status == CIRCUIT_STATUS_ASSIGNED) {
                info("%s: circuit %i is still initializing, queuing stream %i on session %s until circuit is built",
                        player->id, circuitID, streamID, sessionID);
                oniontracecircuit_addWaitingStreamID(circuit, streamID);
                player->counts.streamsAssigning++;
            } else if(status == CIRCUIT_STATUS_BUILT) {
                info("%s: circuit %i found for session %s; attaching stream %i now",
                        player->id, circuitID, sessionID, streamID);

                oniontracetorctl_commandAttachStreamToCircuit(player->torctl, streamID, circuitID);
                player->counts.streamsAssigned++;
            } else {
                error("%s: status unknown for circuit %i", circuitID);
            }

            break;
        }

        case STREAM_STATUS_FAILED: {
            if(username) {
                OnionTraceCircuit* circuit = g_hash_table_lookup(player->circuitsBySession, username);
                if(circuit) {
                    player->counts.streamsFailed++;
                }
            }
            break;
        }

        case STREAM_STATUS_SUCCEEDED: {
            if(username) {
                OnionTraceCircuit* circuit = g_hash_table_lookup(player->circuitsBySession, username);
                if(circuit) {
                    player->counts.streamsSucceeded++;
                }
            }
            break;
        }

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
            info("%s: circuit %i ASSIGNED", player->id, circuitID);

            /* if we build a custom circuit, Tor will assign your path a
             * circuit id and emit this status event to tell us the circuit id */
            OnionTraceCircuit* circuit = g_queue_pop_head(player->pendingCircuits);
            if(circuit) {
                /* we now save the circuit id */
                oniontracecircuit_setCircuitID(circuit, circuitID);
                oniontracecircuit_setCircuitStatus(circuit, CIRCUIT_STATUS_ASSIGNED);

                /* add all circuits to the active table */
                gint* circuitIDPtr = oniontracecircuit_getID(circuit);
                g_hash_table_replace(player->activeCircuits, circuitIDPtr, circuit);

                /* log progress */
                const gchar* sessionID = oniontracecircuit_getSessionID(circuit);
                const gchar* circuitPath = oniontracecircuit_getPath(circuit);
                info("%s: circuit %i is now building for session %s and path %s",
                        player->id, circuitID, sessionID, circuitPath);
            }

            break;
        }

        case CIRCUIT_STATUS_BUILT: {
            info("%s: circuit %i BUILT for path %s", player->id, circuitID, path);

            OnionTraceCircuit* circuit = g_hash_table_lookup(player->activeCircuits, &circuitID);
            if(circuit) {
                oniontracecircuit_setCircuitStatus(circuit, CIRCUIT_STATUS_BUILT);
                player->counts.circuitsBuilding--;
                player->counts.circuitsBuilt++;

                const gchar* sessionID = oniontracecircuit_getSessionID(circuit);

                /* track circuits by session id so we can assign streams later */
                if(sessionID != NULL) {
                    message("%s: circuit %i is ready for stream assignment on session %s and path %s",
                            player->id, circuitID, sessionID, path);

                    /* if we have waiting streams, assign them now */
                    GQueue* waitingStreams = oniontracecircuit_getWaitingStreamIDs(circuit);

                    while(waitingStreams != NULL && !g_queue_is_empty(waitingStreams)) {
                        gpointer streamIDPtr = g_queue_pop_head(waitingStreams);
                        gint streamID = GPOINTER_TO_INT(streamIDPtr);

                        info("%s: attaching waiting stream %i to circuit %i on session %s",
                                player->id, streamID, circuitID, sessionID);
                        oniontracetorctl_commandAttachStreamToCircuit(player->torctl, streamID, circuitID);

                        player->counts.streamsAssigning--;
                    }
                }
            }

            break;
        }

        case CIRCUIT_STATUS_FAILED:
            info("circuit %i FAILED for path %s", circuitID, path);

            /* try again if it's one of our circuits */
            OnionTraceCircuit* circuit = g_hash_table_lookup(player->activeCircuits, &circuitID);
            if(circuit) {
                player->counts.circuitsFailed++;

                const gchar* sessionID = oniontracecircuit_getSessionID(circuit);
                const gchar* circuitPath = oniontracecircuit_getPath(circuit);

                info("%s: retrying circuit %i on session %s and path %s",
                        player->id, circuitID, sessionID, circuitPath);

                /* create a new circuit and wait for the assigned circuit id */
                oniontracetorctl_commandBuildNewCircuit(player->torctl, circuitPath);

                /* the new circuit will get a new circuit id */
                oniontracecircuit_setCircuitID(circuit, 0);
                oniontracecircuit_setCircuitStatus(circuit, CIRCUIT_STATUS_NONE);

                /* move the circuit back into pending without freeing it */
                g_hash_table_steal(player->activeCircuits, &circuitID);
                g_queue_push_tail(player->pendingCircuits, circuit);

                break;
            }

            /* no break - continue to clear the original circuit */

        case CIRCUIT_STATUS_CLOSED: {
            info("circuit %i CLOSED for path %s", circuitID, path);

            OnionTraceCircuit* circuit = g_hash_table_lookup(player->activeCircuits, &circuitID);
            if(circuit) {
                const gchar* sessionID = oniontracecircuit_getSessionID(circuit);
                const gchar* circuitPath = oniontracecircuit_getPath(circuit);

                info("%s: clearing circuit %i on session %s and path %s",
                        player->id, circuitID, sessionID, circuitPath);

                if(sessionID) {
                    g_hash_table_remove(player->circuitsBySession, sessionID);
                }

                /* this will free the circuit */
                g_hash_table_remove(player->activeCircuits, &circuitID);
            }
            break;
        }

        case CIRCUIT_STATUS_EXTENDED:
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

    /* lets launch it 10 seconds early so its ready when we need it */
    launchtime->tv_sec -= 10;

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

        oniontracecircuit_setCircuitStatus(circuit, CIRCUIT_STATUS_NONE);
        const gchar* sessionID = oniontracecircuit_getSessionID(circuit);
        if(sessionID != NULL) {
            g_hash_table_replace(player->circuitsBySession, (gpointer)sessionID, circuit);
        }
        player->counts.circuitsBuilding++;

        message("%s: launched new circuit for session %s at "
                "%"G_GSIZE_FORMAT".%09"G_GSIZE_FORMAT" "
                "using launch offset "
                "%"G_GSIZE_FORMAT".%09"G_GSIZE_FORMAT" "
                "from start "
                "%"G_GSIZE_FORMAT".%09"G_GSIZE_FORMAT" "
                "circuit path is %s",
                player->id, sessionID,
                (gsize)now.tv_sec, (gsize)now.tv_nsec,
                (gsize)elapsed.tv_sec, (gsize)elapsed.tv_nsec,
                (gsize)player->startTime.tv_sec, (gsize)player->startTime.tv_nsec,
                path);
    }
}

gchar* oniontraceplayer_toString(OnionTracePlayer* player) {
    GString* string = g_string_new("");
    g_string_append_printf(string,
            "n_strms_assigning=%u n_strms_assigned=%u n_strms_succeeded=%u n_strms_failed=%u n_strms_detached=%u "
            "n_circs_building=%u n_circs_built=%u n_circs_failed=%u",
            player->counts.streamsAssigning, player->counts.streamsAssigned,
            player->counts.streamsSucceeded, player->counts.streamsFailed,
            player->counts.streamsDetached, player->counts.circuitsBuilding,
            player->counts.circuitsBuilt, player->counts.circuitsFailed);
    return g_string_free(string, FALSE);
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
    player->circuitsBySession = g_hash_table_new(g_str_hash, g_str_equal);
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

    if(player->circuitsBySession) {
        g_hash_table_destroy(player->circuitsBySession);
    }

    if(player->activeCircuits) {
        g_hash_table_destroy(player->activeCircuits);
    }

    if(player->id) {
        g_free(player->id);
    }

    g_free(player);
}
