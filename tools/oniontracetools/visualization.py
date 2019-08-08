'''
  oniontracetools
  Authored by Rob Jansen, 2015
  See LICENSE for licensing information
'''

import matplotlib
matplotlib.use('Agg') # for systems without X11
import matplotlib.pyplot as pyplot
from matplotlib.backends.backend_pdf import PdfPages

import numpy, time, logging, re
from abc import abstractmethod, ABCMeta

class Visualization(object):

    __metaclass__ = ABCMeta

    def __init__(self, hostpatterns):
        self.datasets = []
        self.hostpatterns = hostpatterns

    def add_dataset(self, analysis, label, lineformat):
        self.datasets.append((analysis, label, lineformat))

    @abstractmethod
    def plot_all(self, output_prefix):
        pass

class OnionTraceVisualization(Visualization):

    def plot_all(self, output_prefix):
        if len(self.datasets) == 0:
            return

        prefix = output_prefix + '.' if output_prefix is not None else ''
        ts = time.strftime("%Y-%m-%d_%H:%M:%S")
        pagename = "{0}oniontrace.viz.{1}.pdf".format(prefix, ts)

        logging.info("Starting to plot graphs to {}".format(pagename))

        self.page = PdfPages(pagename)
        logging.info("Plotting bootstrapping times")
        self.__plot_bootstrapping_levels()
        self.__plot_bootstrapping_completed_times()
        logging.info("Plotting goodput time series")
        self.__plot_goodput_timeseries("bytes_read")
        self.__plot_goodput_timeseries("bytes_written")
        logging.info("Plotting total bytes CDF")
        self.__plot_bandwidth_summary("bytes_read_total")
        self.__plot_bandwidth_summary("bytes_written_total")
        logging.info("Plotting min bytes CDF")
        self.__plot_bandwidth_summary("bytes_read_min")
        self.__plot_bandwidth_summary("bytes_written_min")
        logging.info("Plotting median bytes CDF")
        self.__plot_bandwidth_summary("bytes_read_median")
        self.__plot_bandwidth_summary("bytes_written_median")
        logging.info("Plotting max bytes CDF")
        self.__plot_bandwidth_summary("bytes_read_max")
        self.__plot_bandwidth_summary("bytes_written_max")
        logging.info("Plotting mean bytes CDF")
        self.__plot_bandwidth_summary("bytes_read_mean")
        self.__plot_bandwidth_summary("bytes_written_mean")
        logging.info("Plotting standard deviation bytes CDF")
        self.__plot_bandwidth_summary("bytes_read_std")
        self.__plot_bandwidth_summary("bytes_written_std")
        self.page.close()
        logging.info("Saved PDF page {}".format(pagename))

    def __get_nodes(self, anal):
        nodes = set()
        for node in anal.get_nodes():
            if len(self.hostpatterns) > 0:
                for pattern in self.hostpatterns:
                    if re.search(pattern, node) is not None:
                        nodes.add(node)
            else:
                nodes.add(node)
        return nodes

    def __plot_bootstrapping_levels(self):
        fig = None

        for (anal, label, lineformat) in self.datasets:
            max_percents = []
            for node in self.__get_nodes(anal):
                times = {}
                d = anal.get_data_bootstrapping(node)
                if d is None:
                    continue
                for secstr in d:
                    percent = int(d[secstr])
                    sec = int(secstr)
                    times[sec] = percent
                if len(times) > 0:
                    secs = sorted(list(times.keys()))
                    last_sec = secs[-1]
                    last_percent = d[str(last_sec)]
                    max_percents.append(last_percent)
            if len(max_percents) > 0:
                if fig is None:
                    fig = pyplot.figure()
                x, y = getcdf(max_percents, shownpercentile=1.0)
                pyplot.plot(x, y, lineformat, label=label)

        if fig != None:
            pyplot.xlabel("Max Bootstrap Percent")
            pyplot.ylabel("Cumulative Fraction")
            pyplot.title("distribution of highest bootstrapping percents across nodes")
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

    def __plot_bootstrapping_completed_times(self):
        fig = None

        for (anal, label, lineformat) in self.datasets:
            completed_times = []
            for node in self.__get_nodes(anal):
                times = {}
                d = anal.get_data_bootstrapping(node)
                if d is None:
                    continue
                for secstr in d:
                    percent = int(d[secstr])
                    sec = int(secstr)
                    times[sec] = percent
                if len(times) > 0:
                    secs = sorted(list(times.keys()))
                    first_sec, last_sec = secs[0], secs[-1]
                    first_percent, last_percent = d[str(first_sec)], d[str(last_sec)]
                    # first_percent may not be 0 if it reached a higher percent
                    # in the same second as it started
                    if last_percent == 100:
                        completed_times.append(last_sec - first_sec)
            if len(completed_times) > 0:
                if fig is None:
                    fig = pyplot.figure()
                x, y = getcdf(completed_times, shownpercentile=1.0)
                pyplot.plot(x, y, lineformat, label=label)

        if fig != None:
            pyplot.xlabel("Time to Complete Bootstrapping (s)")
            pyplot.ylabel("Cumulative Fraction")
            pyplot.title("distribution of bootstrapping completion times across nodes")
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

    def __plot_goodput_timeseries(self, direction):
        fig = None

        for (anal, label, lineformat) in self.datasets:
            gput = {}
            for node in self.__get_nodes(anal):
                d = anal.get_data_bandwidth(node)
                if d is None:
                    continue
                if direction in d:
                    for secstr in d[direction]:
                        bytes = int(d[direction][secstr])
                        sec = int(secstr)
                        gbit = bytes*8.0/1000.0/1000.0/1000.0
                        gput.setdefault(sec, 0)
                        gput[sec] += gbit

            if len(gput) > 0:
                if fig == None:
                    fig = pyplot.figure()
                x = list(gput.keys())
                x.sort()
                y = [gput[sec] for sec in x]
                #if numpy.median(y) > 1000.0:
                #    y[:] = [mbit/1000.0 for mbit in y]
                if len(y) > 20:
                    y = movingaverage(y, len(y)*0.05)
                start_sec = min(x)
                x[:] = [sec - start_sec for sec in x]
                pyplot.plot(x, y, lineformat, label=label)

        if fig != None:
            pyplot.xlabel("Tick (s)")
            pyplot.ylabel("Goodput (Gbit/s)")
            pyplot.title("moving average of the sum of {} goodput for all nodes over time".format('read' if 'read' in direction else 'write'))
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

    def __plot_bandwidth_summary(self, bwkey):
        fig = None

        for (anal, label, lineformat) in self.datasets:
            values = []
            for node in self.__get_nodes(anal):
                d = anal.get_data_bandwidth_summary(node)
                if d is None:
                    continue
                if bwkey in d:
                    bytes = int(d[bwkey])
                    mbit = bytes*8.0/1000.0/1000.0
                    values.append(mbit)
            if len(values) > 0:
                if fig is None:
                    fig = pyplot.figure()
                x, y = getcdf(values, shownpercentile=1.0)
                pyplot.plot(x, y, lineformat, label=label)

        if fig != None:
            xstatlabel = bwkey.split('_')[-1].lower()
            direction = 'read' if 'read' in bwkey else 'write'
            if xstatlabel == 'total':
                xlabelstr = "{} Data {} (Mbit)".format(xstatlabel.capitalize(), direction.capitalize())
            else:
                xlabelstr = "{} Goodput (Mbit/s)".format(xstatlabel.capitalize())
            pyplot.xlabel(xlabelstr)
            pyplot.ylabel("Cumulative Fraction")
            pyplot.title("distribution of: {} {} goodput per second, across nodes".format(xstatlabel, direction))
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

# helper - compute the window_size moving average over the data in interval
def movingaverage(interval, window_size):
    window = numpy.ones(int(window_size)) / float(window_size)
    return numpy.convolve(interval, window, 'same')

# # helper - cumulative fraction for y axis
def cf(d): return numpy.arange(1.0, float(len(d)) + 1.0) / float(len(d))

# # helper - return step-based CDF x and y values
# # only show to the 99th percentile by default
def getcdf(data, shownpercentile=0.99, maxpoints=10000.0):
    data = sorted(data)
    frac = cf(data)
    k = len(data) / maxpoints
    x, y, lasty = [], [], 0.0
    for i in iter(range(int(round(len(data) * shownpercentile)))):
        if i % k > 1.0: continue
        assert not numpy.isnan(data[i])
        x.append(data[i])
        y.append(lasty)
        x.append(data[i])
        y.append(frac[i])
        lasty = frac[i]
    return x, y
