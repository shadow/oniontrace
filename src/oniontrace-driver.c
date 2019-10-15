/*
 * See LICENSE for licensing information
 */

#include "oniontrace.h"

typedef enum _OnionTraceDriverState OnionTraceDriverState;
enum _OnionTraceDriverState {
    ONIONTRACE_DRIVER_IDLE,
    ONIONTRACE_DRIVER_CONNECTING,
    ONIONTRACE_DRIVER_AUTHENTICATING,
    ONIONTRACE_DRIVER_BOOTSTRAPPING,
    ONIONTRACE_DRIVER_RECORDING,
    ONIONTRACE_DRIVER_PLAYING,
    ONIONTRACE_DRIVER_LOGGING,
};

struct _OnionTraceDriver {
    /* objects we don't own */
    OnionTraceConfig* config;
    OnionTraceEventManager* manager;

    /* objects/data we own */
    OnionTraceDriverState state;
    gchar* id;
    OnionTraceTimer* heartbeatTimer;
    OnionTraceTimer* shutdownTimer;
    OnionTraceTimer* cleanupTimer;
    struct timespec nowCached;

    OnionTraceTorCtl* torctl;
    OnionTraceRecorder* recorder;
    OnionTracePlayer* player;
    OnionTraceLogger* logger;
};

const gchar* _oniontracedriver_stateToString(OnionTraceDriverState state) {
    switch(state) {
        case ONIONTRACE_DRIVER_IDLE: return "IDLE";
        case ONIONTRACE_DRIVER_CONNECTING: return "CONNECTING";
        case ONIONTRACE_DRIVER_AUTHENTICATING: return "AUTHENTICATING";
        case ONIONTRACE_DRIVER_BOOTSTRAPPING: return "BOOTSTRAPPING";
        case ONIONTRACE_DRIVER_RECORDING: return "RECORDING";
        case ONIONTRACE_DRIVER_PLAYING: return "PLAYING";
        case ONIONTRACE_DRIVER_LOGGING: return "LOGGING";
        default: return "NONE";
    }
}

static void _oniontracedriver_playTimerReadable(OnionTraceTimer* timer, OnionTraceEventFlag type) {
    g_assert(timer);
    g_assert(type & ONIONTRACE_EVENT_READ);

    /* if the timer triggered, this will call the timer callback function */
    gboolean calledNotify = oniontracetimer_check(timer);
    if(!calledNotify) {
        warning("Authority unable to execute play timer callback function.");
    }
    /* free the timer since we don't track it anywhere else */
    oniontracetimer_free(timer);
}

static void _oniontracedriver_playCallback(OnionTraceDriver* driver, gpointer unused) {
    g_assert(driver);

    /* compute the time until the next circuit should be created */
    struct itimerspec armTime;
    memset(&armTime, 0, sizeof(struct itimerspec));

    /* build the circuit that we should be building now, and get next circuit time */
    armTime.it_value = oniontraceplayer_launchNextCircuit(driver->player);

    /* schedule another timer for the next circuit */
    if(armTime.it_value.tv_sec > 0 || armTime.it_value.tv_nsec > 0) {
        info("%s: launching next circuit in %"G_GSIZE_FORMAT".%09"G_GSIZE_FORMAT" seconds",
                driver->id, (gsize)armTime.it_value.tv_sec, (gsize)armTime.it_value.tv_nsec);

        OnionTraceTimer* timer = oniontracetimer_new((GFunc)_oniontracedriver_playCallback, driver, NULL);
        oniontracetimer_armGranular(timer, &armTime);

        gint timerFD = oniontracetimer_getFD(timer);
        oniontraceeventmanager_register(driver->manager, timerFD, ONIONTRACE_EVENT_READ,
                (OnionTraceOnEventFunc)_oniontracedriver_playTimerReadable, timer);
    }
}

static void _oniontracedriver_genericTimerReadable(OnionTraceTimer* timer, OnionTraceEventFlag type) {
    g_assert(timer);
    g_assert(type & ONIONTRACE_EVENT_READ);

    /* if the timer triggered, this will call the timer callback function */
    gboolean calledNotify = oniontracetimer_check(timer);
    if(!calledNotify) {
        warning("Authority unable to execute timer callback function. "
                "The timer might trigger again since we did not delete it.");
    }
}

static void _oniontracedriver_shutdown(OnionTraceDriver* driver, gpointer unused) {
    g_assert(driver);
    oniontraceeventmanager_stopMainLoop(driver->manager);
}

static void _oniontracedriver_registerShutdown(OnionTraceDriver* driver, guint seconds) {
    driver->shutdownTimer = oniontracetimer_new((GFunc)_oniontracedriver_shutdown, driver, NULL);
    oniontracetimer_arm(driver->shutdownTimer, seconds, 0);

    gint timerFD = oniontracetimer_getFD(driver->shutdownTimer);
    oniontraceeventmanager_register(driver->manager, timerFD, ONIONTRACE_EVENT_READ,
            (OnionTraceOnEventFunc)_oniontracedriver_genericTimerReadable, driver->shutdownTimer);
}

