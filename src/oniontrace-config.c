/*
 * See LICENSE for licensing information
 */

#include "oniontrace.h"

struct _OnionTraceConfig {
    /* See the README file for explanation of these arguments */
    OnionTraceMode mode;

    gint runTimeSeconds;
    in_port_t torControlPort;
    GLogLevelFlags logLevel;
    gchar* filename;
    /* space-delimited events like 'BW CIRC STREAM', suitable for sending in control command */
    gchar* events;
};

static gchar* _oniontrace_getHomePath(const gchar* path) {
    g_assert(path);
    GString* sbuffer = g_string_new(path);
    if(g_ascii_strncasecmp(path, "~", 1) == 0) {
        /* replace ~ with home directory */
        const gchar* home = g_get_home_dir();
        g_string_erase(sbuffer, 0, 1);
        g_string_prepend(sbuffer, home);
    }
    return g_string_free(sbuffer, FALSE);
}

static gboolean _oniontraceconfig_parseMode(OnionTraceConfig* config, gchar* value) {
    g_assert(config && value);

    if(!g_ascii_strcasecmp(value, "record")) {
        config->mode = ONIONTRACE_MODE_RECORD;
    } else if(!g_ascii_strcasecmp(value, "play")) {
        config->mode = ONIONTRACE_MODE_PLAY;
    } else if(!g_ascii_strcasecmp(value, "log")) {
        config->mode = ONIONTRACE_MODE_LOG;
    } else {
        warning("invalid mode '%s' provided, see README for valid values", value);
        return FALSE;
    }

    return TRUE;
}

static gboolean _oniontraceconfig_parseTorControlPort(OnionTraceConfig* config, gchar* value) {
    g_assert(config && value);

    gint port = atoi(value);

    if(port < 1 || port > G_MAXUINT16) {
        return FALSE;
    }

    config->torControlPort = (in_port_t)port;

    return TRUE;
}

static gboolean _oniontraceconfig_parseRunTimeSeconds(OnionTraceConfig* config, gchar* value) {
    g_assert(config && value);

    gint numSeconds = atoi(value);

    config->runTimeSeconds = numSeconds;

    return TRUE;
}

static gboolean _oniontraceconfig_parseLogLevel(OnionTraceConfig* config, gchar* value) {
    g_assert(config && value);

    if(!g_ascii_strcasecmp(value, "debug")) {
        config->logLevel = G_LOG_LEVEL_DEBUG;
    } else if(!g_ascii_strcasecmp(value, "info")) {
        config->logLevel = G_LOG_LEVEL_INFO;
    } else if(!g_ascii_strcasecmp(value, "message")) {
        config->logLevel = G_LOG_LEVEL_MESSAGE;
    } else if(!g_ascii_strcasecmp(value, "warning")) {
        config->logLevel = G_LOG_LEVEL_WARNING;
    } else {
        warning("invalid log level '%s' provided, see README for valid values", value);
        return FALSE;
    }

    return TRUE;
}

static gboolean _oniontraceconfig_parseTraceFile(OnionTraceConfig* config, gchar* value) {
    g_assert(config && value);

    if(value) {
        if(config->filename) {
            g_free(config->filename);
        }
        config->filename = _oniontrace_getHomePath(value);
    } else {
        warning("invalid filename '%s' provided, see README for valid values", value);
        return FALSE;
    }

    return TRUE;
}

static gboolean _oniontraceconfig_parseCommaDelimitedEvents(OnionTraceConfig* config, gchar* value) {
    g_assert(config && value);

    /* turn the comma-delimited string input into a space-delimited string that
     * is suitable to sending to the Tor control port in a 'SETEVENTS EVENT1 EVENT2'
     * type of command. */
    if(config->events) {
        g_free(config->events);
        config->events = NULL;
    }

    gchar** eventStrs = g_strsplit(value, ",", 0);
    config->events = g_strjoinv(" ", eventStrs);
    g_strfreev(eventStrs);

    if(config->events) {
        return TRUE;
    } else {
        return FALSE;
    }
}

