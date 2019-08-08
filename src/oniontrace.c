/*
 * See LICENSE for licensing information
 */

#include "oniontrace.h"

static GLogLevelFlags globalLogFilterLevel = G_LOG_LEVEL_INFO;

static const gchar* _oniontrace_logLevelToString(GLogLevelFlags logLevel) {
    switch (logLevel) {
        case G_LOG_LEVEL_ERROR:
            return "error";
        case G_LOG_LEVEL_CRITICAL:
            return "critical";
        case G_LOG_LEVEL_WARNING:
            return "warning";
        case G_LOG_LEVEL_MESSAGE:
            return "message";
        case G_LOG_LEVEL_INFO:
            return "info";
        case G_LOG_LEVEL_DEBUG:
            return "debug";
        default:
            return "default";
    }
}

void oniontrace_log(GLogLevelFlags level, const gchar* functionName, const gchar* format, ...) {
    if(level > globalLogFilterLevel) {
        return;
    }

    va_list vargs;
    va_start(vargs, format);

    GDateTime* dt = g_date_time_new_now_local();
    GString* newformat = g_string_new(NULL);

    g_string_append_printf(newformat, "%04i-%02i-%02i %02i:%02i:%02i %"G_GINT64_FORMAT".%06i [%s] [%s] %s",
            g_date_time_get_year(dt), g_date_time_get_month(dt), g_date_time_get_day_of_month(dt),
            g_date_time_get_hour(dt), g_date_time_get_minute(dt), g_date_time_get_second(dt),
            g_date_time_to_unix(dt), g_date_time_get_microsecond(dt),
            _oniontrace_logLevelToString(level), functionName, format);

    gchar* message = g_strdup_vprintf(newformat->str, vargs);
    g_print("%s\n", message);
    g_free(message);

    g_string_free(newformat, TRUE);
    g_date_time_unref(dt);

    va_end(vargs);
}

int main(int argc, char *argv[]) {
    gchar hostname[128];
    memset(hostname, 0, 128);
    gethostname(hostname, 128);
    message("Starting OnionTrace v%s on host %s process id %i",
            ONIONTRACE_VERSION, hostname, (gint )getpid());

    message("Parsing program arguments");
    OnionTraceConfig* config = oniontraceconfig_new(argc, argv);
    if (config == NULL) {
        message("Parsing config failed, exiting with failure");
        return EXIT_FAILURE;
    }

    /* update to the configured log level */
    globalLogFilterLevel = oniontraceconfig_getLogLevel(config);

    message("Creating event manager to run main loop");
    OnionTraceEventManager* manager = oniontraceeventmanager_new();
    if (manager == NULL) {
        message("Creating event manager failed, exiting with failure");
        return EXIT_FAILURE;
    }

    OnionTraceMode mode = oniontraceconfig_getMode(config);
    OnionTraceDriver* driver = NULL;
    gboolean success = TRUE;

    message("Creating driver");

    driver = oniontracedriver_new(config, manager);
    if (driver == NULL) {
        message("Creating driver failed, exiting with failure");
        success = FALSE;
    } else {
        success = oniontracedriver_start(driver);
    }

    if (success) {
        /* the recorder should be waiting for circuit and stream events, and
         * when those occur, new actions will be taken. */
        message("Running main loop");
        success = oniontraceeventmanager_runMainLoop(manager);
        message("Main loop returned, cleaning up");
    }

    if (driver != NULL) {
        oniontracedriver_stop(driver);
        oniontracedriver_free(driver);
    }

    oniontraceeventmanager_free(manager);
    oniontraceconfig_free(config);

    message("Exiting cleanly with %s code", success ? "success" : "failure");
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
