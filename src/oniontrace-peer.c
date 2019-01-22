/*
 * See LICENSE for licensing information
 */

#include "oniontrace.h"

struct _OnionTracePeer {
    gchar* name;
    gchar* hostIPString;
    in_addr_t netIP;
    in_port_t netPort;

    gint refcount;
};

static gchar* _oniontracepeer_ipToNewString(in_addr_t netIP) {
    gchar* ipStringBuffer = g_malloc0(INET6_ADDRSTRLEN+1);
    const gchar* ipString = inet_ntop(AF_INET, &netIP, ipStringBuffer, INET6_ADDRSTRLEN);
    GString* result = ipString ? g_string_new(ipString) : g_string_new("NULL");
    g_free(ipStringBuffer);
    return g_string_free(result, FALSE);
}

static in_addr_t _oniontracepeer_lookupAddress(const gchar* name) {
    struct addrinfo* info = NULL;
    gint ret = getaddrinfo((gchar*) name, NULL, NULL, &info);
    if(ret != 0 || !info) {
        error("hostname lookup failed '%s'", name);
        return 0;
    }
    in_addr_t netIP = ((struct sockaddr_in*)(info->ai_addr))->sin_addr.s_addr;
    freeaddrinfo(info);
    return netIP;
}

OnionTracePeer* oniontracepeer_new(const gchar* name, in_port_t networkPort) {
    in_addr_t networkIP = _oniontracepeer_lookupAddress(name);
    if(networkIP == 0 || networkPort == 0) {
        return NULL;
    }

    OnionTracePeer* peer = g_new0(OnionTracePeer, 1);
    peer->refcount = 1;

    peer->netIP = networkIP;
    peer->netPort = networkPort;
    peer->name = g_strdup(name);
    peer->hostIPString = _oniontracepeer_ipToNewString(networkIP);

    return peer;
}

static void _oniontracepeer_free(OnionTracePeer* peer) {
    g_assert(peer);
    if(peer->name) {
        g_free(peer->name);
    }
    if(peer->hostIPString) {
        g_free(peer->hostIPString);
    }
    g_free(peer);
}

void oniontracepeer_ref(OnionTracePeer* peer) {
    g_assert(peer);
    peer->refcount++;
}

void oniontracepeer_unref(OnionTracePeer* peer) {
    g_assert(peer);
    if(--peer->refcount == 0) {
        _oniontracepeer_free(peer);
    }
}

in_addr_t oniontracepeer_getNetIP(OnionTracePeer* peer) {
    g_assert(peer);
    return peer->netIP;
}

in_port_t oniontracepeer_getNetPort(OnionTracePeer* peer) {
    g_assert(peer);
    return peer->netPort;
}

const gchar* oniontracepeer_getName(OnionTracePeer* peer) {
    g_assert(peer);
    return peer->name;
}

const gchar*  oniontracepeer_getHostIPStr(OnionTracePeer* peer) {
    g_assert(peer);
    return peer->hostIPString;
}

