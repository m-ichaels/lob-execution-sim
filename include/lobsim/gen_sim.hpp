#pragma once
// Simulator modes (b) queue-reactive and (c) Hawkes.  Both drive the same generative book:
// others' liquidity lives in a Book keyed by price, the state is read through the HLR reference
// price (K levels per side), new levels entering the window are revealed from the calibrated
// stationary law, and our own orders are part of the visible queue - which is exactly where the
// two modes differ from replay: they *react* to us.
//
//  QRSim     : Huang-Lehalle-Rosenbaum (2015) intensities lambda^{L,C,M}(level, queue bucket),
//              non-unit order sizes per (type, level, bucket) after Bodor-Carlier (2024).
//              Between events the process is Markov, so when one of our actions lands before the
//              next drawn event we simply apply it and redraw (memorylessness makes this exact).
//  Reference-price jumps (all generative modes): BTCUSDT on a $0.01 tick is a small-tick book -
//              the best quotes reposition by hundreds of ticks about once a second, which no
//              tick-by-tick queue mechanism can generate.  So the book is re-initialised around a
//              new reference price (HLR re-initialisation, theta = 1) at jump times drawn from a
//              calibrated rate and size law.  In QRSim the rate and direction depend on the touch
//              imbalance (state-dependent: our own resting orders move it); in ZISim they are
//              unconditional.  Our resting orders survive a jump; if the new book crosses them they
//              are filled (the incoming quotes were marketable against us).
//  HawkesSim : 6-type mutually exciting process, sums-of-exponentials kernels, Ogata thinning.
//              Our own limit / cancel / market orders enter the event history, so the market
//              reacts to us through the fitted excitation.  Placement levels and sizes come from
//              the empirical marginals.
#include "simulator.hpp"
#include "book.hpp"
#include "market_model.hpp"

namespace lobsim {

class GenSim : public Simulator {
public:
    GenSim(const MarketModel& m, Ts t_start, Ts t_end, Price start_mid, uint64_t seed)
        : m_(m), rng_(seed), t_end_(t_end) {
        now_ = t_start; t_start_ = t_start;
        init_book(start_mid);
    }
    // Best prices include our own resting orders: they are visible liquidity like anyone else's.
    Price best_bid() const override { return best_px(Side::Bid); }
    Price best_ask() const override { return best_px(Side::Ask); }
    Qty best_bid_qty() const override { Price p = best_px(Side::Bid); return book_.qty_at(Side::Bid, p) + our_resting_qty(Side::Bid, p); }
    Qty best_ask_qty() const override { Price p = best_px(Side::Ask); return book_.qty_at(Side::Ask, p) + our_resting_qty(Side::Ask, p); }
    Qty qty_at(Side s, Price p) const override { return book_.qty_at(s, p); }
    Qty depth(Side s, int levels) const override { return book_.depth(s, levels) + our_resting_qty_total(s); }
    bool book_valid() const override { return book_.valid(); }
    Ts start_ts() const override { return t_start_; }
    Ts end_ts() const override { return t_end_; }
    const Book& book() const { return book_; }
    const MarketModel& model() const { return m_; }
    uint64_t generated_events() const { return n_gen_; }

    bool step() override {
        if (now_ >= t_end_) return false;
        Ts t_ev; GenEvent ev;
        bool have = draw_next(now_, t_ev, ev);
        // reference-price jump clock (memoryless: redrawn every step)
        double jr = jump_rate();
        Ts t_jump = jr > 0 ? now_ + (Ts)std::llround(rng_.exponential(jr) * 1e9) : INT64_MAX;
        if (t_jump < (have ? t_ev : t_end_)) { have = true; t_ev = t_jump; ev = GenEvent{EvType::Limit, Side::Bid, -1, 0}; ev.jump = true; }
        Ts t_act = next_action_ts();
        if (t_act < (have ? t_ev : t_end_)) { advance_to(t_act); return true; }   // our action first; redraw next step
        if (!have) { advance_to(t_end_); return false; }
        advance_to(t_ev);
        if (ev.jump) apply_jump(); else apply_event(ev);
        ++n_gen_;
        return true;
    }

protected:
    struct GenEvent { EvType type; Side side; int level; Qty size; bool jump = false; };
    virtual double jump_rate() const { return m_.jump_rate_pooled; }        // ZI / Hawkes: unconditional
    virtual bool jump_up() { return rng_.uniform() < m_.jump_up_pooled; }
    virtual bool draw_next(Ts from, Ts& t_ev, GenEvent& ev) = 0;
    virtual void on_generated(const GenEvent&) {}          // hook (Hawkes history)
    virtual void on_ours(EvType, Side) {}                  // our own order as a market event

