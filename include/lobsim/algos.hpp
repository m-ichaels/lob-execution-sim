#pragma once
// Execution algorithms.  All of them are slice schedulers: a target curve says how much should be
// done by time t, and a child-order policy says how each slice is executed.
//
//   TWAP           linear curve, market orders
//   VWAP           curve = calibrated intraday volume profile, market orders
//   AlmgrenChriss  curve = AC (2000) trajectory sinh(kappa (T-t))/sinh(kappa T), market orders
//   OFIAdaptive    the stage-4 scheduler: AC/GLFT-style liquidation curve, slice sizes tilted by the
//                  Cont-Kukanov-Stoikov OFI forecast, each slice executed as a Cont-Kukanov
//                  limit/market split with fill probabilities from the fitted fill model.
//
// The same class runs against any Simulator and against the FIX gateway (see fix.hpp), which is
// why it only talks to the abstract order-entry interface.
#include "simulator.hpp"
#include "market_model.hpp"
#include "fill_model.hpp"
#include <memory>
#include <deque>

namespace lobsim {

struct ParentOrder {
    Side side = Side::Bid;
    Qty qty = 0;
    Ts start = 0, end = 0;
    int slices = 10;
};

// Order-flow imbalance of Cont, Kukanov & Stoikov (2014), accumulated from top-of-book updates.
struct OfiTracker {
    Price pb = 0, pa = 0; Qty qb = 0, qa = 0; bool init = false;
    std::deque<std::pair<Ts, double>> hist;
    void update(Ts t, Price nb, Qty nqb, Price na, Qty nqa) {
        if (init && (nb != pb || nqb != qb || na != pa || nqa != qa)) {
            double e = 0;
            if (nb >= pb) e += (double)nqb; if (nb <= pb) e -= (double)qb;
            if (na <= pa) e -= (double)nqa; if (na >= pa) e += (double)qa;
            hist.emplace_back(t, e);
        }
        pb = nb; qb = nqb; pa = na; qa = nqa; init = true;
    }
    double sum_last(Ts now, Ts window) {
        while (!hist.empty() && hist.front().first < now - window) hist.pop_front();
        double s = 0; for (auto& h : hist) s += h.second; return s;
    }
};

class Algo {
public:
    virtual ~Algo() = default;
    virtual std::string name() const = 0;
    virtual void start(Simulator& sim, const ParentOrder& po) = 0;
    virtual void on_step(Simulator& sim) = 0;
    // Called at the end of the horizon: cancel everything, sweep the remainder.
    virtual void finish(Simulator& sim) = 0;
    virtual int child_orders() const = 0;
};

struct AlgoParams {
    const MarketModel* model = nullptr;
    double ac_lambda = 2e-6;        // AC risk aversion (per tick^2 per lot)
    double ac_kappaT_override = 0;  // if > 0, use kappa*T directly (documented in results)
    double ofi_window_s = 2.0;      // OFI look-back
    double ofi_tilt = 0.5;          // slice multiplier sensitivity (per unit of ofi forecast / sigma)
    double ck_lambda_u = 2.0;       // Cont-Kukanov unfilled penalty (half-spreads)
    double ck_adverse_ticks = 0.0;  // adverse selection charged to passive fills
    FillModel::Kind fill_kind = FillModel::Kind::Structural;
    std::vector<double> logistic_w;  // for Kind::Logistic
    QueueModel qm;                   // queue model for the structural fill model
    bool passive = true;             // OFIAdaptive: false => same schedule but all-market (ablation)
};

class SliceScheduler : public Algo {
public:
    enum class Curve { Twap, Vwap, AC, OfiAdaptive };
    SliceScheduler(Curve c, AlgoParams p) : curve_(c), prm_(std::move(p)) {}
    std::string name() const override {
        switch (curve_) { case Curve::Twap: return "TWAP"; case Curve::Vwap: return "VWAP"; case Curve::AC: return "AlmgrenChriss"; default: return !prm_.passive ? "OFIAdaptive_mkt" : prm_.ck_adverse_ticks > 0 ? "OFIAdaptive_CKadv" : "OFIAdaptive_CK"; }
    }
    int child_orders() const override { return n_child_; }
    double kappaT() const { return kappaT_; }

