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
If you run from the repo root (without `cd cpp_practice/`), prefix paths with `./cpp_practice/` (e.g. `./cpp_practice/build/projects/orderbook_implementation/orderbook_implementation --ui`).

## Test and run

### Project 1: SPSC ring buffer (`spsc_ringbuffer`)

Idea: a lock-free queue for exactly 1 producer + 1 consumer.

What you'll practice:
- C++ memory ordering (`release`/`acquire`) for correctness.
- Throughput + false sharing impact.

```bash
./build/projects/spsc_ringbuffer/spsc_ringbuffer --test
./build/projects/spsc_ringbuffer/spsc_ringbuffer --bench --n 1000000 --capacity 4096
```

### Project 2: Work-stealing thread pool (`workstealing_pool`)

Idea: a task scheduler where idle workers steal work from busy workers.

What you'll practice:
- Work stealing for load balancing on irregular workloads.
- A clean `submit()`/`future` + well-defined `wait_idle()` correctness point.

```bash
./build/projects/workstealing_pool/workstealing_pool --test
./build/projects/workstealing_pool/workstealing_pool --bench --threads 4 --tasks 200000 --block 4096
```

### Project 3: Limit order book (`orderbook_implementation`)

Idea: a fixed-size, single-threaded limit order book engine plus an account-level wrapper.

What you'll practice:
- Allocation-light matching with FIFO per price level and fast best bid/ask.
- Exchange-like order controls (IOC + market), cancels, and end-to-end benchmarks.

```bash
./build/projects/orderbook_implementation/orderbook_implementation --test
./build/projects/orderbook_implementation/orderbook_implementation --demo
./build/projects/orderbook_implementation/orderbook_implementation --ui
./build/projects/orderbook_implementation/orderbook_implementation --bench --orders 100000 --seed 1
./build/projects/orderbook_implementation/orderbook_implementation --bench-wrapper --orders 100000 --seed 1 --cancel-every 1000
```

Tip: run any executable with `--help` to see supported flags.

## Project READMEs

- `projects/spsc_ringbuffer/README.md`
- `projects/workstealing_pool/README.md`
- `projects/orderbook_implementation/README.md`

