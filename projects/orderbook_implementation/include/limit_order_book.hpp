#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

// Single-threaded L1 limit order book (one symbol).
// Fixed-size arrays + intrusive FIFO queues per price level.
// Best bid/ask is tracked with a bitset for fast lookup.
template <class QtyT, int PriceMin, int PriceMax, std::size_t MaxOrders>
class LimitOrderBook {
  static_assert(std::is_integral_v<QtyT> && std::is_unsigned_v<QtyT>,
                "QtyT must be an unsigned integer type");
  static_assert(PriceMin < PriceMax, "PriceMin must be < PriceMax");
  static_assert(MaxOrders > 0, "MaxOrders must be > 0");

 public:
  using Qty = QtyT;
  enum class Side : std::uint8_t { Buy, Sell };

  struct Execution {
    Side taker_side;
    int taker_order_id;
    Side maker_side;
    int maker_order_id;
    int price;
    Qty qty;
  };

 private:
  static constexpr int kPriceLo = PriceMin;
  static constexpr int kPriceHi = PriceMax;
  static constexpr int kLevels = kPriceHi - kPriceLo + 1;
  static_assert(kLevels > 1, "Need at least 2 price levels");

  static constexpr std::int32_t kNil = -1;

  struct BookSide {
    // Per price level: head/tail indices into the order pool.
    std::array<std::int32_t, kLevels> head{};
    std::array<std::int32_t, kLevels> tail{};
    std::array<Qty, kLevels> level_qty{};

    // Bitset of non-empty levels.
    static constexpr std::size_t kWords = (kLevels + 63) / 64;
    std::array<std::uint64_t, kWords> occ{};

    void reset() {
      head.fill(kNil);
      tail.fill(kNil);
      level_qty.fill(0);
      occ.fill(0);
    }
  };

  BookSide bids_;
  BookSide asks_;

  // Best bid/ask: indices into [0, kLevels).
  std::int32_t best_bid_ = kNil;
  std::int32_t best_ask_ = kNil;

  // Order pool nodes (intrusive list, indexed).
  std::array<std::int32_t, MaxOrders> next_{};
  std::array<std::int32_t, MaxOrders> prev_{};
  std::array<int, MaxOrders> order_id_{};
  std::array<int, MaxOrders> order_price_{};
  std::array<Qty, MaxOrders> order_qty_{};
  std::array<std::uint8_t, MaxOrders> order_side_{};

  // Free list over pool indices.
  std::int32_t free_head_ = 0;
  std::array<std::int32_t, MaxOrders> next_free_{};

  // Map order id -> pool index (demo assumes id in [0, MaxOrders)).
  std::array<std::int32_t, MaxOrders> id_to_idx_{};

  static constexpr std::uint8_t kBuy = 1;
  static constexpr std::uint8_t kSell = 2;

  static int price_to_level(int price) {
    return price - kPriceLo;
  }

  static int level_to_price(int level) {
    return level + kPriceLo;
  }

  static bool in_range(int price) {
    return price >= kPriceLo && price <= kPriceHi;
  }

  static std::size_t word_index(int level) {
    return static_cast<std::size_t>(level) / 64u;
  }

  static std::uint64_t bit_mask(int level) {
    return 1ull << (static_cast<unsigned>(level) & 63u);
  }

  void set_occupied(BookSide& side, int level, bool occupied) {
    const auto w = word_index(level);
    const auto m = bit_mask(level);
    if (occupied) {
      side.occ[w] |= m;
    } else {
      side.occ[w] &= ~m;
    }
  }

  bool is_occupied(const BookSide& side, int level) const {
    const auto w = word_index(level);
    const auto m = bit_mask(level);
    return (side.occ[w] & m) != 0;
  }

  // bids: highest occupied level
  std::int32_t find_best_bid() const {
    if constexpr (kLevels <= 64) {
      if (bids_.occ[0] == 0) return kNil;
      return static_cast<std::int32_t>(63 - std::countl_zero(bids_.occ[0]));
    }

    for (std::int32_t w = static_cast<std::int32_t>(bids_.occ.size()) - 1; w >= 0; --w) {
      const std::uint64_t x = bids_.occ[static_cast<std::size_t>(w)];
      if (x == 0) continue;
      const int msb = 63 - std::countl_zero(x);
      const int level = w * 64 + msb;
      if (level < kLevels) return level;
    }
    return kNil;
  }

  // asks: lowest occupied level
  std::int32_t find_best_ask() const {
    if constexpr (kLevels <= 64) {
      if (asks_.occ[0] == 0) return kNil;
      const int lsb = std::countr_zero(asks_.occ[0]);
      return static_cast<std::int32_t>(lsb);
    }

    for (std::size_t w = 0; w < asks_.occ.size(); ++w) {
      const std::uint64_t x = asks_.occ[w];
      if (x == 0) continue;
      const int lsb = std::countr_zero(x);
      const int level = static_cast<int>(w * 64 + static_cast<std::size_t>(lsb));
      if (level < kLevels) return level;
    }
    return kNil;
  }

