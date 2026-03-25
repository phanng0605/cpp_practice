#include "work_stealing_thread_pool.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct Options {
  bool test = false;
  bool bench = false;
  std::size_t threads = 0;  // 0 => use hardware_concurrency()
  std::size_t n = 200000;
  std::size_t block_size = 4096;
};

Options parse_args(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--test") {
      opt.test = true;
    } else if (a == "--bench") {
      opt.bench = true;
    } else if (a == "--threads" && i + 1 < argc) {
      opt.threads = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else if (a == "--tasks" && i + 1 < argc) {
      opt.n = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else if (a == "--block" && i + 1 < argc) {
      opt.block_size = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else if (a == "--help" || a == "-h") {
      std::cout << "Usage:\n"
                   "  ./workstealing_pool --test\n"
                   "  ./workstealing_pool --bench [--threads T] [--tasks N] [--block B]\n";
      std::exit(0);
    }
  }
  return opt;
}

bool run_correctness(std::size_t threads) {
  const std::size_t kN = 10000;
  WorkStealingThreadPool pool(threads);

  std::vector<std::future<std::uint64_t>> futures;
  futures.reserve(kN);

  for (std::uint64_t i = 0; i < kN; ++i) {
    futures.emplace_back(pool.submit([i] {
      std::uint64_t x = i;
      x = x * x + 17 * i;
      return x;
    }));
  }

  std::uint64_t sum = 0;
  for (std::uint64_t i = 0; i < kN; ++i) {
    const auto v = futures[i].get();
    sum += v;
  }

  std::uint64_t expected_sum = 0;
  for (std::uint64_t i = 0; i < kN; ++i) {
    std::uint64_t x = i;
    x = x * x + 17 * i;
    expected_sum += x;
  }

  if (sum != expected_sum) {
    std::cerr << "Correctness failed: got " << sum << " expected " << expected_sum << "\n";
    return false;
  }
  return true;
}

void run_benchmark(std::size_t threads, std::size_t n_tasks, std::size_t block_size) {
  if (block_size == 0) {
    std::cerr << "--block must be > 0\n";
    std::exit(1);
  }

  const std::size_t data_len = n_tasks * block_size;
  std::vector<std::uint64_t> data(data_len);
  for (std::size_t i = 0; i < data_len; ++i) {
    data[i] = static_cast<std::uint64_t>(i % 997);
  }

  WorkStealingThreadPool pool(threads);

  const std::size_t num_blocks = (data_len + block_size - 1) / block_size;
  std::vector<std::future<std::uint64_t>> futures;
  futures.reserve(num_blocks);

  auto t0 = std::chrono::steady_clock::now();

  for (std::size_t b = 0; b < num_blocks; ++b) {
    const std::size_t begin = b * block_size;
    const std::size_t end = std::min(data_len, begin + block_size);
    futures.emplace_back(pool.submit([begin, end, &data] {
      std::uint64_t s = 0;
      for (std::size_t i = begin; i < end; ++i) {
        s += data[i];
      }
      return s;
    }));
  }

  std::uint64_t total = 0;
  for (auto& f : futures) {
    total += f.get();
  }

  const auto t1 = std::chrono::steady_clock::now();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

  (void)total;
  const double seconds = static_cast<double>(ms) / 1000.0;
  const double tasks_per_sec = seconds > 0 ? static_cast<double>(num_blocks) / seconds : 0.0;

  std::cout << "Benchmark:\n"
            << "  threads=" << (threads == 0 ? std::thread::hardware_concurrency() : threads) << "\n"
            << "  data_len=" << data_len << "\n"
            << "  blocks=" << num_blocks << " (block_size=" << block_size << ")\n"
            << "  time_ms=" << ms << "\n"
            << "  tasks_per_sec=" << static_cast<long long>(tasks_per_sec) << "\n";
}

}

int main(int argc, char** argv) {
  const Options opt = parse_args(argc, argv);
  if (!opt.test && !opt.bench) {
    std::cout << "Use --test or --bench. Run with --help for usage.\n";
    return 0;
  }

  if (opt.test) {
    const bool ok = run_correctness(opt.threads);
    std::cout << (ok ? "Correctness passed\n" : "Correctness failed\n");
    return ok ? 0 : 1;
  }

  if (opt.bench) {
    run_benchmark(opt.threads, opt.n, opt.block_size);
  }
  return 0;
}

