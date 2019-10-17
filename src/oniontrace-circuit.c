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
    guint numFailures;

    CircuitStatus status;
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

OnionTraceCircuit* oniontracecircuit_fromCSV(const gchar* line, struct timespec* offset) {
    OnionTraceCircuit* circuit = NULL;

    if(line && offset) {
        gchar** parts = g_strsplit(line, ";", 0);

        if(parts[0] && parts[1] && parts[2]) {
            /* each line represents a circuit */
            circuit = oniontracecircuit_new();

            /* parse the creation time */
            gchar** times = g_strsplit(parts[0], ".", 0);
            if(times[0] && times[1]) {
                struct timespec createTimeRel;
                memset(&createTimeRel, 0, sizeof(struct timespec));
                createTimeRel.tv_sec = (__time_t)atol(times[0]);
                createTimeRel.tv_nsec = (__syscall_slong_t)atol(times[1]);

                struct timespec createTimeAbs;
                memset(&createTimeAbs, 0, sizeof(struct timespec));
                oniontracetimer_timespecadd(&createTimeAbs, offset, &createTimeRel);

                oniontracecircuit_setLaunchTime(circuit, &createTimeAbs);
            }
            g_strfreev(times);

            /* the session id is a string */
            if(g_ascii_strcasecmp(parts[1], "NULL")) {
               /* its not equal to NULL, so it must be valid */
                oniontracecircuit_setSessionID(circuit, parts[1]);
            }

            /* the path is a string */
            if(g_ascii_strcasecmp(parts[2], "NULL")) {
               /* its not equal to NULL, so it must be valid */
                oniontracecircuit_setPath(circuit, parts[2]);
            }
        }

        g_strfreev(parts);
    }

    return circuit;
}

/* offset is the time that oniontrace started running */
GString* oniontracecircuit_toCSV(OnionTraceCircuit* circuit, struct timespec* offset) {
    /* compute the elapsed time until the circuit should be created */
    struct timespec startTime;
    memset(&startTime, 0, sizeof(struct timespec));
    struct timespec elapsed;
    memset(&elapsed, 0, sizeof(struct timespec));

    if(offset) {
        startTime = *offset;
    }

    oniontracetimer_timespecsubtract(&elapsed, &startTime, oniontracecircuit_getLaunchTime(circuit));

    /* get the other circuit elements */
    const gchar* sessionID = oniontracecircuit_getSessionID(circuit);
    const gchar* path = oniontracecircuit_getPath(circuit);

    GString* string = g_string_new("");

    /* print using ';'-separated values, because the path already has commas in it */
    g_string_append_printf(string, "%"G_GSIZE_FORMAT".%09"G_GSIZE_FORMAT";%s;%s\n",
            (gsize)elapsed.tv_sec, (gsize)elapsed.tv_nsec,
            sessionID ? sessionID : "NULL", path ? path : "NULL");

    return string;
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

void oniontracecircuit_setCircuitStatus(OnionTraceCircuit* circuit, CircuitStatus status) {
    g_assert(circuit);
    circuit->status = status;
}

CircuitStatus oniontracecircuit_getCircuitStatus(OnionTraceCircuit* circuit) {
    g_assert(circuit);
    return circuit->status;
}

void oniontracecircuit_setSessionID(OnionTraceCircuit* circuit, const gchar* sessionID) {
    g_assert(circuit);
    if(circuit->sessionID) {
        g_free(circuit->sessionID);
    }
    circuit->sessionID = g_strdup(sessionID);
}

const gchar* oniontracecircuit_getSessionID(OnionTraceCircuit* circuit) {
    g_assert(circuit);
    return circuit->sessionID;
}

void oniontracecircuit_setPath(OnionTraceCircuit* circuit, const gchar* path) {
    g_assert(circuit);
    if(circuit->path) {
        g_free(circuit->path);
    }
    circuit->path = g_strdup(path);
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

void oniontracecircuit_incrementFailureCounter(OnionTraceCircuit* circuit) {
    g_assert(circuit);
    circuit->numFailures++;
}

guint oniontracecircuit_getFailureCounter(OnionTraceCircuit* circuit) {
    g_assert(circuit);
    return circuit->numFailures;
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
