# `orderbook_implementation` (Optimized Limit Order Book)

This project is a **high-performance, single-threaded** limit order book prototype designed to demonstrate “under the hood” systems ideas:

- **Fixed-size order pool** (no allocations during matching/cancel)
- **FIFO per price level** implemented as an **intrusive linked list**
- **Price levels in contiguous arrays** for cache friendliness
- **Best bid/ask tracking using a compact bitset**, so jumping to the next non-empty level is fast

## Under the hood (what to talk about in your application)

Key implementation points you can describe:

1. **Memory layout & allocation strategy**
   - The engine pre-allocates arrays for orders and keeps a free-list for reusing nodes.
2. **FIFO correctness without per-order heap objects**
   - Each price level stores `head`/`tail` indices into the order pool.
3. **Fast best-price lookup**
   - Non-empty levels are tracked with a bitset; best bid/ask is found via bit operations.
4. **Matching loop mechanics**
   - For an aggressive order, the engine walks from the current best maker level and executes trades until either the taker is filled or the price condition fails.

This is intentionally a *performance-leaning prototype* rather than a fully production-grade exchange system.

## Build and run

From `cpp_practice/`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/projects/orderbook_implementation/orderbook_implementation --test
./build/projects/orderbook_implementation/orderbook_implementation --bench --orders 100000 --seed 1
```

