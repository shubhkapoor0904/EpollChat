# Walkthrough - C++17 Epoll Multi-threaded TCP Chat Server

A high-performance, production-quality multi-threaded TCP chat server implemented in **C++17** targeting Linux/WSL2 using POSIX sockets, `epoll` I/O multiplexing, a custom `std::thread` pool, and length-prefixed binary protocol framing.

---

## 🛠️ Components Delivered

### 1. Build System & Skeleton (`CMakeLists.txt`)
- Configured with modern CMake (C++17 standard, `-Wall -Wextra -Wpedantic -pthread`).
- Automatic Catch2 integration via `FetchContent` for unit tests.
- Outputs `chat_server`, `cli_client`, and `unit_tests` binaries.

### 2. POSIX Sockets & Epoll Event Loop (`Server.hpp` / `Server.cpp`)
- Non-blocking server TCP socket setup (`socket`, `SO_REUSEADDR`, `O_NONBLOCK`, `bind`, `listen`).
- Edge-triggered epoll event loop (`epoll_create1`, `epoll_ctl`, `epoll_wait`).
- Decouples network I/O from thread pool worker processing.

### 3. Custom Thread Pool (`ThreadPool.hpp` / `ThreadPool.cpp`)
- Fixed-size worker pool constructed with `std::thread`, `std::mutex`, `std::condition_variable`, and `std::atomic<bool>`.
- Consumes job queue of completed protocol frames for command execution and message broadcasting.

### 4. Length-Prefixed Binary Protocol (`Protocol.hpp` / `Protocol.cpp`)
- 4-byte big-endian `uint32` length header followed by UTF-8 string payload.
- Buffer accumulator handles short reads, partial reads, and frame fragmentation.
- Unit tests implemented in `tests/test_protocol.cpp` covering roundtrip, multi-frame buffers, fragmented packets, and payload limit exceptions.

### 5. Client Tracking & Commands (`ClientConnection.hpp` / `ClientConnection.cpp`)
- Tracks client IDs, file descriptors, IP/port, nicknames, and read buffers.
- Thread-safe socket writes with `m_sendMutex`.
- Supports `/nick <name>`, `/list`, `/quit`, and graceful TCP disconnect handling.

### 6. Benchmark Suite & Documentation (`benchmark/benchmark.py` & `README.md`)
- Python 3 `asyncio` load tester spawning $N$ concurrent clients and measuring throughput and latency.
- Comprehensive `README.md` containing ASCII architecture diagram, build/run commands, binary framing layout, and a benchmark results table for 100, 500, and 1000 concurrent client connections.

---

## 🚀 Quickstart Commands

```bash
# 1. Build project
mkdir -p build && cd build
cmake .. && make -j$(nproc)

# 2. Run unit tests
./unit_tests

# 3. Start server
./chat_server --port 8080 --threads 8

# 4. Run interactive client (in separate terminal)
./cli_client 127.0.0.1 8080

# 5. Run benchmark suite (in separate terminal)
python3 benchmark/benchmark.py --host 127.0.0.1 --port 8080 --clients 500 --messages 20
```
