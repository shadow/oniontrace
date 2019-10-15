/*
 * See LICENSE for licensing information
 */

#include "oniontrace.h"

typedef enum {
    TORCTL_NONE, TORCTL_AUTHENTICATE, TORCTL_BOOTSTRAP, TORCTL_PROCESSING
} TorCtlState;

struct _OnionTraceTorCtl {
    OnionTraceEventManager* manager;

    /* controlling the tor client */
    gint descriptor;
    in_port_t controlClientPort;
    TorCtlState state;
    GQueue* commands;

    /* flag used for watch bootstrapping status */
    gboolean isStatusEventSet;

    /* flags used for parsing descriptors */
    gboolean waitingGetDescriptorsResponse;
    gboolean currentlyReceivingDescriptors;
    GQueue* descriptorLines;

    /* flags used for parsing circuits */
    gboolean circuitStatusCleanup;
    gboolean waitingCircuitStatusResponse;
    gboolean currentlyReceivingCircuitStatuses;
    GQueue* circuitStatusLines;

    GString* receiveLineBuffer;

    OnConnectedFunc onConnected;
    gpointer onConnectedArg;
    OnAuthenticatedFunc onAuthenticated;
    gpointer onAuthenticatedArg;
    OnBootstrappedFunc onBootstrapped;
    gpointer onBootstrappedArg;
    OnDescriptorsReceivedFunc onDescriptorsReceived;
    gpointer onDescriptorsReceivedArg;
    OnCircuitStatusFunc onCircuitStatus;
    gpointer onCircuitStatusArg;
    OnStreamStatusFunc onStreamStatus;
    gpointer onStreamStatusArg;
    OnLineReceivedFunc onLineReceived;
    gpointer onLineReceivedArg;

    gchar* id;
};

static void _oniontracetorctl_commandWatchBootstrapStatus(OnionTraceTorCtl* torctl);

static gint _oniontracetorctl_parseCode(gchar* line) {
    gchar** parts1 = g_strsplit(line, " ", 0);
    gchar** parts2 = g_strsplit_set(parts1[0], "-+", 0);
    gint code = atoi(parts2[0]);
    g_strfreev(parts1);
    g_strfreev(parts2);
    return code;
}

static gint _oniontracetorctl_parseBootstrapProgress(gchar* line) {
    gint progress = -1;
    gchar** parts = g_strsplit(line, " ", 0);
    gchar* part = NULL;
    gboolean foundBootstrap = FALSE;
    for(gint j = 0; (part = parts[j]) != NULL; j++) {
        gchar** subparts = g_strsplit(part, "=", 0);
        if(!g_ascii_strncasecmp(subparts[0], "BOOTSTRAP", 9)) {
            foundBootstrap = TRUE;
        } else if(foundBootstrap && !g_ascii_strncasecmp(subparts[0], "PROGRESS", 8)) {
            progress = atoi(subparts[1]);
        }
        g_strfreev(subparts);
    }
    g_strfreev(parts);
    return progress;
}

static void _oniontracetorctl_processDescriptorLine(OnionTraceTorCtl* torctl, GString* linebuf) {
    /* handle descriptor info */
    if(!torctl->descriptorLines &&
            !g_ascii_strncasecmp(linebuf->str, "250+ns/all=", MIN(linebuf->len, 11))) {
        info("%s: 'GETINFO ns/all\\r\\n' command successful, descriptor response coming next", torctl->id);
        torctl->descriptorLines = g_queue_new();
    }

    if(torctl->descriptorLines) {
        /* descriptors coming */
        if(!g_ascii_strncasecmp(linebuf->str, "250+ns/all=", MIN(linebuf->len, 11))) {
            /* header */
            debug("%s: got descriptor response header '%s'", torctl->id, linebuf->str);
            torctl->currentlyReceivingDescriptors = TRUE;
            torctl->waitingGetDescriptorsResponse = FALSE;
        } else if(linebuf->str[0] == '.') {
            /* footer */
            debug("%s: got descriptor response footer '%s'", torctl->id, linebuf->str);
        } else if(!g_ascii_strncasecmp(linebuf->str, "250 OK", MIN(linebuf->len, 6))) {
            /* all done with descriptors */
            info("%s: finished getting descriptors with success code '%s'", torctl->id, linebuf->str);

            torctl->currentlyReceivingDescriptors = FALSE;

            if(torctl->onDescriptorsReceived) {
                torctl->onDescriptorsReceived(torctl->onDescriptorsReceivedArg, torctl->descriptorLines);
            }

            /* clean up any leftover lines */
            while(g_queue_get_length(torctl->descriptorLines) > 0) {
                g_free(g_queue_pop_head(torctl->descriptorLines));
            }
            g_queue_free(torctl->descriptorLines);
            torctl->descriptorLines = NULL;
        } else {
            /* real descriptor lines */
            g_queue_push_tail(torctl->descriptorLines, g_strdup(linebuf->str));
        }
    }
}

