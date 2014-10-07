#!/usr/bin/arch -i386 python2.7
#
# Author: Kevin Klues <klueska@cs.berkeley.edu>

import os
import shutil
import matplotlib
import matplotlib as mpl
matplotlib.use('Agg')
from pylab import *
import numpy as np

class TestData:
  def __init__(self, line):
    self.line = line
    line = self.line.split(':')
    self.event = line[0]
    self.id = int(line[1])
    self.time = double(line[2])
  
  def __str__(self):
    return self.line

def parse_file(f):
  skipped = 0
  fdata = file(f)
  lines = fdata.readlines()
  for l in lines:
    if l[:3] == "EV_":
      break
    skipped += 1

  data = []
  map(lambda x: data.append(TestData(x)), lines[skipped:])
  return data

def get_response_times(data):
  rsp_times = {}
  start_times = {}
  for d in data:
    if d.event == 'EV_CALL_SEND_START':
      start_times[d.id] = d.time
    if d.event == 'EV_CALL_DESTROYED':
      if d.id in start_times:
        rsp_times[d.id] = d.time - start_times[d.id]
  return rsp_times

def total_sent_received(data, folder):
  title("Total Packets Sent and Received")
  xlabel("Time (s)")
  ylabel("Total Packets")
  recv_start = {}
  send_times = [d.time - data[0].time for d in data if d.event == 'EV_CALL_SEND_START']
  recv_times = []
  for d in data:
    if d.event == 'EV_CALL_RECV_START':
        recv_start[d.id] = True
    if d.event == 'EV_CALL_DESTROYED':
      if d.id in recv_start:
        recv_times.append(d.time - data[0].time)
  count = range(len(send_times))
  plot(send_times, range(len(send_times)), label='Total Sent')
  plot(recv_times, range(len(recv_times)), label='Total Received')
  legend(loc='lower right')
  figname = folder + "/total_sent_received.png"
  savefig(figname)
  clf()

def response_times(data, folder):
  title("Response Time Per Packet")
  xlabel("Packet Number")
  ylabel("Response Time (s)")
  rsp_times = get_response_times(data)
  plot(rsp_times.keys(), rsp_times.values(), marker='+', linestyle='None')
  figname = folder + "/response_times.png"
  savefig(figname)
  clf()

def ordered_response_times(data, folder):
  title("Ordered Response Times")
  xlabel("Rearranged Packet")
  ylabel("Response Time (s)")
  rsp_times = get_response_times(data)
  rsp_times = [(i,t) for i,t in rsp_times.items()]
  rsp_times.sort(key=lambda x: x[1])
  plot(range(len(rsp_times)), [x[1] for x in rsp_times], marker='+', linestyle='None')
  figname = folder + "/ordered_response_times.png"
  savefig(figname)
  clf()

def response_time_distribution(data, folder):
  rsp_times = get_response_times(data)
  rsp_times = [t for t in rsp_times.values()]
  rsp_times.sort()
  num_bins = 20
  bin_size = rsp_times[-1]/num_bins
  dist = [0]*num_bins
  for t in rsp_times:
    bin_idx = int(t/bin_size)
    if bin_idx == num_bins:
      bin_idx -= 1
    dist[bin_idx] += 1
  
  bar_width = 1.0
  index = np.arange(num_bins)
  b = bar(index, dist, bar_width, color='b')
  xticks(index + bar_width/2.0, ['']*num_bins)

  title("Response Time Distribution (%s Total Packets)" % len(rsp_times))
  ylabel("Number of Packets")
  xlabel("Response Time Ranges (%sms per bar)" % int(bin_size*1000))
  legend([b], ["%sms per bar" % int(bin_size*1000)])
  figname = folder + "/response_time_distribution.png"
  savefig(figname)
  clf()

def generate_graphs(args):
  data = []
  for f in args.input_files.split():
    data.extend(parse_file(f))
  os.mkdir(args.output_folder)
  for f in args.input_files.split():
    shutil.copyfile(f, args.output_folder + '/' + args.output_folder + '.dat')

  total_sent_received(data, args.output_folder)
  response_times(data, args.output_folder)
  ordered_response_times(data, args.output_folder)
  response_time_distribution(data, args.output_folder)