    void start(Simulator& sim, const ParentOrder& po) override {
        po_ = po; done_ = 0; open_limit_ = 0; n_child_ = 0; next_slice_ = 0; live_.clear();
        const MarketModel* m = prm_.model;
        double T = (po.end - po.start) / 1e9;
        // Almgren-Chriss kappa = sqrt(lambda sigma^2 / eta)
        if (prm_.ac_kappaT_override > 0) kappaT_ = prm_.ac_kappaT_override;
        else {
            double sigma = m ? m->sigma_1s : 1.0, eta = m && m->eta > 0 ? m->eta : 1e-4;
            double kappa = std::sqrt(prm_.ac_lambda * sigma * sigma / eta);
            kappaT_ = std::min(kappa * T, 8.0);
        }
        slice_len_ = (po.end - po.start) / std::max(po.slices, 1);
        if (curve_ == Curve::OfiAdaptive) fm_ = std::make_unique<FillModel>(prm_.fill_kind, m, prm_.qm);
        if (fm_ && prm_.fill_kind == FillModel::Kind::Logistic) fm_->set_logistic_weights(prm_.logistic_w);
        ck_.lambda_u_halfspreads = prm_.ck_lambda_u; ck_.adverse_ticks = prm_.ck_adverse_ticks;
        on_step(sim);
    }

    void on_step(Simulator& sim) override {
        for (auto& f : sim.take_visible_fills()) { done_ += f.qty; if (f.passive) open_limit_ -= f.qty; }
        Ts now = sim.now();
        if (sim.book_valid()) ofi_.update(now, sim.best_bid(), sim.best_bid_qty(), sim.best_ask(), sim.best_ask_qty());
        // reconcile open limit qty from the simulator's view of our orders (cancels, fills)
        while (next_slice_ < po_.slices && now >= po_.start + next_slice_ * slice_len_) {
            Ts t_end_slice = po_.start + (next_slice_ + 1) * slice_len_;
            cancel_live(sim);
            Qty target = curve_target(t_end_slice);
            Qty want = std::max<Qty>(0, target - done_ - inflight(sim));
            if (next_slice_ == po_.slices - 1) want = std::max<Qty>(0, po_.qty - done_ - inflight(sim));
            execute_slice(sim, want, t_end_slice - now);
            ++next_slice_;
        }
    }