  std::int32_t alloc_order(int order_id) {
    if (order_id < 0 || static_cast<std::size_t>(order_id) >= MaxOrders) {
      return kNil;
    }
    if (id_to_idx_[static_cast<std::size_t>(order_id)] != kNil) {
      return kNil;  // active duplicate id
    }
    if (free_head_ == kNil) {
      return kNil;  // out of pool
    }

    const std::int32_t idx = free_head_;
    free_head_ = next_free_[static_cast<std::size_t>(idx)];
    id_to_idx_[static_cast<std::size_t>(order_id)] = idx;
    return idx;
  }

  void free_order(int order_id) {
    if (order_id < 0 || static_cast<std::size_t>(order_id) >= MaxOrders) {
      return;
    }
    const std::int32_t idx = id_to_idx_[static_cast<std::size_t>(order_id)];
    if (idx == kNil) return;

    id_to_idx_[static_cast<std::size_t>(order_id)] = kNil;
    next_free_[static_cast<std::size_t>(idx)] = free_head_;
    free_head_ = idx;
  }

  void link_new_order(BookSide& side, int level, std::int32_t order_idx, bool is_bid) {
    const std::int32_t tail = side.tail[static_cast<std::size_t>(level)];
    if (tail == kNil) {
      side.head[static_cast<std::size_t>(level)] = order_idx;
      side.tail[static_cast<std::size_t>(level)] = order_idx;
      prev_[static_cast<std::size_t>(order_idx)] = kNil;
      next_[static_cast<std::size_t>(order_idx)] = kNil;
    } else {
      next_[static_cast<std::size_t>(tail)] = order_idx;
      prev_[static_cast<std::size_t>(order_idx)] = tail;
      next_[static_cast<std::size_t>(order_idx)] = kNil;
      side.tail[static_cast<std::size_t>(level)] = order_idx;
    }

    side.level_qty[static_cast<std::size_t>(level)] += order_qty_[static_cast<std::size_t>(order_idx)];
    set_occupied(side, level, true);
    if (is_bid) {
      if (best_bid_ == kNil || level > best_bid_) best_bid_ = level;
    } else {
      if (best_ask_ == kNil || level < best_ask_) best_ask_ = level;
    }
  }

  void unlink_order(BookSide& side, int level, std::int32_t order_idx, bool is_bid) {
    const std::int32_t p = prev_[static_cast<std::size_t>(order_idx)];
    const std::int32_t n = next_[static_cast<std::size_t>(order_idx)];

    if (p != kNil) {
      next_[static_cast<std::size_t>(p)] = n;
    } else {
      side.head[static_cast<std::size_t>(level)] = n;
    }
    if (n != kNil) {
      prev_[static_cast<std::size_t>(n)] = p;
    } else {
      side.tail[static_cast<std::size_t>(level)] = p;
    }

    side.level_qty[static_cast<std::size_t>(level)] -= order_qty_[static_cast<std::size_t>(order_idx)];
    order_qty_[static_cast<std::size_t>(order_idx)] = 0;

    if (side.level_qty[static_cast<std::size_t>(level)] == 0) {
      set_occupied(side, level, false);
      if (is_bid) {
        if (best_bid_ == level) best_bid_ = find_best_bid();
      } else {
        if (best_ask_ == level) best_ask_ = find_best_ask();
      }
    }
  }

 public:
  LimitOrderBook() { reset(); }

  void reset() {
    bids_.reset();
    asks_.reset();
    best_bid_ = kNil;
    best_ask_ = kNil;

    free_head_ = 0;
    for (std::size_t i = 0; i < MaxOrders; ++i) {
      next_free_[i] = (i + 1 < MaxOrders) ? static_cast<std::int32_t>(i + 1) : kNil;
      id_to_idx_[i] = kNil;
    }
    next_.fill(kNil);
    prev_.fill(kNil);
    order_qty_.fill(0);
  }

  std::optional<int> best_bid() const {
    if (best_bid_ == kNil) return std::nullopt;
    return level_to_price(best_bid_);
  }
  std::optional<int> best_ask() const {
    if (best_ask_ == kNil) return std::nullopt;
    return level_to_price(best_ask_);
  }

  Qty level_quantity(Side side, int price) const {
    if (!in_range(price)) return 0;
    const int level = price_to_level(price);
    return (side == Side::Buy ? bids_.level_qty[static_cast<std::size_t>(level)]
                              : asks_.level_qty[static_cast<std::size_t>(level)]);
  }

