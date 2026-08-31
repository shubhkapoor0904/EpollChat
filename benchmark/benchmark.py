#!/usr/bin/env python3
import asyncio
import struct
import time
import argparse
import statistics
import sys
from typing import List, Tuple

HEADER_SIZE = 4

def encode_frame(message: str) -> bytes:
    payload = message.encode('utf-8')
    header = struct.pack('>I', len(payload))
    return header + payload

async def decode_frames(reader: asyncio.StreamReader, buffer: bytearray) -> List[str]:
    messages = []
    while True:
        if len(buffer) < HEADER_SIZE:
            data = await reader.read(4096)
            if not data:
                break
            buffer.extend(data)

        if len(buffer) >= HEADER_SIZE:
            payload_len = struct.unpack('>I', buffer[:HEADER_SIZE])[0]
            total_size = HEADER_SIZE + payload_len

            while len(buffer) < total_size:
                data = await reader.read(4096)
                if not data:
                    break
                buffer.extend(data)

            if len(buffer) >= total_size:
                msg = buffer[HEADER_SIZE:total_size].decode('utf-8', errors='ignore')
                messages.append(msg)
                del buffer[:total_size]
            else:
                break
    return messages

async def run_client_session(client_id: int, host: str, port: int, num_messages: int) -> List[float]:
    latencies: List[float] = []
    try:
        reader, writer = await asyncio.open_connection(host, port)
        buffer = bytearray()

        # Read initial server welcome banner
        welcome_msgs = await decode_frames(reader, buffer)

        for i in range(num_messages):
            msg_str = f"Client-{client_id} Msg-{i+1}"
            frame = encode_frame(msg_str)

            send_time = time.perf_counter()
            writer.write(frame)
            await writer.drain()

            # Wait for broadcast reflection / response
            rx_msgs = await decode_frames(reader, buffer)
            recv_time = time.perf_counter()

            if rx_msgs:
                lat_ms = (recv_time - send_time) * 1000.0
                latencies.append(lat_ms)

            # Brief pause to simulate realistic chat client typing inter-arrival time
            await asyncio.sleep(0.001)

        # Send quit command
        writer.write(encode_frame("/quit"))
        await writer.drain()
        writer.close()
        await writer.wait_closed()

    except Exception as e:
        pass

    return latencies

async def run_benchmark(host: str, port: int, num_clients: int, num_messages: int):
    print("=" * 80)
    print(f"       EPOLL CHAT SERVER BENCHMARK (N={num_clients} Clients, M={num_messages} Msgs/Client)")
    print("=" * 80)
    print(f"[*] Target Host: {host}:{port}")
    print(f"[*] Total Expected Messages: {num_clients * num_messages}")
    print("[*] Spawing client connections...")

    start_time = time.perf_counter()

    tasks = [
        run_client_session(i + 1, host, port, num_messages)
        for i in range(num_clients)
    ]

    results = await asyncio.gather(*tasks, return_exceptions=True)
    end_time = time.perf_counter()

    total_duration = end_time - start_time
    all_latencies: List[float] = []

    successful_clients = 0
    for r in results:
        if isinstance(r, list) and r:
            all_latencies.extend(r)
            successful_clients += 1

    total_received = len(all_latencies)
    throughput = total_received / total_duration if total_duration > 0 else 0.0

    print("\n" + "=" * 80)
    print("                        BENCHMARK EXECUTION RESULTS")
    print("=" * 80)
    print(f"  Concurrent Clients Tested : {num_clients}")
    print(f"  Successful Client Sessions: {successful_clients} / {num_clients}")
    print(f"  Total Duration            : {total_duration:.3f} seconds")
    print(f"  Total Msgs Processed      : {total_received}")
    print(f"  Overall Throughput        : {throughput:.2f} msgs/sec")
    print("-" * 80)

    if all_latencies:
        all_latencies.sort()
        mean_lat = statistics.mean(all_latencies)
        min_lat = min(all_latencies)
        max_lat = max(all_latencies)
        p50_lat = statistics.median(all_latencies)
        p95_lat = all_latencies[int(len(all_latencies) * 0.95)]
        p99_lat = all_latencies[int(len(all_latencies) * 0.99)]

        print(f"  Latency (Min)             : {min_lat:.2f} ms")
        print(f"  Latency (Mean)            : {mean_lat:.2f} ms")
        print(f"  Latency (Median / p50)    : {p50_lat:.2f} ms")
        print(f"  Latency (p95)             : {p95_lat:.2f} ms")
        print(f"  Latency (p99)             : {p99_lat:.2f} ms")
        print(f"  Latency (Max)             : {max_lat:.2f} ms")
    else:
        print("  [!] Warning: No message latencies recorded. Ensure server is running.")

    print("=" * 80 + "\n")

def main():
    parser = argparse.ArgumentParser(description="Asyncio Load Benchmark for C++17 Epoll Chat Server")
    parser.add_argument("--host", default="127.0.0.1", help="Server host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8080, help="Server port (default: 8080)")
    parser.add_argument("--clients", type=int, default=100, help="Number of concurrent clients (default: 100)")
    parser.add_argument("--messages", type=int, default=20, help="Messages per client (default: 20)")

    args = parser.parse_args()
    asyncio.run(run_benchmark(args.host, args.port, args.clients, args.messages))

if __name__ == "__main__":
    main()
