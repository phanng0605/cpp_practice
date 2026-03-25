# `orderbook_implementation` (Limit Order Book + Demo/Bench)

This project is a **single-threaded** limit order book prototype focused on low-latency data-structure design, plus a higher-level `OrderBook` wrapper with accounts and trading helpers.

Including:
- **Engine**: fixed-size order pool (no allocations during matching/cancel), FIFO per price level, and fast best-bid/ask lookup.
- **Wrapper**: user balances with locked funds, `add_bid`/`add_ask`, plus:
  - **IOC** orders (`add_bid_ioc`, `add_ask_ioc`) to cancel unfilled remainder immediately
  - **Market** orders (`add_market_bid`, `add_market_ask`) implemented as “worst-case lock + discard remainder”
  - **Trade log + stats** for demo output and benchmarking

## Motivation
Build an allocation-light matching engine, then wrap it with just enough “exchange realism” (IOC + market, cancels, balances) to make behavior and performance visible in a single runnable demo.

## Key implementation:

1. **Memory layout & allocation strategy**
   - The engine pre-allocates arrays for orders and keeps a free-list for reusing nodes.
2. **FIFO correctness without per-order heap objects**
   - Each price level stores `head`/`tail` indices into the order pool.
3. **Fast best-price lookup**
   - Non-empty levels are tracked with a bitset; best bid/ask is found via bit operations.
4. **Matching loop mechanics**
   - For an aggressive order, the engine walks from the current best maker level and executes trades until either the taker is filled or the price condition fails.

## Build and run

From `cpp_practice/`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/projects/orderbook_implementation/orderbook_implementation --test
./build/projects/orderbook_implementation/orderbook_implementation --demo
./build/projects/orderbook_implementation/orderbook_implementation --ui

# Engine-only throughput (no account/balance updates)
./build/projects/orderbook_implementation/orderbook_implementation --bench --orders 100000 --seed 1

# End-to-end wrapper throughput (balances + locked funds + cancels)
./build/projects/orderbook_implementation/orderbook_implementation --bench-wrapper --orders 100000 --seed 1 --cancel-every 1000
```

If you're at the repo root, run the same commands but prefix paths with `./cpp_practice/` (e.g. `./cpp_practice/build/projects/orderbook_implementation/orderbook_implementation --ui`).

## API overview
`OrderBook` (wrapper) exposes:
- Accounts: `makeUser`, `addBalance`, `getBalance`
- Trading: `add_bid`, `add_ask`, `add_bid_ioc`, `add_ask_ioc`, `add_market_bid`, `add_market_ask`, `cancelBid`, `cancelAsk`
- Market data: `getQuote`, `getDepthBids`, `getDepthAsks`
- Debugging: `setTradeLogEnabled`, `getRecentTrades`, `getStats`

## Code layout
- `include/limit_order_book.hpp`: allocation-light matching engine (FIFO per price level, fixed-size order pool).
- `include/orderbook.hpp` + `src/OrderBook.cpp`: account wrapper (balances/locks, order submission, cancel, settlement).
- `src/main.cpp`: demo/bench harness + interactive `--ui`.

## Flags
1. **Build:** follow the commands in “Build and run”.
2. **Correctness:** run `--test` (verifies FIFO behavior + cancel in the engine).
3. **Feature demo:** run `--demo` (shows FIFO at same price, IOC leaving no remainder, and cancel working; also prints demo stats).
4. **Interactive UI:** run `--ui` and use the menu to place/cancel orders and watch best bid/ask + depth.
5. **Speed tests:** run `--bench` (engine-only) and `--bench-wrapper` (end-to-end wrapper work).

### UI notes
- `--ui` starts a menu-driven terminal app (sign up, place orders, cancel, view depth/balances).
- Price inputs are **cents** (internal units): e.g. `15050` means `$150.50`.

### UI test walkthrough
- Type `1` (sign up), enter a username.
- Type `2` (add balance): enter quote in **cents** (e.g. `1000000` = $10,000.00) and base in **shares** (non-negative integer).
- Type `3` (view market).
- Type `4` (LIMIT bid) or `5` (LIMIT ask).
- Type `6` (MARKET bid) to test immediate execution (unfilled remainder is canceled).
- Type `8` / `9` to cancel by `order_id`.

