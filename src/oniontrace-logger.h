/*
 * See LICENSE for licensing information
 */

#ifndef SRC_ONIONTRACE_LOGGER_H_
#define SRC_ONIONTRACE_LOGGER_H_

#include "oniontrace-torctl.h"

typedef struct _OnionTraceLogger OnionTraceLogger;

OnionTraceLogger* oniontracelogger_new(OnionTraceTorCtl* torctl, const gchar* spaceDelimitedEvents);
void oniontracelogger_free(OnionTraceLogger* logger);

gchar* oniontracelogger_toString(OnionTraceLogger* logger);

#endif /* SRC_ONIONTRACE_LOGGER_H_ */