static void _oniontracedriver_cleanup(OnionTraceDriver* driver, gpointer unused) {
    g_assert(driver);

    if(driver->state == ONIONTRACE_DRIVER_RECORDING && driver->recorder != NULL) {
        oniontracerecorder_cleanup(driver->recorder);
    }
}

static void _oniontracedriver_registerCleanup(OnionTraceDriver* driver, guint seconds) {
    driver->cleanupTimer = oniontracetimer_new((GFunc)_oniontracedriver_cleanup, driver, NULL);
    oniontracetimer_arm(driver->cleanupTimer, seconds, 0);

    gint timerFD = oniontracetimer_getFD(driver->cleanupTimer);
    oniontraceeventmanager_register(driver->manager, timerFD, ONIONTRACE_EVENT_READ,
            (OnionTraceOnEventFunc)_oniontracedriver_genericTimerReadable, driver->cleanupTimer);
}

static void _oniontracedriver_heartbeat(OnionTraceDriver* driver, gpointer unused) {
    g_assert(driver);

    clock_gettime(CLOCK_REALTIME, &driver->nowCached);

    /* log some generally useful info as a status update */
    GString* msg = g_string_new("");
    g_string_append_printf(msg, "%s: heartbeat: state=%s",
            driver->id, _oniontracedriver_stateToString(driver->state));

    gchar* status = NULL;

    if(driver->state == ONIONTRACE_DRIVER_RECORDING && driver->recorder != NULL) {
        status = oniontracerecorder_toString(driver->recorder);
    } else if(driver->state == ONIONTRACE_DRIVER_PLAYING && driver->player != NULL) {
        status = oniontraceplayer_toString(driver->player);
    } else if(driver->state == ONIONTRACE_DRIVER_LOGGING && driver->logger != NULL) {
        status = oniontracelogger_toString(driver->logger);
    }

    if(status) {
        g_string_append_printf(msg, " %s", status);
        g_free(status);
    }

    message("%s", msg->str);
    g_string_free(msg, TRUE);
}

static void _oniontracedriver_registerHeartbeat(OnionTraceDriver* driver) {
    g_assert(driver);

    if(driver->heartbeatTimer) {
        oniontracetimer_free(driver->heartbeatTimer);
    }

    /* log heartbeat message every 1 second */
    driver->heartbeatTimer = oniontracetimer_new((GFunc)_oniontracedriver_heartbeat, driver, NULL);
    oniontracetimer_arm(driver->heartbeatTimer, 1, 1);

    gint timerFD = oniontracetimer_getFD(driver->heartbeatTimer);
    oniontraceeventmanager_register(driver->manager, timerFD, ONIONTRACE_EVENT_READ,
            (OnionTraceOnEventFunc)_oniontracedriver_genericTimerReadable, driver->heartbeatTimer);
}

static void _oniontracedriver_onBootstrapped(OnionTraceDriver* driver) {
    g_assert(driver);

    in_port_t clientPort = oniontracetorctl_getControlClientPort(driver->torctl);

    message("%s: successfully bootstrapped client port %u", driver->id, clientPort);

    const gchar* filename = oniontraceconfig_getTraceFileName(driver->config);

    OnionTraceMode configuredMode = oniontraceconfig_getMode(driver->config);

    if(configuredMode == ONIONTRACE_MODE_RECORD) {
        driver->state = ONIONTRACE_DRIVER_RECORDING;
        driver->recorder = oniontracerecorder_new(driver->torctl, filename);
        if(!driver->recorder) {
            critical("%s: Error creating recorder instance, cannot proceed", driver->id);
            driver->state = ONIONTRACE_DRIVER_IDLE;
            oniontraceeventmanager_stopMainLoop(driver->manager);
            return;
        }
    } else if(configuredMode == ONIONTRACE_MODE_PLAY) {
        driver->state = ONIONTRACE_DRIVER_PLAYING;

        driver->player = oniontraceplayer_new(driver->torctl, filename);
        if(!driver->player) {
            critical("%s: Error creating player instance, cannot proceed", driver->id);
            driver->state = ONIONTRACE_DRIVER_IDLE;
            oniontraceeventmanager_stopMainLoop(driver->manager);
            return;
        }

        /* start building circuits according to the schedule */
        _oniontracedriver_playCallback(driver, NULL);
    } else {
        driver->state = ONIONTRACE_DRIVER_LOGGING;
        const gchar* spaceDelimitedEvents = oniontraceconfig_getSpaceDelimitedEvents(driver->config);
        driver->logger = oniontracelogger_new(driver->torctl, spaceDelimitedEvents);
        if(!driver->logger) {
            critical("%s: Error creating logger instance, cannot proceed", driver->id);
            driver->state = ONIONTRACE_DRIVER_IDLE;
            oniontraceeventmanager_stopMainLoop(driver->manager);
            return;
        }
    }
}