    void finish(Simulator& sim) override {
        for (auto& f : sim.take_visible_fills()) done_ += f.qty;
        cancel_live(sim);
        Qty rem = po_.qty - done_ - inflight(sim);
        if (rem > 0) { sim.submit_market(po_.side, rem); ++n_child_; }
    }

private:
    Qty inflight(const Simulator& sim) const {
        Qty q = 0;
        for (uint64_t id : live_) { const Order* o = sim.order(id); if (o && (o->status == OrdStatus::PendingNew || o->status == OrdStatus::Live)) q += o->leaves; }
        return q;
    }
    void cancel_live(Simulator& sim) {
        for (uint64_t id : live_) { const Order* o = sim.order(id); if (o && o->status == OrdStatus::Live && o->resting) sim.cancel(id); }
        live_.clear();
    }
    // Cumulative target quantity at time t.
    Qty curve_target(Ts t) const {
        double T = (double)(po_.end - po_.start), x = std::min(std::max((double)(t - po_.start) / T, 0.0), 1.0);
        double frac;
        switch (curve_) {
            case Curve::Twap: frac = x; break;
            case Curve::Vwap: frac = vwap_fraction(t); break;
            default: {
                if (kappaT_ < 1e-6) { frac = x; break; }
                frac = 1.0 - std::sinh(kappaT_ * (1 - x)) / std::sinh(kappaT_);
            }
        }
        return (Qty)std::llround(frac * (double)po_.qty);
    }
    double vwap_fraction(Ts t) const {
        const MarketModel* m = prm_.model;
        if (!m || m->volume_profile.size() != 288) return (double)(t - po_.start) / (double)(po_.end - po_.start);
        auto cum = [&](Ts ts) {   // cumulative profile within the day, linear inside a bucket
            double sec = (double)((ts / NS_PER_S) % 86400) + (double)(ts % NS_PER_S) / 1e9;
            double c = 0; int b = (int)(sec / 300);
            for (int i = 0; i < b && i < 288; ++i) c += m->volume_profile[i];
            if (b < 288) c += m->volume_profile[b] * (sec - b * 300) / 300.0;
            return c;
        };
        double c0 = cum(po_.start), c1 = cum(po_.end), ct = cum(t);
        if (c1 < c0) { c1 += 1.0; if (ct < c0) ct += 1.0; }   // crosses midnight
        if (c1 - c0 <= 1e-12) return (double)(t - po_.start) / (double)(po_.end - po_.start);
        return std::min(std::max((ct - c0) / (c1 - c0), 0.0), 1.0);
    }
    void execute_slice(Simulator& sim, Qty want, Ts remaining) {
        if (want <= 0 || !sim.book_valid()) return;
        if (curve_ != Curve::OfiAdaptive || !prm_.passive) {
            if (curve_ == Curve::OfiAdaptive) want = tilt(sim, want);
            live_.push_back(sim.submit_market(po_.side, want)); ++n_child_;
            return;
        }
        want = tilt(sim, want);
        const MarketModel* m = prm_.model;
        FillFeatures f;
        Price touch = sim.best(po_.side);
        f.level = 1; f.queue_ahead = (double)sim.qty_at(po_.side, touch); f.spread = (double)sim.spread();
        Qty qs = sim.best_qty(po_.side), qo = sim.best_qty(other(po_.side));
        f.imbalance = (qs + qo) > 0 ? (double)(qs - qo) / (double)(qs + qo) : 0;
        f.sigma = m ? m->sigma_1s : 0; f.tau_s = remaining / 1e9; f.aes = m ? m->aes : 1;
        CKSplit s = ck_.split(*fm_, f, want, 0.5 * f.spread);
        if (s.market > 0) { live_.push_back(sim.submit_market(po_.side, s.market)); ++n_child_; }
        if (s.limit > 0) { live_.push_back(sim.submit_limit(po_.side, touch, s.limit)); ++n_child_; open_limit_ += s.limit; }
    }
    Qty tilt(Simulator& sim, Qty want) {
        const MarketModel* m = prm_.model;
        if (!m || m->ofi_beta == 0 || m->sigma_1s <= 0 || prm_.ofi_tilt == 0) return want;
        double ofi = ofi_.sum_last(sim.now(), (Ts)(prm_.ofi_window_s * 1e9));
        double forecast_ticks = m->ofi_beta * ofi;             // expected mid move
        double z = forecast_ticks / (m->sigma_1s * std::sqrt(std::max(prm_.ofi_window_s, 1e-3)));
        double adverse = sign(po_.side) * z;                   // buyer: price rising is adverse => accelerate
        double mult = std::min(std::max(1.0 + prm_.ofi_tilt * adverse, 0.5), 1.5);
        Qty w = (Qty)std::llround(want * mult);
        return std::min(w, po_.qty - done_ - inflight(sim));
    }