static void _oniontracetorctl_handleCircuitStatuses(OnionTraceTorCtl* torctl) {
    if(!torctl || !torctl->circuitStatusLines) {
        return;
    }

    /* if there is no callback set, we will take no action so no need to parse anything */
    if(!torctl->onCircuitStatus) {
        return;
    }

    while(g_queue_get_length(torctl->circuitStatusLines) > 0) {
        gchar* line = g_queue_pop_head(torctl->circuitStatusLines);
        info("GETINFO circuit-status result: %s", line);

        gint circuitID = 0;
        gchar* path = NULL;

        gchar** parts = g_strsplit(line, " ", 0);
        if(parts[0] != NULL) {
            circuitID = atoi(parts[0]);
        }
        if(parts[2] != NULL) {
            path = g_strdup(parts[2]);
        }
        g_strfreev(parts);

        if(torctl->circuitStatusCleanup) {
            /* simulate a close event so recorder can clean up circuit */
            torctl->onCircuitStatus(torctl->onCircuitStatusArg, CIRCUIT_STATUS_CLOSED, circuitID, path);
        } else {
            /* simulate a create event so recorder can log circuit */
            torctl->onCircuitStatus(torctl->onCircuitStatusArg, CIRCUIT_STATUS_ASSIGNED, circuitID, NULL);
            torctl->onCircuitStatus(torctl->onCircuitStatusArg, CIRCUIT_STATUS_BUILT, circuitID, path);
        }

        if(path != NULL) {
            g_free(path);
        }
    }
}

static void _oniontracetorctl_processCircuitStatusLine(OnionTraceTorCtl* torctl, GString* linebuf) {
    /* handle descriptor info */
    if(!torctl->circuitStatusLines &&
            !g_ascii_strncasecmp(linebuf->str, "250+circuit-status=", MIN(linebuf->len, 19))) {
        info("%s: 'GETINFO circuit-status\\r\\n' command successful, circuit-status coming next", torctl->id);
        torctl->circuitStatusLines = g_queue_new();
    }

    if(torctl->circuitStatusLines) {
        /* circuits coming */
        if(!g_ascii_strncasecmp(linebuf->str, "250+circuit-status=", MIN(linebuf->len, 19))) {
            /* header */
            debug("%s: got circuit-status response header '%s'", torctl->id, linebuf->str);
            torctl->currentlyReceivingCircuitStatuses = TRUE;
            torctl->waitingCircuitStatusResponse = FALSE;
        } else if(linebuf->str[0] == '.') {
            /* footer */
            debug("%s: got circuit-status response footer '%s'", torctl->id, linebuf->str);
        } else if(!g_ascii_strncasecmp(linebuf->str, "250 OK", MIN(linebuf->len, 6))) {
            /* all done with descriptors */
            info("%s: finished getting circuit-status with success code '%s'", torctl->id, linebuf->str);

            torctl->currentlyReceivingCircuitStatuses = FALSE;

            _oniontracetorctl_handleCircuitStatuses(torctl);

            /* clean up any leftover lines */
            while(g_queue_get_length(torctl->circuitStatusLines) > 0) {
                g_free(g_queue_pop_head(torctl->circuitStatusLines));
            }
            g_queue_free(torctl->circuitStatusLines);
            torctl->circuitStatusLines = NULL;
        } else {
            /* real descriptor lines */
            g_queue_push_tail(torctl->circuitStatusLines, g_strdup(linebuf->str));
        }
    }
}

