# Performance Optimization Plan - C++17 Epoll TCP Chat Server

Deep performance optimization of the C++17 Epoll multi-threaded TCP Chat Server for high throughput (msgs/sec) and ultra-low latency.

## Key Optimizations

1. **Compilation & Link-Time Optimization (LTO)**:
   - Configure CMake for `-O3 -DNDEBUG -flto -march=native`.

2. **Socket Layer Performance Tuning**:
   - Enable `TCP_NODELAY` (`IPPROTO_TCP`, `TCP_NODELAY`, 1) to disable Nagle's algorithm and eliminate inter-packet latency.
   - Enable `SO_KEEPALIVE` for socket health tracking.
   - Reuse read/write buffer memory allocations.

3. **Lock Contention Reduction (Reader-Writer Locks)**:
   - Upgrade client registry lock from `std::mutex` to `std::shared_mutex`.
   - Use `std::shared_lock` for broadcast operations and user lookups (read-heavy concurrent paths).
   - Use `std::unique_lock` only for connection acceptances and disconnects.

4. **Zero-Copy / Single-Encode Broadcast Pipeline**:
   - Encode message frame **once** per broadcast into a binary buffer using `Protocol::encodeToBuffer`, then send pre-encoded raw frame bytes directly to all recipient sockets.
   - Use `std::string_view` for zero-allocation command parsing (`/nick`, `/list`, `/quit`).

5. **Thread-Safe Fast Logger & Buffer Recycling**:
   - Reduce mutex lock hold time in logging routines.
