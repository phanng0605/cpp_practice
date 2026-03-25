# `workstealing_pool` (Work-Stealing Thread Pool)

This project implements a **work-stealing scheduler** in C++20:

- Each worker owns a local deque of tasks.
- If a worker runs out of local work, it **steals** from other workers.
- The pool exposes `submit()` (returns `std::future`) and a `wait_idle()` API.

## Under the hood (what to talk about in the application)

What you can emphasize:

- **Work placement**: `submit()` pushes tasks to a chosen worker (round-robin), so locality is improved.
- **Stealing strategy**: thieves take from the opposite end to reduce contention and improve cache behavior.
- **Concurrency lifecycle**: tasks are tracked via an atomic `pending_` counter so `wait_idle()` is well-defined.
- **Memory ordering + visibility**: the pool uses atomics and condition variables to coordinate state transitions safely.

## Build and run

From `cpp_practice/`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/projects/workstealing_pool/workstealing_pool --test
./build/projects/workstealing_pool/workstealing_pool --bench --threads 4 --tasks 200000 --block 4096
```

## Notes

The internal deque uses `std::mutex` for clarity. The learning focus is the scheduler architecture: how work moves between threads and how the pool knows when it is truly idle.

## Code layout
- `include/work_stealing_thread_pool.hpp`: thread pool + work stealing logic.
- `src/main.cpp`: `--test` + `--bench` harness.