static in_port_t _oniontracetorctl_scanSourcePort(gchar** parts) {
    in_port_t sourcePort = 0;
    for(gint i = 0; parts != NULL && parts[i] != NULL; i++) {
        if(!g_ascii_strncasecmp(parts[i], "SOURCE_ADDR=", 12)) {
            gchar** sourceParts = g_strsplit(&parts[i][12], ":", 0);

            if(sourceParts[0] && sourceParts[1]) {
                sourcePort = (in_port_t)atoi(sourceParts[1]);
            }

            g_strfreev(sourceParts);
        }
    }
    return sourcePort;
}

static gchar* _oniontracetorctl_scanUsername(gchar** parts) {
    gchar* username = NULL;
    for(gint i = 0; parts != NULL && parts[i] != NULL; i++) {
        if(!g_ascii_strncasecmp(parts[i], "USERNAME=", 9)) {
            username = g_strdup(&parts[i][9]);
        }
    }
    return username;
}

static StreamStatus _oniontracetorctl_parseStreamStatus(gchar* statusStr) {
    if(statusStr != NULL) {
        if(!g_ascii_strncasecmp(statusStr, "NEW", 3)) {
            return STREAM_STATUS_NEW;
        } else if(!g_ascii_strncasecmp(statusStr, "SUCCEEDED", 3)) {
            return STREAM_STATUS_SUCCEEDED;
        } else if(!g_ascii_strncasecmp(statusStr, "DETACHED", 3)) {
            return STREAM_STATUS_DETACHED;
        } else if(!g_ascii_strncasecmp(statusStr, "FAILED", 3)) {
            return STREAM_STATUS_FAILED;
        } else if(!g_ascii_strncasecmp(statusStr, "CLOSED", 3)) {
            return STREAM_STATUS_CLOSED;
        }
    }
    return STREAM_STATUS_NONE;
}

static CircuitStatus _oniontracetorctl_parseCircuitStatus(gchar* statusStr) {
    if(statusStr != NULL) {
        if(!g_ascii_strncasecmp(statusStr, "LAUNCHED", 3)) {
            return CIRCUIT_STATUS_LAUNCHED;
        } else if(!g_ascii_strncasecmp(statusStr, "EXTENDED", 3)) {
            return CIRCUIT_STATUS_EXTENDED;
        } else if(!g_ascii_strncasecmp(statusStr, "BUILT", 3)) {
            return CIRCUIT_STATUS_BUILT;
        } else if(!g_ascii_strncasecmp(statusStr, "FAILED", 3)) {
            return CIRCUIT_STATUS_FAILED;
        } else if(!g_ascii_strncasecmp(statusStr, "CLOSED", 3)) {
            return CIRCUIT_STATUS_CLOSED;
        }
    }
    return CIRCUIT_STATUS_NONE;
}

