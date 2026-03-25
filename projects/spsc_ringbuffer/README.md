# `spsc_ringbuffer` (SPSC Ring Buffer)

This project implements a **single-producer / single-consumer (SPSC)** lock-free ring buffer in C++20 and provides:

- A **correctness test** that verifies the consumer observes values in strict FIFO order.
- A **throughput benchmark** using a producer/consumer thread pair.

## Key concepts

- **C++ memory ordering**: `release` on the producer publish, `acquire` on the consumer consume.
- **Avoiding false sharing**: cache-line alignment for the `head_` and `tail_` atomics.
- **Full vs empty**: one slot is intentionally kept empty to disambiguate states without extra counters.

## Build and run

From `cpp_practice/`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/projects/spsc_ringbuffer/spsc_ringbuffer --test
./build/projects/spsc_ringbuffer/spsc_ringbuffer --bench --n 1000000 --capacity 4096
```

## Notes

Uses compile-time capacity (2^n). The benchmark supports a small set of powers of two.

## Code layout
- `include/spsc_ringbuffer.hpp`: the ring buffer implementation (atomics + release/acquire).
- `src/main.cpp`: `--test` correctness check + `--bench` throughput run.

