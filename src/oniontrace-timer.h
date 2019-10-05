/*
 * See LICENSE for licensing information
 */

#ifndef SRC_ONIONTRACE_TIMER_H_
#define SRC_ONIONTRACE_TIMER_H_

#include <sys/timerfd.h>

#include <glib.h>

typedef struct _OnionTraceTimer OnionTraceTimer;

OnionTraceTimer* oniontracetimer_new(GFunc func, gpointer arg1, gpointer arg2);
void oniontracetimer_free(OnionTraceTimer* timer);

void oniontracetimer_arm(OnionTraceTimer* timer, guint timeoutSeconds, guint periodSeconds);
void oniontracetimer_armGranular(OnionTraceTimer* timer, struct itimerspec* arm);
gboolean oniontracetimer_check(OnionTraceTimer* timer);

gint oniontracetimer_getFD(OnionTraceTimer* timer);

void oniontracetimer_timespecsubtract(struct timespec *result,
        struct timespec *a, struct timespec *b);
void oniontracetimer_timespecadd(struct timespec *result,
        struct timespec *a, struct timespec *b);

#endif /* SRC_ONIONTRACE_TIMER_H_ */
