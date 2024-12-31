#pragma once
// Simulator mode (a): historical L2 replay with queue-position modelling.
//
// Assumptions, stated plainly:
//  * history is fixed - our orders never change what the tape does next (no impact);
//  * a passive order joins the back of the visible queue at its price; it advances on trades at
//    that price and on cancellations according to the selected QueueModel (see queue_model.hpp);
//  * a public trade at a price *through* ours fills us completely (the level must have emptied);
//  * our taker orders walk the displayed book and do not remove liquidity from the tape.
// The known-answer test "replay without our orders reproduces history exactly" holds trivially:
// the market state is the tape.
#include "simulator.hpp"
#include "book.hpp"

namespace lobsim {

class ReplaySim : public Simulator {
public:
    explicit ReplaySim(const Tape* tape, size_t start_index = 0) : tape_(tape) {
        // Prime the book with everything up to start_index so the market is valid when trading starts.
        idx_ = 0;
        while (idx_ < start_index && idx_ < tape_->events.size()) apply_(tape_->events[idx_++], false);
        now_ = idx_ > 0 ? tape_->events[idx_ - 1].ts_ex : (tape_->events.empty() ? 0 : tape_->events[0].ts_ex);
    }
    std::string mode() const override { return "replay"; }

    Price best_bid() const override { return book_.best_bid(); }
    Price best_ask() const override { return book_.best_ask(); }
    Qty best_bid_qty() const override { return book_.best_bid_qty(); }
    Qty best_ask_qty() const override { return book_.best_ask_qty(); }
    Qty qty_at(Side s, Price p) const override { return book_.qty_at(s, p); }
    Qty depth(Side s, int levels) const override { return book_.depth(s, levels); }
    bool book_valid() const override { return book_.valid(); }
    Ts start_ts() const override { return tape_->t0(); }
    Ts end_ts() const override { return tape_->t1(); }
    const Book& book() const { return book_; }
    size_t index() const { return idx_; }

    bool step() override {
        if (idx_ >= tape_->events.size()) return false;
        const L2Event& e = tape_->events[idx_++];
        advance_to(e.ts_ex);
        apply_(e, true);
        return true;
    }

protected:
    Qty execute_taker(Order& o, Price limit) override {
        Qty done = 0;
        std::vector<std::pair<Price, Qty>> hits;
        book_.walk(o.side, [&](Price p, Qty q) {
            if (limit != 0 && (o.side == Side::Bid ? p > limit : p < limit)) return false;
            Qty take = std::min(q, o.leaves - done);
            hits.emplace_back(p, take); done += take;
            return done < o.leaves;
        });
        for (auto& h : hits) record_fill(o, h.first, h.second, false);
        return done;
    }

private:
    void apply_(const L2Event& e, bool notify) {
        switch (e.kind) {
            case EvKind::Snapshot:
                book_.clear();
                if (notify) on_book_reset();
                break;
            case EvKind::Level: {
                Qty prev = book_.set(e.side, e.price, e.qty);
                if (notify && prev != e.qty) on_level_change(e.side, e.price, prev, e.qty);
                break;
            }
            case EvKind::Trade:
                if (notify) on_public_trade(e.side, e.price, e.qty);
                break;
        }
    }
    const Tape* tape_;
    size_t idx_ = 0;
    Book book_;
};

} // namespace lobsim
