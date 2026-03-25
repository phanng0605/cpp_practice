#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// Work-stealing thread pool.
// Each worker has a local deque; thieves steal from the opposite end.
// `pending_` tracks unfinished tasks so `wait_idle()` is well-defined.
class WorkStealingThreadPool {
  struct Worker {
    std::deque<std::function<void()>> q;
    std::mutex m;
  };

  // `Worker` has a mutex (non-movable), so store it via pointers.
  std::vector<std::unique_ptr<Worker>> workers_;
  std::vector<std::thread> threads_;

  std::atomic<bool> stop_{false};
  std::atomic<std::size_t> pending_{0};
  std::atomic<std::size_t> rr_{0};

  std::mutex cv_m_;
  std::condition_variable cv_;

  std::mutex idle_m_;
  std::condition_variable idle_cv_;

  void worker_loop(std::size_t idx) {
    while (true) {
      if (stop_.load(std::memory_order_acquire)) {
        break;
      }

      std::function<void()> task;
      if (pop_local(idx, task) || steal(idx, task)) {
        task();
        const auto remaining = pending_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) {
          std::lock_guard<std::mutex> lk(idle_m_);
          idle_cv_.notify_all();
        }
        continue;
      }

      // Nothing local to do: wait for new work.
      std::unique_lock<std::mutex> lk(cv_m_);
      cv_.wait(lk, [&] {
        return stop_.load(std::memory_order_acquire) || pending_.load(std::memory_order_acquire) > 0;
      });
    }
  }

  bool pop_local(std::size_t idx, std::function<void()>& out) {
    std::lock_guard<std::mutex> lk(workers_[idx]->m);
    if (workers_[idx]->q.empty()) {
      return false;
    }
    out = std::move(workers_[idx]->q.front());
    workers_[idx]->q.pop_front();
    return true;
  }

  bool steal(std::size_t thief_idx, std::function<void()>& out) {
    const std::size_t n = workers_.size();
    // Pick a victim and try to steal.
    for (std::size_t offset = 1; offset < n; ++offset) {
      const std::size_t victim = (thief_idx + offset) % n;
      std::unique_lock<std::mutex> lk(workers_[victim]->m, std::try_to_lock);
      if (!lk.owns_lock()) {
        continue;
      }
      if (workers_[victim]->q.empty()) {
        continue;
      }

      // Steal from the back (reduce contention vs local operations).
      out = std::move(workers_[victim]->q.back());
      workers_[victim]->q.pop_back();
      return true;
    }
    return false;
  }

 public:
  explicit WorkStealingThreadPool(std::size_t threads = std::thread::hardware_concurrency()) {
    if (threads == 0) {
      threads = std::thread::hardware_concurrency();
    }
    if (threads == 0) {
      threads = 1;
    }

    workers_.reserve(threads);
    threads_.reserve(threads);
    for (std::size_t i = 0; i < threads; ++i) {
      workers_.push_back(std::make_unique<Worker>());
      threads_.emplace_back([this, i] { worker_loop(i); });
    }
  }

  ~WorkStealingThreadPool() {
    // Wait for submitted work, then stop worker threads.
    wait_idle();
    stop_.store(true, std::memory_order_release);
    cv_.notify_all();
    for (auto& t : threads_) {
      if (t.joinable()) {
        t.join();
      }
    }
  }

  WorkStealingThreadPool(const WorkStealingThreadPool&) = delete;
  WorkStealingThreadPool& operator=(const WorkStealingThreadPool&) = delete;

  template <class F>
  auto submit(F&& f) -> std::future<std::invoke_result_t<F>> {
    using R = std::invoke_result_t<F>;

    auto task_ptr = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
    std::future<R> fut = task_ptr->get_future();

    std::function<void()> wrapper = [task_ptr]() { (*task_ptr)(); };

    const std::size_t idx = rr_.fetch_add(1, std::memory_order_relaxed) % workers_.size();
    {
      std::lock_guard<std::mutex> lk(workers_[idx]->m);
      workers_[idx]->q.push_front(std::move(wrapper));
    }

    pending_.fetch_add(1, std::memory_order_acq_rel);
    cv_.notify_one();
    return fut;
  }

  void wait_idle() {
    std::unique_lock<std::mutex> lk(idle_m_);
    idle_cv_.wait(lk, [&] { return pending_.load(std::memory_order_acquire) == 0; });
  }
};