OnionTraceConfig* oniontraceconfig_new(gint argc, gchar* argv[]) {
    gboolean hasError = FALSE;

    OnionTraceConfig* config = g_new0(OnionTraceConfig, 1);

    /* set defaults, which will get overwritten if set in args */
    config->mode = ONIONTRACE_MODE_LOG;
    config->runTimeSeconds = 0;
    config->logLevel = G_LOG_LEVEL_INFO;
    config->filename = g_strdup("oniontrace.csv");
    config->events = g_strdup("BW");

    /* parse all of the key=value pairs, skip the first program name arg */
    for(gint i = 1; i < argc; i++) {
        gchar* entry = argv[i];

        gchar** parts = g_strsplit(entry, "=", 2);
        gchar* key = parts[0];
        gchar* value = parts[1];

        if(key != NULL && value != NULL) {
            /* we have both key and value in key=value entry */
            if(!g_ascii_strcasecmp(key, "Mode")) {
                if(!_oniontraceconfig_parseMode(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "TorControlPort")) {
                if(!_oniontraceconfig_parseTorControlPort(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "LogLevel")) {
                if(!_oniontraceconfig_parseLogLevel(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "TraceFile")) {
                if(!_oniontraceconfig_parseTraceFile(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "RunTime")) {
                if(!_oniontraceconfig_parseRunTimeSeconds(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "Events")) {
                if(!_oniontraceconfig_parseCommaDelimitedEvents(config, value)) {
                    hasError = TRUE;
                }
            } else {
                warning("unrecognized key '%s' in config", key);
                hasError = TRUE;
            }

            if(hasError) {
                critical("error in config: key='%s' value='%s'", key, value);
            } else {
                message("successfully parsed key='%s' value='%s'", key, value);
            }
        } else {
            /* we are missing either the key or the value */
            hasError = TRUE;

            if(key != NULL) {
                critical("can't find key '%s' in config entry", key);
            } else {
                critical("can't find value in config entry; key='%s'", key);
            }
        }

        g_strfreev(parts);

        if(hasError) {
            oniontraceconfig_free(config);
            return NULL;
        }
    }

    /* now make sure we have the required arguments */

    /* we always need a tor control port */
    if(config->torControlPort == 0) {
        critical("missing required valid Tor control port argument `TorControlPort`");
        oniontraceconfig_free(config);
        return NULL;
    }

    /* if we are playing, then the trace file better exist */
    if(config->mode == ONIONTRACE_MODE_PLAY) {
        if(!config->filename ||
                !g_file_test(config->filename, G_FILE_TEST_IS_REGULAR|G_FILE_TEST_EXISTS)) {
            critical("path '%s' to trace file is not valid or does not exist", config->filename);
            oniontraceconfig_free(config);
            return NULL;
        }
    }

    return config;
}

void oniontraceconfig_free(OnionTraceConfig* config) {
    g_assert(config);

    if(config->filename) {
        g_free(config->filename);
    }

    if(config->events) {
        g_free(config->events);
    }

    g_free(config);
}

in_port_t oniontraceconfig_getTorControlPort(OnionTraceConfig* config) {
    g_assert(config);
    return config->torControlPort;
}

gint oniontraceconfig_getRunTimeSeconds(OnionTraceConfig* config) {
    g_assert(config);
    return config->runTimeSeconds;
}

GLogLevelFlags oniontraceconfig_getLogLevel(OnionTraceConfig* config) {
    g_assert(config);
    return config->logLevel;
}

OnionTraceMode oniontraceconfig_getMode(OnionTraceConfig* config) {
    g_assert(config);
    return config->mode;
}

const gchar* oniontraceconfig_getTraceFileName(OnionTraceConfig* config) {
    g_assert(config);
    return config->filename ? config->filename : NULL;
}

const gchar* oniontraceconfig_getSpaceDelimitedEvents(OnionTraceConfig* config) {
    g_assert(config);
    return config->events ? config->events : NULL;
}
