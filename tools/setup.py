#!/usr/bin/env python

from distutils.core import setup

setup(name='OnionTraceTools',
      version="0.0.1",
      description='A utility to analyze and visualize OnionTrace output',
      author='Rob Jansen',
      url='https://github.com/shadow/oniontrace',
      packages=['oniontracetools'],
      scripts=['oniontracetools/oniontracetools'],
     )
