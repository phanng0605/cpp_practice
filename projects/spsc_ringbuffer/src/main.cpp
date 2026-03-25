#include "spsc_ringbuffer.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>
#include <thread>
#include <string>
#include <vector>

namespace {

struct Options {
  bool test = false;
  bool bench = false;
  std::size_t n = 1'000'000;
  std::size_t capacity = 4096;
};

Options parse_args(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--test") {
      opt.test = true;
    } else if (a == "--bench") {
      opt.bench = true;
    } else if (a == "--n" && i + 1 < argc) {
      opt.n = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else if (a == "--capacity" && i + 1 < argc) {
      opt.capacity = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else if (a == "--help" || a == "-h") {
      std::cout << "Usage:\n"
                   "  ./spsc_ringbuffer --test\n"
                   "  ./spsc_ringbuffer --bench [--n N] [--capacity C]\n";
      std::exit(0);
    }
  }
  return opt;
}

uint64_t run_correctness(std::size_t n) {
  constexpr std::size_t kCap = 8192;
  SpscRingBuffer<std::uint64_t, kCap> rb;

  std::uint64_t consumed_sum = 0;
  std::atomic<bool> done{false};

  std::thread producer([&] {
    for (std::uint64_t i = 0; i < n; ++i) {
      while (!rb.push(i)) {
        std::this_thread::yield();
      }
    }
    done.store(true, std::memory_order_release);
  });

  std::thread consumer([&] {
    std::uint64_t expected = 0;
    std::uint64_t v = 0;
    while (expected < n) {
      if (rb.pop(v)) {
        if (v != expected) {
          std::cerr << "CORRUPTION: expected " << expected << " got " << v << "\n";
          std::abort();
        }
        consumed_sum += v;
        ++expected;
      } else {
        std::this_thread::yield();
      }
    }
    done.load(std::memory_order_acquire);
  });

  producer.join();
  consumer.join();
  return consumed_sum;
}

void run_benchmark(std::size_t n, std::size_t capacity_pow2) {
  if ((capacity_pow2 & (capacity_pow2 - 1)) != 0) {
    std::cerr << "--capacity must be a power of two (got " << capacity_pow2 << ").\n";
    std::exit(1);
  }

  auto start = std::chrono::steady_clock::now();
  std::uint64_t sum = 0;

  switch (capacity_pow2) {
    case 1024: {
      SpscRingBuffer<std::uint64_t, 1024> rb;
      std::thread producer([&] {
        for (std::uint64_t i = 0; i < n; ++i) {
          while (!rb.push(i)) std::this_thread::yield();
        }
      });
      std::thread consumer([&] {
        std::uint64_t v = 0;
        for (std::uint64_t i = 0; i < n; ++i) {
          while (!rb.pop(v)) std::this_thread::yield();
          sum += v;
        }
      });
      producer.join();
      consumer.join();
      break;
    }
    case 2048: {
      SpscRingBuffer<std::uint64_t, 2048> rb;
      std::thread producer([&] {
        for (std::uint64_t i = 0; i < n; ++i) {
          while (!rb.push(i)) std::this_thread::yield();
        }
      });
      std::thread consumer([&] {
        std::uint64_t v = 0;
        for (std::uint64_t i = 0; i < n; ++i) {
          while (!rb.pop(v)) std::this_thread::yield();
          sum += v;
        }
      });
      producer.join();
      consumer.join();
      break;
    }
    case 4096: {
      SpscRingBuffer<std::uint64_t, 4096> rb;
      std::thread producer([&] {
        for (std::uint64_t i = 0; i < n; ++i) {
          while (!rb.push(i)) std::this_thread::yield();
        }
      });
      std::thread consumer([&] {
        std::uint64_t v = 0;
        for (std::uint64_t i = 0; i < n; ++i) {
          while (!rb.pop(v)) std::this_thread::yield();
          sum += v;
        }
      });
      producer.join();
      consumer.join();
      break;
    }
    case 8192: {
      SpscRingBuffer<std::uint64_t, 8192> rb;
      std::thread producer([&] {
        for (std::uint64_t i = 0; i < n; ++i) {
          while (!rb.push(i)) std::this_thread::yield();
        }
      });
      std::thread consumer([&] {
        std::uint64_t v = 0;
        for (std::uint64_t i = 0; i < n; ++i) {
          while (!rb.pop(v)) std::this_thread::yield();
          sum += v;
        }
      });
      producer.join();
      consumer.join();
      break;
    }
    case 16384: {
      SpscRingBuffer<std::uint64_t, 16384> rb;
      std::thread producer([&] {
        for (std::uint64_t i = 0; i < n; ++i) {
          while (!rb.push(i)) std::this_thread::yield();
        }
      });
      std::thread consumer([&] {
        std::uint64_t v = 0;
        for (std::uint64_t i = 0; i < n; ++i) {
          while (!rb.pop(v)) std::this_thread::yield();
          sum += v;
        }
      });
      producer.join();
      consumer.join();
      break;
    }
    default:
      std::cerr << "Unsupported --capacity " << capacity_pow2
                << " for this demo. Use one of 1024/2048/4096/8192/16384.\n";
      std::exit(1);
  }

  (void)sum;
  auto end = std::chrono::steady_clock::now();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  const double seconds = static_cast<double>(ms) / 1000.0;
  const double items_per_sec = seconds > 0 ? static_cast<double>(n) / seconds : 0.0;

  std::cout << "Benchmark:\n"
            << "  n=" << n << "\n"
            << "  capacity=" << capacity_pow2 << " (max items=" << (capacity_pow2 - 1) << ")\n"
            << "  time_ms=" << ms << "\n"
            << "  throughput_items_per_sec=" << static_cast<long long>(items_per_sec) << "\n";
}

}

int main(int argc, char** argv) {
  const Options opt = parse_args(argc, argv);
  if (!opt.test && !opt.bench) {
    std::cout << "Use --test or --bench. Run with --help for usage.\n";
    return 0;
  }

  if (opt.test) {
    const auto sum = run_correctness(opt.n);
    std::cout << "Correctness passed. Sum=" << sum << "\n";
  }

  if (opt.bench) {
    run_benchmark(opt.n, opt.capacity);
  }
  return 0;
}

