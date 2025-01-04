#pragma once
// Empirical facts measured on our own data (stage 4.1 and stage 5 of the plan):
//   * fill probability of a passive order vs distance from the touch and queue position, by replay
//     under the three queue assumptions (probe orders are independent in replay, so thousands are
//     placed in one pass);
//   * Cont-Kukanov-Stoikov (2014) OFI regression, R^2 by hour of day and by volatility regime;
//   * Almgren-Chriss temporary impact coefficient eta from the same 1 s buckets;
//   * mark-outs of passive fills at 100 ms / 1 s / 10 s / 60 s in each simulator, and the
//     correlation between "got filled" and the post-placement return (adverse selection).
#include "tape.hpp"
#include "book.hpp"
#include "replay_sim.hpp"
#include "gen_sim.hpp"
#include "stats.hpp"
#include "json.hpp"
#include "fill_model.hpp"
#include <map>
#include <functional>

namespace lobsim {

// ---- 1. empirical fill curve -----------------------------------------------------------------------
struct FillSample {
    int distance;         // ticks behind the touch (0 = at the touch)
    double tau_s;
    double queue_ahead;   // lots at placement
    double our_size;
    double imbalance, sigma, spread;
    bool filled;
    Side side;
    Ts t_place;
    double mid_place;
};

struct FillCurveResult {
    std::string queue_model;
    std::vector<FillSample> samples;
    Json table;               // P(fill) by distance x horizon, and by queue-position tercile at the touch
    std::vector<double> logistic_w;
};

inline FillCurveResult fill_curve(const Tape& tape, QueueModel qm, const MarketModel& m, int n_probes, std::vector<int> distances,
                                  std::vector<double> horizons_s, Qty probe_size, uint64_t seed) {
    FillCurveResult res; res.queue_model = queue_model_name(qm.kind);
    if (tape.events.size() < 1000) return res;
    Rng rng(seed);
    // choose placement times uniformly in [t0 + 60s, t1 - max horizon - 5s]
    double maxh = 0; for (double h : horizons_s) maxh = std::max(maxh, h);
    Ts lo = tape.t0() + 60 * NS_PER_S, hi = tape.t1() - (Ts)((maxh + 5) * 1e9);
    if (hi <= lo) return res;
    struct Probe { Ts t; int d; double h; Side side; uint64_t id = 0; bool placed = false; FillSample s; };
    std::vector<Probe> probes;
    for (int i = 0; i < n_probes; ++i) {
        Probe p; p.t = lo + (Ts)(rng.uniform() * (double)(hi - lo)); p.d = distances[rng.index(distances.size())];
        p.h = horizons_s[rng.index(horizons_s.size())]; p.side = rng.uniform() < 0.5 ? Side::Bid : Side::Ask; probes.push_back(p);
    }
    std::sort(probes.begin(), probes.end(), [](const Probe& a, const Probe& b) { return a.t < b.t; });
    ReplaySim sim(&tape, 0); sim.set_queue_model(qm);
    std::map<uint64_t, size_t> by_id;
    size_t next = 0;
    std::vector<size_t> open;
    while (sim.step()) {
        Ts now = sim.now();
        while (next < probes.size() && probes[next].t <= now) {
            Probe& p = probes[next++];
            if (!sim.book_valid()) continue;
            Price touch = sim.best(p.side);
            Price px = p.side == Side::Bid ? touch - p.d : touch + p.d;
            p.id = sim.submit_limit(p.side, px, probe_size); p.placed = true;
            p.s.distance = p.d; p.s.tau_s = p.h; p.s.queue_ahead = (double)sim.qty_at(p.side, px); p.s.our_size = (double)probe_size;
            Qty qs = sim.best_qty(p.side), qo = sim.best_qty(other(p.side));
            p.s.imbalance = (qs + qo) > 0 ? (double)(qs - qo) / (double)(qs + qo) : 0; p.s.sigma = m.sigma_1s; p.s.spread = (double)sim.spread();
            p.s.side = p.side; p.s.t_place = now; p.s.mid_place = sim.mid(); p.s.filled = false;
            by_id[p.id] = next - 1; open.push_back(next - 1);
        }
        // expire probes
        for (size_t k = 0; k < open.size();) {
            Probe& p = probes[open[k]];
            const Order* o = sim.order(p.id);
            bool done = !o || o->status == OrdStatus::Filled || o->status == OrdStatus::Rejected;
            if (!done && now >= p.t + (Ts)(p.h * 1e9)) { if (o->status == OrdStatus::Live) sim.cancel(p.id); done = true; }
            if (done) { open[k] = open.back(); open.pop_back(); } else ++k;
        }
        if (next >= probes.size() && open.empty()) break;
    }
    for (auto& p : probes) {
        if (!p.placed) continue;
        const Order* o = sim.order(p.id);
        p.s.filled = o && o->status == OrdStatus::Filled;
        res.samples.push_back(p.s);
    }
    // tables
    Json tbl = Json::object();
    Json byd = Json::array();
    for (int d : distances) for (double h : horizons_s) {
        size_t n = 0, f = 0; for (auto& s : res.samples) if (s.distance == d && s.tau_s == h) { ++n; f += s.filled; }
        Json e = Json::object(); e["distance_ticks"] = d; e["horizon_s"] = h; e["n"] = (long long)n; e["p_fill"] = n ? (double)f / n : 0; byd.push(e);
    }
    tbl["by_distance_horizon"] = byd;
    // queue-position terciles at the touch
    std::vector<double> qa; for (auto& s : res.samples) if (s.distance == 0) qa.push_back(s.queue_ahead / std::max(m.aes, 1.0));
    if (!qa.empty()) {
        double q1 = quantile(qa, 1.0 / 3), q2 = quantile(qa, 2.0 / 3);
        Json terc = Json::array();
        for (int t = 0; t < 3; ++t) {
            size_t n = 0, f = 0; double qs = 0;
            for (auto& s : res.samples) { if (s.distance != 0) continue; double q = s.queue_ahead / std::max(m.aes, 1.0); int tt = q <= q1 ? 0 : q <= q2 ? 1 : 2; if (tt != t) continue; ++n; f += s.filled; qs += q; }
            Json e = Json::object(); e["tercile"] = t; e["mean_queue_ahead_aes"] = n ? qs / n : 0; e["n"] = (long long)n; e["p_fill"] = n ? (double)f / n : 0; terc.push(e);
        }
        tbl["touch_by_queue_tercile"] = terc;
    }
    res.table = tbl;
    // logistic surrogate (Lokin-Yu-style state-dependent fill probability)
    std::vector<std::vector<double>> X; std::vector<double> y;
    for (auto& s : res.samples) {
        FillFeatures f; f.level = 1 + s.distance; f.queue_ahead = s.queue_ahead; f.our_size = s.our_size; f.imbalance = s.imbalance; f.sigma = s.sigma; f.tau_s = s.tau_s; f.spread = s.spread; f.aes = m.aes;
        X.push_back(f.x()); y.push_back(s.filled ? 1.0 : 0.0);
    }
    if (X.size() > 50) res.logistic_w = logistic_fit(X, y, 1e-2, 25);
    return res;
}

// ---- 2. OFI regression ------------------------------------------------------------------------------
struct OfiResult {
    double beta = 0, alpha = 0, r2 = 0; size_t n = 0;
    Json by_hour, by_vol_regime;
    double eta = 0, eta_r2 = 0;   // mid change vs signed market-order flow (ticks per lot per second)
};

inline OfiResult ofi_regression(const Tape& tape, double bucket_s = 1.0) {
    OfiResult r;
    Book book; Ts bucket_ns = (Ts)(bucket_s * 1e9);
    Ts cur = 0; double ofi = 0, flow = 0, mid0 = 0; bool have = false;
    Price pb = 0, pa = 0; Qty qb = 0, qa = 0; bool init = false;
    struct Row { double ofi, dmid, flow; int hour; };
    std::vector<Row> rows;
    for_each_batch(tape.events, [&](size_t i0, size_t i1) {
        for (size_t k = i0; k < i1; ++k) {
            const L2Event& e = tape.events[k];
            if (e.kind == EvKind::Snapshot) { book.clear(); init = false; have = false; continue; }
            if (e.kind == EvKind::Trade) flow += (e.side == Side::Ask ? 1.0 : -1.0) * (double)e.qty;
            else book.set(e.side, e.price, e.qty);
        }
        if (!book.valid()) return;
        Ts ts = tape.events[i0].ts_ex;
        Ts b = ts / bucket_ns;
        if (!have) { cur = b; mid0 = book.mid(); have = true; ofi = 0; flow = 0; }
        if (b != cur) {
            rows.push_back({ofi, book.mid() - mid0, flow, (int)((cur * bucket_ns / NS_PER_S) % 86400 / 3600)});
            cur = b; mid0 = book.mid(); ofi = 0; flow = 0;
        }
        Price nb = book.best_bid(), na = book.best_ask(); Qty nqb = book.best_bid_qty(), nqa = book.best_ask_qty();
        if (init) {
            double ev = 0;
            if (nb >= pb) ev += (double)nqb; if (nb <= pb) ev -= (double)qb;
            if (na <= pa) ev -= (double)nqa; if (na >= pa) ev += (double)qa;
            ofi += ev;
        }
        pb = nb; pa = na; qb = nqb; qa = nqa; init = true;
    });
    std::vector<double> x, y, fl; for (auto& w : rows) { x.push_back(w.ofi); y.push_back(w.dmid); fl.push_back(w.flow); }
    Ols o = ols(x, y); r.beta = o.b; r.alpha = o.a; r.r2 = o.r2; r.n = o.n;
    Ols o2 = ols(fl, y); r.eta = o2.b / bucket_s; r.eta_r2 = o2.r2;
    Json bh = Json::array();
    for (int h = 0; h < 24; ++h) {
        std::vector<double> xx, yy; for (auto& w : rows) if (w.hour == h) { xx.push_back(w.ofi); yy.push_back(w.dmid); }
        if (xx.size() < 30) continue;
        Ols oh = ols(xx, yy); Json e = Json::object(); e["hour_utc"] = h; e["n"] = (long long)oh.n; e["beta"] = oh.b; e["r2"] = oh.r2; bh.push(e);
    }
    r.by_hour = bh;
    // volatility regime: rolling 60-bucket realised vol terciles
    std::vector<double> vol(rows.size(), 0.0);
    for (size_t i = 0; i < rows.size(); ++i) { size_t j0 = i >= 60 ? i - 60 : 0; double s = 0; for (size_t j = j0; j < i; ++j) s += rows[j].dmid * rows[j].dmid; vol[i] = i > j0 ? std::sqrt(s / (i - j0)) : 0; }
    std::vector<double> vv(vol.begin() + std::min<size_t>(60, vol.size()), vol.end());
    Json bv = Json::array();
    if (vv.size() > 100) {
        double q1 = quantile(vv, 1.0 / 3), q2 = quantile(vv, 2.0 / 3);
        for (int t = 0; t < 3; ++t) {
            std::vector<double> xx, yy; double vs = 0;
            for (size_t i = 60; i < rows.size(); ++i) { int tt = vol[i] <= q1 ? 0 : vol[i] <= q2 ? 1 : 2; if (tt != t) continue; xx.push_back(rows[i].ofi); yy.push_back(rows[i].dmid); vs += vol[i]; }
            if (xx.size() < 30) continue;
            Ols ot = ols(xx, yy); Json e = Json::object(); e["vol_tercile"] = t; e["mean_vol_ticks"] = vs / xx.size(); e["n"] = (long long)ot.n; e["beta"] = ot.b; e["r2"] = ot.r2; bv.push(e);
        }
    }
    r.by_vol_regime = bv;
    return r;
}

// ---- 3. mark-outs & adverse selection probes ------------------------------------------------------
struct MarkoutResult {
    std::string mode, queue_model;
    size_t n_probes = 0, n_filled = 0;
    std::vector<double> horizons_s;
    std::vector<double> markout_ticks;     // mean signed (mid_{t+h} - fill_price) * sign, filled probes only (positive = fill was good for us)
    std::vector<double> markout_se;
    double corr_fill_return = 0;           // corr(filled, signed post-placement return over tau) across all probes
    double ret_filled = 0, ret_unfilled = 0;
    // Queue-position value (Moallemi & Yuan 2016, measured): by tercile of queue ahead at placement,
    //   value_ticks = P(fill) * (half_spread + markout_60s)   with markout signed so that + = favourable.
    struct Tercile { double mean_queue_ahead = 0; size_t n = 0, filled = 0; double markout_60 = 0, half_spread = 0, value_ticks = 0; };
    Tercile terciles[3];
};

// A probe: post `size` at the touch on a random side at time t, hold tau; record mark-outs.
// `make_sim` must return a fresh simulator positioned at (or before) the requested start time.
inline MarkoutResult probe_markouts(const std::function<std::unique_ptr<Simulator>(Ts, uint64_t)>& make_sim, Ts t_lo, Ts t_hi,
                                    int n_probes, double tau_s, Qty size, std::vector<double> horizons_s, QueueModel qm, uint64_t seed,
                                    bool independent_probes) {
    MarkoutResult r; r.horizons_s = horizons_s; r.queue_model = queue_model_name(qm.kind);
    Rng rng(seed);
    double maxh = 0; for (double h : horizons_s) maxh = std::max(maxh, h);
    std::vector<Ts> times; for (int i = 0; i < n_probes; ++i) times.push_back(t_lo + (Ts)(rng.uniform() * (double)(t_hi - t_lo)));
    std::sort(times.begin(), times.end());
    struct P { Ts t; Side side; uint64_t id; Price px; double mid0; bool filled = false; Ts t_fill = 0; Price fill_px = 0; std::vector<double> mo; double ret_tau = 0; bool done_ret = false; double q_ahead = 0, half_spread = 0, mo60 = std::numeric_limits<double>::quiet_NaN(); };
    std::vector<P> all_probes;
    std::vector<P> probes;
    std::vector<double> fills_ind, rets;
    std::vector<std::vector<double>> mo(horizons_s.size());
    auto run_group = [&](std::unique_ptr<Simulator> sim, std::vector<Ts> ts) {
        r.mode = sim->mode();
        sim->set_queue_model(qm);
        std::vector<P> ps; size_t next = 0;
        std::vector<std::pair<Ts, double>> midpath;
        Ts last_needed = ts.back() + (Ts)((tau_s + maxh + 1) * 1e9);
        while (sim->now() < last_needed && sim->step()) {
            Ts now = sim->now();
            if (sim->book_valid()) midpath.emplace_back(now, sim->mid());
            while (next < ts.size() && ts[next] <= now) {
                Ts t = ts[next++];
                if (!sim->book_valid()) continue;
                P p; p.t = t; p.side = rng.uniform() < 0.5 ? Side::Bid : Side::Ask; p.px = sim->best(p.side); p.mid0 = sim->mid();
                p.q_ahead = (double)sim->qty_at(p.side, p.px); p.half_spread = 0.5 * (double)sim->spread();
                p.id = sim->submit_limit(p.side, p.px, size); ps.push_back(p);
            }
            for (auto& p : ps) {
                const Order* o = sim->order(p.id);
                if (!o) continue;
                if (!p.filled && o->status == OrdStatus::Filled) { p.filled = true; p.t_fill = now; p.fill_px = p.px; }
                if (!p.filled && o->status == OrdStatus::Live && now >= p.t + (Ts)(tau_s * 1e9)) sim->cancel(p.id);
            }
        }
        auto mid_at = [&](Ts t) -> double {
            auto it = std::upper_bound(midpath.begin(), midpath.end(), std::make_pair(t, 1e300));
            if (it == midpath.begin()) return std::numeric_limits<double>::quiet_NaN();
            return (it - 1)->second;
        };
        for (auto& p : ps) {
            ++r.n_probes;
            double m_tau = mid_at(p.t + (Ts)(tau_s * 1e9));
            if (std::isfinite(m_tau)) { double ret = sign(p.side) * (m_tau - p.mid0); fills_ind.push_back(p.filled ? 1.0 : 0.0); rets.push_back(ret); }
            if (p.filled) {
                ++r.n_filled;
                for (size_t h = 0; h < horizons_s.size(); ++h) {
                    double mh = mid_at(p.t_fill + (Ts)(horizons_s[h] * 1e9));
                    if (std::isfinite(mh)) { mo[h].push_back(sign(p.side) * (mh - (double)p.fill_px)); if (h + 1 == horizons_s.size()) p.mo60 = sign(p.side) * (mh - (double)p.fill_px); }
                }
            }
            all_probes.push_back(p);
        }
    };
    if (independent_probes) run_group(make_sim(t_lo, seed), times);
    else for (size_t i = 0; i < times.size(); ++i) run_group(make_sim(times[i] - 5 * NS_PER_S, seed + 1000 + i), {times[i]});
    for (size_t h = 0; h < horizons_s.size(); ++h) { r.markout_ticks.push_back(mean(mo[h])); r.markout_se.push_back(mo[h].size() > 1 ? stdev(mo[h]) / std::sqrt((double)mo[h].size()) : 0); }
    // queue-position value by tercile of queue ahead
    { std::vector<double> qa; for (auto& p : all_probes) qa.push_back(p.q_ahead);
      double q1 = quantile(qa, 1.0 / 3), q2 = quantile(qa, 2.0 / 3);
      for (auto& p : all_probes) {
          int t = p.q_ahead <= q1 ? 0 : p.q_ahead <= q2 ? 1 : 2; auto& T = r.terciles[t];
          ++T.n; T.mean_queue_ahead += p.q_ahead; T.half_spread += p.half_spread;
          if (p.filled) { ++T.filled; if (std::isfinite(p.mo60)) T.markout_60 += p.mo60; }
      }
      for (auto& T : r.terciles) if (T.n) { T.mean_queue_ahead /= T.n; T.half_spread /= T.n; double mo60 = T.filled ? T.markout_60 / T.filled : 0; T.markout_60 = mo60; T.value_ticks = (double)T.filled / T.n * (T.half_spread + mo60); } }
    // correlation between fill indicator and post-placement return
    if (rets.size() > 2) {
        double mf = mean(fills_ind), mr = mean(rets), sxy = 0, sxx = 0, syy = 0;
        for (size_t i = 0; i < rets.size(); ++i) { sxy += (fills_ind[i] - mf) * (rets[i] - mr); sxx += (fills_ind[i] - mf) * (fills_ind[i] - mf); syy += (rets[i] - mr) * (rets[i] - mr); }
        r.corr_fill_return = (sxx > 0 && syy > 0) ? sxy / std::sqrt(sxx * syy) : 0;
        double sf = 0, su = 0; size_t nf = 0, nu = 0;
        for (size_t i = 0; i < rets.size(); ++i) { if (fills_ind[i] > 0.5) { sf += rets[i]; ++nf; } else { su += rets[i]; ++nu; } }
        r.ret_filled = nf ? sf / nf : 0; r.ret_unfilled = nu ? su / nu : 0;
    }
    return r;
}

inline Json markout_json(const MarkoutResult& r) {
    Json j = Json::object(); j["mode"] = r.mode; j["queue_model"] = r.queue_model; j["n_probes"] = (long long)r.n_probes; j["n_filled"] = (long long)r.n_filled;
    j["p_fill"] = r.n_probes ? (double)r.n_filled / r.n_probes : 0;
    Json a = Json::array(); for (size_t h = 0; h < r.horizons_s.size(); ++h) { Json e = Json::object(); e["horizon_s"] = r.horizons_s[h]; e["markout_ticks"] = r.markout_ticks[h]; e["se"] = r.markout_se[h]; a.push(e); }
    j["markouts"] = a; j["corr_fill_vs_return"] = r.corr_fill_return; j["mean_return_filled_ticks"] = r.ret_filled; j["mean_return_unfilled_ticks"] = r.ret_unfilled;
    Json tj = Json::array();
    for (int t = 0; t < 3; ++t) { auto& T = r.terciles[t]; Json e = Json::object(); e["tercile"] = t; e["n"] = (long long)T.n; e["mean_queue_ahead_lots"] = T.mean_queue_ahead; e["p_fill"] = T.n ? (double)T.filled / T.n : 0; e["markout_60s_ticks"] = T.markout_60; e["half_spread_ticks"] = T.half_spread; e["queue_position_value_ticks"] = T.value_ticks; tj.push(e); }
    j["queue_position_value"] = tj;
    return j;
}

} // namespace lobsim
