/*
 * See LICENSE for licensing information
 */

#ifndef SRC_ONIONTRACE_RECORDER_H_
#define SRC_ONIONTRACE_RECORDER_H_

#include "oniontrace-torctl.h"

typedef struct _OnionTraceRecorder OnionTraceRecorder;

OnionTraceRecorder* oniontracerecorder_new(OnionTraceTorCtl* torctl, const gchar* filename);
void oniontracerecorder_free(OnionTraceRecorder* recorder);

void oniontracerecorder_cleanup(OnionTraceRecorder* recorder);

gchar* oniontracerecorder_toString(OnionTraceRecorder* recorder);

#endif /* SRC_ONIONTRACE_RECORDER_H_ */
