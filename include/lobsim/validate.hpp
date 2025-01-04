#pragma once
// Simulator realism checks, computed identically on the live tape and on each simulator's tape.
//
//  * Vyetrenko et al. (ICAIF 2020, arXiv 1912.04941) stylised facts: spread, volume at best,
//    depth, inter-arrival times, order-flow (trade sign) autocorrelation, volatility clustering,
//    heavy-tailed returns.  Each is a *distribution* here and we report KS and W1 distances
//    between live and simulated, plus the scalar summaries.
//  * Impact response function (Bouchaud et al., Trades, Quotes and Prices, ch. 11-13):
//    R(l) = E[ eps_n * (m_{n+l} - m_n) ], eps = +1 for a buyer-initiated trade, in trade time,
//    plus the clock-time version at the mark-out horizons.
#include "tape.hpp"
#include "book.hpp"
#include "stats.hpp"
#include "json.hpp"

namespace lobsim {

struct StylisedFacts {
    std::vector<double> spread, vol_at_best, depth5, imbalance, trade_iat_ms, update_iat_ms, ret_1s, trade_size, trade_sign;
    double ret_kurtosis = 0, acf_ret1 = 0, acf_abs1 = 0, acf_abs10 = 0, acf_sign1 = 0, acf_sign10 = 0, tail4 = 0;
    double seconds = 0; size_t n_trades = 0, n_updates = 0;
};

struct ResponseFunction {
    std::vector<int> lags;         // in trades
    std::vector<double> R;         // ticks
    std::vector<double> clock_lags_s;
    std::vector<double> R_clock;   // ticks
    size_t n = 0;
};

inline StylisedFacts compute_facts(const Tape& tape) {
    StylisedFacts f;
    Book book; Ts next = 0; Ts last_trade = 0, last_update = 0; Side last_trade_side = Side::Bid;
    std::vector<double> mids;
    for_each_batch(tape.events, [&](size_t i0, size_t i1) {
        for (size_t k = i0; k < i1; ++k) {
            const L2Event& e = tape.events[k];
            if (e.kind == EvKind::Snapshot) { book.clear(); continue; }
            if (e.kind == EvKind::Level) {
                book.set(e.side, e.price, e.qty);
                if (last_update && e.ts_ex > last_update) f.update_iat_ms.push_back((e.ts_ex - last_update) / 1e6);
                if (e.ts_ex != last_update) { last_update = e.ts_ex; ++f.n_updates; }
            } else {
                // prints with the same timestamp and passive side are one market order
                if (e.ts_ex == last_trade && !f.trade_size.empty() && last_trade_side == e.side) { f.trade_size.back() += (double)e.qty; continue; }
                if (last_trade) f.trade_iat_ms.push_back((e.ts_ex - last_trade) / 1e6);
                last_trade = e.ts_ex; last_trade_side = e.side; ++f.n_trades;
                f.trade_size.push_back((double)e.qty);
                f.trade_sign.push_back(e.side == Side::Ask ? 1.0 : -1.0);
            }
        }
        if (!book.valid()) return;
        Ts ts = tape.events[i0].ts_ex;
        if (next == 0) next = ts;
        while (ts >= next) {
            f.spread.push_back((double)book.spread());
            f.vol_at_best.push_back(0.5 * (double)(book.best_bid_qty() + book.best_ask_qty()));
            f.depth5.push_back(0.5 * (double)(book.depth(Side::Bid, 5) + book.depth(Side::Ask, 5)));
            { double qb = (double)book.best_bid_qty(), qa = (double)book.best_ask_qty(); f.imbalance.push_back(qb + qa > 0 ? (qb - qa) / (qb + qa) : 0); }
            mids.push_back(book.mid());
            next += NS_PER_S;
        }
    });
    for (size_t i = 1; i < mids.size(); ++i) f.ret_1s.push_back(mids[i] - mids[i - 1]);
    f.seconds = tape.seconds();
    f.ret_kurtosis = kurtosis(f.ret_1s);
    f.acf_ret1 = autocorr(f.ret_1s, 1);
    std::vector<double> ab; for (double r : f.ret_1s) ab.push_back(std::fabs(r));
    f.acf_abs1 = autocorr(ab, 1); f.acf_abs10 = autocorr(ab, 10);
    f.acf_sign1 = autocorr(f.trade_sign, 1); f.acf_sign10 = autocorr(f.trade_sign, 10);
    double sd = stdev(f.ret_1s); size_t c = 0; for (double r : f.ret_1s) if (sd > 0 && std::fabs(r) > 4 * sd) ++c;
    f.tail4 = f.ret_1s.empty() ? 0 : (double)c / f.ret_1s.size();
    return f;
}

inline ResponseFunction response_function(const Tape& tape, std::vector<int> lags = {1, 2, 5, 10, 20, 50, 100},
                                          std::vector<double> clock_lags = {0.1, 1, 10, 60}) {
    ResponseFunction rf; rf.lags = lags; rf.clock_lags_s = clock_lags;
    Book book;
    struct Tr { Ts ts; double eps; double mid; };
    std::vector<Tr> tr;
    std::vector<std::pair<Ts, double>> midpath;   // mid after each update
    Ts last_trade_ts = -1; Side last_side = Side::Bid;
    for_each_batch(tape.events, [&](size_t i0, size_t i1) {
        for (size_t k = i0; k < i1; ++k) {
            const L2Event& e = tape.events[k];
            if (e.kind == EvKind::Snapshot) { book.clear(); continue; }
            if (e.kind == EvKind::Level) { book.set(e.side, e.price, e.qty); continue; }
            if (!book.valid()) continue;
            // aggregate fills of one market order (same timestamp & side) into one trade
            if (e.ts_ex == last_trade_ts && e.side == last_side) continue;
            last_trade_ts = e.ts_ex; last_side = e.side;
            tr.push_back({e.ts_ex, e.side == Side::Ask ? 1.0 : -1.0, book.mid()});
        }
        if (book.valid()) midpath.emplace_back(tape.events[i0].ts_ex, book.mid());
    });
    rf.n = tr.size();
    for (int l : lags) {
        double s = 0; size_t n = 0;
        for (size_t i = 0; i + l < tr.size(); ++i) { s += tr[i].eps * (tr[i + l].mid - tr[i].mid); ++n; }
        rf.R.push_back(n ? s / n : 0);
    }
    // clock-time: mid at t + h via the mid path (binary search)
    for (double h : clock_lags) {
        double s = 0; size_t n = 0; Ts dh = (Ts)(h * 1e9);
        size_t j = 0;
        for (auto& t : tr) {
            Ts target = t.ts + dh;
            while (j < midpath.size() && midpath[j].first <= target) ++j;
            if (j == 0 || j >= midpath.size()) continue;
            s += t.eps * (midpath[j - 1].second - t.mid); ++n;
        }
        rf.R_clock.push_back(n ? s / n : 0);
    }
    return rf;
}

inline Json facts_summary(const StylisedFacts& f) {
    Json j = Json::object();
    j["seconds"] = f.seconds; j["n_trades"] = (long long)f.n_trades; j["n_updates"] = (long long)f.n_updates;
    j["spread_mean"] = mean(f.spread); j["spread_p50"] = median(f.spread);
    j["vol_at_best_mean"] = mean(f.vol_at_best); j["depth5_mean"] = mean(f.depth5);
    j["trade_iat_ms_p50"] = median(f.trade_iat_ms); j["update_iat_ms_p50"] = median(f.update_iat_ms);
    j["ret_1s_std"] = stdev(f.ret_1s); j["ret_kurtosis"] = f.ret_kurtosis; j["acf_ret1"] = f.acf_ret1;
    j["acf_absret1"] = f.acf_abs1; j["acf_absret10"] = f.acf_abs10; j["acf_sign1"] = f.acf_sign1; j["acf_sign10"] = f.acf_sign10;
    j["tail_gt4sigma_frac"] = f.tail4; j["trade_size_mean"] = mean(f.trade_size);
    return j;
}

// Distances per statistic between two tapes' facts.
inline Json compare_facts(const StylisedFacts& live, const StylisedFacts& sim) {
    Json j = Json::object();
    auto add = [&](const char* name, const std::vector<double>& a, const std::vector<double>& b) {
        Json d = Json::object();
        d["ks"] = ks_distance(a, b); d["w1"] = wasserstein1(a, b); d["live_mean"] = mean(a); d["sim_mean"] = mean(b);
        d["live_p50"] = median(a); d["sim_p50"] = median(b); d["n_live"] = (long long)a.size(); d["n_sim"] = (long long)b.size();
        j[name] = d;
    };
    add("spread", live.spread, sim.spread);
    add("vol_at_best", live.vol_at_best, sim.vol_at_best);
    add("depth5", live.depth5, sim.depth5);
    add("imbalance", live.imbalance, sim.imbalance);
    add("trade_iat_ms", live.trade_iat_ms, sim.trade_iat_ms);
    add("ret_1s", live.ret_1s, sim.ret_1s);
    add("trade_size", live.trade_size, sim.trade_size);
    Json sc = Json::object();
    auto scal = [&](const char* n, double a, double b) { Json x = Json::object(); x["live"] = a; x["sim"] = b; sc[n] = x; };
    scal("ret_kurtosis", live.ret_kurtosis, sim.ret_kurtosis); scal("acf_ret1", live.acf_ret1, sim.acf_ret1);
    scal("acf_absret1", live.acf_abs1, sim.acf_abs1); scal("acf_absret10", live.acf_abs10, sim.acf_abs10);
    scal("acf_sign1", live.acf_sign1, sim.acf_sign1); scal("acf_sign10", live.acf_sign10, sim.acf_sign10);
    scal("tail_gt4sigma_frac", live.tail4, sim.tail4);
    j["scalars"] = sc;
    return j;
}

inline Json rf_json(const ResponseFunction& r) {
    Json j = Json::object(); j["n_trades"] = (long long)r.n;
    Json a = Json::array(); for (size_t i = 0; i < r.lags.size(); ++i) { Json x = Json::object(); x["lag"] = r.lags[i]; x["R_ticks"] = r.R[i]; a.push(x); }
    j["event_time"] = a;
    Json b = Json::array(); for (size_t i = 0; i < r.clock_lags_s.size(); ++i) { Json x = Json::object(); x["lag_s"] = r.clock_lags_s[i]; x["R_ticks"] = r.R_clock[i]; b.push(x); }
    j["clock_time"] = b;
    return j;
}

} // namespace lobsim
