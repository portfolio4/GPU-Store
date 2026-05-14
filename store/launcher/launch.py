#!/usr/bin/env python3

import json
import os
import pathlib
import subprocess
import signal
import sys
import time


if len(sys.argv) != 3:
    print("Provide 2 arguments: the config.json file and the server instance name")
    sys.exit(1)

config_file = sys.argv[1]
instance = sys.argv[2]

with open(config_file, "r") as f:
    config = json.load(f)

logdir = config[instance]["logdir"]
pathlib.Path(logdir).mkdir(parents=True, exist_ok=True)

services = config[instance]["services"]

# do some validation before spawning anything
def DoValidation():
    pinned_cores = {}
    service_names = set()
    for s in services:
        name = s["name"]
        if name in service_names:
            raise ValueError(f"{instance}: cannot run service {name} twice")
        service_names.add(name)

        core = s["core"]
        if core in pinned_cores:
            raise ValueError(f"{instance}: {name} and {pinned_cores[core]} are both assigned to core {core}")
        pinned_cores[core] = name
DoValidation()

class RunnableService(object):
    def __init__(self, binary, service_instance):
        self._service_instance = service_instance
        self._logfile = os.path.join(logdir, f"{service_instance}.log")
        self._command = [binary, config_file, service_instance]
        self._process = None
        self._log_handle = None

    def Start(self):
        # should never happen, but check this just in case
        if self._process is not None:
            print(f"{self._service_instance} already started. Not starting it again")
            return

        self._log_handle = open(self._logfile, "a")
        try:
            self._process = subprocess.Popen(
                self._command,
                stdout=self._log_handle,
                stderr=subprocess.STDOUT,
            )
        except Exception:
            self._log_handle.close()
            self._log_handle = None
            raise
        print(f"Started {self._service_instance}, pid={self._process.pid}")

    def Stop(self):
        if self._process is None:
            return

        print(f"Stopping {self._service_instance} pid={self._process.pid}")
        self._process.terminate()
        try:
            self._process.wait(timeout=5)
            print(f"{self._service_instance} stopped successfully")
        except subprocess.TimeoutExpired:
            print(f"{self._service_instance} did not stop on it's own, forcefully killing it")
            self._process.kill()
            self._process.wait()
            print(f"{self._service_instance} stopped via kill")

        self._process = None
        self._log_handle.close()
        self._log_handle = None

processes = []
for s in services:
    service_instance = s["name"]
    binary = os.path.join("bin", config[service_instance]["binary"])
    processes.append(RunnableService(binary, service_instance))

for index, p in enumerate(processes):
    p.Start()
    time.sleep(1)

stop_signal_received = False
def signal_handler(sig, frame):
    """Handles termination signals (like Ctrl+C or kill) gracefully."""
    global stop_signal_received
    print(f"\nSignal {sig} received. Preparing to exit gracefully...")
    stop_signal_received = True

signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

while not stop_signal_received:
    try:
        time.sleep(5)
    except:
        pass

print("Caught stop signal, stopping all processes")
for p in processes:
    p.Stop()
sys.exit(0)

# TODO (non-MVP):
#
# If we wanted to productionize this, we would have a --test-run argument for
# every binary. That way, when we add/remove/edit services on a host, we can
# sanity check things will still start up. Deferring this because we have
# to add argument parsing to the C++ binaries which is somewhat annoying to
# set up from scratch
#
# Ideally, we can also start/stop individual services in case they encounter
# problems intraday
#
# We can also check `still_running = (process.poll() is None)` and if any
# process is not running when it should be, then alert a human (e.g. trigger
# pager) and maybe automatically try to restart the process depending on
# what service it is

