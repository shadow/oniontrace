/*
 * See LICENSE for licensing information
 */

#include "oniontrace.h"

typedef enum _OnionTraceFileMode OnionTraceFileMode;
enum _OnionTraceFileMode {
    ONIONTRACE_FILE_READ,
    ONIONTRACE_FILE_WRITE,
};

struct _OnionTraceFile {
    FILE* stream;
    OnionTraceFileMode mode;
};

OnionTraceFile* oniontracefile_newWriter(const gchar* filename) {
    FILE* stream = fopen(filename, "w");
    if(!stream) {
        warning("Failed to open tracefile for writing using path %s: error %i, %s",
                filename, errno, g_strerror(errno));
        return NULL;
    }

    OnionTraceFile* file = g_new0(OnionTraceFile, 1);
    file->stream = stream;
    file->mode = ONIONTRACE_FILE_WRITE;
    return file;
}

OnionTraceFile* oniontracefile_newReader(const gchar* filename) {
    FILE* stream = fopen(filename, "r");
    if(!stream) {
        warning("Failed to open tracefile for reading using path %s: error %i, %s",
                filename, errno, g_strerror(errno));
        return NULL;
    }

    OnionTraceFile* file = g_new0(OnionTraceFile, 1);
    file->stream = stream;
    file->mode = ONIONTRACE_FILE_READ;
    return file;
}

void oniontracefile_free(OnionTraceFile* otfile) {
    g_assert(otfile);
    if(otfile->stream) {
        fclose(otfile->stream);
    }
    g_free(otfile);
}

gboolean oniontracefile_writeCircuit(OnionTraceFile* otfile, OnionTraceCircuit* circuit, struct timespec* offset) {
    g_assert(otfile);

    if(!circuit || otfile->mode != ONIONTRACE_FILE_WRITE) {
        return FALSE;
    }

    /* compute the elapsed time until the circuit should be created */
    struct timespec startTime;
    memset(&startTime, 0, sizeof(struct timespec));
    struct timespec elapsed;
    memset(&elapsed, 0, sizeof(struct timespec));

    if(offset) {
        startTime = *offset;
    }

    oniontracetimer_timespecdiff(&elapsed, &startTime, oniontracecircuit_getLaunchTime(circuit));

    /* get the other circuit elements */
    const gchar* sessionID = oniontracecircuit_getSessionID(circuit);
    const gchar* path = oniontracecircuit_getPath(circuit);

    GString* string = g_string_new("");

    /* print using ';'-separated values, because the path already has commas in it */
    g_string_append_printf(string, "%"G_GSIZE_FORMAT".%09"G_GSIZE_FORMAT";%s;%s\n",
            (gsize)elapsed.tv_sec, (gsize)elapsed.tv_nsec,
            sessionID ? sessionID : "NULL", path ? path : "NULL");

    /* write it to the file */
    fwrite(string->str, 1, string->len, otfile->stream);
    fflush(otfile->stream);

    g_string_free(string, TRUE);

    return TRUE;
}

static GQueue* _oniontracefile_getLines(OnionTraceFile* otfile) {
    GQueue* lineQueue = g_queue_new();

    gchar recvbuf[1024];
    gchar* partialLastLine = NULL;

    while(TRUE) {
        memset(recvbuf, 0, 1024);
        size_t n_bytes = fread(recvbuf, 1, 1023, otfile->stream);
        if(n_bytes < 1) {
            break;
        }

        gboolean lastIsPartial = (recvbuf[n_bytes-1] != '\n') ? TRUE : FALSE;

        gchar** lines = g_strsplit(recvbuf, "\n", 0);

        for(gint i = 0; lines[i] != NULL; i++) {
            /* ignore empty lines */
            if(!g_ascii_strcasecmp(lines[i], "")) {
                continue;
            }

            /* need to check if we have a \n if this is the last line */
            if(lines[i+1] == NULL && lastIsPartial) {
                partialLastLine = g_strdup(lines[i]);
            } else {
                /* if this is the first line and we have a partial line waiting, append them */
                if(i == 0 && partialLastLine) {
                    GString* string = g_string_new("");
                    g_string_append_printf(string, "%s%s", partialLastLine, lines[i]);
                    g_queue_push_tail(lineQueue, g_string_free(string, FALSE));
                    g_free(partialLastLine);
                    partialLastLine = NULL;
                } else {
                    /* take the full line as it is */
                    g_queue_push_tail(lineQueue, g_strdup(lines[i]));
                }
            }
        }

        g_strfreev(lines);
    }

    return lineQueue;
}

/* returns a queue of OnionTraceCircuit* objects sorted by launch time */
GQueue* oniontracefile_parseCircuits(OnionTraceFile* otfile) {
    g_assert(otfile);

    if(otfile->mode != ONIONTRACE_FILE_READ) {
        return NULL;
    }

    /* reset the file position to the beginning if needed */
    glong pos = ftell(otfile->stream);
    if(pos < 0) {
        warning("Unable to get position of trace file: error %i: %s", errno, g_strerror(errno));
        return NULL;
    }

    if(pos > 0) {
        rewind(otfile->stream);
    }

    /* build a sorted queue of circuits */
    GQueue* circuits = g_queue_new();

    /* helper to get the file contents as a queue of lines */
    GQueue* lines = _oniontracefile_getLines(otfile);

    /* now start parsing */
    while(!g_queue_is_empty(lines)) {
        gchar* line = g_queue_pop_head(lines);

        info("importing line from trace file: %s", line);

        gchar** parts = g_strsplit(line, ";", 0);

        if(parts[0] && parts[1] && parts[2]) {
            /* each line represents a circuit */
            OnionTraceCircuit* circuit = oniontracecircuit_new();

            /* parse the creation time */
            gchar** times = g_strsplit(parts[0], ".", 0);
            if(times[0] && times[1]) {
                struct timespec createTime;
                createTime.tv_sec = (__time_t)atol(times[0]);
                createTime.tv_nsec = (__syscall_slong_t)atol(times[1]);
                oniontracecircuit_setLaunchTime(circuit, &createTime);
            }
            g_strfreev(times);

            /* the session id is a string */
            if(g_ascii_strcasecmp(parts[1], "NULL")) {
               /* its not equal to NULL, so it must be valid */
                oniontracecircuit_setSessionID(circuit, g_strdup(parts[1]));
            }

            /* the path is a string */
            if(g_ascii_strcasecmp(parts[2], "NULL")) {
               /* its not equal to NULL, so it must be valid */
                oniontracecircuit_setPath(circuit, g_strdup(parts[2]));
            }

            /* store the circuits in chronological order */
            g_queue_insert_sorted(circuits, circuit,
                    (GCompareDataFunc)oniontracecircuit_compareLaunchTime, NULL);
        }

        g_strfreev(parts);

        g_free(line);
    }
    g_queue_free(lines);

    return circuits;
}
