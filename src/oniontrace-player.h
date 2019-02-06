/*
 * See LICENSE for licensing information
 */

#ifndef SRC_ONIONTRACE_PLAYER_H_
#define SRC_ONIONTRACE_PLAYER_H_

#include "oniontrace-torctl.h"

typedef struct _OnionTracePlayer OnionTracePlayer;

OnionTracePlayer* oniontraceplayer_new(OnionTraceTorCtl* torctl);
void oniontraceplayer_free(OnionTracePlayer* player);

gchar* oniontraceplayer_toString(OnionTracePlayer* player);

#endif /* SRC_ONIONTRACE_PLAYER_H_ */
