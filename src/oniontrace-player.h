/*
 * See LICENSE for licensing information
 */

#ifndef SRC_ONIONTRACE_PLAYER_H_
#define SRC_ONIONTRACE_PLAYER_H_

#include <time.h>
#include <glib.h>

#include "oniontrace-torctl.h"

typedef struct _OnionTracePlayer OnionTracePlayer;

OnionTracePlayer* oniontraceplayer_new(OnionTraceTorCtl* torctl, const gchar* filename);
void oniontraceplayer_free(OnionTracePlayer* player);

gchar* oniontraceplayer_toString(OnionTracePlayer* player);

struct timespec oniontraceplayer_launchNextCircuit(OnionTracePlayer* player);

#endif /* SRC_ONIONTRACE_PLAYER_H_ */