static void _oniontracedriver_onAuthenticated(OnionTraceDriver* driver) {
    g_assert(driver);

    in_port_t clientPort = oniontracetorctl_getControlClientPort(driver->torctl);

    message("%s: successfully authenticated client port %u", driver->id, clientPort);

    message("%s: bootstrapping on client port %u", driver->id, clientPort);

    oniontracetorctl_commandGetBootstrapStatus(driver->torctl,
            (OnBootstrappedFunc)_oniontracedriver_onBootstrapped, driver);
    driver->state = ONIONTRACE_DRIVER_BOOTSTRAPPING;
}

static void _oniontracedriver_onConnected(OnionTraceDriver* driver) {
    g_assert(driver);

    in_port_t clientPort = oniontracetorctl_getControlClientPort(driver->torctl);

    message("%s: connection attempt finished on client port %u to Tor control server port %u",
            driver->id, clientPort, oniontraceconfig_getTorControlPort(driver->config));

    message("%s: attempting to authenticate on client port %u", driver->id, clientPort);

    oniontracetorctl_commandAuthenticate(driver->torctl,
            (OnAuthenticatedFunc)_oniontracedriver_onAuthenticated, driver);
    driver->state = ONIONTRACE_DRIVER_AUTHENTICATING;
}

gboolean oniontracedriver_start(OnionTraceDriver* driver) {
    g_assert(driver);

    if(driver->state != ONIONTRACE_DRIVER_IDLE) {
        message("%s: can't start driver because it is not idle", driver->id);
        return FALSE;
    }

    message("%s: creating control client to connect to Tor", driver->id);

    /* set up our torctl instance to get the descriptors before starting attack */
    in_port_t controlPort = oniontraceconfig_getTorControlPort(driver->config);

    driver->torctl = oniontracetorctl_new(driver->manager, controlPort,
            (OnConnectedFunc)_oniontracedriver_onConnected, driver);

    if(driver->torctl == NULL) {
        message("%s: error creating tor controller instance", driver->id);
        return FALSE;
    }

    message("%s: created tor controller instance, connecting to port %u",
            driver->id, controlPort);
    driver->state = ONIONTRACE_DRIVER_CONNECTING;

    /* now set up the heartbeat so we can log progress over time */
    _oniontracedriver_registerHeartbeat(driver);

    gint runTimeSeconds = oniontraceconfig_getRunTimeSeconds(driver->config);
    if(runTimeSeconds > 0) {
        _oniontracedriver_registerCleanup(driver, runTimeSeconds-1);
        _oniontracedriver_registerShutdown(driver, runTimeSeconds);
    }

    return TRUE;
}

gboolean oniontracedriver_stop(OnionTraceDriver* driver) {
    g_assert(driver);

    if(driver->state == ONIONTRACE_DRIVER_IDLE) {
        message("%s: can't stop driver because it is already idle", driver->id);
        return FALSE;
    }

    if(driver->recorder) {
        /* note that this free() call will record any in-progress circuits to file */
        oniontracerecorder_free(driver->recorder);
        driver->recorder = NULL;
    }

    if(driver->player) {
        oniontraceplayer_free(driver->player);
        driver->player = NULL;
    }

    if(driver->logger) {
        oniontracelogger_free(driver->logger);
        driver->logger = NULL;
    }

    if(driver->heartbeatTimer) {
        oniontracetimer_free(driver->heartbeatTimer);
        driver->heartbeatTimer = NULL;
    }

    if(driver->shutdownTimer) {
        oniontracetimer_free(driver->shutdownTimer);
        driver->shutdownTimer = NULL;
    }

    if(driver->torctl) {
        oniontracetorctl_free(driver->torctl);
        driver->torctl = NULL;
    }

    driver->state = ONIONTRACE_DRIVER_IDLE;

    return TRUE;
}

OnionTraceDriver* oniontracedriver_new(OnionTraceConfig* config, OnionTraceEventManager* manager) {
    OnionTraceDriver* driver = g_new0(OnionTraceDriver, 1);

    driver->manager = manager;
    driver->config = config;

    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Driver");
    driver->id = g_string_free(idbuf, FALSE);

    driver->state = ONIONTRACE_DRIVER_IDLE;

    return driver;
}

void oniontracedriver_free(OnionTraceDriver* driver) {
    g_assert(driver);

    if(driver->recorder) {
        oniontracerecorder_free(driver->recorder);
    }

    if(driver->player) {
        oniontraceplayer_free(driver->player);
    }

    if(driver->logger) {
        oniontracelogger_free(driver->logger);
    }

    if(driver->heartbeatTimer) {
        oniontracetimer_free(driver->heartbeatTimer);
    }

    if(driver->shutdownTimer) {
        oniontracetimer_free(driver->shutdownTimer);
    }

    if(driver->torctl) {
        oniontracetorctl_free(driver->torctl);
    }

    if(driver->id) {
        g_free(driver->id);
    }

    g_free(driver);
}
