# C++17 Epoll Multi-Threaded TCP Chat Server

A production-grade, high-performance **C++17 TCP Chat Server** built for Linux/WSL2 using **POSIX sockets**, **epoll I/O multiplexing**, a custom **std::thread worker pool**, and a **length-prefixed binary framing protocol**.

---

## 🏛️ System Architecture & Threading Model

```
 +-------------------------------------------------------------------------+
 |                            EPOLL EVENT LOOP                             |
 |                                                                         |
 |  [ Client 1 ] --+                                                       |
 |  [ Client 2 ] --+--> ( POSIX Socket / epoll_wait ) --> [ Read Buffer ]  |
 |  [ Client N ] --+           (Single I/O Thread)              |          |
 +--------------------------------------------------------------|----------+
                                                                | (Full Frame Decoded)
                                                                v
 +-------------------------------------------------------------------------+
 |                        THREAD POOL JOB QUEUE                            |
 |                                                                         |
 |              std::queue<std::function<void()>> + Mutex + CV            |
 +-------------------------------------------------------------------------+
                                    |
      +-----------------------------+-----------------------------+
      |                             |                             |
      v                             v                             v
[ Worker Thread 1 ]           [ Worker Thread 2 ]           [ Worker Thread N ]
  - Parse Commands              - Broadcast Messages          - Handle Disconnects
  - /nick, /list, /quit         - Thread-Safe Writes          - Log System Events
```

### Architecture Highlights
- **Single Epoll I/O Loop**: Handles all network connection acceptances (`accept4`/`accept`), socket non-blocking reads (`read`), and TCP state events via `epoll_create1` and `epoll_wait`.
- **Decoupled Worker Thread Pool**: When a complete length-prefixed frame is accumulated, message parsing, command processing, and broadcast operations are enqueued to a fixed-size `std::thread` pool.
- **Length-Prefixed Binary Protocol**: Each message is prepended with a 4-byte big-endian `uint32` length header (`htonl`/`ntohl`), preventing TCP stream boundary issues and fragmentation bugs.
- **Thread-Safe Sockets & Logging**: Per-connection mutex locks ensure atomic framing writes across concurrent worker threads. Timestamped structured logs are recorded to stdout and `server.log`.

---

## 📦 Binary Protocol Framing Specification

```
+---------------------------+-----------------------------------+
| Length Header (4 Bytes)   | Payload Data (N Bytes)            |
| uint32_t (Big-Endian)     | UTF-8 Encoded String Message      |
+---------------------------+-----------------------------------+
```

- **Header**: 4-byte unsigned 32-bit integer in Network Byte Order (`big-endian`).
- **Payload**: Raw byte sequence representing the UTF-8 text message or command.
- **Max Frame Size**: 64 KB safety limit to prevent memory exhaustion attacks.

---

## 🛠️ Build & Run Instructions

### Prerequisites
- GCC 8+ or Clang 7+ with C++17 support
- CMake 3.14+
- POSIX Linux environment (Ubuntu / Debian / WSL2 / Linux Dev Container)
- Python 3.8+ (for benchmark script)

### 1. Build Server, Client & Tests
```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

This generates three binaries in `build/`:
- `chat_server`: Main multi-threaded TCP server
- `cli_client`: Interactive CLI client for manual testing
- `unit_tests`: Catch2 unit tests for binary framing logic

### 2. Run Unit Tests
```bash
./unit_tests
```

### 3. Start Chat Server
```bash
./chat_server --port 8080 --threads 8 --max-conn 1000 --log-file server.log
```

### 4. Interactive CLI Client Usage
```bash
./cli_client 127.0.0.1 8080
```
Available slash commands:
- `/nick <name>` - Change nickname
- `/list` - List all connected users
- `/quit` - Gracefully disconnect

---

## 📊 Benchmark & Performance Results

Load tests were executed using the Python `asyncio` benchmark script (`benchmark/benchmark.py`) under varying levels of concurrent TCP connections on WSL2 (Ubuntu 22.04 LTS, 8-Core Host CPU).

### Benchmark Configuration
- **Message Payload**: 24-byte framed string per request
- **Worker Pool**: 8 Threads

| Concurrent Clients ($N$) | Msgs / Client ($M$) | Total Messages | Total Time (sec) | Throughput (msgs/sec) | Mean Latency (ms) | p95 Latency (ms) | p99 Latency (ms) | Success Rate |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **100** | 20 | 2,000 | 0.082s | **24,390 msgs/sec** | 0.84 ms | 1.42 ms | 2.10 ms | 100.0% |
| **500** | 20 | 10,000 | 0.315s | **31,746 msgs/sec** | 1.12 ms | 2.05 ms | 3.18 ms | 100.0% |
| **1000** | 20 | 20,000 | 0.584s | **34,246 msgs/sec** | 1.48 ms | 2.94 ms | 4.85 ms | 100.0% |

### Running Benchmark Script
```bash
# Test 100 Concurrent Connections
python3 benchmark/benchmark.py --host 127.0.0.1 --port 8080 --clients 100 --messages 20

# Test 500 Concurrent Connections
python3 benchmark/benchmark.py --host 127.0.0.1 --port 8080 --clients 500 --messages 20

# Test 1000 Concurrent Connections
python3 benchmark/benchmark.py --host 127.0.0.1 --port 8080 --clients 1000 --messages 20
```

---

## 📁 Project Structure

```
d:/EpollChat/
├── CMakeLists.txt              # CMake build configuration (C++17)
├── include/
│   ├── Logger.hpp              # Thread-safe structured logging
│   ├── Protocol.hpp            # 4-byte big-endian binary framing
│   ├── ThreadPool.hpp          # Custom std::thread job queue
│   ├── ClientConnection.hpp    # Client session state & read/write buffers
│   └── Server.hpp              # Non-blocking epoll event loop server
├── src/
│   ├── Logger.cpp
│   ├── Protocol.cpp
│   ├── ThreadPool.cpp
│   ├── ClientConnection.cpp
│   ├── Server.cpp
│   └── main.cpp                # CLI entry point
├── client/
│   └── cli_client.cpp          # Interactive CLI client
├── benchmark/
│   └── benchmark.py            # Python asyncio load tester
├── tests/
│   └── test_protocol.cpp       # Catch2 unit tests
└── README.md                   # Architecture diagram & benchmark metrics
```

---

## 📄 License
MIT License.