    // ---- state helpers ------------------------------------------------------------------------
    Price best_px(Side s) const {
        Price b = book_.best(s); bool have = s == Side::Bid ? book_.has_bid() : book_.has_ask();
        for (const Order* o : resting_orders()) if (o->side == s && (!have || (s == Side::Bid ? o->price > b : o->price < b))) { b = o->price; have = true; }
        return b;
    }
    Qty others_at(Side s, int i) const { return book_.qty_at(s, ref_.price(s, i)); }
    Qty total_at(Side s, int i) const { Price p = ref_.price(s, i); return book_.qty_at(s, p) + our_resting_qty(s, p); }
    int best_level(Side s) const { return book_.valid() ? std::max(1, ref_.level(s, best_px(s))) : 1; }
    Qty sample_size(EvType t, int level, Qty q_before) {
        int lv = std::min(std::max(level, 0), m_.K); int b = m_.bucket(q_before);
        const SizeDist& d = m_.sizes[(int)t][lv][b];
        if (d.total >= 20) return d.sample(rng_);
        const SizeDist& p = m_.sizes_pooled[(int)t][lv];
        return p.total > 0 ? p.sample(rng_) : std::max<Qty>(1, (Qty)std::llround(m_.aes));
    }

    void apply_event(const GenEvent& e) {
        switch (e.type) {
            case EvType::Limit:  apply_limit(e.side, e.level, e.size); break;
            case EvType::Cancel: apply_cancel(e.side, e.level, e.size); break;
            case EvType::Market: apply_market(e.side, e.size); break;
        }
        on_generated(e);
        after_change();
    }
    void apply_limit(Side s, int level, Qty size) {
        Price p;
        if (level <= 0) {   // inside the spread: one tick better than the current best on that side
            if (spread() <= 1) { level = 1; p = ref_.price(s, 1); }
            else p = s == Side::Bid ? best_px(Side::Bid) + 1 : best_px(Side::Ask) - 1;
        } else p = ref_.price(s, level);
        // never post through the opposite best (that would be a marketable order = market event)
        if (book_.valid() && (s == Side::Bid ? p >= best_px(Side::Ask) : p <= best_px(Side::Bid))) return;
        set_level(s, p, book_.qty_at(s, p) + size);
    }
    void apply_cancel(Side s, int level, Qty size) {
        Price p = ref_.price(s, level);
        Qty q = book_.qty_at(s, p);
        if (q <= 0) return;
        set_level(s, p, q - std::min(size, q));
    }
    // A market order of `size` hits passive side `s` (walks levels).
    void apply_market(Side s, Qty size) {
        while (size > 0 && book_.valid()) {
            Price p = best_px(s);
            Qty others = book_.qty_at(s, p);
            Qty ours = our_resting_qty(s, p);
            if (others + ours <= 0) { book_.set(s, p, 0); continue; }
            Qty take = std::min(size, others + ours);
            Qty filled_ours = absorb_market_flow(s, p, take);
            Qty from_others = std::min(take - filled_ours, others);
            book_.set(s, p, others - from_others);
            emit({now_, 0, EvKind::Trade, s, p, take});
            emit({now_, 0, EvKind::Level, s, p, others - from_others + our_resting_qty(s, p)});
            size -= take;
            if (size > 0 && book_.qty_at(s, p) + our_resting_qty(s, p) > 0) break;   // safety
        }
    }
    void set_level(Side s, Price p, Qty q) {
        Qty prev = book_.set(s, p, q);
        if (prev != q) on_level_change(s, p, prev, q);
        emit({now_, 0, EvKind::Level, s, p, q + our_resting_qty(s, p)});
    }
    void after_change() {
        if (!book_.valid()) { refill_empty_side(); }
        long long old = ref_.pref2;
        ref_.update(best_px(Side::Bid), best_px(Side::Ask));
        if (ref_.pref2 != old) {
            if (m_.theta > 0 && rng_.uniform() < m_.theta) reinit_others();
            reveal();
        }
    }
    void refill_empty_side() {
        // A side got completely eaten (rare, tiny books): rebuild it from the stationary law.
        for (Side s : {Side::Bid, Side::Ask}) if (!(s == Side::Bid ? book_.has_bid() : book_.has_ask())) {
            Price anchor = s == Side::Bid ? (book_.has_ask() ? book_.best_ask() - 1 : (Price)ref_.value()) : (book_.has_bid() ? book_.best_bid() + 1 : (Price)ref_.value() + 1);
            for (int i = 1; i <= m_.K; ++i) {
                Price p = s == Side::Bid ? anchor - (i - 1) : anchor + (i - 1);
                Qty q = m_.stationary[i].total > 0 ? m_.stationary[i].sample(rng_) : (Qty)std::llround(m_.aes);
                set_level(s, p, std::max<Qty>(1, q));
            }
            if (s == Side::Bid) bid_reveal_ = anchor - (m_.K - 1); else ask_reveal_ = anchor + (m_.K - 1);
        }
    }
    void reveal() {
        for (int i = 1; i <= m_.K; ++i) {
            Price pb = ref_.price(Side::Bid, i), pa = ref_.price(Side::Ask, i);
            if (pb < bid_reveal_) { Qty q = m_.stationary[i].total > 0 ? m_.stationary[i].sample(rng_) : (Qty)std::llround(m_.aes); if (q > 0 && book_.qty_at(Side::Bid, pb) == 0) set_level(Side::Bid, pb, q); }
            if (pa > ask_reveal_) { Qty q = m_.stationary[i].total > 0 ? m_.stationary[i].sample(rng_) : (Qty)std::llround(m_.aes); if (q > 0 && book_.qty_at(Side::Ask, pa) == 0) set_level(Side::Ask, pa, q); }
        }
        bid_reveal_ = std::min(bid_reveal_, ref_.price(Side::Bid, m_.K));
        ask_reveal_ = std::max(ask_reveal_, ref_.price(Side::Ask, m_.K));
    }
    void reinit_others() {
        for (Side s : {Side::Bid, Side::Ask}) for (int i = 1; i <= m_.K; ++i) {
            Price p = ref_.price(s, i);
            Qty q = m_.stationary[i].total > 0 ? m_.stationary[i].sample(rng_) : (Qty)std::llround(m_.aes);
            if (i == 1) q = std::max<Qty>(q, 1);
            set_level(s, p, q);
        }
    }
    // Move the reference price by a calibrated jump and re-initialise the others' book around it.
    void apply_jump() {
        if (m_.jump_size.total <= 0) return;
        Qty size = m_.jump_size.sample(rng_);
        bool up = jump_up();
        Price old_bb = best_px(Side::Bid);
        Price new_bb = up ? old_bb + size : old_bb - size;
        // remove every existing level (emit removals), then rebuild K levels each side
        std::vector<std::pair<Side, Price>> gone;
        for (auto& kv : book_.bids()) gone.emplace_back(Side::Bid, kv.first);
        for (auto& kv : book_.asks()) gone.emplace_back(Side::Ask, kv.first);
        for (auto& g : gone) set_level(g.first, g.second, 0);
        // the incoming quotes cross any of our resting orders on the wrong side of the new spread
        for (const Order* o : resting_orders()) {
            bool crossed = o->side == Side::Bid ? (o->price >= new_bb + 1) : (o->price <= new_bb);
            if (crossed) { Qty q = o->leaves; absorb_market_flow(o->side, o->price, q + (Qty)std::llround(std::max(o->q.front, 0.0))); }
        }
        for (int i = 1; i <= m_.K; ++i) {
            Qty qb = m_.stationary[i].total > 0 ? m_.stationary[i].sample(rng_) : (Qty)std::llround(m_.aes);
            Qty qa = m_.stationary[i].total > 0 ? m_.stationary[i].sample(rng_) : (Qty)std::llround(m_.aes);
            if (i == 1) { qb = std::max<Qty>(qb, 1); qa = std::max<Qty>(qa, 1); }
            if (qb > 0) set_level(Side::Bid, new_bb - i + 1, qb);
            if (qa > 0) set_level(Side::Ask, new_bb + i, qa);
        }
        bid_reveal_ = new_bb - m_.K + 1; ask_reveal_ = new_bb + m_.K;
        ref_.update(best_px(Side::Bid), best_px(Side::Ask));
        on_book_reset();
    }
    void init_book(Price mid) {
        book_.clear();
        emit({now_, 0, EvKind::Snapshot, Side::Bid, 0, 0});
        for (int i = 1; i <= m_.K; ++i) {
            Qty qb = m_.stationary.size() > (size_t)i && m_.stationary[i].total > 0 ? m_.stationary[i].sample(rng_) : (Qty)std::llround(m_.aes);
            Qty qa = m_.stationary.size() > (size_t)i && m_.stationary[i].total > 0 ? m_.stationary[i].sample(rng_) : (Qty)std::llround(m_.aes);
            if (i == 1) { qb = std::max<Qty>(qb, 1); qa = std::max<Qty>(qa, 1); }
            if (qb > 0) set_level(Side::Bid, mid - i + 1, qb);
            if (qa > 0) set_level(Side::Ask, mid + i, qa);
        }
        bid_reveal_ = mid - m_.K + 1; ask_reveal_ = mid + m_.K;
        ref_.update(book_.best_bid(), book_.best_ask());
    }

