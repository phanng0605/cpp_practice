#include "limit_order_book.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

struct Options {
  bool test = false;
  bool bench = false;
  std::size_t orders = 100000;
  std::uint64_t seed = 1;
};

Options parse_args(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--test") {
      opt.test = true;
    } else if (a == "--bench") {
      opt.bench = true;
    } else if (a == "--orders" && i + 1 < argc) {
      opt.orders = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else if (a == "--seed" && i + 1 < argc) {
      opt.seed = static_cast<std::uint64_t>(std::stoull(argv[++i]));
    } else if (a == "--help" || a == "-h") {
      std::cout << "Usage:\n"
                   "  ./orderbook_implementation --test\n"
                   "  ./orderbook_implementation --bench [--orders N] [--seed S]\n";
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

  // FIFO at same price level.
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

  // Remaining ask at 101 should be 4.
  if (book.level_quantity(Book::Side::Sell, 101) != 4) {
    std::cerr << "Expected remaining ask qty=4\n";
    return false;
  }

  // Cross across multiple levels.
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

  // Cancel resting order.
  if (!book.cancel(3)) {
    std::cerr << "Cancel failed\n";
    return false;
  }
  if (book.best_bid().has_value()) {
    std::cerr << "Expected no best bid after cancel\n";
    return false;
  }

  // Cancel invalid order id should fail.
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

void run_bench(std::size_t orders, std::uint64_t seed) {
  Book book;
  book.reset();

  std::uint64_t x = seed;
  std::uint64_t executions = 0;

  const int price_span = 200 - 100 + 1;

  if (orders > 200000) {
    orders = 200000;  // demo keeps ids within the fixed pool.
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

}

int main(int argc, char** argv) {
  const Options opt = parse_args(argc, argv);
  if (!opt.test && !opt.bench) {
    std::cout << "Use --test or --bench. Run with --help for usage.\n";
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
  return 0;
}

