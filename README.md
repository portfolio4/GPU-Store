# GPU Store

GPU Store runs a transaction database, matching engine, gateway, market data
service, and simulated supplier, with a Python web server serving the trading UI.

## Prerequisites

- Linux: the services use Linux/POSIX networking APIs and UDP multicast.
- CMake **3.28 or newer**.
- A C++ compiler and standard library supporting **C++23 or later**, including
  `std::format`, `std::print`/`std::println`, and `std::expected`. A compiler that
  accepts `-std=c++23` still needs a sufficiently recent standard library.
- A build tool, such as Make or Ninja.
- Python 3 and its development headers/libraries, for building the Python bindings.
- **pybind11**, including its headers and CMake package configuration.
- **Catch2 3.x**, including its CMake package configuration.
- Python packages **FastAPI**, **Uvicorn**, **Pydantic**, and a Uvicorn WebSocket
  backend: **websockets** or **wsproto**.
- A browser with JavaScript and WebSocket support.

The nlohmann/json header is included in this repository. The `flip` and `flop`
Python modules are built here; they do not need to be installed from PyPI.
There is no Node.js or frontend package installation step.

For a Debian/Ubuntu system with sufficiently recent packages, an example setup is:

```bash
sudo apt update
sudo apt install build-essential cmake python3 python3-dev pybind11-dev catch2 \
    python3-fastapi python3-uvicorn python3-pydantic python3-websockets
```

Check that your distribution provides CMake >= 3.28 and Catch2 >= 3, as well as
the required C++23 library features. Older distributions may require newer
toolchain packages or separately installed dependencies.

Use the same Python interpreter to build the bindings and run the web server.
If you use a virtual environment, install the Python runtime packages there and
pass `-DPython_EXECUTABLE="$(command -v python3)"` when configuring CMake.

## Build

Run these commands from the repository root (`GPU-Store/`):

```bash
cmake -S store -B store/build
cmake --build store/build -j
```

This builds service binaries in `store/build/bin`, Python bindings and modules in
`store/build/python`, and copies the launcher and web assets into `store/build`.
Run the build command again after changing source files, Python code, or UI assets
so that the copies used at runtime are updated. Restart the web server after
changing its Python code.

## Run

Use two terminals. Both commands must run from `store/build`, because the launcher
and web server resolve binaries, Python modules, and static assets relative to
the current directory.

### 1. Start the backend

From the repository root, in the first terminal:

```bash
cd store/build
python3 launch.py ../config.json funxuser@region-city-server0
```

The launcher starts the transaction database, matching engine, gateway, market
data service, and supplier in order. Allow a few seconds for startup before
starting the web server. The supplier generates simulated orders for the UI.

`funxuser@region-city-server0` is a configuration key in `store/config.json`,
not an SSH destination or a required local username/hostname. If you rename that
key, use its new name in the command.

### 2. Start the web server

From the repository root, in the second terminal:

```bash
cd store/build
python3 bin/webserver ../config.json Webserver_0
```

The web server connects to the gateway's retail sessions and subscribes to market
data snapshots. It runs separately from the backend launcher.

### 3. Open the UI

Open **http://127.0.0.1:8000/trade.html** in your browser.

Select a product to view its order book, then use the order form to submit orders.
Order statuses appear under **Open Orders**, and fills appear under **Trade
History**. The demo assumes company ID 2 (retail); there is no login step.

### Stop

Press **Ctrl+C** in the web server terminal and then in the launcher terminal.
The launcher shuts down the backend processes it started. After restarting the
web server, refresh the browser page to reconnect live updates.

## Configuration and logs

`store/config.json` defines service instances, addresses, ports, and launcher
groups. By default:

- Web UI: `127.0.0.1:8000`.
- Database and matching services: TCP ports `9999`, `9998`, and `9997`.
- Gateway sessions: TCP ports `20000`, `20001`, `21000`, and `21001`.
- Market data: UDP multicast `239.0.0.1:30000` (incrementals) and
  `239.0.0.2:31000` (snapshots).
- Backend logs: `/tmp/logs/<instance>.log`; web server output goes to its terminal.
- Transaction storage: `/tmp/TransactionDb_0_storage.bin`.

Keep these ports available and allow local UDP multicast delivery. Change the
configuration if you need different addresses, ports, or storage paths.

For example, follow gateway logs with:

```bash
tail -f /tmp/logs/GatewayService_0.log
```

If the UI stops updating, refresh the page and check the web server terminal for
errors. If HTTP requests work but live updates do not, check the browser's `/ws`
connection and ensure the Python environment has `websockets` or `wsproto`.

## Basic benchmarking

`store/latencies.txt` contains sample results from a very basic benchmarking setup
intended to give a quick overview of system latency. These measurements run all
services on a single laptop. Results are in **nanoseconds**: the sample averages
are roughly **30 microseconds for add/cancel** and **150 microseconds for a trade**
(measured from the aggressor's submission to the passive client's trade response).

For trades, the matching service waits for the transaction service to acknowledge it saved
the transaction to disk before publishing matching events. This storage round trip
likely contributes to the higher latency and would be much lower if the matching
engine optimistically assumed every transaction file write would succeed.

Serious profiling would require more workload
scenarios and testing in an actual data center, with services distributed across
machines, to assess performance under realistic deployment conditions.

## Tests

After building, run from `store/build` (the same directory used to launch services):

```bash
ctest --output-on-failure
```

This runs the C++ Catch2 tests.