    Qty execute_taker(Order& o, Price limit) override {
        Side passive = other(o.side);
        Qty done = 0;
        while (o.leaves > 0 && book_.valid()) {
            Price p = best_px(passive);
            if (limit != 0 && (o.side == Side::Bid ? p > limit : p < limit)) break;
            Qty avail = book_.qty_at(passive, p) + our_resting_qty(passive, p);
            if (avail <= 0) { book_.set(passive, p, 0); continue; }
            Qty take = std::min(avail, o.leaves);
            Qty self_match = absorb_market_flow(passive, p, take);   // crossing our own resting order
            Qty from_others = std::min(take - self_match, book_.qty_at(passive, p));
            book_.set(passive, p, book_.qty_at(passive, p) - from_others);
            record_fill(o, p, take, false);
            emit({now_, 0, EvKind::Trade, passive, p, take});
            emit({now_, 0, EvKind::Level, passive, p, book_.qty_at(passive, p) + our_resting_qty(passive, p)});
            done += take;
        }
        if (done > 0) { on_ours(EvType::Market, passive); after_change(); }
        return done;
    }
    void on_our_limit_posted(Side s, Price p, Qty) override { on_ours(EvType::Limit, s); emit({now_, 0, EvKind::Level, s, p, book_.qty_at(s, p) + our_resting_qty(s, p)}); }
    void on_our_limit_removed(Side s, Price p, Qty q) override { if (q > 0) on_ours(EvType::Cancel, s); emit({now_, 0, EvKind::Level, s, p, book_.qty_at(s, p) + our_resting_qty(s, p)}); }

