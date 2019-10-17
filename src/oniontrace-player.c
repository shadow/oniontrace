/*
 * See LICENSE for licensing information
 */

#include "oniontrace.h"

typedef struct _Session {
    gchar* id;
    GQueue* circuitsSorted;
    GQueue* waitingStreamIDs;
} Session;

typedef struct _LaunchInfo {
    struct timespec abstime;
    Session* session;
} LaunchInfo;

struct _OnionTracePlayer {
    /* objects we don't own */
    OnionTraceTorCtl* torctl;

    /* objects/data we own */
    struct timespec startTime;

    gchar* id;
    GHashTable* sessions;
    GHashTable* circuits;

    GQueue* launches;

    Session* sessionAwaitingAssignment;
    GQueue* sessionAssignmentBacklog;

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

static Session* _oniontraceplayer_newSession(const gchar* sessionID) {
    Session* session = g_new0(Session, 1);
    session->id = g_strdup(sessionID);
    session->circuitsSorted = g_queue_new();
    session->waitingStreamIDs = g_queue_new();
    return session;
}

static void _oniontraceplayer_freeSession(Session* session) {
    if(session->circuitsSorted) {
        while(!g_queue_is_empty(session->circuitsSorted)) {
            OnionTraceCircuit* circuit = g_queue_pop_head(session->circuitsSorted);
            if(circuit) {
                oniontracecircuit_free(circuit);
            }
        }
        g_queue_free(session->circuitsSorted);
    }

    if(session->waitingStreamIDs) {
        g_queue_free(session->waitingStreamIDs);
    }

    if(session->id) {
        g_free(session->id);
    }

    g_free(session);
}

static OnionTraceCircuit* _oniontraceplayer_getCurrentCircuit(OnionTracePlayer* player, Session* session) {
    g_assert(player);
    g_assert(session);

    OnionTraceCircuit* circuit = g_queue_peek_head(session->circuitsSorted);
    g_assert(circuit);

    OnionTraceCircuit* nextCircuit = g_queue_peek_nth(session->circuitsSorted, 1);

    if(!nextCircuit) {
        return circuit;
    }

    /* what time is it now */
    struct timespec now;
    memset(&now, 0, sizeof(struct timespec));
    clock_gettime(CLOCK_REALTIME, &now);

    /* the absolute circuit launch time */
    struct timespec* nextTime = oniontracecircuit_getLaunchTime(nextCircuit);

    if(nextTime->tv_sec < now.tv_sec ||
            (nextTime->tv_sec == now.tv_sec && nextTime->tv_nsec <= now.tv_nsec)){
        /* we should now rotate to the next circuit */
        gint circuitID = oniontracecircuit_getCircuitID(circuit);

        info("%s: rotating from circuit %i to new circuit on session %s",
                player->id, circuitID, session->id);

        g_queue_pop_head(session->circuitsSorted);

        g_hash_table_remove(player->circuits, &circuitID);
        oniontracecircuit_free(circuit);

        return nextCircuit;
    } else {
        /* we stay with the current circuit and rotate to the next in the future */
        return circuit;
    }
}

static void _oniontraceplayer_handleSession(OnionTracePlayer* player, Session* session) {
    g_assert(player);
    g_assert(session);

    OnionTraceCircuit* circuit = _oniontraceplayer_getCurrentCircuit(player, session);
    gint circuitID = oniontracecircuit_getCircuitID(circuit);
    CircuitStatus status = oniontracecircuit_getCircuitStatus(circuit);

    if(status == CIRCUIT_STATUS_NONE) {
        if(player->sessionAwaitingAssignment && session == player->sessionAwaitingAssignment) {
            info("%s: waiting for circuit id assignment for session %s",
                    player->id, session->id);
        } else if(player->sessionAwaitingAssignment) {
            info("%s: session %s entering assignment backlog", player->id, session->id);
            g_queue_push_tail(player->sessionAssignmentBacklog, session);
        } else {
            /* launch new circuit */
            if(oniontracecircuit_getFailureCounter(circuit) >= 3) {
                oniontracetorctl_commandBuildNewCircuit(player->torctl, NULL);

                message("%s: launched new circuit on session %s without path "
                        "(original path failed too many times)",
                        player->id, session->id);
            } else {
                const gchar* path = oniontracecircuit_getPath(circuit);
                oniontracetorctl_commandBuildNewCircuit(player->torctl, path);

                message("%s: launched new circuit on session %s with path %s",
                        player->id, session->id, path ? path : "NULL");
            }
            player->counts.circuitsBuilding++;
            player->sessionAwaitingAssignment = session;

            /* update circuit status */
            oniontracecircuit_setCircuitStatus(circuit, CIRCUIT_STATUS_LAUNCHED);
        }
    } else if(status == CIRCUIT_STATUS_LAUNCHED) {
        info("%s: waiting for circuit id assignment for session %s",
                player->id, session->id);
    } else if(status == CIRCUIT_STATUS_ASSIGNED) {
        gint circuitID = oniontracecircuit_getCircuitID(circuit);

        info("%s: waiting for circuit %i to be built for session %s",
                player->id, circuitID, session->id);
    } else if(status == CIRCUIT_STATUS_BUILT) {
        while(!g_queue_is_empty(session->waitingStreamIDs)) {
            gint streamID = GPOINTER_TO_INT(g_queue_pop_head(session->waitingStreamIDs));

            if(streamID >= 0) {
                oniontracetorctl_commandAttachStreamToCircuit(player->torctl, streamID, circuitID);

                info("%s: assigned stream %i to circuit %i for session %s",
                        player->id, streamID, circuitID, session->id);
                player->counts.streamsAssigning--;
                player->counts.streamsAssigned++;
            } else {
                info("%s: preemptively built circuit %i for session %s",
                        player->id, circuitID, session->id);
            }
        }
    } else {
        gint circuitID = oniontracecircuit_getCircuitID(circuit);
        error("%s: status unknown for circuit %s on session %s", circuitID, session->id);
    }
}

static void _oniontraceplayer_handleSessionBacklog(OnionTracePlayer* player) {
    g_assert(player);

    while(!player->sessionAwaitingAssignment && !g_queue_is_empty(player->sessionAssignmentBacklog)) {
        Session* session = g_queue_pop_head(player->sessionAssignmentBacklog);
        _oniontraceplayer_handleSession(player, session);
    }
}

static void _oniontraceplayer_onStreamStatus(OnionTracePlayer* player,
        StreamStatus status, gint circuitID, gint streamID, gchar* username) {
    g_assert(player);

    /* note: the sourcePort is only valid for STREAM_STATUS_NEW, otherwise its 0 */

    /* do the session lookup */
    Session* session = NULL;
    if(username) {
        session = g_hash_table_lookup(player->sessions, username);
    }

    switch(status) {
        case STREAM_STATUS_DETACHED:
            if(session) {
                player->counts.streamsDetached++;
            }
            // no break, continue into the NEW case
        case STREAM_STATUS_NEW: {
            /* note: the provided circuitID is 0 here, because it doesn't have a circuit yet.
             * we need to figure out how to assign it. */


            /* if its not for a markov stream, let Tor do the assignment */
            if(!username) {
                info("%s: stream %i doesn't have a session id; let Tor attach it to a circuit",
                            player->id, streamID);

                oniontracetorctl_commandAttachStreamToCircuit(player->torctl, streamID, 0);

                break;
            }

            /* this could happen if the session never completed during the record phase, so
             * it never got recorded. */
            if(!session) {
                warning("%s: no session exists for %s; creating new session now",
                        player->id, username);

                session = _oniontraceplayer_newSession(username);
                g_hash_table_replace(player->sessions, session->id, session);
            }

            /* this could happen if an existing session ran out of circuits, or if we created a
             * new session from an id that we never seen before and so it has no circuits either */
            if(g_queue_is_empty(session->circuitsSorted)) {
                warning("%s: no circuit exists for session %s; creating new circuit now with NULL path",
                                        player->id, session->id);

                struct timespec now;
                memset(&now, 0, sizeof(struct timespec));
                clock_gettime(CLOCK_REALTIME, &now);

                OnionTraceCircuit* circuit = oniontracecircuit_new();
                oniontracecircuit_setSessionID(circuit, session->id);
                oniontracecircuit_setCircuitStatus(circuit, CIRCUIT_STATUS_NONE);
                oniontracecircuit_setLaunchTime(circuit, &now);

                g_queue_insert_sorted(session->circuitsSorted, circuit,
                        (GCompareDataFunc)oniontracecircuit_compareLaunchTime, NULL);
            }

            g_queue_push_tail(session->waitingStreamIDs, GINT_TO_POINTER(streamID));
            player->counts.streamsAssigning++;
            g_queue_push_tail(player->sessionAssignmentBacklog, session);
            _oniontraceplayer_handleSessionBacklog(player);

            break;
        }

        case STREAM_STATUS_FAILED: {
            if(session) {
                player->counts.streamsFailed++;
            }
            break;
        }

        case STREAM_STATUS_SUCCEEDED: {
            if(session) {
                player->counts.streamsSucceeded++;
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
            if(player->sessionAwaitingAssignment) {
                Session* session = player->sessionAwaitingAssignment;
                OnionTraceCircuit* circuit = g_queue_peek_head(session->circuitsSorted);

                /* we now save the circuit id */
                oniontracecircuit_setCircuitID(circuit, circuitID);
                oniontracecircuit_setCircuitStatus(circuit, CIRCUIT_STATUS_ASSIGNED);

                gint* circuitIDPtr = oniontracecircuit_getID(circuit);
                g_hash_table_replace(player->circuits, circuitIDPtr, circuit);

                info("%s: circuit %i assigned id on session %s", player->id, circuitID, session->id);

                player->sessionAwaitingAssignment = NULL;
                _oniontraceplayer_handleSessionBacklog(player);
            }

            break;
        }

        case CIRCUIT_STATUS_BUILT: {
            info("%s: circuit %i BUILT", player->id, circuitID);

            OnionTraceCircuit* circuit = g_hash_table_lookup(player->circuits, &circuitID);
            if(circuit) {
                player->counts.circuitsBuilding--;
                player->counts.circuitsBuilt++;
                oniontracecircuit_setCircuitStatus(circuit, CIRCUIT_STATUS_BUILT);

                /* now that its built, we can assign any waiting streams to it */
                const gchar* sessionID = oniontracecircuit_getSessionID(circuit);
                Session* session = g_hash_table_lookup(player->sessions, sessionID);

                if(session) {
                    message("%s: circuit %i is built for session %s and path %s",
                            player->id, circuitID, sessionID, path);
                    _oniontraceplayer_handleSession(player, session);
                }
            }

            break;
        }

        case CIRCUIT_STATUS_FAILED:
        case CIRCUIT_STATUS_CLOSED: {
            info("%s: circuit %i %s", player->id, circuitID,
                    status == CIRCUIT_STATUS_FAILED ? "FAILED" : "CLOSED");

            /* try again if it's one of our circuits */
            OnionTraceCircuit* circuit = g_hash_table_lookup(player->circuits, &circuitID);
            if(circuit) {
                if(status == CIRCUIT_STATUS_FAILED) {
                    player->counts.circuitsFailed++;
                    oniontracecircuit_incrementFailureCounter(circuit);
                }
                g_hash_table_remove(player->circuits, &circuitID);

                const gchar* sessionID = oniontracecircuit_getSessionID(circuit);
                Session* session = g_hash_table_lookup(player->sessions, sessionID);

                if(session) {
                    /* reset the circuit */
                    oniontracecircuit_setCircuitID(circuit, 0);
                    oniontracecircuit_setCircuitStatus(circuit, CIRCUIT_STATUS_NONE);

                    /* if we have waiting streams, we need to retry */
                    if(!g_queue_is_empty(session->waitingStreamIDs)) {
                        const gchar* circuitPath = oniontracecircuit_getPath(circuit);
                        info("%s: retrying circuit on session %s with path %s",
                                player->id, sessionID, circuitPath ? circuitPath : "NULL");

                        g_queue_push_tail(player->sessionAssignmentBacklog, session);
                        _oniontraceplayer_handleSessionBacklog(player);
                    }
                }
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
 * returns the relative time, which is 0 if we have no more circuits to build. */
struct timespec oniontraceplayer_launchNextCircuit(OnionTracePlayer* player) {
    g_assert(player);

    struct timespec nextLaunchTime;
    memset(&nextLaunchTime, 0, sizeof(struct timespec));

    LaunchInfo* launch = g_queue_peek_head(player->launches);
    if(!launch) {
        /* return 0 to stop trying to launch more */
        return nextLaunchTime;
    }

    /* what time is it now */
    struct timespec now;
    memset(&now, 0, sizeof(struct timespec));
    clock_gettime(CLOCK_REALTIME, &now);

    gboolean callHandleSession = FALSE;

    /* prepare to launch a circuit if its time to do so */
    while(launch != NULL && (launch->abstime.tv_sec < now.tv_sec ||
            (launch->abstime.tv_sec == now.tv_sec && launch->abstime.tv_nsec <= now.tv_nsec))) {
        /* the circuit should have been launched in the past or now.
         * use negative stream id to build circuit but skip the actual stream assignment */
        g_queue_push_tail(launch->session->waitingStreamIDs, GINT_TO_POINTER(-1));
        g_queue_push_tail(player->sessionAssignmentBacklog, launch->session);

        callHandleSession = TRUE;

        /* now update for the next circuit launch */
        g_free(g_queue_pop_head(player->launches));
        launch = g_queue_peek_head(player->launches);
    }

    if(callHandleSession) {
        _oniontraceplayer_handleSessionBacklog(player);
    }

    if(launch) {
        /* compute how much time we have from now until the next circuit should launch */
        oniontracetimer_timespecsubtract(&nextLaunchTime, &now, &launch->abstime);
    }

    return nextLaunchTime;
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

OnionTracePlayer* oniontraceplayer_new(OnionTraceTorCtl* torctl, const gchar* filename) {
    g_assert(torctl);

    struct timespec now;
    memset(&now, 0, sizeof(struct timespec));
    clock_gettime(CLOCK_REALTIME, &now);

    /* open the csv file containing the circuits */
    OnionTraceFile* otfile = oniontracefile_newReader(filename);

    if(!otfile) {
        critical("Error opening circuit file, cannot proceed");
        return NULL;
    }

    /* get the circuits we should create, sorted by launch times */
    GQueue* parsedCircuits = oniontracefile_parseCircuits(otfile, &now);
    oniontracefile_free(otfile);

    if(!parsedCircuits) {
        critical("Error parsing circuits, cannot proceed");
        return NULL;
    }

    OnionTracePlayer* player = g_new0(OnionTracePlayer, 1);
    player->startTime = now;
    player->torctl = torctl;

    player->sessions = g_hash_table_new(g_str_hash, g_str_equal);
    player->circuits = g_hash_table_new(g_int_hash, g_int_equal);
    player->launches = g_queue_new();

    player->sessionAssignmentBacklog = g_queue_new();

    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Player");
    player->id = g_string_free(idbuf, FALSE);

    guint numParsedCircuits = g_queue_get_length(parsedCircuits);
    guint numSessionCircuits = 0;

    /* store the circuits with session ids so we can build them when needed */
    while(!g_queue_is_empty(parsedCircuits)) {
        OnionTraceCircuit* circuit = g_queue_pop_head(parsedCircuits);

        const gchar* sessionID = oniontracecircuit_getSessionID(circuit);
        const gchar* path = oniontracecircuit_getPath(circuit);

        if(sessionID && path) {
            oniontracecircuit_setCircuitStatus(circuit, CIRCUIT_STATUS_NONE);

            /* store it in the appropriate session */
            Session* session = g_hash_table_lookup(player->sessions, sessionID);

            if(!session) {
                session = _oniontraceplayer_newSession(sessionID);
                g_hash_table_replace(player->sessions, session->id, session);
            }

            g_queue_insert_sorted(session->circuitsSorted, circuit,
                    (GCompareDataFunc)oniontracecircuit_compareLaunchTime, NULL);
            numSessionCircuits++;

            /* also store launch info so we can launch it before its needed */
            LaunchInfo* launch = g_new0(LaunchInfo, 1);
            launch->session = session;
            launch->abstime = *oniontracecircuit_getLaunchTime(circuit);

            /* we will try to build circuits preemptively */
            launch->abstime.tv_sec -= 10; /* build 10 seconds early */

            g_queue_push_tail(player->launches, launch);
        } else {
            /* there is no session id or path, so we do not need to track it */
            oniontracecircuit_free(circuit);
        }
    }

    g_queue_free(parsedCircuits);

    message("%s: successfully parsed %u circuits (%u with sessions) from tracefile %s",
            player->id, numParsedCircuits, numSessionCircuits, filename);

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

    return player;
}

void oniontraceplayer_free(OnionTracePlayer* player) {
    g_assert(player);

    if(player->circuits) {
        g_hash_table_destroy(player->circuits);
    }

    if(player->sessionAssignmentBacklog) {
        g_queue_free(player->sessionAssignmentBacklog);
    }

    if(player->launches) {
        g_queue_free_full(player->launches, g_free);
    }

    if(player->sessions) {
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init(&iter, player->sessions);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            Session* session = value;
            if(session) {
                _oniontraceplayer_freeSession(session);
            }
        }

        g_hash_table_destroy(player->sessions);
    }

    if(player->id) {
        g_free(player->id);
    }

    g_free(player);
}