  // Process an incoming limit order and perform matching.
  bool process_order(Side side, int price, Qty qty, int order_id,
                      std::vector<Execution>* executions = nullptr) {
    if (qty == 0) return false;
    if (!in_range(price)) return false;
    if (order_id < 0 || static_cast<std::size_t>(order_id) >= MaxOrders) return false;

    const int level = price_to_level(price);
    Qty remaining = qty;

    if (side == Side::Buy) {
      while (remaining > 0 && best_ask_ != kNil) {
        const int ask_price = level_to_price(best_ask_);
        if (ask_price > price) break;

        auto& maker_side = asks_;
        const std::int32_t maker_idx =
            maker_side.head[static_cast<std::size_t>(best_ask_)];
        if (maker_idx == kNil) {
          set_occupied(asks_, best_ask_, false);
          best_ask_ = find_best_ask();
          continue;
        }

        const Qty maker_qty = order_qty_[static_cast<std::size_t>(maker_idx)];
        const Qty trade_qty = std::min(remaining, maker_qty);

        remaining -= trade_qty;
        maker_side.level_qty[static_cast<std::size_t>(best_ask_)] -= trade_qty;
        order_qty_[static_cast<std::size_t>(maker_idx)] -= trade_qty;

        if (executions) {
          const int maker_id = order_id_[static_cast<std::size_t>(maker_idx)];
          executions->push_back(Execution{side, order_id, Side::Sell, maker_id,
                                           ask_price, trade_qty});
        }

        // If maker fully consumed, unlink and free.
        if (order_qty_[static_cast<std::size_t>(maker_idx)] == 0) {
          const int maker_id = order_id_[static_cast<std::size_t>(maker_idx)];
          unlink_order(maker_side, best_ask_, maker_idx, false);
          free_order(maker_id);
        } else {
          // Partial fill: keep maker in the same FIFO position.
        }
      }

      if (remaining > 0) {
        const std::int32_t idx = alloc_order(order_id);
        if (idx == kNil) return false;
        order_id_[static_cast<std::size_t>(idx)] = order_id;
        order_side_[static_cast<std::size_t>(idx)] = kBuy;
        order_price_[static_cast<std::size_t>(idx)] = price;
        order_qty_[static_cast<std::size_t>(idx)] = remaining;
        link_new_order(bids_, level, idx, true);
        return true;
      }
      return true;
    } else {
      while (remaining > 0 && best_bid_ != kNil) {
        const int bid_price = level_to_price(best_bid_);
        if (bid_price < price) break;

        auto& maker_side = bids_;
        const std::int32_t maker_idx =
            maker_side.head[static_cast<std::size_t>(best_bid_)];
        if (maker_idx == kNil) {
          set_occupied(bids_, best_bid_, false);
          best_bid_ = find_best_bid();
          continue;
        }

        const Qty maker_qty = order_qty_[static_cast<std::size_t>(maker_idx)];
        const Qty trade_qty = std::min(remaining, maker_qty);

        remaining -= trade_qty;
        maker_side.level_qty[static_cast<std::size_t>(best_bid_)] -= trade_qty;
        order_qty_[static_cast<std::size_t>(maker_idx)] -= trade_qty;

        if (executions) {
          const int maker_id = order_id_[static_cast<std::size_t>(maker_idx)];
          executions->push_back(Execution{side, order_id, Side::Buy, maker_id,
                                           bid_price, trade_qty});
        }

        if (order_qty_[static_cast<std::size_t>(maker_idx)] == 0) {
          const int maker_id = order_id_[static_cast<std::size_t>(maker_idx)];
          unlink_order(maker_side, best_bid_, maker_idx, true);
          free_order(maker_id);
        }
      }

      if (remaining > 0) {
        const std::int32_t idx = alloc_order(order_id);
        if (idx == kNil) return false;
        order_id_[static_cast<std::size_t>(idx)] = order_id;
        order_side_[static_cast<std::size_t>(idx)] = kSell;
        order_price_[static_cast<std::size_t>(idx)] = price;
        order_qty_[static_cast<std::size_t>(idx)] = remaining;
        link_new_order(asks_, level, idx, false);
        return true;
      }
      return true;
    }
  }

  // Cancel a resting order by id. Returns false if not found/inactive.
  bool cancel(int order_id) {
    if (order_id < 0 || static_cast<std::size_t>(order_id) >= MaxOrders) return false;
    const std::int32_t idx = id_to_idx_[static_cast<std::size_t>(order_id)];
    if (idx == kNil) return false;

    const int price = order_price_[static_cast<std::size_t>(idx)];
    const int level = price_to_level(price);
    if (order_side_[static_cast<std::size_t>(idx)] == kBuy) {
      unlink_order(bids_, level, idx, true);
    } else {
      unlink_order(asks_, level, idx, false);
    }
    free_order(order_id);
    return true;
  }
};