static void _oniontracetorctl_processLineHelper(OnionTraceTorCtl* torctl, GString* linebuf) {
    /* circuit responses that we care about:
     *   250 EXTENDED 3    (in response to an EXTEND command)
     *   650 CIRC 3 LAUNCHED ...
     *   650 CIRC 3 EXTENDED
     *   650 CIRC 3 BUILT <path> ...
     *   650 CIRC 3 FAILED <path> ...
     *   650 CIRC 3 CLOSED <path> ...
     *
     *   where <path> is like:
     *   $FF197204099FA0E507FA46D41FED97D3337B4BAA~guard,$F63C257B0819549FCD3E476FB534C08E550AC29D~middle,$4EBB385C80A2CA5D671E16F1C722FBFB5F176891~exit
     *
     * stream responses that we care about:
     *   650 STREAM 21 NEW 0 11.0.0.6:18080 SOURCE_ADDR=127.0.0.1:21437 ...
     *   650 STREAM 21 SUCCEEDED 20 11.0.0.6:18080
     *   650 STREAM 21 DETACHED 20 11.0.0.6:18080 ...
     *   650 STREAM 21 FAILED 20 11.0.0.6:18080
     *   650 STREAM 21 CLOSED 20 11.0.0.6:18080 ...
     */

    if(torctl->currentlyReceivingDescriptors) {
        _oniontracetorctl_processDescriptorLine(torctl, linebuf);
        return;
    } else if(torctl->currentlyReceivingCircuitStatuses) {
        _oniontracetorctl_processCircuitStatusLine(torctl, linebuf);
        return;
    }

    gint code = _oniontracetorctl_parseCode(linebuf->str);

    if(code == 250) {
        if(torctl->waitingGetDescriptorsResponse &&
                !g_ascii_strncasecmp(linebuf->str, "250+ns/all=", MIN(linebuf->len, 11))) {
            _oniontracetorctl_processDescriptorLine(torctl, linebuf);
        } else if(torctl->waitingCircuitStatusResponse &&
                !g_ascii_strncasecmp(linebuf->str, "250+circuit-status=", MIN(linebuf->len, 19))) {
            _oniontracetorctl_processCircuitStatusLine(torctl, linebuf);
        } else if(!g_ascii_strncasecmp(linebuf->str, "250 EXTENDED ", MIN(linebuf->len, 13))) {
            gchar** parts = g_strsplit(linebuf->str, " ", 0);

            gint circuitID = 0;

            if(parts[0] && parts[1] && parts[2]) {
                circuitID = atoi(parts[2]);
            }

            g_strfreev(parts);

            if(torctl->onCircuitStatus) {
                torctl->onCircuitStatus(torctl->onCircuitStatusArg, CIRCUIT_STATUS_ASSIGNED, circuitID, NULL);
            }
        }
    } else if(code == 650) {
        /* ignore internal .exit streams/circuits */
        if(g_strstr_len(linebuf->str, linebuf->len, ".exit")) {
            debug("%s: ignoring tor-internal response '%s'", torctl->id, linebuf->str);
            return;
        }

        if(!g_ascii_strncasecmp(linebuf->str, "650 CIRC ", MIN(linebuf->len, 9))) {
            gchar** parts = g_strsplit(linebuf->str, " ", 0);

            gint circuitID = 0;
            CircuitStatus status = CIRCUIT_STATUS_NONE;
            gchar* path = NULL;

            if(parts[0] && parts[1] && parts[2]) {
                circuitID = atoi(parts[2]);
                if(parts[3]) {
                    status = _oniontracetorctl_parseCircuitStatus(parts[3]);

                    /* get path if we can */
                    if(status == CIRCUIT_STATUS_EXTENDED ||
                            status == CIRCUIT_STATUS_BUILT ||
                            status == CIRCUIT_STATUS_CLOSED) {
                        if(parts[4]) {
                            path = g_strdup(parts[4]);
                        }
                    }
                }
            }

            g_strfreev(parts);

            if(torctl->onCircuitStatus) {
                torctl->onCircuitStatus(torctl->onCircuitStatusArg, status, circuitID, path);
            }
            if(path != NULL) {
                g_free(path);
            }
        } else if(!g_ascii_strncasecmp(linebuf->str, "650 STREAM ", MIN(linebuf->len, 11))) {
            gchar** parts = g_strsplit(linebuf->str, " ", 0);

            gint streamID = 0, circuitID = 0;
            in_port_t clientPort = 0;
            StreamStatus status = STREAM_STATUS_NONE;
            gchar* username = NULL;

            if(parts[0] && parts[1] && parts[2]) {
                streamID = atoi(parts[2]);
                if(parts[3] && parts[4]) {
                    status = _oniontracetorctl_parseStreamStatus(parts[3]);
                    circuitID = atoi(parts[4]);
                    /* note: the sourcePort is only valid for STREAM_STATUS_NEW, otherwise its 0 */
                    clientPort = _oniontracetorctl_scanSourcePort(&parts[5]);
                    username = _oniontracetorctl_scanUsername(&parts[5]);
                }
            }

            g_strfreev(parts);

            if(torctl->onStreamStatus) {
                torctl->onStreamStatus(torctl->onStreamStatusArg, status, circuitID, streamID, username);
            }
            if(username) {
                g_free(username);
            }
        }
    } else {
        debug("%s: ignoring code %i", torctl->id, code);
    }
}

