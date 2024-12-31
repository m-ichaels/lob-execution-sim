#pragma once
// Common order-entry interface shared by the three simulator back-ends (replay, queue-reactive,
// Hawkes).  Algorithms only ever see this class; the FIX gateway wraps it too.
//
// Time model: `now()` is exchange time.  An order submitted at t becomes effective at
// t + latency.order_in; a fill executed at exchange time t' is visible to the algorithm at
// t' + latency.report_out.  All pending actions are interleaved with market events in time order,
// so a replay run is fully deterministic.
#include "types.hpp"
#include "queue_model.hpp"
#include "tape.hpp"
#include <vector>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <algorithm>

namespace lobsim {

enum class OrdType : uint8_t { Limit = 0, Market = 1 };
enum class OrdStatus : uint8_t { PendingNew = 0, Live = 1, Filled = 2, Cancelled = 3, Rejected = 4 };

struct Order {
    uint64_t id = 0;
    Side side = Side::Bid;
    OrdType type = OrdType::Limit;
    Price price = 0;          // limit price (ticks); ignored for market orders
    Qty qty = 0;
    Qty leaves = 0;
    Ts submit_ts = 0;         // algorithm time of submission
    Ts active_ts = 0;         // exchange time it became effective
    OrdStatus status = OrdStatus::PendingNew;
    QueuePos q;               // queue position while resting
    bool resting = false;     // true while sitting passively in the book
};

struct Fill {
    uint64_t order_id;
    Ts ts;                    // exchange time
    Price price;
    Qty qty;
    bool passive;             // true: we were the resting side
    double mid_at_fill;       // mid immediately before the fill (ticks)
};

struct Latency {
    Ts order_in = 0;          // ns from submit to effective
    Ts report_out = 0;        // ns from execution to visibility
};

class Simulator {
public:
    virtual ~Simulator() = default;
    virtual std::string mode() const = 0;

    // ---- market state (others' liquidity only; our resting orders are tracked separately) ----
    virtual Price best_bid() const = 0;
    virtual Price best_ask() const = 0;
    virtual Qty   best_bid_qty() const = 0;
    virtual Qty   best_ask_qty() const = 0;
    virtual Qty   qty_at(Side s, Price p) const = 0;
    virtual Qty   depth(Side s, int levels) const = 0;
    virtual bool  book_valid() const = 0;
    double mid() const { return 0.5 * (best_bid() + best_ask()); }
    Price spread() const { return best_ask() - best_bid(); }
    Price best(Side s) const { return s == Side::Bid ? best_bid() : best_ask(); }
    Qty best_qty(Side s) const { return s == Side::Bid ? best_bid_qty() : best_ask_qty(); }

    // ---- clock ---------------------------------------------------------------------------
    Ts now() const { return now_; }
    virtual Ts start_ts() const = 0;
    virtual Ts end_ts() const = 0;
    // Advance by one exchange event. Returns false when the tape / horizon is exhausted.
    virtual bool step() = 0;
    // Advance until now() >= t (processing pending actions), returns false if exhausted before.
    bool run_until(Ts t) { while (now_ < t) { if (!step()) return false; } return true; }

    // ---- order entry (virtual so the FIX client can stand in for an in-process simulator) ------
    virtual uint64_t submit_limit(Side s, Price p, Qty q) { return enqueue(s, OrdType::Limit, p, q); }
    virtual uint64_t submit_market(Side s, Qty q) { return enqueue(s, OrdType::Market, 0, q); }
    virtual void cancel(uint64_t id) {
        Action a; a.at = now_ + latency_.order_in; a.seq = ++action_seq_; a.kind = Action::Cancel; a.id = id;
        actions_.push_back(a);
    }
    // Cancel/replace: on the venues modelled here an amended order loses its queue position, so a
    // replace is a cancel followed by a new order (both subject to the order latency). Returns the new id.
    virtual uint64_t replace(uint64_t id, Price new_price, Qty new_qty) {
        const Order* o = order(id); if (!o) return 0;
        cancel(id);
        return o->type == OrdType::Market ? submit_market(o->side, new_qty) : submit_limit(o->side, new_price, new_qty);
    }
    virtual const Order* order(uint64_t id) const { auto it = orders_.find(id); return it == orders_.end() ? nullptr : &it->second; }
    virtual std::vector<const Order*> resting_orders() const {
        std::vector<const Order*> v; for (auto& kv : orders_) if (kv.second.resting) v.push_back(&kv.second); return v;
    }
    // Fills that have become visible (ts + report_out <= now) since the last call.
    virtual std::vector<Fill> take_visible_fills() {
        std::vector<Fill> out;
        while (fill_cursor_ < fills_.size() && fills_[fill_cursor_].ts + latency_.report_out <= now_) out.push_back(fills_[fill_cursor_++]);
        return out;
    }
    virtual const std::vector<Fill>& all_fills() const { return fills_; }
    // Total lots executed for us so far (all orders).
    virtual Qty executed() const { Qty q = 0; for (auto& f : fills_) q += f.qty; return q; }

