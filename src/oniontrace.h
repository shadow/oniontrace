/*
 * See LICENSE for licensing information
 */

#ifndef SRC_ONIONTRACE_H_
#define SRC_ONIONTRACE_H_

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <errno.h>
#include <math.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "oniontrace-config.h"
#include "oniontrace-event-manager.h"
#include "oniontrace-peer.h"
#include "oniontrace-timer.h"
#include "oniontrace-torctl.h"
#include "oniontrace-circuit.h"
#include "oniontrace-file.h"
#include "oniontrace-logger.h"
#include "oniontrace-player.h"
#include "oniontrace-recorder.h"
#include "oniontrace-driver.h"

#define ONIONTRACE_VERSION "1.0.0"

/* logging facility */
void oniontrace_log(GLogLevelFlags level, const gchar* functionName, const gchar* format, ...);

#ifdef DEBUG
#define debug(...) oniontrace_log(G_LOG_LEVEL_DEBUG, __FUNCTION__, __VA_ARGS__)
#else
#define debug(...)
#endif
#define info(...) oniontrace_log(G_LOG_LEVEL_INFO, __FUNCTION__, __VA_ARGS__)
#define message(...) oniontrace_log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, __VA_ARGS__)
#define warning(...) oniontrace_log(G_LOG_LEVEL_WARNING, __FUNCTION__, __VA_ARGS__)
#define critical(...) oniontrace_log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, __VA_ARGS__)
#define error(...) oniontrace_log(G_LOG_LEVEL_ERROR, __FUNCTION__, __VA_ARGS__)

#endif /* SRC_ONIONTRACE_H_ */
