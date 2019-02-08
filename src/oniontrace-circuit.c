/*
 * See LICENSE for licensing information
 */

#include "oniontrace.h"

struct _OnionTraceCircuit {
    struct timespec launchTime;
    gint circuitID;
    gchar* path;
    gchar* sessionID;
    guint numStreams;
};

OnionTraceCircuit* oniontracecircuit_new() {
    OnionTraceCircuit* circuit = g_new0(OnionTraceCircuit, 1);
    return circuit;
}

void oniontracecircuit_free(OnionTraceCircuit* circuit) {
    g_assert(circuit);
    if(circuit->path) {
        g_free(circuit->path);
    }
    if(circuit->sessionID) {
        g_free(circuit->sessionID);
    }
    g_free(circuit);
}

gint* oniontracecircuit_getID(OnionTraceCircuit* circuit) {
    g_assert(circuit);
    return &circuit->circuitID;
}

void oniontracecircuit_setLaunchTime(OnionTraceCircuit* circuit, struct timespec* launchTime) {
    g_assert(circuit);
    if(launchTime) {
        circuit->launchTime = *launchTime;
    }
}

struct timespec* oniontracecircuit_getLaunchTime(OnionTraceCircuit* circuit) {
    g_assert(circuit);
    return &circuit->launchTime;
}

void oniontracecircuit_setCircuitID(OnionTraceCircuit* circuit, gint circuitID) {
    g_assert(circuit);
    circuit->circuitID = circuitID;
}

gint oniontracecircuit_getCircuitID(OnionTraceCircuit* circuit) {
    g_assert(circuit);
    return circuit->circuitID;
}

void oniontracecircuit_setSessionID(OnionTraceCircuit* circuit, gchar* sessionID) {
    g_assert(circuit);
    if(circuit->sessionID) {
        g_free(circuit->sessionID);
    }
    circuit->sessionID = sessionID;
}

const gchar* oniontracecircuit_getSessionID(OnionTraceCircuit* circuit) {
    g_assert(circuit);
    return circuit->sessionID;
}

void oniontracecircuit_setPath(OnionTraceCircuit* circuit, gchar* path) {
    g_assert(circuit);
    if(circuit->path) {
        g_free(circuit->path);
    }
    circuit->path = path;
}

const gchar* oniontracecircuit_getPath(OnionTraceCircuit* circuit) {
    g_assert(circuit);
    return circuit->path;
}

void oniontracecircuit_incrementStreamCounter(OnionTraceCircuit* circuit) {
    g_assert(circuit);
    circuit->numStreams++;
}

guint oniontracecircuit_getStreamCounter(OnionTraceCircuit* circuit) {
    g_assert(circuit);
    return circuit->numStreams;
}

GString* oniontracecircuit_toCSV(OnionTraceCircuit* circuit, struct timespec* startTime) {
    g_assert(circuit);

    struct timespec elapsed;
    oniontracetimer_timespecdiff(&elapsed, startTime, &circuit->launchTime);

    GString* string = g_string_new("");

    g_string_append_printf(string, "%"G_GSIZE_FORMAT".%09"G_GSIZE_FORMAT";%s;%s\n",
            (gsize)elapsed.tv_sec, (gsize)elapsed.tv_nsec,
            circuit->sessionID ? circuit->sessionID : "NULL",
            circuit->path ? circuit->path : "NULL");

    return string;
}

gint oniontracecircuit_compareLaunchTime(const OnionTraceCircuit* a, const OnionTraceCircuit* b,
        gpointer unused) {
    g_assert(a);
    g_assert(b);
    if(a->launchTime.tv_sec < b->launchTime.tv_sec) {
        return -1;
    } else if(a->launchTime.tv_sec > b->launchTime.tv_sec) {
        return 1;
    } else {
        /* seconds are the same, check nanos */
        if(a->launchTime.tv_nsec < b->launchTime.tv_nsec) {
            return -1;
        } else if(a->launchTime.tv_nsec > b->launchTime.tv_nsec) {
            return 1;
        } else {
            /* both seconds and nanos are the same */
            return 0;
        }
    }
}
