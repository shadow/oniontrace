# OnionTraceTools

OnionTraceTools is a toolkit to parse and plot `oniontrace` log files.
OnionTraceTools is not required to run `oniontrace`, but may be helpful
to understand its output.

## Install system dependencies

Dependencies in Fedora/RedHat:

    sudo yum install python python-devel python-pip python-virtualenv libxml2 libxml2-devel libxslt libxslt-devel libpng libpng-devel freetype freetype-devel blas blas-devel lapack lapack-devel

Dependencies in Ubuntu/Debian:

    sudo apt-get install python python-dev python-pip python-virtualenv libxml2 libxml2-dev libxslt1.1 libxslt1-dev libpng16-16 libpng16-16 libfreetype6 libfreetype6-dev libblas-dev liblapack-dev

## Install OnionTraceTools Python modules

We show how to install python modules using `pip` (although you can also
use your OS package manager). We recommend using virtual environments to
keep all of the dependencies self-contained and to avoid conflicts with
your other python projects.

    virtualenv --no-site-packages otenv
    source otenv/bin/activate
    pip install -r path/to/oniontrace/tools/requirements.txt
    pip install -I path/to/oniontrace/tools

## Run OnionTraceTools

OnionTraceTools has several modes of operation and a help menu for each. For a
description of each mode, use the following (make sure you have activated
the tgen virtual environment with `source otenv/bin/activate` first):

```
oniontracetools -h
```

  + **parse**: Analyze OnionTrace output
  + **plot**: Visualize OnionTrace analysis results

## Example parsing and plotting OnionTrace output

Assuming you have already run `oniontrace` and saved the output to a log file
called `oniontrace.client.log`, you can then parse the log file like this:

    oniontracetools parse oniontrace.client.log

This produces the `oniontrace.analysis.json.xz` file.
The analysis file can be plotted:

    oniontracetools plot --data oniontrace.analysis.json.xz "oniontrace-test"

This will save new PDFs containing several graphs in the current directory.
Depending on the data that was analyzed, the graphs may include:

- Distribution of the highest bootstrapping percent across nodes
- Distribution of the bootstrapping completion times across nodes
- Moving average of the sum of read/write goodput per second for nodes over time
- Total data read/written per node
- Distribution of the min/median/max/mean/stdev read/write goodput per second across nodes
