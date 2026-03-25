#include "limit_order_book.hpp"
#include "orderbook.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <string>
#include <vector>
#include <optional>

namespace {

struct Options {
  bool test = false;
  bool bench = false;
  bool demo = false;
  bool bench_wrapper = false;
  bool ui = false;
  std::size_t orders = 100000;
  std::uint64_t seed = 1;
  std::size_t cancel_every = 0;
};

Options parse_args(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--test") {
      opt.test = true;
    } else if (a == "--bench") {
      opt.bench = true;
    } else if (a == "--demo") {
      opt.demo = true;
    } else if (a == "--bench-wrapper") {
      opt.bench_wrapper = true;
    } else if (a == "--ui" || a == "--interactive") {
      opt.ui = true;
    } else if (a == "--orders" && i + 1 < argc) {
      opt.orders = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else if (a == "--seed" && i + 1 < argc) {
      opt.seed = static_cast<std::uint64_t>(std::stoull(argv[++i]));
    } else if (a == "--cancel-every" && i + 1 < argc) {
      opt.cancel_every = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else if (a == "--help" || a == "-h") {
      std::cout << "Usage:\n"
                   "  ./orderbook_implementation --test\n"
                   "  ./orderbook_implementation --bench [--orders N] [--seed S]\n"
                   "  ./orderbook_implementation --demo\n"
                   "  ./orderbook_implementation --bench-wrapper [--orders N] [--seed S] "
                   "[--cancel-every K]\n"
                   "  ./orderbook_implementation --ui\n";
      std::exit(0);
    }
  }
  return opt;
}

using Book = LimitOrderBook<std::uint32_t, 100, 200, 200000>;

bool run_test() {
  Book book;
  book.reset();

  std::vector<Book::Execution> exec;

  // FIFO at same price.
  book.process_order(Book::Side::Sell, 101, 5, 1, &exec);
  book.process_order(Book::Side::Sell, 101, 7, 2, &exec);

  exec.clear();
  book.process_order(Book::Side::Buy, 101, 8, 10, &exec);

  if (exec.size() != 2) {
    std::cerr << "Expected 2 executions, got " << exec.size() << "\n";
    return false;
  }
  if (exec[0].maker_order_id != 1 || exec[0].qty != 5 || exec[0].price != 101) {
    std::cerr << "First exec mismatch\n";
    return false;
  }
  if (exec[1].maker_order_id != 2 || exec[1].qty != 3 || exec[1].price != 101) {
    std::cerr << "Second exec mismatch\n";
    return false;
  }

  // Remaining ask @101 should be 4.
  if (book.level_quantity(Book::Side::Sell, 101) != 4) {
    std::cerr << "Expected remaining ask qty=4\n";
    return false;
  }

  // Cross multiple price levels.
  book.reset();
  exec.clear();
  book.process_order(Book::Side::Sell, 101, 10, 1, &exec);
  book.process_order(Book::Side::Sell, 100, 5, 2, &exec);

  exec.clear();
  book.process_order(Book::Side::Buy, 100, 6, 3, &exec);

  if (book.best_bid().value_or(-1) != 100) {
    std::cerr << "Best bid should be 100\n";
    return false;
  }
  if (book.best_ask().value_or(-1) != 101) {
    std::cerr << "Best ask should be 101\n";
    return false;
  }
  if (book.level_quantity(Book::Side::Buy, 100) != 1) {
    std::cerr << "Expected resting bid qty=1\n";
    return false;
  }
  if (book.level_quantity(Book::Side::Sell, 100) != 0) {
    std::cerr << "Expected ask at 100 empty\n";
    return false;
  }
  if (book.level_quantity(Book::Side::Sell, 101) != 10) {
    std::cerr << "Expected ask at 101 unchanged qty=10\n";
    return false;
  }

  // Cancel a resting order.
  if (!book.cancel(3)) {
    std::cerr << "Cancel failed\n";
    return false;
  }
  if (book.best_bid().has_value()) {
    std::cerr << "Expected no best bid after cancel\n";
    return false;
  }

  // Invalid cancel should fail.
  if (book.cancel(999999)) {
    std::cerr << "Expected cancel to fail for invalid id\n";
    return false;
  }

  return true;
}

std::uint64_t splitmix64(std::uint64_t& x) {
  x += 0x9e3779b97f4a7c15ull;
  std::uint64_t z = x;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

bool run_demo() {
  OrderBook book;
  book.setTradeLogEnabled(true);
  book.clearTradeLog();
  book.resetStats();

  auto alice = book.makeUser("alice");
  auto bob = book.makeUser("bob");
  auto carol = book.makeUser("carol");

  (void)book.addBalance(alice, 10'000'000, 50'000);
  (void)book.addBalance(bob, 10'000'000, 50'000);
  (void)book.addBalance(carol, 10'000'000, 50'000);

  std::cout << "Demo on " << OrderBook::TICKER << "\n";
  const auto q0 = book.getQuote();
  std::cout << "Initial quote: "
            << "best_bid=" << (q0.best_bid ? std::to_string(q0.best_bid->price) : "-")
            << " best_ask=" << (q0.best_ask ? std::to_string(q0.best_ask->price) : "-")
            << "\n";

  std::cout << "\nStep 1: FIFO on same-price asks\n";
  const auto ask1 = book.add_ask(alice, 20'000, 3);
  const auto ask2 = book.add_ask(bob, 20'000, 4);
  if (!ask1 || !ask2) {
    std::cerr << "Failed to add maker asks\n";
    return false;
  }

  // Seeded asks are 60@15050 and 80@15100 (=140).
  // Send 140 + 7 so we also hit the 20'000 makers.
  const OrderBook::Qty taker_qty = 147;
  const auto bid_res = book.add_bid_ioc(carol, 20'000, taker_qty);
  if (!bid_res) {
    std::cerr << "Failed to submit taker bid\n";
    return false;
  }

  const auto trades = book.getRecentTrades(200);
  OrderBook::Qty got1 = 0;
  OrderBook::Qty got2 = 0;
  std::size_t idx1 = static_cast<std::size_t>(-1);
  std::size_t idx2 = static_cast<std::size_t>(-1);

  for (std::size_t i = 0; i < trades.size(); ++i) {
    if (trades[i].maker_order_id == *ask1) {
      got1 += trades[i].qty;
      idx1 = idx1 == static_cast<std::size_t>(-1) ? i : idx1;
    }
    if (trades[i].maker_order_id == *ask2) {
      got2 += trades[i].qty;
      idx2 = idx2 == static_cast<std::size_t>(-1) ? i : idx2;
    }
  }

  if (got1 != 3 || got2 != 4) {
    std::cerr << "FIFO qty mismatch: ask1=" << got1 << " ask2=" << got2 << "\n";
    return false;
  }

  // `getRecentTrades()` is newest-first, so older fills show up later.
  if (idx1 == static_cast<std::size_t>(-1) || idx2 == static_cast<std::size_t>(-1) ||
      !(idx2 < idx1)) {
    std::cerr << "FIFO order mismatch for same-price makers\n";
    return false;
  }

  std::cout << "Maker fills OK: ask1 qty=3, ask2 qty=4\n";

  std::cout << "\nStep 2: IOC leaves no resting remainder\n";
  (void)book.add_bid_ioc(carol, 15'100, 200);
  const auto bids = book.getDepthBids(10);
  for (const auto& l : bids) {
    if (l.price == 15'100) {
      std::cerr << "IOC failed: resting bids at 15100\n";
      return false;
    }
  }

  std::cout << "\nStep 3: cancel a resting order\n";
  const auto rest_bid = book.add_bid(alice, 14'920, 10);
  if (!rest_bid) {
    std::cerr << "Failed to add resting bid\n";
    return false;
  }
  if (!book.cancelBid(alice, *rest_bid)) {
    std::cerr << "Cancel failed\n";
    return false;
  }
  const auto bids_after = book.getDepthBids(10);
  for (const auto& l : bids_after) {
    if (l.price == 14'920) {
      std::cerr << "Cancel failed: 14920 level still present\n";
      return false;
    }
  }

  const auto st = book.getStats();
  std::cout << "\nDemo stats: "
            << "orders_submitted=" << st.orders_submitted
            << " accepted=" << st.orders_accepted
            << " trades=" << st.trades_executed
            << " cancels_ok=" << st.cancels_succeeded << "\n";
  return true;
}

// Interactive terminal UI for exploring the wrapper API.
bool run_ui() {
  OrderBook book;
  book.setTradeLogEnabled(false);

  auto money_to_str = [](OrderBook::Money cents) {
    const bool neg = cents < 0;
    const std::int64_t v = neg ? -cents : cents;
    const std::int64_t dollars = v / 100;
    const std::int64_t frac = v % 100;
    std::ostringstream oss;
    if (neg) oss << "-";
    oss << dollars << "." << std::setw(2) << std::setfill('0') << frac;
    return oss.str();
  };

  auto price_to_str = [&money_to_str](OrderBook::Price p) {
    return money_to_str(static_cast<OrderBook::Money>(p));
  };

  auto prompt_int64 = [](std::string_view label) -> std::int64_t {
    std::cout << label;
    std::int64_t v{};
    while (!(std::cin >> v)) {
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      std::cout << "Invalid input. " << label;
    }
    return v;
  };

  auto prompt_u32 = [](std::string_view label) -> std::uint32_t {
    std::cout << label;
    std::uint64_t v{};
    while (!(std::cin >> v)) {
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      std::cout << "Invalid input. " << label;
    }
    return static_cast<std::uint32_t>(v);
  };

  auto prompt_u64 = [](std::string_view label) -> std::uint64_t {
    std::cout << label;
    std::uint64_t v{};
    while (!(std::cin >> v)) {
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      std::cout << "Invalid input. " << label;
    }
    return v;
  };

  auto prompt_name = []() -> std::string {
    std::string name;
    std::cout << "User name: ";
    std::cin >> name;
    return name;
  };

  OrderBook::UserId current_user = 0;
  bool has_user = false;

  std::cout << "Welcome to the " << OrderBook::TICKER << " market.\n";

  while (true) {
    std::cout << "\n========== MENU ==========\n";
    std::cout << "1. Sign up user\n";
    std::cout << "2. Add balance\n";
    std::cout << "3. Show current market (best bid/ask + depth)\n";
    std::cout << "4. Place LIMIT bid (buy base)\n";
    std::cout << "5. Place LIMIT ask (sell base)\n";
    std::cout << "6. Place MARKET bid (buy immediately)\n";
    std::cout << "7. Show current user balance\n";
    std::cout << "8. Cancel bid\n";
    std::cout << "9. Cancel ask\n";
    std::cout << "10. Exit\n";
    std::cout << "Enter your choice: ";

    int choice{};
    if (!(std::cin >> choice)) break;

    if (choice == 10) break;

    if (!has_user && choice != 1 && choice != 3) {
      std::cout << "Sign up first (option 1).\n";
      continue;
    }

    if (choice == 1) {
      const std::string name = prompt_name();
      current_user = book.makeUser(name);
      has_user = true;
      std::cout << "User ready. user_id=" << current_user << "\n";
      continue;
    }

    if (choice == 2) {
      const std::int64_t quote_delta = prompt_int64("Add quote (cents, can be negative): ");
      const std::uint32_t base_delta = prompt_u32("Add base (shares, non-negative): ");
      const bool ok = book.addBalance(current_user, quote_delta, base_delta);
      std::cout << (ok ? "Balance updated.\n" : "Balance update failed.\n");
      continue;
    }

    if (choice == 3) {
      const auto q = book.getQuote();
      std::cout << "\nGOOGL Depth: CURRENT MARKET PRICES\n";
      if (q.best_bid) {
        std::cout << "Best bid:  " << price_to_str(q.best_bid->price)
                  << " x " << q.best_bid->qty << "\n";
      } else {
        std::cout << "Best bid:  -\n";
      }
      if (q.best_ask) {
        std::cout << "Best ask:  " << price_to_str(q.best_ask->price)
                  << " x " << q.best_ask->qty << "\n";
      } else {
        std::cout << "Best ask:  -\n";
      }

      std::cout << "\nAsks above:\n";
      for (const auto& l : book.getDepthAsks(10)) {
        std::cout << "  " << price_to_str(l.price) << " x " << l.qty << "\n";
      }

      std::cout << "Bids below:\n";
      for (const auto& l : book.getDepthBids(10)) {
        std::cout << "  " << price_to_str(l.price) << " x " << l.qty << "\n";
      }
      continue;
    }

    if (choice == 4) {
      const OrderBook::Price price = static_cast<OrderBook::Price>(prompt_u64("Bid price (cents, e.g. 15050 for 150.50): "));
      const OrderBook::Qty qty = prompt_u32("Bid quantity (shares): ");
      const auto id = book.add_bid(current_user, price, qty);
      if (!id) std::cout << "Bid rejected (insufficient funds or price out of range).\n";
      else std::cout << "Bid accepted. order_id=" << *id << "\n";
      continue;
    }

    if (choice == 5) {
      const OrderBook::Price price = static_cast<OrderBook::Price>(prompt_u64("Ask price (cents, e.g. 15100 for 151.00): "));
      const OrderBook::Qty qty = prompt_u32("Ask quantity (shares): ");
      const auto id = book.add_ask(current_user, price, qty);
      if (!id) std::cout << "Ask rejected (insufficient shares or price out of range).\n";
      else std::cout << "Ask accepted. order_id=" << *id << "\n";
      continue;
    }

    if (choice == 6) {
      const OrderBook::Qty qty = prompt_u32("Market buy quantity (shares): ");
      const auto id = book.add_market_bid(current_user, qty);
      if (!id) std::cout << "Market bid rejected.\n";
      else std::cout << "Market bid submitted. order_id=" << *id << "\n";
      continue;
    }

    if (choice == 7) {
      const auto b = book.getBalance(current_user);
      std::cout << "User balance:\n";
      std::cout << "  quote: " << money_to_str(b.quote) << " cents\n";
      std::cout << "  base:  " << b.base << " shares\n";
      continue;
    }

    if (choice == 8) {
      const OrderBook::OrderId id = static_cast<OrderBook::OrderId>(
          prompt_u64("Bid order_id to cancel: "));
      const bool ok = book.cancelBid(current_user, id);
      std::cout << (ok ? "Bid cancelled.\n" : "Cancel failed.\n");
      continue;
    }

    if (choice == 9) {
      const OrderBook::OrderId id = static_cast<OrderBook::OrderId>(
          prompt_u64("Ask order_id to cancel: "));
      const bool ok = book.cancelAsk(current_user, id);
      std::cout << (ok ? "Ask cancelled.\n" : "Cancel failed.\n");
      continue;
    }
  }

  std::cout << "Bye.\n";
  return true;
}

void run_bench(std::size_t orders, std::uint64_t seed) {
  Book book;
  book.reset();

  std::uint64_t x = seed;
  std::uint64_t executions = 0;

  const int price_span = 200 - 100 + 1;

  if (orders > 200000) {
    orders = 200000;  // keep ids within the fixed pool
  }

  const auto t0 = std::chrono::steady_clock::now();

  for (std::size_t i = 0; i < orders; ++i) {
    const std::uint64_t r1 = splitmix64(x);
    const std::uint64_t r2 = splitmix64(x);
    const std::uint64_t r3 = splitmix64(x);

    const bool buy = (r1 & 1ull) != 0;
    const int price = 100 + static_cast<int>(r2 % static_cast<std::uint64_t>(price_span));
    const std::uint32_t qty = 1 + static_cast<std::uint32_t>(r3 % 10);

    const int order_id = static_cast<int>(i);

    const bool ok = book.process_order(buy ? Book::Side::Buy : Book::Side::Sell, price, qty,
                                        order_id, nullptr);
    if (!ok) {
      break;
    }
  }

  (void)executions;

  const auto t1 = std::chrono::steady_clock::now();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  const double seconds = static_cast<double>(ms) / 1000.0;
  const double throughput = seconds > 0 ? static_cast<double>(orders) / seconds : 0.0;

  std::cout << "Benchmark:\n"
            << "  orders_submitted=" << orders << "\n"
            << "  seed=" << seed << "\n"
            << "  time_ms=" << ms << "\n"
            << "  throughput_orders_per_sec=" << static_cast<long long>(throughput) << "\n";
}

void run_bench_wrapper(std::size_t orders, std::uint64_t seed,
                        std::size_t cancel_every) {
  OrderBook book;
  auto uid = book.makeUser("bench_trader");
  (void)book.addBalance(uid, 5'000'000'000LL, 500'000);
  book.setTradeLogEnabled(false);
  book.clearTradeLog();
  book.resetStats();

  struct Placed {
    OrderBook::OrderId id;
    bool is_bid;
  };
  std::vector<Placed> parked;
  parked.reserve(orders / 2 + 1);

  std::uint64_t x = seed;
  if (orders > 500000) orders = 500000;

  const auto t0 = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < orders; ++i) {
    const bool buy = (splitmix64(x) & 1ull) != 0;
    const std::uint64_t r = splitmix64(x) % 100ull;
    const OrderBook::Qty qty = 1 + static_cast<OrderBook::Qty>(splitmix64(x) % 50u);
    const OrderBook::Price price =
        14'500 + static_cast<OrderBook::Price>(splitmix64(x) % 2'001u);

    std::optional<OrderBook::OrderId> id;
    if (r < 10) {
      id = buy ? book.add_market_bid(uid, qty) : book.add_market_ask(uid, qty);
    } else if (r < 25) {
      id = buy ? book.add_bid_ioc(uid, price, qty) : book.add_ask_ioc(uid, price, qty);
    } else {
      id = buy ? book.add_bid(uid, price, qty) : book.add_ask(uid, price, qty);
    }

    if (id) parked.push_back({*id, buy});

    if (cancel_every != 0 && !parked.empty() && ((i + 1) % cancel_every) == 0) {
      const std::size_t k = static_cast<std::size_t>(splitmix64(x) % parked.size());
      const auto [cid, is_bid] = parked[k];
      if (is_bid) (void)book.cancelBid(uid, cid);
      else (void)book.cancelAsk(uid, cid);
    }
  }

  const auto t1 = std::chrono::steady_clock::now();
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  const double seconds = static_cast<double>(ms) / 1000.0;
  const double throughput = seconds > 0 ? static_cast<double>(orders) / seconds : 0.0;

  const auto st = book.getStats();
  std::cout << "Wrapper benchmark:\n"
            << "  orders_submitted=" << st.orders_submitted << "\n"
            << "  accepted=" << st.orders_accepted << "\n"
            << "  trades=" << st.trades_executed << "\n"
            << "  cancels_ok=" << st.cancels_succeeded << "\n"
            << "  time_ms=" << ms << "\n"
            << "  throughput_orders_per_sec=" << static_cast<long long>(throughput)
            << "\n";
}

}

int main(int argc, char** argv) {
  const Options opt = parse_args(argc, argv);
  if (!opt.test && !opt.bench && !opt.demo && !opt.bench_wrapper && !opt.ui) {
    std::cout << "Use one of --test, --bench, --demo, or --bench-wrapper. "
                 "Run with --help for usage.\n";
    return 0;
  }

  if (opt.test) {
    const bool ok = run_test();
    std::cout << (ok ? "Correctness passed\n" : "Correctness failed\n");
    return ok ? 0 : 1;
  }

  if (opt.bench) {
    run_bench(opt.orders, opt.seed);
  }
  if (opt.demo) {
    const bool ok = run_demo();
    std::cout << (ok ? "Demo passed\n" : "Demo failed\n");
    return ok ? 0 : 1;
  }
  if (opt.bench_wrapper) {
    run_bench_wrapper(opt.orders, opt.seed, opt.cancel_every);
  }
  if (opt.ui) {
    const bool ok = run_ui();
    return ok ? 0 : 1;
  }
  return 0;
}

