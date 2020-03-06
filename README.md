## OnionTrace

![](https://github.com/shadow/oniontrace/workflows/Builds/badge.svg)

This program records and plays back Tor circuit building and stream assignment.
It can also register for several asynchronous Tor events and logs the events
as they are received from Tor over time.

## Setup

Dependencies in Fedora/RedHat:

    sudo yum install cmake glib2 glib2-devel

Dependencies in Ubuntu/Debian:

    sudo apt-get install cmake libglib2.0-0 libglib2.0-dev

Build with a custom install prefix:

    mkdir build && cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=/home/$USER/.local
    make

Optionally install to the prefix:

    make install

## Usage

oniontrace arguments should be configured as follows:
	arguments="oniontrace key=value ..."

The valid keys and the type of the valid values are listed below, along with
any defaults. The format is:  
 + `key`:ValueType (default=`val`) [Mode=`ValidMode`] - explanation

Specifying the run mode is **optional**, the default mode is `log`:

 + `Mode`:String (default=`log`) [Mode=`record`,`play`,`log`]  
    Valid values for the running mode are `record`, `play`, and `log`.  
    `record` mode records circuit creation and stream assignment schedules.  
    `play` mode creates circuits and assigns streams according to a  
    schedule as previously recorded with `record` mode.
    `log` mode registers for async events and logs them to stdout as they occur.

The following are **required** arguments (default values do not exist):

 + `TorControlPort`:Integer [Mode=`record`,`play`,`log`]  
    The Tor Control server port, set in the torrc file of the Tor instance that  
    you want to trace.

The following are **optional** arguments (default values exist):

 + `LogLevel`:String (default=`info`) [Mode=`record`,`play`,`log`]  
    The log level to use while running oniontrace. Valid values are:  
    `debug` > `info` > `message` > `warning`  
    Messages logged at a higher level than the one configured will be filtered.
    
 + `TraceFile`:String (default=`oniontrace.csv`) [Mode=`record`,`play`]  
   The filename to write the trace when in `record` mode, or read a previously  
   recorded trace when in `play` mode.

 + `RunTime`:Integer (default=`0`) [Mode=`record`,`play`]  
   If positive, OnionTrace will stop running after the number of seconds  
   specified in this value. In `record` mode, this has the effect of recording  
   all circuits that are being tracked even if those circuits have not yet  
   been closed by Tor.  

 + `Events`:String (default=`BW`) [Mode=`log`]  
   The asynchronous Tor events for which we should listen and log when  
   we receive them from Tor. The value string should be a comma-delimited list  
   of events, e.g., 'BW,CIRC,STREAM'. See Section 4.1 of the  
   'Tor control protocol' specification for a full list of acceptable events.  
   (https://gitweb.torproject.org/torspec.git/tree/control-spec.txt)

## Tor Changes Required for record Mode

In order for the `record` mode to work correctly, we need Tor to export the
socks username in all stream control events. That will allow us to keep the
the same streams on the correct circuits when using `playback` mode to play
back a recorded trace file.

If the original stream control event is this:

```
650 STREAM 21 NEW 0 11.0.0.6:18080 SOURCE_ADDR=127.0.0.1:21437
```

We need to change Tor so that it outputs events like:

```
650 STREAM 21 NEW 0 11.0.0.6:18080 SOURCE_ADDR=127.0.0.1:21437 USERNAME=MYUSER
```

Where `MYUSER` is the SOCKS username in use to tie that stream to a circuit.
Here is a patch that was created to do this for `tor-0.3.5.8`, which may help
you understand how to change a newer version of Tor to export the correct info.

```
diff --git a/src/feature/control/control.c b/src/feature/control/control.c
index cc7ecff2f..fac51eb38 100644
--- a/src/feature/control/control.c
+++ b/src/feature/control/control.c
@@ -5904,16 +5904,28 @@ control_event_stream_status(entry_connection_t *conn, stream_status_event_t tp,
       purpose = " PURPOSE=USER";
   }
 
+  /* send socks username along with stream events. */
+  char user[64];
+  int do_user = (conn->socks_request && conn->socks_request->username) ? 1 : 0;
+
+  if(do_user) {
+    char* u_null_term = tor_memdup_nulterm(conn->socks_request->username,
+        conn->socks_request->usernamelen);
+    tor_snprintf(user, 64, " USERNAME=%s", u_null_term);
+    free(u_null_term);
+  }
+
   circ = circuit_get_by_edge_conn(ENTRY_TO_EDGE_CONN(conn));
   if (circ && CIRCUIT_IS_ORIGIN(circ))
     origin_circ = TO_ORIGIN_CIRCUIT(circ);
   send_control_event(EVENT_STREAM_STATUS,
-                        "650 STREAM %"PRIu64" %s %lu %s%s%s%s\r\n",
+                        "650 STREAM %"PRIu64" %s %lu %s%s%s%s%s\r\n",
                      (ENTRY_TO_CONN(conn)->global_identifier),
                      status,
                         origin_circ?
                            (unsigned long)origin_circ->global_identifier : 0ul,
-                        buf, reason_buf, addrport_buf, purpose);
+                        buf, reason_buf, addrport_buf, purpose,
+                        do_user ? user : "");
 
   /* XXX need to specify its intended exit, etc? */
```