    Curve curve_;
    AlgoParams prm_;
    ParentOrder po_;
    Qty done_ = 0, open_limit_ = 0;
    int n_child_ = 0, next_slice_ = 0;
    Ts slice_len_ = 0;
    double kappaT_ = 0;
    std::vector<uint64_t> live_;
    OfiTracker ofi_;
    std::unique_ptr<FillModel> fm_;
    ContKukanov ck_;
};

// ---- episode runner ----------------------------------------------------------------------------
struct ExecResult {
    std::string algo, mode, queue_model;
    Qty target = 0, filled = 0;
    double arrival_mid = 0, avg_px = 0, end_mid = 0;
    double is_bp = 0;              // implementation shortfall vs arrival mid, basis points, cost positive
    double is_ticks = 0;
    double passive_frac = 0;
    int n_child = 0, n_fills = 0;
    double kappaT = 0;
    // Implementation-shortfall attribution (ticks, cost positive), sums to is_ticks by construction:
    //   spread      = sum_i q_i * s * (px_i - m1(t_i))            m1 = mid just before fill i in this run
    //   impact      = sum_i q_i * s * (m1(t_i) - m0(t_i))         m0 = counterfactual mid path (same seed, no orders)
    //   timing      = sum_i q_i * s * (m0(t_i) - m0(t_arrival))
    //   opportunity = unfilled * s * (end_price - m_arrival)
    // In replay m1 == m0, so impact is identically zero: the tape does not react to us.
    double spread_ticks = 0, timing_ticks = 0, impact_ticks = 0, opportunity_ticks = 0;
    double fee_bp = 0;             // maker_bp * passive_frac + taker_bp * (1 - passive_frac), reported separately from IS
    std::vector<Fill> fills;
};

// Piecewise-constant lookup on a (ts, mid) path.
inline double mid_at(const std::vector<std::pair<Ts, double>>& path, Ts t, double fallback) {
    if (path.empty()) return fallback;
    auto it = std::upper_bound(path.begin(), path.end(), std::make_pair(t, 1e300));
    if (it == path.begin()) return path.front().second;
    return (it - 1)->second;
}

// `cf_mid`: counterfactual mid path of the same simulator run without our orders (nullptr => use this run's own path, i.e. impact = 0).
inline ExecResult run_episode(Simulator& sim, Algo& algo, const ParentOrder& po, const std::vector<std::pair<Ts, double>>* cf_mid = nullptr, double maker_bp = 0, double taker_bp = 0) {
    ExecResult r; r.algo = algo.name(); r.mode = sim.mode(); r.queue_model = queue_model_name(sim.queue_model().kind); r.target = po.qty;
    sim.run_until(po.start);
    r.arrival_mid = sim.mid();
    std::vector<std::pair<Ts, double>> m1; m1.emplace_back(sim.now(), sim.mid());
    algo.start(sim, po);
    while (sim.now() < po.end) { if (!sim.step()) break; if (sim.book_valid()) m1.emplace_back(sim.now(), sim.mid()); algo.on_step(sim); }
    algo.finish(sim);
    // let the final sweep and its report come through
    Ts deadline = sim.now() + 2 * (sim.latency().order_in + sim.latency().report_out) + 2 * NS_PER_S;
    while (sim.now() < deadline) { if (!sim.step()) break; if (sim.book_valid()) m1.emplace_back(sim.now(), sim.mid()); if (sim.executed() >= po.qty) break; }
    sim.run_until(sim.now() + sim.latency().order_in + sim.latency().report_out + 1);
    r.end_mid = sim.book_valid() ? sim.mid() : r.arrival_mid;
    const auto& m0 = cf_mid ? *cf_mid : m1;
    double m0_arrival = mid_at(m0, po.start, r.arrival_mid);
    for (auto& f : sim.all_fills()) {
        double mm1 = f.mid_at_fill, mm0 = mid_at(m0, f.ts, mm1);
        r.spread_ticks += f.qty * sign(po.side) * ((double)f.price - mm1);
        r.impact_ticks += f.qty * sign(po.side) * (mm1 - mm0);
        r.timing_ticks += f.qty * sign(po.side) * (mm0 - m0_arrival);
    }
    double notional = 0; Qty pq = 0;
    for (auto& f : sim.all_fills()) { r.filled += f.qty; notional += (double)f.price * f.qty; if (f.passive) pq += f.qty; r.fills.push_back(f); }
    r.n_fills = (int)sim.all_fills().size();
    r.n_child = algo.child_orders();
    r.passive_frac = r.filled > 0 ? (double)pq / r.filled : 0;
    r.fee_bp = maker_bp * r.passive_frac + taker_bp * (1 - r.passive_frac);
    // unfilled remainder (should be ~0): charge it at the far touch at the end
    Qty rem = po.qty - r.filled;
    if (rem > 0 && sim.book_valid()) { double far_px = (double)(po.side == Side::Bid ? sim.best_ask() : sim.best_bid()); notional += far_px * rem; r.opportunity_ticks += rem * sign(po.side) * (far_px - r.arrival_mid); }
    r.avg_px = po.qty > 0 ? notional / po.qty : r.arrival_mid;
    r.is_ticks = sign(po.side) * (r.avg_px - r.arrival_mid);
    // per-lot units, plus the arrival-mid difference between the run and the counterfactual (m0 - m_arrival), which is zero by construction of the seed
    double q = (double)std::max<Qty>(po.qty, 1);
    r.spread_ticks /= q; r.impact_ticks /= q; r.timing_ticks /= q; r.opportunity_ticks /= q;
    r.timing_ticks += (m0_arrival - r.arrival_mid) * sign(po.side) * (double)r.filled / q;
    r.is_bp = r.arrival_mid > 0 ? r.is_ticks / r.arrival_mid * 1e4 : 0;
    if (auto* ss = dynamic_cast<SliceScheduler*>(&algo)) r.kappaT = ss->kappaT();
    return r;
}

} // namespace lobsim