    // ---- configuration --------------------------------------------------------------------
    void set_latency(Latency l) { latency_ = l; }
    const Latency& latency() const { return latency_; }
    void set_queue_model(QueueModel m) { qm_ = m; }
    const QueueModel& queue_model() const { return qm_; }
    // Generative simulators write the market events they generate here (for validation tapes).
    void set_tape_writer(TapeWriter* w) { tape_out_ = w; }
    // Number of exchange events processed so far
    uint64_t event_count() const { return event_count_; }
    // Reset order state but keep market state (used between episodes on the same tape).
    void reset_orders() { orders_.clear(); fills_.clear(); fill_cursor_ = 0; actions_.clear(); }

protected:
    // ---- to be called by derived simulators --------------------------------------------------
    // Move the clock forward to t, executing any pending order actions that are due before t.
    void advance_to(Ts t) {
        while (!actions_.empty()) {
            auto& a = actions_.front();
            if (a.at > t) break;
            Action act = a; actions_.pop_front();
            now_ = std::max(now_, act.at);
            execute_action(act);
        }
        now_ = std::max(now_, t);
        ++event_count_;
    }
    // Others' quantity at (side, price) changed prev -> now (limit arrival or cancellation, NOT a trade).
    void on_level_change(Side s, Price p, Qty prev, Qty now) {
        for (auto& kv : orders_) {
            Order& o = kv.second;
            if (!o.resting || o.side != s || o.price != p) continue;
            qm_.depth(o.q, prev, now);
            Qty ex = std::min(qm_.executable(o.q), o.leaves);
            if (ex > 0) { o.q.front = 0; record_fill(o, o.price, ex, true); }
        }
    }
    // Replay only: a public trade of `qty` at `price` hit the passive side `s`.
    void on_public_trade(Side s, Price p, Qty qty) {
        for (auto& kv : orders_) {
            Order& o = kv.second;
            if (!o.resting || o.side != s) continue;
            bool through = (s == Side::Bid) ? (p < o.price) : (p > o.price);
            if (through) { record_fill(o, o.price, o.leaves, true); continue; }
            if (p != o.price) continue;
            qm_.trade(o.q, qty);
            Qty ex = std::min(qm_.executable(o.q), o.leaves);
            if (ex > 0) { o.q.front = 0; record_fill(o, o.price, ex, true); }
        }
    }
    // Generative sims: `qty` lots of market flow reach level (s, p). Others ahead of us are consumed
    // first, then our resting orders, then others behind.  Returns lots filled against our orders.
    Qty absorb_market_flow(Side s, Price p, Qty qty) {
        Qty ours = 0; double consumed_others = 0;   // others consumed so far were ahead of every later order too
        for (auto& kv : orders_) {
            Order& o = kv.second;
            if (!o.resting || o.side != s || o.price != p) continue;
            double front = std::max(o.q.front - consumed_others, 0.0);
            if ((double)qty <= front) { o.q.front = front - (double)qty; consumed_others += (double)qty; qty = 0; continue; }
            consumed_others += front; qty -= (Qty)std::llround(front); o.q.front = 0;
            Qty f = std::min(qty, o.leaves);
            qty -= f; ours += f;
            record_fill(o, o.price, f, true);
        }
        return ours;
    }
    // Our resting quantity at a level (generative sims add it to the visible queue size).
    Qty our_resting_qty(Side s, Price p) const {
        Qty q = 0; for (auto& kv : orders_) if (kv.second.resting && kv.second.side == s && kv.second.price == p) q += kv.second.leaves; return q;
    }
    Qty our_resting_qty_total(Side s) const {
        Qty q = 0; for (auto& kv : orders_) if (kv.second.resting && kv.second.side == s) q += kv.second.leaves; return q;
    }
    void on_book_reset() {
        for (auto& kv : orders_) if (kv.second.resting) kv.second.q.front = std::min(kv.second.q.front, (double)qty_at(kv.second.side, kv.second.price));
    }
    void emit(const L2Event& e) { if (tape_out_) tape_out_->write(e); }
    // Exchange time of the earliest pending order action (INT64_MAX if none). Actions are
    // appended with non-decreasing effective times, so the front of the deque is the earliest.
    Ts next_action_ts() const { return actions_.empty() ? INT64_MAX : actions_.front().at; }

