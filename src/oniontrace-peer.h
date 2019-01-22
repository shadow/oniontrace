/*
 * See LICENSE for licensing information
 */

#ifndef SRC_ONIONTRACE_PEER_H_
#define SRC_ONIONTRACE_PEER_H_

#include <netinet/in.h>

#include <glib.h>

typedef struct _OnionTracePeer OnionTracePeer;

OnionTracePeer* oniontracepeer_new(const gchar* name, in_port_t networkPort);
void oniontracepeer_ref(OnionTracePeer* peer);
void oniontracepeer_unref(OnionTracePeer* peer);

in_addr_t oniontracepeer_getNetIP(OnionTracePeer* peer);
in_port_t oniontracepeer_getNetPort(OnionTracePeer* peer);
const gchar* oniontracepeer_getName(OnionTracePeer* peer);
const gchar*  oniontracepeer_getHostIPStr(OnionTracePeer* peer);

#endif /* SRC_ONIONTRACE_PEER_H_ */