static void _oniontracetorctl_processLine(OnionTraceTorCtl* torctl, GString* linebuf) {
    switch(torctl->state) {

        case TORCTL_AUTHENTICATE: {
            gint code = _oniontracetorctl_parseCode(linebuf->str);
            if(code == 250) {
                info("%s: successfully received auth response '%s'", torctl->id, linebuf->str);

                if(torctl->onAuthenticated) {
                    torctl->onAuthenticated(torctl->onAuthenticatedArg);
                }
            } else {
                critical("%s: received failed auth response '%s'", torctl->id, linebuf->str);
            }
            break;
        }

        case TORCTL_BOOTSTRAP: {
            /* we will be getting all client status events, not all of them have bootstrap status */
            gint progress = _oniontracetorctl_parseBootstrapProgress(linebuf->str);
            if(progress >= 0) {
                info("%s: successfully received bootstrap phase response '%s'", torctl->id, linebuf->str);
                if(progress >= 100) {
                    message("%s: torctl client is now ready (Bootstrapped 100)", torctl->id);

                    torctl->isStatusEventSet = FALSE;
                    torctl->state = TORCTL_PROCESSING;

                    if(torctl->onBootstrapped) {
                        torctl->onBootstrapped(torctl->onBootstrappedArg);
                    }
                } else {
                    info("%s: Tor has not yet fully bootstrapped (currently at %i/100)", torctl->id, progress);

                    if(!(torctl->isStatusEventSet)) {
                        info("%s: Registering an async status event to wait for bootstrapping", torctl->id);
                        _oniontracetorctl_commandWatchBootstrapStatus(torctl);
                        torctl->isStatusEventSet = TRUE;
                    } else {
                        info("%s: Async status event is already registered, we will continue waiting", torctl->id);
                    }
                }
            }
            break;
        }

        case TORCTL_PROCESSING: {
            /* if someone (the logger) wants the raw line, send it */
            if(torctl->onLineReceived) {
                torctl->onLineReceived(torctl->onLineReceivedArg, linebuf->str);
            }

            /* we only need to parse the line if we actually have a function that cares about it */
            if(torctl->onDescriptorsReceived || torctl->onCircuitStatus || torctl->onStreamStatus) {
                _oniontracetorctl_processLineHelper(torctl, linebuf);
            }
            break;
        }

        case TORCTL_NONE:
        default:
            /* this should never happen */
            g_assert(FALSE);
            break;
    }
}

static void _oniontracetorctl_receiveLines(OnionTraceTorCtl* torctl, OnionTraceEventFlag eventType) {
    g_assert(torctl);

    if(eventType & ONIONTRACE_EVENT_READ) {
        debug("%s: descriptor %i is readable", torctl->id, torctl->descriptor);

        gchar recvbuf[10240];
        memset(recvbuf, 0, 10240);
        gssize bytes = 0;

        while((bytes = recv(torctl->descriptor, recvbuf, 10000, 0)) > 0) {
            recvbuf[bytes] = '\0';
            debug("%s: recvbuf:%s", torctl->id, recvbuf);

            gboolean isLastLineIncomplete = FALSE;
            if(bytes < 2 || recvbuf[bytes-2] != '\r' || recvbuf[bytes-1] != '\n') {
                isLastLineIncomplete = TRUE;
            }

            //Check for corner case where first element is \r\n
            gboolean isStartCRLF = FALSE;
            if(recvbuf[0] == '\r' && recvbuf[1] == '\n') {
                isStartCRLF = TRUE;
            }

            gchar** lines = g_strsplit(recvbuf, "\r\n", 0);
            gchar* line = NULL;
            for(gint i = 0; (line = lines[i]) != NULL; i++) {
                if(!torctl->receiveLineBuffer) {
                    torctl->receiveLineBuffer = g_string_new(line);
                } else if (isStartCRLF && i == 0 &&
                        !g_ascii_strcasecmp(line, "")) {
                    /* do nothing; we want to process the line in buffer already */
                } else {
                    g_string_append_printf(torctl->receiveLineBuffer, "%s", line);
                }

                if(!(isStartCRLF && i == 0) && (!g_ascii_strcasecmp(line, "") ||
                        (isLastLineIncomplete && lines[i+1] == NULL))) {
                    /* this is '', or the last line, and its not all here yet */
                    continue;
                } else {
                    /* we have a full line in our buffer */
                    debug("%s: received '%s'", torctl->id, torctl->receiveLineBuffer->str);

                    _oniontracetorctl_processLine(torctl, torctl->receiveLineBuffer);

                    g_string_free(torctl->receiveLineBuffer, TRUE);
                    torctl->receiveLineBuffer = NULL;
                }
            }
            g_strfreev(lines);
        }
    }
}

