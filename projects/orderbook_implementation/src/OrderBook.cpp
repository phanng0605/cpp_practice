#include "orderbook.hpp"

#include "limit_order_book.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using Engine = LimitOrderBook<std::uint32_t, 10000, 30000, 1'000'000>;
// Prices are cents: demo range $100–$300.

constexpr OrderBook::Money mul_money(OrderBook::Price price, OrderBook::Qty qty) {
  return static_cast<OrderBook::Money>(price) * static_cast<OrderBook::Money>(qty);
}

}  // namespace

struct OrderBook::Impl {
  struct User {
    std::string name;
    Balance bal{};
    Money locked_quote = 0;
    Qty locked_base = 0;
  };

  struct LiveOrder {
    bool active = false;
    UserId user = 0;
    bool is_bid = false;
    Price limit_price = 0;
    Qty remaining = 0;
    Money locked_quote = 0;  // for bids: limit_price * remaining
    Qty locked_base = 0;     // for asks: remaining
  };

  Engine engine;
  std::unordered_map<std::string, UserId> name_to_id;
  std::vector<User> users;
  std::vector<LiveOrder> orders;
  OrderId next_order_id = 1;

  static constexpr std::size_t kTradeLogCap = 4096;
  bool trade_log_enabled = false;
  std::array<OrderBook::Trade, kTradeLogCap> trade_log{};
  std::size_t trade_log_size = 0;
  std::size_t trade_log_head = 0;  // next write position

  OrderBook::Stats stats{};
  std::vector<Engine::Execution> exec_scratch;

