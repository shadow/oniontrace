## OnionTrace

This program records and plays back Tor circuit building and stream assignment.
It can also register for several asynchronous Tor events and logs the events
as they are received from Tor over time.

## Setup

```
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/home/user/.local
make
make install # optional, installs to path set above
```

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
   