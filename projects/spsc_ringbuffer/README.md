# `spsc_ringbuffer` (SPSC Ring Buffer)

This project implements a **single-producer / single-consumer (SPSC)** lock-free ring buffer in C++20 and provides:

- A **correctness test** that verifies the consumer observes values in strict FIFO order.
- A **throughput benchmark** using a producer/consumer thread pair.

## Under the hood (what to talk about in the application)

Key concepts:

- **C++ memory ordering**: `release` on the producer publish, `acquire` on the consumer consume.
- **Avoiding false sharing**: cache-line alignment for the `head_` and `tail_` atomics.
- **Full vs empty**: one slot is intentionally kept empty to disambiguate states without extra counters.

What you can claim you learned:

- How to reason about data races when sharing a buffer between threads.
- How the memory model guarantees that payload writes become visible after the publishing atomic store.

## Build and run

From `cpp_practice/`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/projects/spsc_ringbuffer/spsc_ringbuffer --test
./build/projects/spsc_ringbuffer/spsc_ringbuffer --bench --n 1000000 --capacity 4096
```

## Notes

This demo template uses compile-time capacity (power-of-two). The benchmark supports a small set of powers of two.