static void _oniontracetorctl_flushCommands(OnionTraceTorCtl* torctl, OnionTraceEventFlag eventType) {
    g_assert(torctl);

    oniontraceeventmanager_deregister(torctl->manager, torctl->descriptor);

    /* send all queued commands */
    if(eventType & ONIONTRACE_EVENT_WRITE) {
        debug("%s: descriptor %i is writable", torctl->id, torctl->descriptor);

        while(!g_queue_is_empty(torctl->commands)) {
            GString* command = g_queue_pop_head(torctl->commands);

            gssize bytes = send(torctl->descriptor, command->str, command->len, 0);

            if(bytes > 0) {
                /* at least some parts of the command were sent successfully */
                GString* sent = g_string_new(command->str);
                sent = g_string_truncate(sent, bytes);
                debug("%s: sent '%s'", torctl->id, g_strchomp(sent->str));
                g_string_free(sent, TRUE);
            } else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                warning("%s: problem writing to descriptor %i: error %i: %s",
                        torctl->id, torctl->descriptor, errno, g_strerror(errno));
            }

            if(bytes == command->len) {
                g_string_free(command, TRUE);
            } else {
                /* partial or no send */
                command = g_string_erase(command, (gssize)0, (gssize)bytes);
                g_queue_push_head(torctl->commands, command);
                break;
            }
        }
    }

    gboolean success = TRUE;

    if(g_queue_is_empty(torctl->commands)) {
        /* we wrote all of the commands, go back into reading mode */
        success = oniontraceeventmanager_register(torctl->manager, torctl->descriptor, ONIONTRACE_EVENT_READ,
                (OnionTraceOnEventFunc)_oniontracetorctl_receiveLines, torctl);
    } else {
        /* we still want to write more */
        success = oniontraceeventmanager_register(torctl->manager, torctl->descriptor, ONIONTRACE_EVENT_WRITE,
                (OnionTraceOnEventFunc)_oniontracetorctl_flushCommands, torctl);
    }

    if(!success) {
        warning("%s: Unable to register descriptor %i with event manager", torctl->id, torctl->descriptor);
    }

}

static void _oniontracetorctl_onConnected(OnionTraceTorCtl* torctl, OnionTraceEventFlag type) {
    g_assert(torctl);
    oniontraceeventmanager_deregister(torctl->manager, torctl->descriptor);
    if(torctl->onConnected) {
        torctl->onConnected(torctl->onConnectedArg);
    }
}

OnionTraceTorCtl* oniontracetorctl_new(OnionTraceEventManager* manager, in_port_t controlPort,
        OnConnectedFunc onConnected, gpointer onConnectedArg) {
    OnionTraceTorCtl* torctl = g_new0(OnionTraceTorCtl, 1);

    torctl->manager = manager;
    torctl->commands = g_queue_new();

    /* set our ID string for logging purposes */
    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Controller");
    torctl->id = idbuf->str;

    /* create the control socket */
    torctl->descriptor = socket(AF_INET, (SOCK_STREAM | SOCK_NONBLOCK), 0);

    /* check for error */
    if(torctl->descriptor < 0) {
        critical("%s: error %i in socket(): %s", torctl->id, errno, g_strerror(errno));
        g_string_free(idbuf, FALSE);
        oniontracetorctl_free(torctl);
        return NULL;
    }

    /* connect to the control port */
    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);;
    serverAddress.sin_port = htons(controlPort);

    /* connect to server. since we are non-blocking, we expect this to return EINPROGRESS */
    gint res = connect(torctl->descriptor, (struct sockaddr *) &serverAddress, sizeof(serverAddress));
    if (res < 0 && errno != EINPROGRESS) {
        critical("%s: error %i in connect(): %s", torctl->id, errno, g_strerror(errno));
        g_string_free(idbuf, FALSE);
        oniontracetorctl_free(torctl);
        return NULL;
    }

    /* we want to get client side name info for the control socket */
    struct sockaddr name;
    memset(&name, 0, sizeof(struct sockaddr));
    socklen_t nameLen = (socklen_t)sizeof(struct sockaddr);

    /* get the socket name, i.e., address and port */
    gint result = getsockname(torctl->descriptor, &name, &nameLen);

    /* check for sockname error */
    if(result < 0) {
        warning("%s: unable to get client port on Control socket: error in getsockname", torctl->id);
        g_string_free(idbuf, FALSE);
        oniontracetorctl_free(torctl);
        return NULL;
    }

    torctl->controlClientPort = (in_port_t)ntohs(((struct sockaddr_in*)&name)->sin_port);

    g_string_append_printf(idbuf, "-%u", torctl->controlClientPort);
    torctl->id = g_string_free(idbuf, FALSE);

    /* get notified when the connection succeeds */
    torctl->onConnected = onConnected;
    torctl->onConnectedArg = onConnectedArg;
    gboolean success = oniontraceeventmanager_register(torctl->manager, torctl->descriptor, ONIONTRACE_EVENT_WRITE,
            (OnionTraceOnEventFunc)_oniontracetorctl_onConnected, torctl);

    if(!success) {
        critical("%s: unable to register descriptor %i with event manager", torctl->id, torctl->descriptor);
        oniontracetorctl_free(torctl);
        return NULL;
    }

    return torctl;
}

