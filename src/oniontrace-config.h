/*
 * See LICENSE for licensing information
 */

#ifndef SRC_ONIONTRACE_CONFIG_H_
#define SRC_ONIONTRACE_CONFIG_H_

#include <netdb.h>

#include <glib.h>

typedef enum _OnionTraceMode OnionTraceMode;
enum _OnionTraceMode {
    ONIONTRACE_MODE_RECORD, ONIONTRACE_MODE_PLAY, ONIONTRACE_MODE_LOG,
};

typedef struct _OnionTraceConfig OnionTraceConfig;

OnionTraceConfig* oniontraceconfig_new(gint argc, gchar* argv[]);
void oniontraceconfig_free(OnionTraceConfig* config);

OnionTraceMode oniontraceconfig_getMode(OnionTraceConfig* config);
GLogLevelFlags oniontraceconfig_getLogLevel(OnionTraceConfig* config);
in_port_t oniontraceconfig_getTorControlPort(OnionTraceConfig* config);
gint oniontraceconfig_getRunTimeSeconds(OnionTraceConfig* config);
const gchar* oniontraceconfig_getTraceFileName(OnionTraceConfig* config);
const gchar* oniontraceconfig_getSpaceDelimitedEvents(OnionTraceConfig* config);

#endif /* SRC_ONIONTRACE_CONFIG_H_ */