    const MarketModel& m_;
    Rng rng_;
    Book book_;
    RefPrice ref_;
    Ts t_start_ = 0, t_end_;
    Price bid_reveal_ = 0, ask_reveal_ = 0;
    uint64_t n_gen_ = 0;
};

// ---- (b) queue-reactive ------------------------------------------------------------------------
class QRSim : public GenSim {
public:
    using GenSim::GenSim;
    std::string mode() const override { return "queue_reactive"; }
protected:
    int imb() const { return MarketModel::imb_bucket(best_bid_qty(), best_ask_qty()); }
    double jump_rate() const override { return m_.jump_rate.size() == 5 ? m_.jump_rate[imb()] : m_.jump_rate_pooled; }
    bool jump_up() override { double p = m_.jump_up_prob.size() == 5 ? m_.jump_up_prob[imb()] : m_.jump_up_pooled; return rng_.uniform() < p; }
    bool draw_next(Ts from, Ts& t_ev, GenEvent& ev) override {
        w_.clear(); cand_.clear();
        for (Side s : {Side::Bid, Side::Ask}) {
            for (int i = 1; i <= m_.K; ++i) {
                Qty tot = total_at(s, i), oth = others_at(s, i);
                int b = m_.bucket(tot);
                double lL = m_.lam_L[i - 1][b];
                double lC = tot > 0 ? m_.lam_C[i - 1][b] * (double)oth / (double)tot : 0.0;
                if (lL > 0) { w_.push_back(lL); cand_.push_back({EvType::Limit, s, i, tot}); }
                if (lC > 0 && oth > 0) { w_.push_back(lC); cand_.push_back({EvType::Cancel, s, i, tot}); }
            }
            if (spread() > 1 && !m_.lam_L_inside.empty() && m_.lam_L_inside[0] > 0) { w_.push_back(m_.lam_L_inside[0]); cand_.push_back({EvType::Limit, s, 0, 0}); }
            int ib = best_level(s);
            Qty tb = total_at(s, ib);
            double lM = m_.lam_M[m_.bucket(tb)];
            if (lM > 0) { w_.push_back(lM); cand_.push_back({EvType::Market, s, ib, tb}); }
        }
        double tot = 0; for (double x : w_) tot += x;
        if (tot <= 0) return false;
        double tau = rng_.exponential(tot);
        t_ev = from + (Ts)std::llround(tau * 1e9);
        if (t_ev >= t_end_) return false;
        size_t k = rng_.weighted(w_);
        const Cand& c = cand_[k];
        ev.type = c.type; ev.side = c.side; ev.level = c.level;
        ev.size = sample_size(c.type, c.level, c.q_before);
        if (c.type == EvType::Cancel) ev.size = std::min(ev.size, others_at(c.side, c.level));
        return true;
    }
private:
    struct Cand { EvType type; Side side; int level; Qty q_before; };
    std::vector<double> w_; std::vector<Cand> cand_;
};

// ---- null model: zero-intelligence Poisson book (Abergel & Jedidi 2013) -------------------------
// Same book mechanics as the queue-reactive mode, but the intensities do not depend on the queue
// state: limit arrivals lambda_L(i), cancellations c_i * q_i, market orders lambda_M.  Sizes are
// drawn from the pooled per-(type, level) distributions.  This is the "does the reactivity matter"
// control: anything the queue-reactive mode gets right that this one gets wrong is due to the
// state dependence.
class ZISim : public GenSim {
public:
    using GenSim::GenSim;
    std::string mode() const override { return "zero_intelligence"; }
protected:
    bool draw_next(Ts from, Ts& t_ev, GenEvent& ev) override {
        w_.clear(); cand_.clear();
        for (Side s : {Side::Bid, Side::Ask}) {
            for (int i = 1; i <= m_.K; ++i) {
                Qty oth = others_at(s, i);
                double lL = i - 1 < (int)m_.zi_lam_L.size() ? m_.zi_lam_L[i - 1] : 0;
                double lC = i - 1 < (int)m_.zi_c.size() ? m_.zi_c[i - 1] * (double)oth : 0;
                if (lL > 0) { w_.push_back(lL); cand_.push_back({EvType::Limit, s, i}); }
                if (lC > 0) { w_.push_back(lC); cand_.push_back({EvType::Cancel, s, i}); }
            }
            if (m_.zi_lam_M > 0) { w_.push_back(m_.zi_lam_M); cand_.push_back({EvType::Market, s, best_level(s)}); }
        }
        double tot = 0; for (double x : w_) tot += x;
        if (tot <= 0) return false;
        t_ev = from + (Ts)std::llround(rng_.exponential(tot) * 1e9);
        if (t_ev >= t_end_) return false;
        const Cand& c = cand_[rng_.weighted(w_)];
        ev.type = c.type; ev.side = c.side; ev.level = c.level;
        int lv = std::min(std::max(c.level, 0), m_.K);
        const SizeDist& d = m_.sizes_pooled[(int)c.type][lv];
        ev.size = d.total > 0 ? d.sample(rng_) : std::max<Qty>(1, (Qty)std::llround(m_.aes));
        if (c.type == EvType::Cancel) ev.size = std::min(ev.size, others_at(c.side, c.level));
        return true;
    }
private:
    struct Cand { EvType type; Side side; int level; };
    std::vector<double> w_; std::vector<Cand> cand_;
};

// ---- (c) Hawkes -------------------------------------------------------------------------------------
class HawkesSim : public GenSim {
public:
    HawkesSim(const MarketModel& m, Ts t_start, Ts t_end, Price start_mid, uint64_t seed)
        : GenSim(m, t_start, t_end, start_mid, seed) {
        P_ = (int)m_.beta.size();
        S_.assign(6, std::vector<double>(P_, 0.0));
        t_last_ = t_start;
    }
    std::string mode() const override { return "hawkes"; }
    // intensity of each type at time t (without adding events)
    std::array<double, 6> intensities(Ts t) const {
        double dt = (t - t_last_) / 1e9;
        std::array<double, 6> lam{};
        for (int e = 0; e < 6; ++e) {
            double v = m_.mu[e];
            for (int s = 0; s < 6; ++s) for (int p = 0; p < P_; ++p) v += m_.alpha[e][s][p] * m_.beta[p] * S_[s][p] * std::exp(-m_.beta[p] * dt);
            lam[e] = v;
        }
        return lam;
    }
protected:
    bool draw_next(Ts from, Ts& t_ev, GenEvent& ev) override {
        // Ogata thinning: intensities are non-increasing between events, so lambda(from) bounds them.
        double t = (double)from;
        for (int guard = 0; guard < 100000; ++guard) {
            auto lam = intensities((Ts)t);
            double bar = 0; for (double x : lam) bar += x;
            if (bar <= 0) return false;
            double tau = rng_.exponential(bar);
            t += tau * 1e9;
            if (t >= (double)t_end_) return false;
            auto lam2 = intensities((Ts)t);
            double tot = 0; for (double x : lam2) tot += x;
            if (rng_.uniform() * bar <= tot) {
                std::vector<double> w(lam2.begin(), lam2.end());
                int e = (int)rng_.weighted(w);
                t_ev = (Ts)t;
                ev.type = (EvType)(e / 2); ev.side = (Side)(e % 2);
                switch (ev.type) {
                    case EvType::Limit: {
                        int lv = (int)rng_.weighted(m_.limit_level_dist);
                        if (lv == 0 && spread() <= 1) { std::vector<double> w2(m_.limit_level_dist); w2[0] = 0; lv = (int)rng_.weighted(w2); if (lv == 0) lv = 1; }
                        ev.level = lv; ev.size = sample_size(EvType::Limit, lv, lv > 0 ? total_at(ev.side, lv) : 0);
                        break;
                    }
                    case EvType::Cancel: {
                        std::vector<double> w2(m_.K + 1, 0.0); bool any = false;
                        for (int i = 1; i <= m_.K; ++i) if (others_at(ev.side, i) > 0) { w2[i] = m_.cancel_level_dist[i] + 1e-9; any = true; }
                        if (!any) { ev.level = 1; ev.size = 0; break; }
                        ev.level = (int)rng_.weighted(w2);
                        ev.size = std::min(sample_size(EvType::Cancel, ev.level, total_at(ev.side, ev.level)), others_at(ev.side, ev.level));
                        break;
                    }
                    case EvType::Market: {
                        int ib = best_level(ev.side);
                        ev.level = ib; ev.size = sample_size(EvType::Market, ib, total_at(ev.side, ib));
                        break;
                    }
                }
                return true;
            }
        }
        return false;
    }
    void on_generated(const GenEvent& e) override { add_event(now_, (int)e.type * 2 + (int)e.side); }
    void on_ours(EvType t, Side s) override { add_event(now_, (int)t * 2 + (int)s); }
private:
    void add_event(Ts t, int type) {
        double dt = (t - t_last_) / 1e9;
        if (dt < 0) dt = 0;
        for (int s = 0; s < 6; ++s) for (int p = 0; p < P_; ++p) S_[s][p] *= std::exp(-m_.beta[p] * dt);
        for (int p = 0; p < P_; ++p) S_[type][p] += 1.0;
        t_last_ = t;
    }
    int P_ = 0;
    std::vector<std::vector<double>> S_;
    Ts t_last_ = 0;
};

} // namespace lobsim
