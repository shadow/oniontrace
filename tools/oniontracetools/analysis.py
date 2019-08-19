'''
  oniontracetools
  Authored by Rob Jansen, 2015
  See LICENSE for licensing information
'''

import sys, os, re, json, datetime, logging

from multiprocessing import Pool, cpu_count
from signal import signal, SIGINT, SIG_IGN
from socket import gethostname
from abc import ABCMeta, abstractmethod

from numpy import min, max, median, mean, std

# oniontracetools imports
from oniontracetools._version import __version__
import oniontracetools.util as util

class Analysis(object):

    def __init__(self, nickname=None, ip_address=None):
        self.nickname = nickname
        self.measurement_ip = ip_address
        self.hostname = gethostname().split('.')[0]
        self.json_db = {'type':'oniontrace', 'version':__version__, 'data':{}}
        self.log_filepaths = []
        self.date_filter = None
        self.did_analysis = False

    def add_log_file(self, filepath):
        self.log_filepaths.append(filepath)

    def get_nodes(self):
        return self.json_db['data'].keys()

    def get_data_bootstrapping(self, node):
        try:
            return self.json_db['data'][node]['oniontrace']['bootstrapping']
        except:
            return None

    def get_data_bandwidth(self, node):
        try:
            return self.json_db['data'][node]['oniontrace']['bandwidth']
        except:
            return None

    def get_data_bandwidth_summary(self, node):
        try:
            return self.json_db['data'][node]['oniontrace']['bandwidth_summary']
        except:
            return None

    def analyze(self, do_complete=False, date_filter=None):
        if self.did_analysis:
            return

        self.date_filter = date_filter
        oniontrace_parser = OnionTraceParser(date_filter=self.date_filter)

        for (filepaths, parser, json_db_key) in [(self.log_filepaths, oniontrace_parser, 'oniontrace')]:
            if len(filepaths) > 0:
                for filepath in filepaths:
                    logging.info("parsing log file at {}".format(filepath))
                    parser.parse(util.DataSource(filepath), do_complete=do_complete)

                if self.nickname is None:
                    parsed_name = parser.get_name()
                    if parsed_name is not None:
                        self.nickname = parsed_name
                    elif self.hostname is not None:
                        self.nickname = self.hostname
                    else:
                        self.nickname = "unknown"

                if self.measurement_ip is None:
                    self.measurement_ip = "unknown"

                self.json_db['data'].setdefault(self.nickname, {'measurement_ip': self.measurement_ip}).setdefault(json_db_key, parser.get_data())

        self.did_analysis = True

    def merge(self, analysis):
        for nickname in analysis.json_db['data']:
            if nickname in self.json_db['data']:
                raise Exception("Merge does not yet support multiple Analysis objects from the same node \
(add multiple files from the same node to the same Analysis object before calling analyze instead)")
            else:
                self.json_db['data'][nickname] = analysis.json_db['data'][nickname]

    def save(self, filename=None, output_prefix=os.getcwd(), do_compress=True):
        if filename is None:
            if self.date_filter is None:
                filename = "oniontrace.analysis.json.xz"
            else:
                filename = "{}.oniontrace.analysis.json.xz".format(util.date_to_string(self.date_filter))

        filepath = os.path.abspath(os.path.expanduser("{}/{}".format(output_prefix, filename)))
        if not os.path.exists(output_prefix):
            os.makedirs(output_prefix)

        logging.info("saving analysis results to {}".format(filepath))

        outf = util.FileWritable(filepath, do_compress=do_compress)
        json.dump(self.json_db, outf, sort_keys=True, separators=(',', ': '), indent=2)
        outf.close()

        logging.info("done saving analysis results!")

    @classmethod
    def load(cls, filename="oniontrace.analysis.json.xz", input_prefix=os.getcwd()):
        filepath = os.path.abspath(os.path.expanduser("{}".format(filename)))
        if not os.path.exists(filepath):
            filepath = os.path.abspath(os.path.expanduser("{}/{}".format(input_prefix, filename)))
            if not os.path.exists(filepath):
                logging.warning("file does not exist at '{}'".format(filepath))
                return None

        logging.info("loading analysis results from {}".format(filepath))

        inf = util.DataSource(filepath)
        inf.open()
        db = json.load(inf.get_file_handle())
        inf.close()

        logging.info("finished loading analysis file, checking type and version")

        if 'type' not in db or 'version' not in db:
            logging.warning("'type' or 'version' not present in database")
            return None
        elif db['type'] != 'oniontrace':
            logging.warning("type '{}' not supported (expected type='oniontrace')".format(db['type']))
            return None
        elif db['version'] != __version__:
            logging.warning("version '{}' not supported (expected version='{}')".format(db['version'], __version__))
            return None
        else:
            logging.info("type '{}' and version '{}' is supported".format(db['type'], db['version']))
            analysis_instance = cls()
            analysis_instance.json_db = db
            return analysis_instance

