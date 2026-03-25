#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

class OrderBook {
 public:
  using UserId = std::uint32_t;
  using OrderId = std::uint32_t;
  using Qty = std::uint32_t;
  using Money = std::int64_t;   // quote currency minor units
  using Price = std::int32_t;   // quote minor units per 1 base unit

  struct Balance {
    Money quote = 0;  // USD cents
    Qty base = 0;     // shares
  };

  struct QuoteLevel {
    Price price = 0;
    Qty qty = 0;
  };

  struct Quote {
    std::optional<QuoteLevel> best_bid;
    std::optional<QuoteLevel> best_ask;
  };

  struct DepthLevel {
    Price price = 0;
    Qty qty = 0;
  };

  struct Trade {
    OrderId taker_order_id = 0;
    OrderId maker_order_id = 0;
    Price price = 0;
    Qty qty = 0;
    bool taker_is_bid = false;
    bool maker_is_bid = false;
  };

  struct Stats {
    std::uint64_t orders_submitted = 0;
    std::uint64_t orders_accepted = 0;
    std::uint64_t trades_executed = 0;
    std::uint64_t cancels_attempted = 0;
    std::uint64_t cancels_succeeded = 0;
  };

  static constexpr std::string_view TICKER = "GOOGL/USD";

  explicit OrderBook();
  ~OrderBook();
  OrderBook(OrderBook&&) noexcept = default;
  OrderBook& operator=(OrderBook&&) noexcept = default;
  OrderBook(const OrderBook&) = delete;
  OrderBook& operator=(const OrderBook&) = delete;

  // Users
  UserId makeUser(std::string_view name);
  bool addBalance(UserId user, Money quote_delta, Qty base_delta);
  Balance getBalance(UserId user) const;

  // Trading
  std::optional<OrderId> add_bid(UserId user, Price price, Qty qty);
  std::optional<OrderId> add_ask(UserId user, Price price, Qty qty);
  std::optional<OrderId> add_bid_ioc(UserId user, Price price, Qty qty);
  std::optional<OrderId> add_ask_ioc(UserId user, Price price, Qty qty);
  std::optional<OrderId> add_market_bid(UserId user, Qty qty);
  std::optional<OrderId> add_market_ask(UserId user, Qty qty);
  bool cancelBid(UserId user, OrderId id);
  bool cancelAsk(UserId user, OrderId id);

  // Market data
  Quote getQuote() const;
  std::vector<DepthLevel> getDepthBids(std::size_t max_levels = 10) const;
  std::vector<DepthLevel> getDepthAsks(std::size_t max_levels = 10) const;

  // Diagnostics
  void setTradeLogEnabled(bool enabled);
  void clearTradeLog();
  std::vector<Trade> getRecentTrades(std::size_t max = 50) const;
  Stats getStats() const;
  void resetStats();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

