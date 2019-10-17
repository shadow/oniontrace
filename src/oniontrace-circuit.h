/*
 * See LICENSE for licensing information
 */

#ifndef SRC_ONIONTRACE_CIRCUIT_H_
#define SRC_ONIONTRACE_CIRCUIT_H_

#include <time.h>
#include <glib.h>

typedef struct _OnionTraceCircuit OnionTraceCircuit;

OnionTraceCircuit* oniontracecircuit_new();
void oniontracecircuit_free(OnionTraceCircuit* circuit);

OnionTraceCircuit* oniontracecircuit_fromCSV(const gchar* line, struct timespec* offset);
GString* oniontracecircuit_toCSV(OnionTraceCircuit* circuit, struct timespec* offset);

gint* oniontracecircuit_getID(OnionTraceCircuit* circuit);

void oniontracecircuit_setLaunchTime(OnionTraceCircuit* circuit, struct timespec* launchTime);
struct timespec* oniontracecircuit_getLaunchTime(OnionTraceCircuit* circuit);

void oniontracecircuit_setCircuitID(OnionTraceCircuit* circuit, gint circuitID);
gint oniontracecircuit_getCircuitID(OnionTraceCircuit* circuit);

void oniontracecircuit_setSessionID(OnionTraceCircuit* circuit, const gchar* sessionID);
const gchar* oniontracecircuit_getSessionID(OnionTraceCircuit* circuit);

void oniontracecircuit_setCircuitStatus(OnionTraceCircuit* circuit, CircuitStatus status);
CircuitStatus oniontracecircuit_getCircuitStatus(OnionTraceCircuit* circuit);

void oniontracecircuit_setPath(OnionTraceCircuit* circuit, const gchar* path);
const gchar* oniontracecircuit_getPath(OnionTraceCircuit* circuit);

void oniontracecircuit_incrementStreamCounter(OnionTraceCircuit* circuit);
guint oniontracecircuit_getStreamCounter(OnionTraceCircuit* circuit);

void oniontracecircuit_incrementFailureCounter(OnionTraceCircuit* circuit);
guint oniontracecircuit_getFailureCounter(OnionTraceCircuit* circuit);

gint oniontracecircuit_compareLaunchTime(const OnionTraceCircuit* a, const OnionTraceCircuit* b,
        gpointer unused);

#endif /* SRC_ONIONTRACE_CIRCUIT_H_ */
