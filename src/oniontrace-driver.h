/*
 * See LICENSE for licensing information
 */

#ifndef SRC_ONIONTRACE_DRIVER_H_
#define SRC_ONIONTRACE_DRIVER_H_

#include "oniontrace-config.h"
#include "oniontrace-event-manager.h"

typedef struct _OnionTraceDriver OnionTraceDriver;

OnionTraceDriver* oniontracedriver_new(OnionTraceConfig* config, OnionTraceEventManager* manager);
void oniontracedriver_free(OnionTraceDriver* driver);

gboolean oniontracedriver_start(OnionTraceDriver* driver);
gboolean oniontracedriver_stop(OnionTraceDriver* driver);

#endif /* SRC_ONIONTRACE_DRIVER_H_ */