class SerialAnalysis(Analysis):

    def analyze(self, paths, do_complete=False, date_filter=None):
        logging.info("processing input from {} paths...".format(len(paths)))

        analyses = []
        for path in paths:
            a = Analysis()
            a.add_log_file(path)
            a.analyze(do_complete=do_complete, date_filter=date_filter)
            analyses.append(a)

        logging.info("merging {} analysis results now...".format(len(analyses)))
        while analyses is not None and len(analyses) > 0:
            self.merge(analyses.pop())
        logging.info("done merging results: {} total nicknames present in json db".format(len(self.json_db['data'])))

def subproc_analyze_func(analysis_args):
    signal(SIGINT, SIG_IGN)  # ignore interrupts
    a = analysis_args[0]
    do_complete = analysis_args[1]
    date_filter = analysis_args[2]
    a.analyze(do_complete=do_complete, date_filter=date_filter)
    return a

class ParallelAnalysis(Analysis):

    def analyze(self, paths, do_complete=False, date_filter=None,
        num_subprocs=cpu_count()):
        logging.info("processing input from {} paths...".format(len(paths)))

        analysis_jobs = []
        for path in paths:
            a = Analysis()
            a.add_log_file(path)
            analysis_args = [a, do_complete, date_filter]
            analysis_jobs.append(analysis_args)

        analyses = None
        pool = Pool(num_subprocs if num_subprocs > 0 else cpu_count())
        try:
            mr = pool.map_async(subproc_analyze_func, analysis_jobs)
            pool.close()
            while not mr.ready(): mr.wait(1)
            analyses = mr.get()
        except KeyboardInterrupt:
            logging.info("interrupted, terminating process pool")
            pool.terminate()
            pool.join()
            sys.exit()

        logging.info("merging {} analysis results now...".format(len(analyses)))
        while analyses is not None and len(analyses) > 0:
            self.merge(analyses.pop())
        logging.info("done merging results: {} total nicknames present in json db".format(len(self.json_db['data'])))

def parse_tagged_csv_string(csv_string):
    d = {}
    parts = csv_string.strip('[]').split(',')
    for key_value_pair in parts:
        pair = key_value_pair.split('=')
        if len(pair) < 2:
            continue
        d[pair[0]] = pair[1]
    return d

class Parser(object):
    __metaclass__ = ABCMeta
    @abstractmethod
    def parse(self, source, do_complete):
        pass
    @abstractmethod
    def get_data(self):
        pass
    @abstractmethod
    def get_name(self):
        pass