void oniontracetorctl_free(OnionTraceTorCtl* torctl) {
    g_assert(torctl);

    /* make sure we dont get a callback on our torctl instance which we are about to free and invalidate */
    oniontraceeventmanager_deregister(torctl->manager, torctl->descriptor);

    if(torctl->descriptor) {
        close(torctl->descriptor);
    }

    if(torctl->receiveLineBuffer) {
        g_string_free(torctl->receiveLineBuffer, TRUE);
    }

    if(torctl->commands) {
        while(!g_queue_is_empty(torctl->commands)) {
            g_string_free(g_queue_pop_head(torctl->commands), TRUE);
        }
        g_queue_free(torctl->commands);
    }

    if(torctl->id) {
        g_free(torctl->id);
    }

    g_free(torctl);
}

in_port_t oniontracetorctl_getControlClientPort(OnionTraceTorCtl* torctl) {
    g_assert(torctl);
    return torctl->controlClientPort;
}

void oniontracetorctl_setCircuitStatusCallback(OnionTraceTorCtl* torctl,
        OnCircuitStatusFunc onCircuitStatus, gpointer onCircuitStatusArg) {
    g_assert(torctl);
    torctl->onCircuitStatus = onCircuitStatus;
    torctl->onCircuitStatusArg = onCircuitStatusArg;
}

void oniontracetorctl_setStreamStatusCallback(OnionTraceTorCtl* torctl,
        OnStreamStatusFunc onStreamStatus, gpointer onStreamStatusArg) {
    g_assert(torctl);
    torctl->onStreamStatus = onStreamStatus;
    torctl->onStreamStatusArg = onStreamStatusArg;
}

void oniontracetorctl_setLineReceivedCallback(OnionTraceTorCtl* torctl,
        OnLineReceivedFunc onLineReceived, gpointer onLineReceivedArg) {
    g_assert(torctl);
    torctl->onLineReceived = onLineReceived;
    torctl->onLineReceivedArg = onLineReceivedArg;
}

static void _oniontracetorctl_commandHelperV(OnionTraceTorCtl* torctl, const gchar *format, va_list vargs) {
    g_assert(torctl);

    GString* command = g_string_new(NULL);
    g_string_append_vprintf(command, format, vargs);
    g_queue_push_tail(torctl->commands, command);

    debug("%s: queued torctl command '%s'", torctl->id, command->str);

    /* send the commands */
    _oniontracetorctl_flushCommands(torctl, ONIONTRACE_EVENT_NONE);
}

static void _oniontracetorctl_commandHelper(OnionTraceTorCtl* torctl, const gchar *format, ...) {
    va_list vargs;
    va_start(vargs, format);
    _oniontracetorctl_commandHelperV(torctl, format, vargs);
    va_end(vargs);
}

void oniontracetorctl_commandAuthenticate(OnionTraceTorCtl* torctl,
        OnAuthenticatedFunc onAuthenticated, gpointer onAuthenticatedArg) {
    g_assert(torctl);

    torctl->onAuthenticated = onAuthenticated;
    torctl->onAuthenticatedArg = onAuthenticatedArg;
    torctl->state = TORCTL_AUTHENTICATE;

    /* our control socket is connected, authenticate to control port */
    _oniontracetorctl_commandHelper(torctl, "AUTHENTICATE \"password\"\r\n");
}

