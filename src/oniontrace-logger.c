/*
 * See LICENSE for licensing information
 */
#include <stdarg.h>

#include "oniontrace.h"

struct _OnionTraceLogger {
    /* objects we don't own */
    OnionTraceTorCtl* torctl;

    /* objects/data we own */
    gchar* id;
    struct timespec startTime;

    gsize messagesLogged;
};

void _oniontracelogger_logControlLine(OnionTraceLogger* logger, gchar* line) {
    g_assert(logger);
    if(line != NULL) {
        /* always log these messages no matter what the log level is set at */
        oniontrace_log(0, __FUNCTION__, "%s: %s", logger->id, line);
        logger->messagesLogged++;
    }
}

/* returns a status string for the heartbeat message */
gchar* oniontracelogger_toString(OnionTraceLogger* logger) {
    GString* string = g_string_new("");
    g_string_append_printf(string, "n_msgs_logged=%zu", logger->messagesLogged);
    return g_string_free(string, FALSE);
}

OnionTraceLogger* oniontracelogger_new(OnionTraceTorCtl* torctl, const gchar* spaceDelimitedEvents) {
    OnionTraceLogger* logger = g_new0(OnionTraceLogger, 1);

    logger->torctl = torctl;

    clock_gettime(CLOCK_REALTIME, &logger->startTime);

    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Logger");
    logger->id = g_string_free(idbuf, FALSE);

    oniontracetorctl_setLineReceivedCallback(logger->torctl,
            (OnLineReceivedFunc)_oniontracelogger_logControlLine, logger);

    /* start watching for circuit and stream events */
    oniontracetorctl_commandEnableEvents(logger->torctl, spaceDelimitedEvents);

    return logger;
}

void oniontracelogger_free(OnionTraceLogger* logger) {
    g_assert(logger);

    if(logger->id) {
        g_free(logger->id);
    }

    g_free(logger);
}
