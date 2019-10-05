/*
 * See LICENSE for licensing information
 */

#include "oniontrace.h"

struct _OnionTraceTimer {
    GFunc notifyTimerExpired;
    gpointer arg1;
    gpointer arg2;
    gint timerFD;
};

OnionTraceTimer* oniontracetimer_new(GFunc func, gpointer arg1, gpointer arg2) {
    /* store the data in the timer table */
    OnionTraceTimer* timer = g_new0(OnionTraceTimer, 1);
    timer->notifyTimerExpired = func;
    timer->arg1 = arg1;
    timer->arg2 = arg2;

    /* create new timerfd */
    timer->timerFD = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

    return timer;
}

void oniontracetimer_armGranular(OnionTraceTimer* timer, struct itimerspec* arm) {
    timerfd_settime(timer->timerFD, 0, arm, NULL);
}

void oniontracetimer_arm(OnionTraceTimer* timer, guint timeoutSeconds, guint periodSeconds) {
    g_assert(timer && timer->timerFD > 0);

    /* create the timer info */
    struct itimerspec arm;

    guint64 seconds = (guint64) timeoutSeconds;
    guint64 nanoseconds = (guint64) 0;

    /* a timer with 0 delay will cause timerfd to disarm, so we use a 1 nano
     * delay instead, in order to execute the event as close to now as possible */
    if(seconds == 0 && nanoseconds == 0) {
        nanoseconds = 1;
    }

    /* set the initial expiration */
    arm.it_value.tv_sec = seconds;
    arm.it_value.tv_nsec = nanoseconds;

    /* if 0, timer never repeats, otherwise it repeats periodically */
    arm.it_interval.tv_sec = (guint64) periodSeconds;
    arm.it_interval.tv_nsec = 0;

    /* arm the timer, flags=0 -> relative time, NULL -> ignore previous setting */
    gint result = timerfd_settime(timer->timerFD, 0, &arm, NULL);
}

static gboolean _oniontracetimer_didExpire(OnionTraceTimer* timer) {
    g_assert(timer);

    /* clear the event from the descriptor */
    guint64 numExpirations = 0;
    ssize_t result = read(timer->timerFD, &numExpirations, sizeof(guint64));

    /* return TRUE if read succeeded and the timer expired at least once */
    if(result > 0 && numExpirations > 0) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static void _oniontracetimer_callNotify(OnionTraceTimer* timer) {
    g_assert(timer);

    /* execute the callback function */
    if(timer->notifyTimerExpired != NULL) {
        timer->notifyTimerExpired(timer->arg1, timer->arg2);
    }
}

gboolean oniontracetimer_check(OnionTraceTimer* timer) {
    g_assert(timer);

    /* if the timer expired, execute the callback function; otherwise do nothing */
    if(_oniontracetimer_didExpire(timer)) {
        _oniontracetimer_callNotify(timer);
        return TRUE;
    } else {
        return FALSE;
    }
}

gint oniontracetimer_getFD(OnionTraceTimer* timer) {
    g_assert(timer);

    return timer->timerFD;
}

void oniontracetimer_free(OnionTraceTimer* timer) {
    g_assert(timer);

    close(timer->timerFD);
    g_free(timer);
}

/* adapted from the libc manual for diffing timevals to be compat with timespec
 * Subtract a from b, i.e., returns b - a in result */
void oniontracetimer_timespecsubtract(struct timespec *result,
        struct timespec *a, struct timespec *b) {

    struct timespec minuend;
    memset(&minuend, 0, sizeof(struct timespec));
    minuend = *b;

    struct timespec subtrahend;
    memset(&subtrahend, 0, sizeof(struct timespec));
    subtrahend = *a;

    /* Perform the carry for the later subtraction by updating start. */
    if (minuend.tv_nsec < subtrahend.tv_nsec) {
        long num_secs = (subtrahend.tv_nsec - minuend.tv_nsec) / 1000000000 + 1;
        subtrahend.tv_nsec -= 1000000000 * num_secs;
        subtrahend.tv_sec += num_secs;
    }
    if (minuend.tv_nsec - subtrahend.tv_nsec > 1000000000) {
        long num_secs = (minuend.tv_nsec - subtrahend.tv_nsec) / 1000000000;
        subtrahend.tv_nsec += 1000000000 * num_secs;
        subtrahend.tv_sec -= num_secs;
    }

    /* Compute the time remaining to wait. tv_nsec is certainly positive. */
    result->tv_sec = minuend.tv_sec - subtrahend.tv_sec;
    result->tv_nsec = minuend.tv_nsec - subtrahend.tv_nsec;
}

void oniontracetimer_timespecadd(struct timespec *result,
        struct timespec *a, struct timespec *b) {
    result->tv_sec = a->tv_sec + b->tv_sec;

    long nsec = a->tv_nsec + b->tv_nsec;
    while(nsec >= 1000000000) {
        result->tv_sec += 1;
        nsec -= 1000000000;
    }

    result->tv_nsec = nsec;
}
