# Implementation Plan - C++17 Epoll Multi-threaded TCP Chat Server

Build a high-performance, production-quality multi-threaded TCP chat server in **C++17** targeting Linux/WSL2 using POSIX sockets (`sys/socket.h`), `epoll` I/O multiplexing, a custom `std::thread` pool for message processing, and length-prefixed binary protocol framing.

## Architectural Decisions & Options for User Review

> [!IMPORTANT]
> Per requirement in `prompt.txt`, please review the following proposed choices before we begin implementation:

1. **Testing Framework**: Catch2 (via CMake `FetchContent`) for testing protocol framing/parsing logic. Catch2 is clean, modern, and does not require pre-installed system libraries. (Alternative: GoogleTest or a lightweight header-only test runner).
2. **Thread Pool Sizing & Queue Strategy**:
   - Fixed worker thread pool size specified via CLI argument `--threads N` (default: number of hardware threads `std::thread::hardware_concurrency()`).
   - Thread-safe job queue using `std::queue`, `std::mutex`, `std::condition_variable`, and `std::atomic<bool>` for graceful shutdown.
3. **Protocol Framing & Binary Format**:
   - 4-byte big-endian uint32 payload length header followed by string payload.
   - Buffer accumulators in `ClientConnection` handle partial reads, short reads, and packet fragmentation across socket boundaries.
4. **I/O & Worker Thread Decoupling**:
   - Single epoll event loop thread handles accepting connections, socket readability/writability events, and connection lifecycle.
   - When a complete binary frame is decoded, the message task is dispatched to the worker thread pool for processing, command execution (`/nick`, `/list`, `/quit`), and broadcasting.
5. **Benchmark Suite**:
   - Python 3 (`asyncio`) load-tester script in `benchmark/benchmark.py` that spawns $N$ concurrent clients, sends $M$ length-prefixed binary messages each, and measures throughput (msgs/sec), mean/p95/p99 latency, and success rate under 100, 500, and 1000 concurrent connections.

---

## Proposed Project Structure

```
d:/EpollChat/
├── CMakeLists.txt              # CMake build configuration (C++17 standard)
├── include/
│   ├── Logger.hpp              # Thread-safe structured logging (stdout + file)
│   ├── Protocol.hpp            # Binary framing encoder/decoder (4-byte length prefix)
│   ├── ThreadPool.hpp          # Fixed-size std::thread job queue
│   ├── ClientConnection.hpp    # Client session state, read/write buffers, nick, ID
│   └── Server.hpp              # Epoll event loop & socket management
├── src/
│   ├── Logger.cpp
│   ├── Protocol.cpp
│   ├── ThreadPool.cpp
│   ├── ClientConnection.cpp
│   ├── Server.cpp
│   └── main.cpp                # CLI entry point (arg parsing: --port, --max-conn, --threads)
├── client/
│   └── cli_client.cpp          # Minimal interactive CLI client for manual testing
├── benchmark/
│   └── benchmark.py            # Python asyncio load-testing script
├── tests/
│   └── test_protocol.cpp       # Unit tests for protocol encoding/decoding & edge cases
└── README.md                   # Architecture ASCII diagram, build/run commands, benchmark table
```

---

## Phased Implementation Roadmap

### Phase 1: Project Skeleton & Build Setup
- Create `CMakeLists.txt` configured for C++17 (`-std=c++17 -Wall -Wextra -pthread`).
- Create headers and skeleton source files (`Server`, `ClientConnection`, `ThreadPool`, `Protocol`, `Logger`).
- Create entry point `main.cpp` parsing command line options (`--port`, `--max-conn`, `--threads`, `--log-file`).

### Phase 2: Core POSIX TCP & Epoll Event Loop
- Implement non-blocking socket setup (`socket`, `setsockopt` with `SO_REUSEADDR`, `fcntl` with `O_NONBLOCK`, `bind`, `listen`).
- Implement epoll event loop (`epoll_create1`, `epoll_ctl` with `EPOLLIN | EPOLLET` or level-triggered `EPOLLIN`, `epoll_wait`).
- Implement client connection acceptance and socket lifetime management.

### Phase 3: Thread Pool & Work Decoupling
- Implement `ThreadPool` class with worker threads waiting on `std::condition_variable`.
- Enqueue message parsing, command handling, and broadcast dispatching to worker threads.
- Thread-safe socket writing and client registry locking (`std::shared_mutex` or `std::mutex`).

### Phase 4: Protocol Framing & Broadcast Logic
- Implement `Protocol::encode` (4-byte big endian header + payload) and `Protocol::decode` with partial read accumulation buffers.
- Implement client broadcast logic (relaying framed messages to all active client sockets).

### Phase 5: Client Management, Commands & Disconnects
- Support `/nick <name>` to set custom nickname.
- Support `/list` to return connected client count and user list.
- Support `/quit` and handle unexpected TCP disconnects (detecting `read()` == 0 or `EPOLLHUP`/`EPOLLERR`, closing FD, cleaning client map, notifying pool).
- Implement thread-safe `Logger` writing timestamped structured logs to stdout and `server.log`.

### Phase 6: Benchmark Script, Unit Tests & Documentation
- Implement unit tests for `Protocol` framing using Catch2 in `tests/test_protocol.cpp`.
- Implement `client/cli_client.cpp` CLI client.
- Implement `benchmark/benchmark.py` for testing 100, 500, and 1000 concurrent connection loads.
- Write `README.md` with ASCII architecture diagram, build/run guide, and benchmark results table.

---

## Verification Plan

### Automated Verification
1. **Compilation Check**:
   ```bash
   mkdir -p build && cd build && cmake .. && make -j$(nproc)
   ```
2. **Protocol Unit Tests**:
   ```bash
   ./bin/unit_tests
   ```
3. **Load Benchmark Execution**:
   ```bash
   python3 benchmark/benchmark.py --host 127.0.0.1 --port 8080 --clients 100 --messages 50
   python3 benchmark/benchmark.py --host 127.0.0.1 --port 8080 --clients 500 --messages 50
   python3 benchmark/benchmark.py --host 127.0.0.1 --port 8080 --clients 1000 --messages 20
   ```

### Manual Verification
- Test interactive client (`./bin/cli_client`) connecting to `./bin/chat_server`, changing nickname (`/nick Alice`), listing users (`/list`), sending messages, and quitting (`/quit`).
