/*
 * See LICENSE for licensing information
 */

#include "oniontrace.h"

struct _OnionTracePlayer {
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

static void _oniontraceplayer_onStreamStatus(OnionTracePlayer* player,
        StreamStatus status, gint circuitID, gint streamID, gchar* username) {
    g_assert(player);

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

static void _oniontraceplayer_onCircuitStatus(OnionTracePlayer* player,
        CircuitStatus status, gint circuitID, gchar* path) {
    g_assert(player);

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

gchar* oniontraceplayer_toString(OnionTracePlayer* player) {
    return NULL;
}

OnionTracePlayer* oniontraceplayer_new(OnionTraceTorCtl* torctl, const gchar* filename) {
    OnionTracePlayer* player = g_new0(OnionTracePlayer, 1);

    player->torctl = torctl;

    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Player");
    player->id = g_string_free(idbuf, FALSE);

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

    return player;
}

void oniontraceplayer_free(OnionTracePlayer* player) {
    g_assert(player);

    if(player->id) {
        g_free(player->id);
    }

    g_free(player);
}