    // Execute a taker order against the current book. Generative simulators consume liquidity;
    // replay does not (history is fixed). Must call record_fill for each level executed and return
    // lots executed. `limit` = 0 means no limit (pure market order).
    virtual Qty execute_taker(Order& o, Price limit) = 0;
    // Hooks for generative simulators to react to our passive orders entering / leaving the book.
    virtual void on_our_limit_posted(Side, Price, Qty) {}
    virtual void on_our_limit_removed(Side, Price, Qty) {}

    void record_fill(Order& o, Price px, Qty q, bool passive) {
        if (q <= 0) return;
        Fill f{o.id, now_, px, q, passive, book_valid() ? mid() : (double)px};
        fills_.push_back(f);
        o.leaves -= q;
        if (o.leaves <= 0) {
            o.status = OrdStatus::Filled;
            if (o.resting) { o.resting = false; on_our_limit_removed(o.side, o.price, 0); }
        }
    }

    Ts now_ = 0;
    Latency latency_;
    QueueModel qm_;
    TapeWriter* tape_out_ = nullptr;
    uint64_t event_count_ = 0;

private:
    struct Action { Ts at; uint64_t seq; enum Kind { New, Cancel } kind; uint64_t id; Order o; };

    uint64_t enqueue(Side s, OrdType t, Price p, Qty q) {
        Order o; o.id = ++next_id_; o.side = s; o.type = t; o.price = p; o.qty = q; o.leaves = q; o.submit_ts = now_;
        orders_[o.id] = o;
        Action a; a.at = now_ + latency_.order_in; a.seq = ++action_seq_; a.kind = Action::New; a.id = o.id; a.o = o;
        actions_.push_back(a);
        return o.id;
    }
    void execute_action(const Action& a) {
        auto it = orders_.find(a.id);
        if (it == orders_.end()) return;
        Order& o = it->second;
        if (a.kind == Action::Cancel) {
            if (o.status == OrdStatus::Live || o.status == OrdStatus::PendingNew) {
                o.status = OrdStatus::Cancelled;
                if (o.resting) { o.resting = false; on_our_limit_removed(o.side, o.price, o.leaves); }
            }
            return;
        }
        if (o.status != OrdStatus::PendingNew) return;
        o.active_ts = now_;
        if (!book_valid()) { o.status = OrdStatus::Rejected; return; }
        o.status = OrdStatus::Live;
        if (o.type == OrdType::Market) {
            execute_taker(o, 0);
            if (o.leaves > 0) o.status = OrdStatus::Cancelled;   // nothing left to hit
            return;
        }
        // limit: marketable part first
        bool crosses = o.side == Side::Bid ? (o.price >= best_ask()) : (o.price <= best_bid());
        if (crosses) execute_taker(o, o.price);
        if (o.leaves > 0 && o.status == OrdStatus::Live) {
            o.resting = true;
            qm_.new_order(o.q, qty_at(o.side, o.price));
            on_our_limit_posted(o.side, o.price, o.leaves);
        }
    }

    std::map<uint64_t, Order> orders_;
    std::vector<Fill> fills_;
    size_t fill_cursor_ = 0;
    std::deque<Action> actions_;
    uint64_t next_id_ = 0;
    uint64_t action_seq_ = 0;
};

} // namespace lobsim
