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
- Configured sockets with `TCP_NODELAY` (`IPPROTO_TCP`) to disable Nagle's algorithm for low latency and `SO_KEEPALIVE` for session health tracking.
- Replaced global map mutex with `std::shared_mutex` (C++17 reader-writer locks): `std::shared_lock` for read-heavy lookups/broadcasts and `std::unique_lock` for connection state modifications.
- Edge-triggered epoll event loop (`epoll_create1`, `epoll_ctl`, `epoll_wait`).
- Decouples network I/O from thread pool worker processing.

### 3. Custom Thread Pool (`ThreadPool.hpp` / `ThreadPool.cpp`)
- Fixed-size worker pool constructed with `std::thread`, `std::mutex`, `std::condition_variable`, and `std::atomic<bool>`.
- Consumes job queue of completed protocol frames for command execution and message broadcasting.

### 4. Length-Prefixed Binary Protocol (`Protocol.hpp` / `Protocol.cpp`)
- 4-byte big-endian `uint32` length header followed by UTF-8 string payload.
- Added `Protocol::encodeToBuffer` using `std::string_view` for zero-allocation binary frame encoding.
- Optimized `broadcast()` to encode the binary frame **once** per message broadcast instead of re-encoding for every connected client.
- Buffer accumulator handles short reads, partial reads, and frame fragmentation.
- Unit tests implemented in `tests/test_protocol.cpp` covering roundtrip, zero-allocation encoding, multi-frame buffers, fragmented packets, and payload limit exceptions.

### 5. Client Tracking, Multi-Channel Architecture & Rate Limiter (`ClientConnection.hpp` / `Server.cpp`)
- **Dynamic Multi-Channel Support**: Scope broadcasts to active channels (`#general`, `#tech`, `#random`). Supports `/join <#channel>`, `/leave`, and `/rooms` with channel membership tracking.
- **In-Memory Channel History**: Stores ring buffer of last 15 messages per channel (`m_channelHistory`), delivering instant scrollback when joining a channel.
- **Private Messaging (Whisper)**: Direct user-to-user messaging via `/msg <target> <message>` or `/w` by nickname or client ID.
- **Token-Bucket Rate Limiter**: Enforces max 10 token burst capacity with 5 tokens/sec refill rate per client socket to prevent flood/spam attacks.
- **Interactive Commands & Diagnostics**: Added `/ping` server health check and interactive `/help` menu.

### 6. Colorized Interactive CLI Client (`client/cli_client.cpp`)
- Enhanced client UI with ANSI color codes (`CYAN` for server notices, `GREEN` for chat messages, `MAGENTA` for private DMs, `YELLOW` for channel notifications, `RED` for warnings).
- Smooth prompt clearing and prompt line restoration (`> `).

### 7. Benchmark Suite & Documentation (`benchmark/benchmark.py` & `README.md`)
- Python 3 `asyncio` load tester spawning $N$ concurrent clients and measuring throughput and latency.
- Comprehensive `README.md` containing ASCII architecture diagram, build/run commands, binary framing layout, and benchmark results table for 100, 500, and 1000 concurrent client connections.


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