class OnionTraceParser(Parser):

    def __init__(self, date_filter=None):
        ''' date_filter should be given in UTC '''
        self.bootstrapping = {}
        self.bandwidth = {'bytes_read': {}, 'bytes_written': {}}
        self.bandwidth_summary = {'bytes_read_total': 0, 'bytes_written_total': 0}
        self.name = None
        self.date_filter = date_filter
        self.version_mismatch = False
        self.boot_succeeded = False
        self.start_ts = None

    def __is_date_valid(self, date_to_check):
        if self.date_filter is None:
            # we are not asked to filter, so every date is valid
            return True
        else:
            # we are asked to filter, so the line is only valid if the date matches the filter
            # both the filter and the unix timestamp should be in UTC at this point
            return util.do_dates_match(self.date_filter, date_to_check)

    def __parse_line(self, line, do_complete):
        if self.name is None and re.search("Starting\sOnionTrace\sv", line) is not None:
            parts = line.strip().split()

            if len(parts) < 11:
                return True

            version_str = parts[7].strip('v')
            if version_str != __version__:
                self.version_mismatch = True
                logging.warning("Version mismatch: the log file we are parsing was generated using \
                OnionTrace v{}, but this version of OnionTraceTools is v{}".format(version_str, __version__))
                return True

            self.start_ts = float(parts[2])
            second = int(round(self.start_ts))
            self.bootstrapping[second] = 0

            self.name = parts[10]

        if self.date_filter is not None:
            parts = line.strip().split(' ', 3)
            if len(parts) < 4: # the 3rd is the timestamp, the 4th is the rest of the line
                return True
            unix_ts = float(parts[2])
            line_date = datetime.datetime.utcfromtimestamp(unix_ts).date()
            if not self.__is_date_valid(line_date):
                return True

        #elif do_complete and re.search("stream-status", line) is not None:
        #    pass

        elif not self.boot_succeeded:
            if re.search("Tor\shas\snot\syet\sfully\sbootstrapped", line) is not None:
                parts = line.strip().split()

                if len(parts) < 15:
                    return True

                second = int(float(parts[2]))
                percent = int(parts[14].strip(')').split('/')[0])

                self.bootstrapping[second] = percent

            elif re.search("Bootstrapped\s100", line) is not None:
                parts = line.strip().split()

                if len(parts) < 3:
                    return True

                second = int(float(parts[2]))

                self.bootstrapping[second] = 100
                self.boot_succeeded = True

        elif self.boot_succeeded and re.search("Logger:\s650\sBW\s", line) is not None:
            parts = line.strip().split()

            if len(parts) < 10:
                return True

            second = int(float(parts[2]))
            bwr = int(parts[8])
            bww = int(parts[9])

            self.bandwidth['bytes_read'].setdefault(second, 0)
            self.bandwidth['bytes_written'].setdefault(second, 0)

            self.bandwidth['bytes_read'][second] += bwr
            self.bandwidth['bytes_written'][second] += bww

            self.bandwidth_summary['bytes_read_total'] += bwr
            self.bandwidth_summary['bytes_written_total'] += bww

        return True

    def parse(self, source, do_complete=False):
        source.open()
        for line in source:
            if self.version_mismatch:
                break
            # ignore line parsing errors
            try:
                if not self.__parse_line(line, do_complete):
                    break
            except:
                logging.warning("OnionTraceParser: skipping line due to parsing error: {}".format(line))
                raise
                continue
        source.close()
        self.__finalize_summary_stats()

    def __finalize_summary_stats(self):

        if self.boot_succeeded and \
                len(self.bandwidth['bytes_read']) > 0 and len(self.bandwidth['bytes_written']) > 0:
            bytes_read = list(self.bandwidth['bytes_read'].values())
            bytes_written = list(self.bandwidth['bytes_written'].values())

            # json cannot serialize numpy int64s, so make sure we convert to python ints

            self.bandwidth_summary['bytes_read_min'] = int(min(bytes_read))
            self.bandwidth_summary['bytes_written_min'] = int(min(bytes_written))

            self.bandwidth_summary['bytes_read_max'] = int(max(bytes_read))
            self.bandwidth_summary['bytes_written_max'] = int(max(bytes_written))

            self.bandwidth_summary['bytes_read_median'] = int(median(bytes_read))
            self.bandwidth_summary['bytes_written_median'] = int(median(bytes_written))

            self.bandwidth_summary['bytes_read_mean'] = int(mean(bytes_read))
            self.bandwidth_summary['bytes_written_mean'] = int(mean(bytes_written))

            self.bandwidth_summary['bytes_read_std'] = int(std(bytes_read))
            self.bandwidth_summary['bytes_written_std'] = int(std(bytes_written))

    def get_data(self):
        have_bw = True if len(self.bandwidth['bytes_read']) > 0 and len(self.bandwidth['bytes_written']) > 0 else False
        return {
            'bootstrapping': self.bootstrapping if len(self.bootstrapping) > 0 else None,
            'bandwidth': self.bandwidth if have_bw else None,
            'bandwidth_summary': self.bandwidth_summary if have_bw else None,
        }

    def get_name(self):
        return self.name
