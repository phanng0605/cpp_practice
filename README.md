# C++ Systems Practice (cpp_practice)

This directory contains three small, runnable C++20 projects focused on systems programming:
memory ordering + lock-free queues, concurrency/scheduling, and an allocation-free order book prototype.

## Build

From inside `cpp_practice/`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The executables are generated under `build/projects/<project_name>/`.

## Test and run

### Project 1: SPSC ring buffer (`spsc_ringbuffer`)

Idea: a lock-free queue for exactly 1 producer + 1 consumer.

Motivation:
- Learn the C++ memory model by placing `release`/`acquire` where correctness actually depends on it.
- Measure real producer/consumer throughput and see the impact of cache behavior (false sharing).

```bash
./build/projects/spsc_ringbuffer/spsc_ringbuffer --test
./build/projects/spsc_ringbuffer/spsc_ringbuffer --bench --n 1000000 --capacity 4096
```

### Project 2: Work-stealing thread pool (`workstealing_pool`)

Idea: a task scheduler where idle workers steal work from busy workers.

Motivation:
- Understand how modern runtimes balance load (local queues + stealing) and why it helps with irregular workloads.
- Provide a clean `submit()` + `future` interface and a well-defined `wait_idle()` for correctness.

```bash
./build/projects/workstealing_pool/workstealing_pool --test
./build/projects/workstealing_pool/workstealing_pool --bench --threads 4 --tasks 200000 --block 4096
```

### Project 3: Limit order book (`orderbook_implementation`)

Idea: a fast, single-threaded limit order book prototype using fixed-size storage.

Motivation:
- Practice low-latency data-structure design (contiguous arrays, intrusive FIFO per price level, fast best bid/ask).
- Keep the hot path allocation-free to make performance and invariants easier to reason about.

```bash
./build/projects/orderbook_implementation/orderbook_implementation --test
./build/projects/orderbook_implementation/orderbook_implementation --bench --orders 100000 --seed 1
```

Tip: run any executable with `--help` to see supported flags.

## Project READMEs

- `projects/spsc_ringbuffer/README.md`
- `projects/workstealing_pool/README.md`
- `projects/orderbook_implementation/README.md`

