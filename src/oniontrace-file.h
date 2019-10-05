/*
 * See LICENSE for licensing information
 */

#ifndef SRC_ONIONTRACE_FILE_H_
#define SRC_ONIONTRACE_FILE_H_

#include <time.h>
#include <glib.h>

typedef struct _OnionTraceFile OnionTraceFile;

OnionTraceFile* oniontracefile_newWriter(const gchar* filename);
OnionTraceFile* oniontracefile_newReader(const gchar* filename);
void oniontracefile_free(OnionTraceFile* otfile);

gboolean oniontracefile_writeCircuit(OnionTraceFile* otfile, OnionTraceCircuit* circuit, struct timespec* offset);
GQueue* oniontracefile_parseCircuits(OnionTraceFile* otfile, struct timespec* offset);

#endif /* SRC_ONIONTRACE_FILE_H_ */
