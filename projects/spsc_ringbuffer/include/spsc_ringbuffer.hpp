#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>

// SPSC lock-free ring buffer (single producer / single consumer).
// Capacity is a power of two; one slot stays empty to tell full vs empty.
// Memory ordering: producer publishes with `release`, consumer consumes with `acquire`.
template <class T, std::size_t Capacity>
class SpscRingBuffer {
  static_assert(Capacity >= 2, "Capacity must be at least 2");
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
  static_assert(std::is_copy_assignable_v<T>, "T must be copy-assignable");
  static_assert(std::is_nothrow_copy_assignable_v<T> || std::is_copy_assignable_v<T>,
                "T must be copy-assignable (strong guarantee not required)");

  static constexpr std::size_t kMask = Capacity - 1;

  // Cache-line padding to reduce false sharing between producer and consumer.
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
  alignas(64) T buffer_[Capacity];

 public:
  constexpr std::size_t capacity() const noexcept { return Capacity; }

  bool empty() const noexcept {
    const auto h = head_.load(std::memory_order_acquire);
    const auto t = tail_.load(std::memory_order_acquire);
    return h == t;
  }

  bool full() const noexcept {
    const auto t = tail_.load(std::memory_order_acquire);
    const auto h = head_.load(std::memory_order_acquire);
    const auto next = (t + 1) & kMask;
    return next == h;
  }

  bool push(const T& value) noexcept {
    const auto t = tail_.load(std::memory_order_relaxed);
    const auto h = head_.load(std::memory_order_acquire);
    const auto next = (t + 1) & kMask;
    if (next == h) {
      return false;
    }

    buffer_[t] = value;
    tail_.store(next, std::memory_order_release);
    return true;
  }

  bool pop(T& out) noexcept {
    const auto h = head_.load(std::memory_order_relaxed);
    const auto t = tail_.load(std::memory_order_acquire);
    if (h == t) {
      return false;
    }

    out = buffer_[h];
    const auto next = (h + 1) & kMask;
    head_.store(next, std::memory_order_release);
    return true;
  }
};

