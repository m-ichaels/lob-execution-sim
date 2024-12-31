#pragma once
// Calibrated market model shared by the queue-reactive and Hawkes simulators.
//
//  * reference price p_ref and level indexing follow Huang, Lehalle & Rosenbaum (JASA 2015):
//    p_ref is the mid when the spread is an odd number of ticks; when it is even, p_ref is the
//    half-tick on either side of the mid that is closest to the previous p_ref.  Level i on the
//    bid (ask) side sits at p_ref - (i - 1/2) (p_ref + (i - 1/2)) ticks, i = 1..K.
//  * event decomposition from L2 diffs: an increase at a level is a limit arrival; a decrease is a
//    market order up to the volume traded at that price since the last update, and a cancellation
//    for the remainder.  Market orders themselves are taken from the trade stream (exact times).
//  * queue-reactive intensities lambda^{L,C,M}(i, bucket) with bucket = floor(q / AES) capped;
//    order sizes are non-unit and drawn from empirical size distributions conditional on
//    (event type, level, bucket) - the Bodor & Carlier (arXiv 2405.18594) extension.
//  * Hawkes: 6 event types (limit/cancel/market x bid/ask), kernels = sums of exponentials on a
//    log-spaced decay grid (a standard approximation of the power-law kernels of Bacry,
//    Mastromatteo & Muzy 2015), fitted by EM.
#include "types.hpp"
#include "tape.hpp"
#include "book.hpp"
#include "json.hpp"
#include "stats.hpp"
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace lobsim {

// ---- reference price ---------------------------------------------------------------------
struct RefPrice {
    long long pref2 = 0;   // 2 * p_ref, always odd once initialised
    bool init = false;
    void update(Price bb, Price ba) {
        long long mid2 = bb + ba;
        if ((ba - bb) % 2 != 0) { pref2 = mid2; init = true; return; }
        if (!init) { pref2 = mid2 - 1; init = true; return; }
        long long lo = mid2 - 1, hi = mid2 + 1;
        pref2 = (std::llabs(lo - pref2) <= std::llabs(hi - pref2)) ? lo : hi;
    }
    // level index (>=1) of a price on a side; <=0 means at/inside the reference price
    int level(Side s, Price p) const { return s == Side::Bid ? (int)((pref2 - 2 * p + 1) / 2) : (int)((2 * p - pref2 + 1) / 2); }
    Price price(Side s, int i) const { return s == Side::Bid ? (Price)((pref2 - 2 * i + 1) / 2) : (Price)((pref2 + 2 * i - 1) / 2); }
    double value() const { return pref2 / 2.0; }
};

// ---- empirical size distribution on log-spaced bins ----------------------------------------
struct SizeDist {
    std::vector<double> edges;   // bin edges in lots, size n+1
    std::vector<double> w;       // counts per bin
    double total = 0;
    // Absolute log-spaced bins: 1 lot .. 2^25 lots, half-octave resolution (independent of AES so
    // that the thin deep levels of a small-tick book are represented as well as the wall at the touch).
    void init(double /*aes*/) {
        edges.clear(); w.clear(); total = 0;
        for (int k = 0; k <= 50; ++k) edges.push_back(std::pow(2.0, k / 2.0));
        edges.push_back(edges.back() * 1e6);
        w.assign(edges.size() - 1, 0.0);
    }
    void add(Qty s) {
        if (edges.empty() || s <= 0) return;
        size_t b = (size_t)(std::upper_bound(edges.begin(), edges.end(), (double)s) - edges.begin());
        if (b == 0) b = 1; if (b > w.size()) b = w.size();
        w[b - 1] += 1; total += 1;
    }
    Qty sample(Rng& rng) const {
        if (total <= 0) return 1;
        size_t b = rng.weighted(w);
        double lo = edges[b], hi = std::min(edges[b + 1], edges[b] * 4);
        double v = lo * std::pow(hi / lo, rng.uniform());
        return std::max<Qty>(1, (Qty)std::llround(v));
    }
    Json to_json() const { Json j = Json::object(); j["edges"] = Json(edges); j["w"] = Json(w); j["total"] = total; return j; }
    static SizeDist from_json(const Json& j) {
        SizeDist d; for (auto& x : j["edges"].arr()) d.edges.push_back(x.num());
        for (auto& x : j["w"].arr()) d.w.push_back(x.num()); d.total = j.get("total", 0.0); return d;
    }
};

// ---- decomposed market events ---------------------------------------------------------------
enum class EvType : uint8_t { Limit = 0, Cancel = 1, Market = 2 };
struct MarketEvent {
    Ts ts;            // exchange time (dequantised for batched diffs if requested)
    EvType type;
    Side side;
    int level;        // 1..K (0 = inside the spread for limit orders)
    Qty size;
    Qty q_before;     // others' quantity at that level before the event
    int hawkes_type() const { return (int)type * 2 + (int)side; }   // 0..5
};

struct MarketModel {
    SymbolSpec spec;
    int K = 10;                // levels per side in the state
    int B = 12;                // queue-size buckets (half-octaves of q/AES, see bucket_of)
    double aes = 1;            // average limit-order size at level 1 (lots)
    double sigma_1s = 0;       // std of 1s mid returns (ticks)
    double spread_mean = 1;    // ticks
    double trades_per_s = 0;
    double events_per_s = 0;
    double level_depth_mean[16] = {0};   // mean others' qty at level i (lots), i=1..K
    double theta = 0;          // prob. of re-initialising the book from the stationary law when p_ref moves
    // queue-reactive intensities (per second), indexed [level-1][bucket]; market only by bucket at the best
    std::vector<std::vector<double>> lam_L, lam_C;
    std::vector<double> lam_M;
    std::vector<double> lam_L_inside;   // limit arrivals inside the spread, by spread size (ticks-1)
    // size distributions [type][level][bucket] with a pooled fallback [type][level]
    std::vector<std::vector<std::vector<SizeDist>>> sizes;
    std::vector<std::vector<SizeDist>> sizes_pooled;
    // stationary law of the queue at level i (for revealing new levels)
    std::vector<SizeDist> stationary;
    // Hawkes
    std::vector<double> beta;                        // decay grid (1/s)
    std::array<double, 6> mu{};                      // baseline (1/s)
    std::vector<std::vector<std::vector<double>>> alpha;  // [target][source][p]
    double branching_ratio = 0;
    std::vector<double> limit_level_dist;            // [0..K], 0 = inside spread
    std::vector<double> cancel_level_dist;           // [1..K] stored at index i
    // volume profile: fraction of traded lots per 5-minute bucket of the UTC day
    std::vector<double> volume_profile;
    // Reference-price jumps (small-tick extension, see gen_sim.hpp): rate per second and P(up) by
    // imbalance bucket at the touch (5 buckets of (Qb-Qa)/(Qb+Qa) on [-1,1]); size law in ticks.
    // A "jump" is a change of the mid by at least jump_min_ticks between consecutive diff batches.
    int jump_min_ticks = 5;
    std::vector<double> jump_rate, jump_up_prob;     // [5]
    SizeDist jump_size;
    double jump_rate_pooled = 0, jump_up_pooled = 0.5;
    static int imb_bucket(Qty qb, Qty qa) { double t = (double)(qb + qa); if (t <= 0) return 2; double i = (double)(qb - qa) / t; return std::min(4, std::max(0, (int)std::floor((i + 1.0) * 2.5))); }
    // Zero-intelligence Poisson book (Abergel & Jedidi 2013): limit arrivals at level i at constant
    // rate zi_lam_L[i-1]; cancellations at level i at rate zi_c[i-1] * q_i (proportional to the
    // resting quantity); market orders at constant rate zi_lam_M per side. No state dependence.
    std::vector<double> zi_lam_L, zi_c;
    double zi_lam_M = 0;
    // Cont-Kukanov-Stoikov OFI slope (ticks per lot of OFI, 1s buckets)
    double ofi_beta = 0, ofi_r2 = 0;
    // Almgren-Chriss temporary impact coefficient: mid move (ticks) per lot of net market-order flow per second
    double eta = 0;

    // Queue-size bucket: 0 for empty, then half-octaves of q/AES (<0.41 | <1 | <1.8 | <3 | <4.7 | <7 | <10 | ...), capped at B-1.
    static int bucket_of(Qty q, double aes, int B) { if (q <= 0) return 0; int b = 1 + (int)std::floor(2.0 * std::log2(1.0 + (double)q / std::max(aes, 1.0))); return std::min(std::max(b, 1), B - 1); }
    int bucket(Qty q) const { return bucket_of(q, aes, B); }

    Json to_json() const;
    static MarketModel from_json(const Json& j);
    void save(const std::string& path) const { write_file(path, to_json().dump(1)); }
    static MarketModel load(const std::string& path) { return from_json(Json::parse(read_file(path))); }
};

inline Json vec2json(const std::vector<std::vector<double>>& v) { Json a = Json::array(); for (auto& r : v) a.push(Json(r)); return a; }
inline std::vector<std::vector<double>> json2vec2(const Json& j) { std::vector<std::vector<double>> v; for (auto& r : j.arr()) { std::vector<double> x; for (auto& e : r.arr()) x.push_back(e.num()); v.push_back(x); } return v; }

inline Json MarketModel::to_json() const {
    Json j = Json::object();
    j["symbol"] = spec.name; j["tick"] = spec.tick; j["lot"] = spec.lot;
    j["K"] = K; j["B"] = B; j["aes"] = aes; j["sigma_1s"] = sigma_1s; j["spread_mean"] = spread_mean;
    j["trades_per_s"] = trades_per_s; j["events_per_s"] = events_per_s; j["theta"] = theta;
    std::vector<double> ld(level_depth_mean, level_depth_mean + K); j["level_depth_mean"] = Json(ld);
    j["lam_L"] = vec2json(lam_L); j["lam_C"] = vec2json(lam_C); j["lam_M"] = Json(lam_M); j["lam_L_inside"] = Json(lam_L_inside);
    Json sz = Json::array();
    for (auto& t : sizes) { Json lv = Json::array(); for (auto& l : t) { Json bk = Json::array(); for (auto& d : l) bk.push(d.to_json()); lv.push(bk); } sz.push(lv); }
    j["sizes"] = sz;
    Json sp = Json::array(); for (auto& t : sizes_pooled) { Json lv = Json::array(); for (auto& d : t) lv.push(d.to_json()); sp.push(lv); } j["sizes_pooled"] = sp;
    Json st = Json::array(); for (auto& d : stationary) st.push(d.to_json()); j["stationary"] = st;
    j["beta"] = Json(beta); j["mu"] = Json(std::vector<double>(mu.begin(), mu.end()));
    Json al = Json::array(); for (auto& t : alpha) al.push(vec2json(t)); j["alpha"] = al;
    j["branching_ratio"] = branching_ratio;
    j["limit_level_dist"] = Json(limit_level_dist); j["cancel_level_dist"] = Json(cancel_level_dist);
    j["volume_profile"] = Json(volume_profile); j["ofi_beta"] = ofi_beta; j["ofi_r2"] = ofi_r2; j["eta"] = eta;
    j["zi_lam_L"] = Json(zi_lam_L); j["zi_c"] = Json(zi_c); j["zi_lam_M"] = zi_lam_M;
    j["jump_min_ticks"] = jump_min_ticks; j["jump_rate"] = Json(jump_rate); j["jump_up_prob"] = Json(jump_up_prob); j["jump_size"] = jump_size.to_json();
    j["jump_rate_pooled"] = jump_rate_pooled; j["jump_up_pooled"] = jump_up_pooled;
    return j;
}
inline MarketModel MarketModel::from_json(const Json& j) {
    MarketModel m;
    m.spec.name = j.get("symbol", std::string("SYN")); m.spec.tick = j.get("tick", 0.01); m.spec.lot = j.get("lot", 0.00001);
    m.K = j.get("K", 10); m.B = j.get("B", 12); m.aes = j.get("aes", 1.0); m.sigma_1s = j.get("sigma_1s", 0.0);
    m.spread_mean = j.get("spread_mean", 1.0); m.trades_per_s = j.get("trades_per_s", 0.0); m.events_per_s = j.get("events_per_s", 0.0);
    m.theta = j.get("theta", 0.0);
    if (j.has("level_depth_mean")) { int i = 0; for (auto& x : j["level_depth_mean"].arr()) if (i < 16) m.level_depth_mean[i++] = x.num(); }
    m.lam_L = json2vec2(j["lam_L"]); m.lam_C = json2vec2(j["lam_C"]);
    for (auto& x : j["lam_M"].arr()) m.lam_M.push_back(x.num());
    if (j.has("lam_L_inside")) for (auto& x : j["lam_L_inside"].arr()) m.lam_L_inside.push_back(x.num());
    for (auto& t : j["sizes"].arr()) { std::vector<std::vector<SizeDist>> lv; for (auto& l : t.arr()) { std::vector<SizeDist> bk; for (auto& d : l.arr()) bk.push_back(SizeDist::from_json(d)); lv.push_back(bk); } m.sizes.push_back(lv); }
    for (auto& t : j["sizes_pooled"].arr()) { std::vector<SizeDist> lv; for (auto& d : t.arr()) lv.push_back(SizeDist::from_json(d)); m.sizes_pooled.push_back(lv); }
    for (auto& d : j["stationary"].arr()) m.stationary.push_back(SizeDist::from_json(d));
    for (auto& x : j["beta"].arr()) m.beta.push_back(x.num());
    { int i = 0; for (auto& x : j["mu"].arr()) if (i < 6) m.mu[i++] = x.num(); }
    for (auto& t : j["alpha"].arr()) m.alpha.push_back(json2vec2(t));
    m.branching_ratio = j.get("branching_ratio", 0.0);
    for (auto& x : j["limit_level_dist"].arr()) m.limit_level_dist.push_back(x.num());
    for (auto& x : j["cancel_level_dist"].arr()) m.cancel_level_dist.push_back(x.num());
    if (j.has("volume_profile")) for (auto& x : j["volume_profile"].arr()) m.volume_profile.push_back(x.num());
    m.ofi_beta = j.get("ofi_beta", 0.0); m.ofi_r2 = j.get("ofi_r2", 0.0); m.eta = j.get("eta", 0.0);
    if (j.has("zi_lam_L")) for (auto& x : j["zi_lam_L"].arr()) m.zi_lam_L.push_back(x.num());
    if (j.has("zi_c")) for (auto& x : j["zi_c"].arr()) m.zi_c.push_back(x.num());
    m.zi_lam_M = j.get("zi_lam_M", 0.0);
    m.jump_min_ticks = j.get("jump_min_ticks", 5);
    if (j.has("jump_rate")) for (auto& x : j["jump_rate"].arr()) m.jump_rate.push_back(x.num());
    if (j.has("jump_up_prob")) for (auto& x : j["jump_up_prob"].arr()) m.jump_up_prob.push_back(x.num());
    if (j.has("jump_size")) m.jump_size = SizeDist::from_json(j["jump_size"]);
    m.jump_rate_pooled = j.get("jump_rate_pooled", 0.0); m.jump_up_pooled = j.get("jump_up_pooled", 0.5);
    return m;
}

// ---- event decomposition ---------------------------------------------------------------------
struct Decomposition {
    std::vector<MarketEvent> events;
    // per-level time-in-bucket accumulators for the queue-reactive calibration: [level-1][bucket] seconds
    std::vector<std::vector<double>> time_in_state;
    std::vector<double> time_best_bucket;      // time the best level spent in each bucket
    std::vector<double> time_spread;           // seconds spent with spread = s ticks (index s)
    double seconds = 0;
    std::vector<double> mid_1s;                // mid sampled each second (ticks)
    std::vector<Ts> mid_1s_ts;
    std::vector<double> level_qty_samples[16]; // others' qty at level i sampled each second
    std::vector<double> spread_samples;
    std::vector<double> vol_by_5min;           // traded lots per 5-minute UTC bucket (288)
    size_t n_trades = 0;
    // reference-price jumps between consecutive batches
    struct Jump { double size_ticks; int imb_bucket; };
    std::vector<Jump> jumps;
    std::vector<double> time_imb;              // seconds spent in each imbalance bucket [5]
};

// `dequantize_ns` > 0 spreads the level updates of a batched diff uniformly over the preceding
// window (Binance @depth@100ms, Coinbase level2_batch 50ms) so that Hawkes timestamps are not
// piled on a 100 ms grid.  Market orders keep their exact trade-stream timestamps.
inline Decomposition decompose(const Tape& tape, int K, int B, double aes_hint, Ts dequantize_ns, uint64_t seed = 1, int jump_min_ticks = 5) {
    Decomposition d;
    d.time_imb.assign(5, 0.0);
    double last_mid = 0; bool have_mid = false; int last_imb = 2;
    d.time_in_state.assign(K, std::vector<double>(B, 0.0));
    d.time_best_bucket.assign(B, 0.0);
    d.time_spread.assign(64, 0.0);
    d.vol_by_5min.assign(288, 0.0);
    Book book; RefPrice ref; Rng rng(seed);
    std::map<std::pair<int, Price>, Qty> pending_trades;   // (side, price) -> traded since last level update
    double aes = std::max(aes_hint, 1.0);
    auto bucket = [&](Qty q) { return MarketModel::bucket_of(q, aes, B); };
    Ts last_ts = 0; bool have_state = false;
    Ts next_sample = 0;
    std::vector<Ts> last_level_update;   // unused placeholder for potential extensions
    // accumulate time in state between consecutive events
    auto accumulate = [&](Ts now) {
        if (!have_state || now <= last_ts) return;
        double dt = (now - last_ts) / 1e9;
        for (int i = 1; i <= K; ++i) {
            Qty qb = book.qty_at(Side::Bid, ref.price(Side::Bid, i)), qa = book.qty_at(Side::Ask, ref.price(Side::Ask, i));
            d.time_in_state[i - 1][bucket(qb)] += dt; d.time_in_state[i - 1][bucket(qa)] += dt;
        }
        d.time_best_bucket[bucket(book.best_bid_qty())] += dt; d.time_best_bucket[bucket(book.best_ask_qty())] += dt;
        Price s = book.spread(); if (s >= 0 && s < 64) d.time_spread[(size_t)s] += dt;
        d.seconds += dt;
    };
    // group trades with identical (ts, side, price-range) into one market order
    MarketEvent pending_mo{}; bool have_mo = false;
    auto flush_mo = [&]() { if (have_mo) { d.events.push_back(pending_mo); have_mo = false; } };

    // Batches (same exchange timestamp) are processed atomically: time-in-state and the 1 s samples
    // are taken between batches, never on a half-applied diff; the tape is already ordered
    // decreases-first inside a batch (see normalize_batches).
    for_each_batch(tape.events, [&](size_t i0, size_t i1) {
        const L2Event& first = tape.events[i0];
        if (book.valid()) {
            accumulate(first.ts_ex);
            if (have_state && first.ts_ex > last_ts) d.time_imb[(size_t)last_imb] += (first.ts_ex - last_ts) / 1e9;
            if (next_sample == 0) next_sample = first.ts_ex;
            while (first.ts_ex >= next_sample) {
                d.mid_1s.push_back(book.mid()); d.mid_1s_ts.push_back(next_sample);
                d.spread_samples.push_back((double)book.spread());
                for (int i = 1; i <= K && i < 16; ++i)
                    d.level_qty_samples[i].push_back((double)(book.qty_at(Side::Bid, ref.price(Side::Bid, i)) + book.qty_at(Side::Ask, ref.price(Side::Ask, i))) * 0.5);
                next_sample += NS_PER_S;
            }
        }
        for (size_t k = i0; k < i1; ++k) {
            const L2Event& e = tape.events[k];
            switch (e.kind) {
                case EvKind::Snapshot: book.clear(); pending_trades.clear(); have_state = false; break;
                case EvKind::Trade: {
                    if (!book.valid()) break;
                    pending_trades[{(int)e.side, e.price}] += e.qty;
                    ++d.n_trades;
                    int slot = (int)(((e.ts_ex / NS_PER_S) % 86400) / 300); d.vol_by_5min[(size_t)slot] += (double)e.qty;
                    if (have_mo && pending_mo.ts == e.ts_ex && pending_mo.side == e.side) { pending_mo.size += e.qty; break; }
                    flush_mo();
                    pending_mo = MarketEvent{e.ts_ex, EvType::Market, e.side, std::max(1, ref.level(e.side, e.price)), e.qty, book.best_qty(e.side)};
                    have_mo = true;
                    break;
                }
                case EvKind::Level: {
                    flush_mo();
                    Qty prev = book.qty_at(e.side, e.price);
                    if (have_state && book.valid() && prev != e.qty) {
                        int lvl = ref.level(e.side, e.price);
                        Ts ts = e.ts_ex - (dequantize_ns > 0 ? (Ts)(rng.uniform() * (double)dequantize_ns) : 0);
                        if (e.qty > prev) {
                            if (lvl <= K) d.events.push_back(MarketEvent{ts, EvType::Limit, e.side, std::max(lvl, 0), e.qty - prev, prev});
                        } else {
                            Qty dec = prev - e.qty;
                            auto it = pending_trades.find({(int)e.side, e.price});
                            Qty traded = it == pending_trades.end() ? 0 : it->second;
                            Qty m = std::min(dec, traded);
                            if (it != pending_trades.end()) { it->second -= m; if (it->second <= 0) pending_trades.erase(it); }
                            Qty c = dec - m;
                            if (c > 0 && lvl >= 1 && lvl <= K) d.events.push_back(MarketEvent{ts, EvType::Cancel, e.side, lvl, c, prev});
                        }
                    }
                    book.set(e.side, e.price, e.qty);
                    if (book.valid()) { ref.update(book.best_bid(), book.best_ask()); have_state = true; }
                    break;
                }
            }
        }
        if (book.valid()) {
            double mid = book.mid();
            if (have_mid && std::fabs(mid - last_mid) >= jump_min_ticks) d.jumps.push_back({mid - last_mid, last_imb});
            last_mid = mid; have_mid = true;
            last_imb = MarketModel::imb_bucket(book.best_bid_qty(), book.best_ask_qty());
        }
        last_ts = first.ts_ex;
    });
    flush_mo();
    std::stable_sort(d.events.begin(), d.events.end(), [](const MarketEvent& a, const MarketEvent& b) { return a.ts < b.ts; });
    return d;
}

// ---- Hawkes EM ---------------------------------------------------------------------------------
struct HawkesFit {
    std::array<double, 6> mu{};
    std::vector<std::vector<std::vector<double>>> alpha;   // [target][source][p]
    double loglik = 0, branching_ratio = 0;
};

inline HawkesFit fit_hawkes(const std::vector<MarketEvent>& ev, const std::vector<double>& beta, double T_seconds, int iters = 40) {
    const int E = 6, P = (int)beta.size();
    HawkesFit fit;
    fit.alpha.assign(E, std::vector<std::vector<double>>(E, std::vector<double>(P, 0.02 / P)));
    std::array<double, 6> n{}; for (auto& e : ev) n[e.hawkes_type()] += 1;
    for (int e = 0; e < E; ++e) fit.mu[e] = std::max(n[e] / std::max(T_seconds, 1.0) * 0.5, 1e-6);
    if (ev.empty()) return fit;
    const double t0 = ev.front().ts / 1e9;
    std::vector<double> t; t.reserve(ev.size()); for (auto& e : ev) t.push_back(e.ts / 1e9 - t0);
    std::vector<int> ty; ty.reserve(ev.size()); for (auto& e : ev) ty.push_back(e.hawkes_type());
    // compensator normalisers sum_j (1 - exp(-beta_p (T - t_j))) per source and p
    std::vector<std::vector<double>> comp(E, std::vector<double>(P, 0.0));
    for (size_t i = 0; i < t.size(); ++i) for (int p = 0; p < P; ++p) comp[ty[i]][p] += 1.0 - std::exp(-beta[p] * (T_seconds - t[i]));
    std::vector<std::vector<double>> S(E, std::vector<double>(P, 0.0));
    for (int it = 0; it < iters; ++it) {
        std::array<double, 6> r0{}; std::vector<std::vector<std::vector<double>>> R(E, std::vector<std::vector<double>>(E, std::vector<double>(P, 0.0)));
        for (auto& s : S) std::fill(s.begin(), s.end(), 0.0);
        double last = t[0], ll = 0;
        for (size_t i = 0; i < t.size(); ++i) {
            double dt = t[i] - last; last = t[i];
            for (int e = 0; e < E; ++e) for (int p = 0; p < P; ++p) S[e][p] *= std::exp(-beta[p] * dt);
            int e = ty[i];
            double lam = fit.mu[e];
            for (int s = 0; s < E; ++s) for (int p = 0; p < P; ++p) lam += fit.alpha[e][s][p] * beta[p] * S[s][p];
            if (lam <= 0) lam = 1e-12;
            ll += std::log(lam);
            r0[e] += fit.mu[e] / lam;
            for (int s = 0; s < E; ++s) for (int p = 0; p < P; ++p) R[e][s][p] += fit.alpha[e][s][p] * beta[p] * S[s][p] / lam;
            for (int p = 0; p < P; ++p) S[e][p] += 1.0;
        }
        for (int e = 0; e < E; ++e) {
            ll -= fit.mu[e] * T_seconds;
            for (int s = 0; s < E; ++s) for (int p = 0; p < P; ++p) ll -= fit.alpha[e][s][p] * comp[s][p];
            fit.mu[e] = std::max(r0[e] / T_seconds, 1e-8);
            for (int s = 0; s < E; ++s) for (int p = 0; p < P; ++p) fit.alpha[e][s][p] = comp[s][p] > 0 ? R[e][s][p] / comp[s][p] : 0.0;
        }
        fit.loglik = ll;
    }
    // spectral radius of the branching matrix (power iteration)
    std::vector<double> v(E, 1.0);
    double rho = 0;
    for (int k = 0; k < 100; ++k) {
        std::vector<double> w(E, 0.0);
        for (int e = 0; e < E; ++e) for (int s = 0; s < E; ++s) { double a = 0; for (int p = 0; p < P; ++p) a += fit.alpha[e][s][p]; w[e] += a * v[s]; }
        double nrm = 0; for (double x : w) nrm += x * x; nrm = std::sqrt(nrm); if (nrm <= 0) break;
        rho = nrm; for (int e = 0; e < E; ++e) v[e] = w[e] / nrm;
    }
    fit.branching_ratio = rho;
    return fit;
}

// ---- full calibration ------------------------------------------------------------------------------
struct CalibOptions {
    int K = 10, B = 12;
    int jump_min_ticks = 5;
    Ts dequantize_ns = 100 * NS_PER_MS;
    std::vector<double> beta = {1000, 100, 10, 1, 0.1};
    int hawkes_iters = 40;
    double theta = 0.0;
};

inline MarketModel calibrate(const Tape& tape, const CalibOptions& opt, Json* report = nullptr) {
    MarketModel m; m.spec = tape.spec; m.K = opt.K; m.B = opt.B; m.theta = opt.theta; m.beta = opt.beta;
    // pass 1: AES (mean limit arrival size at level 1) - needs a preliminary decomposition
    Decomposition d0 = decompose(tape, opt.K, opt.B, 1.0, opt.dequantize_ns);
    double s = 0; size_t n = 0;
    for (auto& e : d0.events) if (e.type == EvType::Limit && e.level == 1) { s += (double)e.size; ++n; }
    m.aes = n ? s / n : 1.0;
    Decomposition d = decompose(tape, opt.K, opt.B, m.aes, opt.dequantize_ns, 1, opt.jump_min_ticks);
    m.jump_min_ticks = opt.jump_min_ticks;
    m.jump_rate.assign(5, 0.0); m.jump_up_prob.assign(5, 0.5); m.jump_size.init(1);
    { std::vector<double> n(5, 0.0), up(5, 0.0); double nt = 0, upt = 0;
      for (auto& jp : d.jumps) { n[jp.imb_bucket] += 1; if (jp.size_ticks > 0) up[jp.imb_bucket] += 1; nt += 1; if (jp.size_ticks > 0) upt += 1; m.jump_size.add((Qty)std::llround(std::fabs(jp.size_ticks))); }
      double tt = 0; for (double x : d.time_imb) tt += x;
      m.jump_rate_pooled = tt > 0 ? nt / tt : 0; m.jump_up_pooled = nt > 0 ? upt / nt : 0.5;
      for (int b = 0; b < 5; ++b) { m.jump_rate[b] = d.time_imb[b] > 5.0 ? n[b] / d.time_imb[b] : m.jump_rate_pooled; m.jump_up_prob[b] = n[b] >= 10 ? up[b] / n[b] : m.jump_up_pooled; } }
    // intensities
    m.lam_L.assign(opt.K, std::vector<double>(opt.B, 0.0)); m.lam_C = m.lam_L; m.lam_M.assign(opt.B, 0.0);
    m.lam_L_inside.assign(64, 0.0);
    std::vector<std::vector<double>> nL = m.lam_L, nC = m.lam_L; std::vector<double> nM(opt.B, 0.0), nIn(64, 0.0);
    m.sizes.assign(3, std::vector<std::vector<SizeDist>>(opt.K + 1, std::vector<SizeDist>(opt.B)));
    m.sizes_pooled.assign(3, std::vector<SizeDist>(opt.K + 1));
    for (auto& t : m.sizes) for (auto& l : t) for (auto& b : l) b.init(m.aes);
    for (auto& t : m.sizes_pooled) for (auto& l : t) l.init(m.aes);
    std::vector<double> ll(opt.K + 1, 0.0), cl(opt.K + 1, 0.0);
    for (auto& e : d.events) {
        int b = m.bucket(e.q_before);
        if (e.type == EvType::Limit) {
            if (e.level == 0) { nIn[0] += 1; }
            else if (e.level >= 1 && e.level <= opt.K) nL[e.level - 1][b] += 1;
            if (e.level <= opt.K) ll[e.level] += 1;
        } else if (e.type == EvType::Cancel) {
            if (e.level >= 1 && e.level <= opt.K) { nC[e.level - 1][b] += 1; cl[e.level] += 1; }
        } else nM[b] += 1;
        int lv = std::min(std::max(e.level, 0), opt.K);
        m.sizes[(int)e.type][lv][b].add(e.size);
        m.sizes_pooled[(int)e.type][lv].add(e.size);
    }
    const double T_MIN = 2.0;   // seconds: below this, fall back to the pooled level estimate
    for (int i = 0; i < opt.K; ++i) {
        double NL = 0, NC = 0, TT = 0;
        for (int b = 0; b < opt.B; ++b) { NL += nL[i][b]; NC += nC[i][b]; TT += d.time_in_state[i][b]; }
        for (int b = 0; b < opt.B; ++b) {
            double T = d.time_in_state[i][b];
            m.lam_L[i][b] = T > T_MIN ? nL[i][b] / T : (TT > 0 ? NL / TT : 0);
            m.lam_C[i][b] = T > T_MIN ? nC[i][b] / T : (TT > 0 ? NC / TT : 0);
        }
        m.lam_C[i][0] = 0;   // nothing to cancel in an empty queue
    }
    { double NM = 0, TT = 0; for (int b = 0; b < opt.B; ++b) { NM += nM[b]; TT += d.time_best_bucket[b]; }
      for (int b = 0; b < opt.B; ++b) { double T = d.time_best_bucket[b]; m.lam_M[b] = T > T_MIN ? nM[b] / T : (TT > 0 ? NM / TT : 0); } }
    // zero-intelligence null model: pooled rates, cancels proportional to resting quantity
    m.zi_lam_L.assign(opt.K, 0.0); m.zi_c.assign(opt.K, 0.0);
    for (int i = 0; i < opt.K; ++i) {
        double NL = 0, NC = 0, TT = 0; for (int b = 0; b < opt.B; ++b) { NL += nL[i][b]; NC += nC[i][b]; TT += d.time_in_state[i][b]; }
        double qbar = mean(d.level_qty_samples[i + 1]);   // mean resting quantity at level i (lots)
        m.zi_lam_L[i] = TT > 0 ? NL / TT : 0;             // per side per second
        m.zi_c[i] = (TT > 0 && qbar > 0) ? NC / (TT * qbar) : 0;   // per lot per second
    }
    { double NM = 0; for (int b = 0; b < opt.B; ++b) NM += nM[b]; m.zi_lam_M = d.seconds > 0 ? NM / d.seconds / 2.0 : 0; }
    // inside-spread limit arrivals: one rate per unit of time spent with spread > 1
    { double Tw = 0; for (size_t sp = 2; sp < d.time_spread.size(); ++sp) Tw += d.time_spread[sp]; m.lam_L_inside[0] = Tw > 0 ? nIn[0] / Tw : 0; }
    // stationary queue laws and depth means
    m.stationary.assign(opt.K + 1, SizeDist());
    for (int i = 1; i <= opt.K; ++i) {
        m.stationary[i].init(m.aes);
        for (double q : d.level_qty_samples[i]) m.stationary[i].add((Qty)std::llround(q));
        m.level_depth_mean[i - 1] = mean(d.level_qty_samples[i]);
    }
    // level distributions for the Hawkes book actions
    double sl = 0, sc = 0; for (double x : ll) sl += x; for (double x : cl) sc += x;
    m.limit_level_dist.assign(opt.K + 1, 0.0); m.cancel_level_dist.assign(opt.K + 1, 0.0);
    for (int i = 0; i <= opt.K; ++i) { m.limit_level_dist[i] = sl > 0 ? ll[i] / sl : 0; m.cancel_level_dist[i] = sc > 0 ? cl[i] / sc : 0; }
    // summary stats
    m.spread_mean = mean(d.spread_samples);
    std::vector<double> r; for (size_t i = 1; i < d.mid_1s.size(); ++i) r.push_back(d.mid_1s[i] - d.mid_1s[i - 1]);
    m.sigma_1s = stdev(r);
    m.trades_per_s = d.seconds > 0 ? d.n_trades / d.seconds : 0;
    m.events_per_s = d.seconds > 0 ? d.events.size() / d.seconds : 0;
    double vt = 0; for (double x : d.vol_by_5min) vt += x;
    m.volume_profile.assign(288, 1.0 / 288);
    if (vt > 0) { double cov = 0; for (double x : d.vol_by_5min) if (x > 0) cov += 1; for (size_t i = 0; i < 288; ++i) m.volume_profile[i] = d.vol_by_5min[i] > 0 ? d.vol_by_5min[i] / vt : 0; }
    // Hawkes fit
    HawkesFit hf = fit_hawkes(d.events, opt.beta, std::max(d.seconds, 1.0), opt.hawkes_iters);
    m.mu = hf.mu; m.alpha = hf.alpha; m.branching_ratio = hf.branching_ratio;
    if (report) {
        Json& r = *report;
        r["seconds"] = d.seconds; r["n_events"] = (long long)d.events.size(); r["n_trades"] = (long long)d.n_trades;
        r["aes_lots"] = m.aes; r["sigma_1s_ticks"] = m.sigma_1s; r["spread_mean_ticks"] = m.spread_mean;
        r["hawkes_loglik"] = hf.loglik; r["hawkes_branching_ratio"] = hf.branching_ratio;
        Json cnt = Json::object(); double c[3] = {0, 0, 0}; for (auto& e : d.events) c[(int)e.type] += 1;
        cnt["limit"] = c[0]; cnt["cancel"] = c[1]; cnt["market"] = c[2]; r["event_counts"] = cnt;
        Json tis = Json::array(); for (auto& row : d.time_in_state) tis.push(Json(row)); r["time_in_state_s"] = tis;
        r["time_spread_s"] = Json(d.time_spread); r["n_inside_spread_limit_events"] = nIn[0];
        r["n_jumps"] = (long long)d.jumps.size(); r["jump_rate_by_imbalance"] = Json(m.jump_rate); r["jump_up_prob_by_imbalance"] = Json(m.jump_up_prob); r["time_by_imbalance_s"] = Json(d.time_imb);
    }
    return m;
}

} // namespace lobsim
