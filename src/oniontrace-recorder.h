/*
 * See LICENSE for licensing information
 */

#ifndef SRC_ONIONTRACE_RECORDER_H_
#define SRC_ONIONTRACE_RECORDER_H_

#include "oniontrace-config.h"
#include "oniontrace-event-manager.h"

typedef struct _OnionTraceRecorder OnionTraceRecorder;

OnionTraceRecorder* oniontracerecorder_new(OnionTraceConfig* config, OnionTraceEventManager* manager);
void oniontracerecorder_free(OnionTraceRecorder* authority);

gboolean oniontracerecorder_start(OnionTraceRecorder* recorder);
gboolean oniontracerecorder_stop(OnionTraceRecorder* recorder);

#endif /* SRC_ONIONTRACE_RECORDER_H_ */