  Impl() {
    users.reserve(1024);
    orders.resize(1);  // index 0 unused
    name_to_id.reserve(1024);

    // Seed market makers.
    const UserId mm1 = makeUser("mm_alpha");
    const UserId mm2 = makeUser("mm_beta");
    addBalance(mm1, 10'000'000, 10'000);
    addBalance(mm2, 10'000'000, 10'000);

    // Seed around $150.
    (void)add_bid(mm1, 14950, 50);
    (void)add_bid(mm2, 14900, 75);
    (void)add_ask(mm1, 15050, 60);
    (void)add_ask(mm2, 15100, 80);
  }

  UserId makeUser(std::string_view name) {
    auto it = name_to_id.find(std::string(name));
    if (it != name_to_id.end()) return it->second;

    const UserId id = static_cast<UserId>(users.size());
    users.push_back(User{std::string(name), {}});
    name_to_id.emplace(users.back().name, id);
    return id;
  }

  bool addBalance(UserId user, Money quote_delta, Qty base_delta) {
    if (user >= users.size()) return false;
    auto& u = users[user];

    if (quote_delta < 0 && u.bal.quote + quote_delta < 0) return false;
    if (base_delta > 0) u.bal.base += base_delta;
    if (quote_delta != 0) u.bal.quote += quote_delta;
    return true;
  }

  Balance getBalance(UserId user) const {
    if (user >= users.size()) return {};
    return users[user].bal;
  }

  void push_trade(const Engine::Execution& e) {
    if (!trade_log_enabled) return;
    const OrderId taker_id = static_cast<OrderId>(e.taker_order_id);
    const OrderId maker_id = static_cast<OrderId>(e.maker_order_id);
    trade_log[trade_log_head] = OrderBook::Trade{
        taker_id, maker_id, static_cast<Price>(e.price), e.qty,
        e.taker_side == Engine::Side::Buy, e.maker_side == Engine::Side::Buy};
    trade_log_head = (trade_log_head + 1) % kTradeLogCap;
    trade_log_size = std::min(trade_log_size + 1, kTradeLogCap);
  }

  void record_stats_cancel_success() { stats.cancels_succeeded += 1; }

  std::optional<OrderId> submit_bid(UserId user, Price price, Qty qty,
                                     bool discard_unfilled) {
    stats.orders_submitted += 1;
    if (user >= users.size()) return std::nullopt;
    if (qty == 0) return std::nullopt;

    auto& u = users[user];
    const Money need = mul_money(price, qty);
    if (u.bal.quote - u.locked_quote < need) return std::nullopt;

    const OrderId id = next_order_id++;
    if (id >= orders.size()) orders.resize(id + 1);
    orders[id] = LiveOrder{true, user, true, price, qty, need, 0};

    u.locked_quote += need;

    exec_scratch.clear();
    if (!engine.process_order(Engine::Side::Buy, price, qty, static_cast<int>(id),
                              &exec_scratch)) {
      u.locked_quote -= need;
      orders[id].active = false;
      orders[id].remaining = 0;
      orders[id].locked_quote = 0;
      return std::nullopt;
    }

    apply_executions(static_cast<int>(id), exec_scratch);
    if (discard_unfilled && orders[id].active) {
      (void)cancel(user, id, true);
    }
    stats.orders_accepted += 1;
    return id;
  }

  std::optional<OrderId> submit_ask(UserId user, Price price, Qty qty,
                                     bool discard_unfilled) {
    stats.orders_submitted += 1;
    if (user >= users.size()) return std::nullopt;
    if (qty == 0) return std::nullopt;

    auto& u = users[user];
    if (u.bal.base - u.locked_base < qty) return std::nullopt;

    const OrderId id = next_order_id++;
    if (id >= orders.size()) orders.resize(id + 1);
    orders[id] = LiveOrder{true, user, false, price, qty, 0, qty};

    u.locked_base += qty;

    exec_scratch.clear();
    if (!engine.process_order(Engine::Side::Sell, price, qty, static_cast<int>(id),
                              &exec_scratch)) {
      u.locked_base -= qty;
      orders[id].active = false;
      orders[id].remaining = 0;
      orders[id].locked_base = 0;
      return std::nullopt;
    }

    apply_executions(static_cast<int>(id), exec_scratch);
    if (discard_unfilled && orders[id].active) {
      (void)cancel(user, id, false);
    }
    stats.orders_accepted += 1;
    return id;
  }

  std::optional<OrderId> add_bid(UserId user, Price price, Qty qty) {
    return submit_bid(user, price, qty, false);
  }

  std::optional<OrderId> add_ask(UserId user, Price price, Qty qty) {
    return submit_ask(user, price, qty, false);
  }

  std::optional<OrderId> add_bid_ioc(UserId user, Price price, Qty qty) {
    return submit_bid(user, price, qty, true);
  }

  std::optional<OrderId> add_ask_ioc(UserId user, Price price, Qty qty) {
    return submit_ask(user, price, qty, true);
  }

  std::optional<OrderId> add_market_bid(UserId user, Qty qty) {
    // Worst-case lock: buy up to Engine::PriceMax, then cancel any remainder.
    constexpr Price price_max = 30000;
    return submit_bid(user, price_max, qty, true);
  }

  std::optional<OrderId> add_market_ask(UserId user, Qty qty) {
    // Worst-case: sell down to Engine::PriceMin, then cancel any remainder.
    constexpr Price price_min = 10000;
    return submit_ask(user, price_min, qty, true);
  }

  bool cancel(UserId user, OrderId id, bool expect_bid) {
    stats.cancels_attempted += 1;
    if (user >= users.size()) return false;
    if (id == 0 || id >= orders.size()) return false;
    auto& o = orders[id];
    if (!o.active) return false;
    if (o.user != user) return false;
    if (o.is_bid != expect_bid) return false;

    const bool ok = engine.cancel(static_cast<int>(id));
    if (!ok) return false;

    auto& u = users[user];
    if (o.is_bid) {
      u.locked_quote -= o.locked_quote;
    } else {
      u.locked_base -= o.locked_base;
    }

    o.active = false;
    o.remaining = 0;
    o.locked_quote = 0;
    o.locked_base = 0;
    record_stats_cancel_success();
    return true;
  }

  bool cancelBid(UserId user, OrderId id) { return cancel(user, id, true); }
  bool cancelAsk(UserId user, OrderId id) { return cancel(user, id, false); }

  void setTradeLogEnabled(bool enabled) { trade_log_enabled = enabled; }
  void clearTradeLog() {
    trade_log_size = 0;
    trade_log_head = 0;
  }

  std::vector<OrderBook::Trade> getRecentTrades(std::size_t max) const {
    if (trade_log_size == 0 || max == 0) return {};
    const std::size_t n = std::min(max, trade_log_size);
    std::vector<OrderBook::Trade> out;
    out.reserve(n);

    for (std::size_t i = 0; i < n; ++i) {
      const std::size_t idx = (trade_log_head + kTradeLogCap - 1 - i) % kTradeLogCap;
      out.push_back(trade_log[idx]);
    }
    return out;
  }

  OrderBook::Stats getStats() const { return stats; }
  void resetStats() { stats = {}; }

  OrderBook::Quote getQuote() const {
    OrderBook::Quote q;

    if (auto p = engine.best_bid()) {
      const Qty qty = engine.level_quantity(Engine::Side::Buy, *p);
      q.best_bid = OrderBook::QuoteLevel{static_cast<Price>(*p), qty};
    }
    if (auto p = engine.best_ask()) {
      const Qty qty = engine.level_quantity(Engine::Side::Sell, *p);
      q.best_ask = OrderBook::QuoteLevel{static_cast<Price>(*p), qty};
    }
    return q;
  }

  std::vector<OrderBook::DepthLevel> getDepth(bool bids, std::size_t max_levels) const {
    std::vector<OrderBook::DepthLevel> out;
    out.reserve(max_levels);

    if (bids) {
      auto p = engine.best_bid();
      while (p && out.size() < max_levels) {
        const Qty qty = engine.level_quantity(Engine::Side::Buy, *p);
        if (qty) out.push_back({static_cast<Price>(*p), qty});
        // scan down price space (demo range small)
        for (int next = *p - 1; next >= 10000; --next) {
          if (engine.level_quantity(Engine::Side::Buy, next) != 0) {
            p = next;
            goto continue_outer_bid;
          }
        }
        break;
      continue_outer_bid:;
      }
    } else {
      auto p = engine.best_ask();
      while (p && out.size() < max_levels) {
        const Qty qty = engine.level_quantity(Engine::Side::Sell, *p);
        if (qty) out.push_back({static_cast<Price>(*p), qty});
        for (int next = *p + 1; next <= 30000; ++next) {
          if (engine.level_quantity(Engine::Side::Sell, next) != 0) {
            p = next;
            goto continue_outer_ask;
          }
        }
        break;
      continue_outer_ask:;
      }
    }
    return out;
  }

  std::vector<OrderBook::DepthLevel> getDepthBids(std::size_t max_levels) const {
    return getDepth(true, max_levels);
  }
  std::vector<OrderBook::DepthLevel> getDepthAsks(std::size_t max_levels) const {
    return getDepth(false, max_levels);
  }

  void apply_executions(int taker_id, const std::vector<Engine::Execution>& execs) {
    if (taker_id <= 0 || static_cast<std::size_t>(taker_id) >= orders.size()) return;
    auto& taker = orders[static_cast<std::size_t>(taker_id)];
    if (!taker.active) return;

    stats.trades_executed += execs.size();
    for (const auto& e : execs) {
      const auto maker_id = static_cast<OrderId>(e.maker_order_id);
      if (maker_id == 0 || maker_id >= orders.size()) continue;
      auto& maker = orders[maker_id];
      if (!maker.active) continue;

      push_trade(e);

      const Qty traded = static_cast<Qty>(e.qty);
      const Price px = static_cast<Price>(e.price);
      const Money notional = mul_money(px, traded);

      // Update maker remaining/locks.
      maker.remaining -= traded;
      if (maker.is_bid) {
        const Money delta_lock = mul_money(maker.limit_price, traded);
        maker.locked_quote -= delta_lock;
        users[maker.user].locked_quote -= delta_lock;
      } else {
        maker.locked_base -= traded;
        users[maker.user].locked_base -= traded;
      }
      if (maker.remaining == 0) maker.active = false;

      // Update taker remaining/locks.
      taker.remaining -= traded;
      if (taker.is_bid) {
        const Money delta_lock = mul_money(taker.limit_price, traded);
        taker.locked_quote -= delta_lock;
        users[taker.user].locked_quote -= delta_lock;
      } else {
        taker.locked_base -= traded;
        users[taker.user].locked_base -= traded;
      }
      if (taker.remaining == 0) taker.active = false;

      // Settlement depends on maker side.
      if (maker.is_bid) {
        users[maker.user].bal.base += traded;
        users[maker.user].bal.quote -= notional;
        users[taker.user].bal.base -= traded;
        users[taker.user].bal.quote += notional;
      } else {
        users[maker.user].bal.base -= traded;
        users[maker.user].bal.quote += notional;
        users[taker.user].bal.base += traded;
        users[taker.user].bal.quote -= notional;
      }

      // Refund BUY taker price improvement.
      if (taker.is_bid) {
        const Money locked_at_limit = mul_money(taker.limit_price, traded);
        const Money refund = locked_at_limit - notional;
        if (refund > 0) {
          users[taker.user].bal.quote += refund;
        }
      }
      if (maker.is_bid) {
        const Money locked_at_limit = mul_money(maker.limit_price, traded);
        const Money refund = locked_at_limit - notional;
        if (refund > 0) {
          users[maker.user].bal.quote += refund;
        }
      }
    }
  }
};

OrderBook::OrderBook() : impl_(new Impl()) {}
OrderBook::~OrderBook() = default;

OrderBook::UserId OrderBook::makeUser(std::string_view name) { return impl_->makeUser(name); }
bool OrderBook::addBalance(UserId user, Money quote_delta, Qty base_delta) {
  return impl_->addBalance(user, quote_delta, base_delta);
}
OrderBook::Balance OrderBook::getBalance(UserId user) const { return impl_->getBalance(user); }

std::optional<OrderBook::OrderId> OrderBook::add_bid(UserId user, Price price, Qty qty) {
  return impl_->add_bid(user, price, qty);
}
std::optional<OrderBook::OrderId> OrderBook::add_ask(UserId user, Price price, Qty qty) {
  return impl_->add_ask(user, price, qty);
}
std::optional<OrderBook::OrderId> OrderBook::add_bid_ioc(UserId user, Price price, Qty qty) {
  return impl_->add_bid_ioc(user, price, qty);
}
std::optional<OrderBook::OrderId> OrderBook::add_ask_ioc(UserId user, Price price, Qty qty) {
  return impl_->add_ask_ioc(user, price, qty);
}
std::optional<OrderBook::OrderId> OrderBook::add_market_bid(UserId user, Qty qty) {
  return impl_->add_market_bid(user, qty);
}
std::optional<OrderBook::OrderId> OrderBook::add_market_ask(UserId user, Qty qty) {
  return impl_->add_market_ask(user, qty);
}
bool OrderBook::cancelBid(UserId user, OrderId id) { return impl_->cancelBid(user, id); }
bool OrderBook::cancelAsk(UserId user, OrderId id) { return impl_->cancelAsk(user, id); }

OrderBook::Quote OrderBook::getQuote() const { return impl_->getQuote(); }
std::vector<OrderBook::DepthLevel> OrderBook::getDepthBids(std::size_t max_levels) const {
  return impl_->getDepthBids(max_levels);
}
std::vector<OrderBook::DepthLevel> OrderBook::getDepthAsks(std::size_t max_levels) const {
  return impl_->getDepthAsks(max_levels);
}

void OrderBook::setTradeLogEnabled(bool enabled) { impl_->setTradeLogEnabled(enabled); }
void OrderBook::clearTradeLog() { impl_->clearTradeLog(); }
std::vector<OrderBook::Trade> OrderBook::getRecentTrades(std::size_t max) const {
  return impl_->getRecentTrades(max);
}
OrderBook::Stats OrderBook::getStats() const { return impl_->getStats(); }
void OrderBook::resetStats() { impl_->resetStats(); }