void oniontracetorctl_commandGetBootstrapStatus(OnionTraceTorCtl* torctl,
        OnBootstrappedFunc onBootstrapped, gpointer onBootstrappedArg) {
    g_assert(torctl);

    torctl->onBootstrapped = onBootstrapped;
    torctl->onBootstrappedArg = onBootstrappedArg;
    torctl->state = TORCTL_BOOTSTRAP;

    _oniontracetorctl_commandHelper(torctl, "GETINFO status/bootstrap-phase\r\n");
}

static void _oniontracetorctl_commandWatchBootstrapStatus(OnionTraceTorCtl* torctl) {
    g_assert(torctl);
    _oniontracetorctl_commandHelper(torctl, "SETEVENTS EXTENDED STATUS_CLIENT\r\n");
}

void oniontracetorctl_commandSetupTorConfig(OnionTraceTorCtl* torctl) {
    g_assert(torctl);
    _oniontracetorctl_commandHelper(torctl, "SETCONF __LeaveStreamsUnattached=1 __DisablePredictedCircuits=1 MaxCircuitDirtiness=1200 CircuitStreamTimeout=1200\r\n");
    _oniontracetorctl_commandHelper(torctl, "SIGNAL NEWNYM\r\n");
}

void oniontracetorctl_commandEnableEvents(OnionTraceTorCtl* torctl, const gchar* spaceDelimitedEvents) {
    g_assert(torctl);
    _oniontracetorctl_commandHelper(torctl, "SETEVENTS %s\r\n", spaceDelimitedEvents);
}

void oniontracetorctl_commandDisableEvents(OnionTraceTorCtl* torctl) {
    g_assert(torctl);
    _oniontracetorctl_commandHelper(torctl, "SETEVENTS\r\n");
}

void oniontracetorctl_commandGetDescriptorInfo(OnionTraceTorCtl* torctl,
        OnDescriptorsReceivedFunc onDescriptorsReceived, gpointer onDescriptorsReceivedArg) {
    g_assert(torctl);

    torctl->onDescriptorsReceived = onDescriptorsReceived;
    torctl->onDescriptorsReceivedArg = onDescriptorsReceivedArg;
    torctl->waitingGetDescriptorsResponse = TRUE;
    //g_string_printf(command, "GETINFO dir/status-vote/current/consensus\r\n");
    _oniontracetorctl_commandHelper(torctl, "GETINFO ns/all\r\n");
}

void oniontracetorctl_commandBuildNewCircuit(OnionTraceTorCtl* torctl, const gchar* path) {
    g_assert(torctl);
    if(path) {
        _oniontracetorctl_commandHelper(torctl, "EXTENDCIRCUIT 0 %s\r\n", path);
    } else {
        _oniontracetorctl_commandHelper(torctl, "EXTENDCIRCUIT 0\r\n");
    }
}

void oniontracetorctl_commandAttachStreamToCircuit(OnionTraceTorCtl* torctl, gint streamID, gint circuitID) {
    g_assert(torctl);
    _oniontracetorctl_commandHelper(torctl, "ATTACHSTREAM %i %i\r\n", streamID, circuitID);
}

void oniontracetorctl_commandCloseCircuit(OnionTraceTorCtl* torctl, gint circuitID) {
    g_assert(torctl);
    // "CLOSECIRCUIT" SP CircuitID *(SP Flag) CRLF
    _oniontracetorctl_commandHelper(torctl, "CLOSECIRCUIT %i\r\n", circuitID);
}

void oniontracetorctl_commandCloseStream(OnionTraceTorCtl* torctl, gint streamID) {
    g_assert(torctl);
    // "CLOSESTREAM" SP StreamID SP Reason *(SP Flag) CRLF
    _oniontracetorctl_commandHelper(torctl, "CLOSESTREAM %i REASON_MISC\r\n", streamID);
}

void oniontracetorctl_commandGetAllCircuitStatus(OnionTraceTorCtl* torctl) {
    g_assert(torctl);
    torctl->waitingCircuitStatusResponse = TRUE;
    _oniontracetorctl_commandHelper(torctl, "GETINFO circuit-status\r\n");
}

void oniontracetorctl_commandGetAllCircuitStatusCleanup(OnionTraceTorCtl* torctl) {
    g_assert(torctl);
    torctl->circuitStatusCleanup = TRUE;
    oniontracetorctl_commandGetAllCircuitStatus(torctl);
}
