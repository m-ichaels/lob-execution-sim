#pragma once
// The benchmark that *is* the project: cost of each execution algorithm under each simulator mode
// and each queue assumption, paired per day, with bootstrap confidence intervals - and the same
// table again with the assumed round-trip latency doubled.
#include "replay_sim.hpp"
#include "gen_sim.hpp"
#include "algos.hpp"
#include "stats.hpp"
#include "json.hpp"
#include <map>
#include <set>
#include <functional>

namespace lobsim {

struct BenchConfig {
    std::vector<std::string> tape_paths;    // one per day
    std::string calib_path;
    Qty qty = 0;                            // parent size in lots (0 = auto: 20 x AES)
    double horizon_s = 300;
    int slices = 10;
    int episodes_per_day = 6;
    double latency_ms = 50;                 // one-way; report_out = order_in = latency
    std::vector<std::string> modes = {"replay", "queue_reactive", "zero_intelligence"};
    std::vector<std::string> queue_models = {"naive_fifo", "power_prob", "risk_averse"};
    std::vector<std::string> algos = {"TWAP", "VWAP", "AlmgrenChriss", "OFIAdaptive_CK", "OFIAdaptive_CKadv"};
    double warmup_s = 60;
    uint64_t seed = 1;
    AlgoParams params;
    std::string fill_model_json;            // optional: logistic weights per queue model
    bool latency_sensitivity = true;
    double maker_bp = 0, taker_bp = 0;      // venue fees, reported as a separate column (Binance spot VIP0: 10 / 10)
};

struct BenchRow {
    std::string day, mode, qm, algo; int episode; double latency_ms;
    double is_bp, is_ticks, passive_frac; Qty filled, target; int n_child; double kappaT;
    double spread_bp, timing_bp, impact_bp, opp_bp, fee_bp;
};

// Mid path of a simulator run with no orders (common random numbers: same seed as the trading run).
inline std::vector<std::pair<Ts, double>> counterfactual_path(Simulator& sim, Ts t_end) {
    std::vector<std::pair<Ts, double>> m;
    while (sim.now() < t_end && sim.step()) if (sim.book_valid()) m.emplace_back(sim.now(), sim.mid());
    return m;
}

inline std::unique_ptr<Algo> make_algo(const std::string& name, AlgoParams p) {
    if (name == "TWAP") return std::make_unique<SliceScheduler>(SliceScheduler::Curve::Twap, p);
    if (name == "VWAP") return std::make_unique<SliceScheduler>(SliceScheduler::Curve::Vwap, p);
    if (name == "AlmgrenChriss" || name == "AC") return std::make_unique<SliceScheduler>(SliceScheduler::Curve::AC, p);
    if (name == "OFIAdaptive_CK") { p.passive = true; p.ck_adverse_ticks = 0; return std::make_unique<SliceScheduler>(SliceScheduler::Curve::OfiAdaptive, p); }
    // mark-out-aware variant: passive fills are charged the measured adverse mark-out (params.ck_adverse_ticks)
    if (name == "OFIAdaptive_CKadv") { p.passive = true; return std::make_unique<SliceScheduler>(SliceScheduler::Curve::OfiAdaptive, p); }
    if (name == "OFIAdaptive_mkt") { p.passive = false; return std::make_unique<SliceScheduler>(SliceScheduler::Curve::OfiAdaptive, p); }
    throw Error("unknown algo " + name);
}

inline std::unique_ptr<Simulator> make_sim(const std::string& mode, const Tape& tape, const MarketModel& model, Ts t_start, Price start_mid, uint64_t seed, double horizon_s) {
    if (mode == "replay") {
        size_t idx = (size_t)(std::lower_bound(tape.events.begin(), tape.events.end(), t_start, [](const L2Event& e, Ts t) { return e.ts_ex < t; }) - tape.events.begin());
        return std::make_unique<ReplaySim>(&tape, idx);
    }
    Ts t_end = t_start + (Ts)((horizon_s + 120) * 1e9);
    if (mode == "queue_reactive" || mode == "qr") return std::make_unique<QRSim>(model, t_start, t_end, start_mid, seed);
    if (mode == "hawkes") return std::make_unique<HawkesSim>(model, t_start, t_end, start_mid, seed);
    if (mode == "zero_intelligence" || mode == "zi") return std::make_unique<ZISim>(model, t_start, t_end, start_mid, seed);
    throw Error("unknown mode " + mode);
}

struct BenchOutput { Json json; std::string markdown; std::vector<BenchRow> rows; };

inline BenchOutput run_bench(const BenchConfig& cfg, const std::function<void(const std::string&)>& log = nullptr) {
    MarketModel model = MarketModel::load(cfg.calib_path);
    std::vector<Tape> tapes;
    for (auto& p : cfg.tape_paths) tapes.push_back(read_tape(p));
    Qty qty = cfg.qty > 0 ? cfg.qty : (Qty)std::llround(20 * model.aes);
    std::map<std::string, std::vector<double>> logistic_w;
    if (!cfg.fill_model_json.empty()) {
        Json fj = Json::parse(read_file(cfg.fill_model_json));
        for (auto& kv : fj.obj()) if (kv.second.has("logistic_w")) { std::vector<double> w; for (auto& x : kv.second["logistic_w"].arr()) w.push_back(x.num()); logistic_w[kv.first] = w; }
    }
    std::vector<BenchRow> rows;
    std::vector<double> latencies = {cfg.latency_ms};
    if (cfg.latency_sensitivity) latencies.push_back(2 * cfg.latency_ms);
    Rng rng(cfg.seed);
    for (size_t d = 0; d < tapes.size(); ++d) {
        const Tape& tape = tapes[d];
        std::string day = cfg.tape_paths[d];
        auto sl = day.find_last_of("/\\"); if (sl != std::string::npos) day = day.substr(sl + 1);
        Ts lo = tape.t0() + (Ts)(cfg.warmup_s * 1e9), hi = tape.t1() - (Ts)((cfg.horizon_s + 70) * 1e9);
        if (hi <= lo) { if (log) log("tape too short: " + day); continue; }
        // episode start times: evenly spaced, side alternating (buy/sell pairs)
        for (int ep = 0; ep < cfg.episodes_per_day; ++ep) {
            Ts t_start = lo + (Ts)((double)(hi - lo) * (ep + 0.5) / cfg.episodes_per_day);
            Side side = ep % 2 == 0 ? Side::Bid : Side::Ask;
            // arrival mid from the live tape at t_start (so generative sims start at the same price)
            ReplaySim probe(&tape, (size_t)(std::lower_bound(tape.events.begin(), tape.events.end(), t_start, [](const L2Event& e, Ts t) { return e.ts_ex < t; }) - tape.events.begin()));
            Price start_mid = probe.book_valid() ? (Price)std::llround(probe.mid()) : 0;
            if (start_mid == 0) continue;
            uint64_t ep_seed = cfg.seed * 1000003ULL + d * 1009ULL + (uint64_t)ep;
            std::map<std::string, std::vector<std::pair<Ts, double>>> cf;
            for (auto& mode : cfg.modes) {
                auto sim0 = make_sim(mode, tape, model, t_start - (Ts)(cfg.warmup_s * 1e9), start_mid, ep_seed, cfg.horizon_s);
                cf[mode] = counterfactual_path(*sim0, t_start + (Ts)((cfg.horizon_s + 10) * 1e9));
            }
            for (double lat : latencies) for (auto& mode : cfg.modes) for (auto& qmn : cfg.queue_models) {
                QueueModel qm; qm.kind = parse_queue_model(qmn);
                for (auto& an : cfg.algos) {
                    AlgoParams p = cfg.params; p.model = &model; p.qm = qm;
                    if (p.fill_kind == FillModel::Kind::Logistic && logistic_w.count(qmn)) p.logistic_w = logistic_w[qmn];
                    auto sim = make_sim(mode, tape, model, t_start - (Ts)(cfg.warmup_s * 1e9), start_mid, ep_seed, cfg.horizon_s);
                    sim->set_queue_model(qm);
                    Latency L; L.order_in = (Ts)(lat * 1e6); L.report_out = (Ts)(lat * 1e6); sim->set_latency(L);
                    auto algo = make_algo(an, p);
                    ParentOrder po; po.side = side; po.qty = qty; po.start = t_start; po.end = t_start + (Ts)(cfg.horizon_s * 1e9); po.slices = cfg.slices;
                    // replay: the tape itself is the counterfactual, so impact is identically zero
                    ExecResult r = run_episode(*sim, *algo, po, mode == "replay" ? nullptr : &cf[mode], cfg.maker_bp, cfg.taker_bp);
                    double bp = r.arrival_mid > 0 ? 1e4 / r.arrival_mid : 0;
                    rows.push_back({day, mode, qmn, an, ep, lat, r.is_bp, r.is_ticks, r.passive_frac, r.filled, r.target, r.n_child, r.kappaT,
                                    r.spread_ticks * bp, r.timing_ticks * bp, r.impact_ticks * bp, r.opportunity_ticks * bp, r.fee_bp});
                }
            }
            if (log) log("day " + day + " episode " + std::to_string(ep + 1) + "/" + std::to_string(cfg.episodes_per_day) + " done");
        }
    }
    // ---- aggregation --------------------------------------------------------------------------
    // per (latency, mode, qm, algo): per-day mean IS; per-day paired difference vs TWAP; bootstrap CI over days
    BenchOutput out; out.rows = rows;
    Json j = Json::object();
    j["config"] = Json::object();
    j["config"]["qty_lots"] = (long long)qty; j["config"]["horizon_s"] = cfg.horizon_s; j["config"]["slices"] = cfg.slices;
    j["config"]["episodes_per_day"] = cfg.episodes_per_day; j["config"]["days"] = (long long)tapes.size(); j["config"]["latency_ms"] = cfg.latency_ms;
    j["config"]["fill_model"] = FillModel::name(cfg.params.fill_kind); j["config"]["maker_bp"] = cfg.maker_bp; j["config"]["taker_bp"] = cfg.taker_bp;
    std::set<std::string> days; for (auto& r : rows) days.insert(r.day);
    bool single_day = days.size() < 2;
    j["pairing_unit"] = single_day ? "episode (only one day available: CI is over episodes, not days)" : "day";
    Json cells = Json::array();
    std::string md;
    for (double lat : latencies) {
        md += "\n### Latency " + std::to_string((int)lat) + " ms one-way\n\n";
        md += "| mode | queue model | algo | IS bp (mean) | 95% CI | vs TWAP bp | 95% CI | spread | timing | impact | opp. | fees | passive % | rank |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
        for (auto& mode : cfg.modes) for (auto& qmn : cfg.queue_models) {
            // rank algos within this cell by mean IS
            std::vector<std::pair<double, std::string>> ranking;
            std::map<std::string, Json> cell_json;
            std::map<std::string, std::string> cell_md;
            for (auto& an : cfg.algos) {
                // group by pairing unit
                std::map<std::string, std::vector<double>> by_unit, by_unit_twap; std::vector<double> pf, sp, tm, im, op, fe;
                for (auto& r : rows) {
                    if (r.latency_ms != lat || r.mode != mode || r.qm != qmn) continue;
                    std::string unit = single_day ? std::to_string(r.episode) : r.day;
                    if (r.algo == an) { by_unit[unit].push_back(r.is_bp); pf.push_back(r.passive_frac); sp.push_back(r.spread_bp); tm.push_back(r.timing_bp); im.push_back(r.impact_bp); op.push_back(r.opp_bp); fe.push_back(r.fee_bp); }
                    if (r.algo == "TWAP") by_unit_twap[unit].push_back(r.is_bp);
                }
                std::vector<double> unit_means, diffs;
                for (auto& kv : by_unit) { double m = mean(kv.second); unit_means.push_back(m); if (by_unit_twap.count(kv.first)) diffs.push_back(m - mean(by_unit_twap[kv.first])); }
                CI ci = bootstrap_mean_ci(unit_means), cid = bootstrap_mean_ci(diffs);
                Json c = Json::object(); c["latency_ms"] = lat; c["mode"] = mode; c["queue_model"] = qmn; c["algo"] = an;
                c["is_bp_mean"] = ci.mean; c["is_bp_lo"] = ci.lo; c["is_bp_hi"] = ci.hi; c["n_units"] = (long long)ci.n;
                c["vs_twap_bp"] = cid.mean; c["vs_twap_lo"] = cid.lo; c["vs_twap_hi"] = cid.hi; c["passive_frac"] = mean(pf);
                c["spread_bp"] = mean(sp); c["timing_bp"] = mean(tm); c["impact_bp"] = mean(im); c["opportunity_bp"] = mean(op); c["fee_bp"] = mean(fe);
                cell_json[an] = c;
                ranking.emplace_back(ci.mean, an);
                char buf[512];
                std::snprintf(buf, sizeof buf, "| %s | %s | %s | %.2f | [%.2f, %.2f] | %+.2f | [%.2f, %.2f] | %.2f | %.2f | %.2f | %.2f | %.2f | %.0f%% |", mode.c_str(), qmn.c_str(), an.c_str(), ci.mean, ci.lo, ci.hi, cid.mean, cid.lo, cid.hi, mean(sp), mean(tm), mean(im), mean(op), mean(fe), 100 * mean(pf));
                cell_md[an] = buf;
            }
            std::sort(ranking.begin(), ranking.end());
            for (size_t k = 0; k < ranking.size(); ++k) { cell_json[ranking[k].second]["rank"] = (long long)(k + 1); }
            for (auto& an : cfg.algos) { cells.push(cell_json[an]); md += cell_md[an] + " " + std::to_string((long long)cell_json[an]["rank"].i64()) + " |\n"; }
        }
    }
    j["cells"] = cells;
    // ranking instability summary: for each (latency) how many distinct orderings of algos across (mode, qm)
    Json inst = Json::array();
    for (double lat : latencies) {
        std::map<std::string, std::set<std::string>> orderings;   // algo ordering string -> set of cells
        for (auto& mode : cfg.modes) for (auto& qmn : cfg.queue_models) {
            std::vector<std::pair<long long, std::string>> v;
            for (auto& c : cells.arr()) if (c["latency_ms"].num() == lat && c["mode"].str() == mode && c["queue_model"].str() == qmn) v.emplace_back(c["rank"].i64(), c["algo"].str());
            std::sort(v.begin(), v.end());
            std::string key; for (auto& x : v) key += x.second + " < ";
            orderings[key].insert(mode + "/" + qmn);
        }
        Json e = Json::object(); e["latency_ms"] = lat; e["distinct_rankings"] = (long long)orderings.size();
        Json o = Json::array(); for (auto& kv : orderings) { Json x = Json::object(); x["ranking"] = kv.first; Json cs = Json::array(); for (auto& c : kv.second) cs.push(c); x["cells"] = cs; o.push(x); }
        e["rankings"] = o; inst.push(e);
    }
    j["ranking_instability"] = inst;
    Json rj = Json::array();
    for (auto& r : rows) { Json x = Json::object(); x["day"] = r.day; x["mode"] = r.mode; x["queue_model"] = r.qm; x["algo"] = r.algo; x["episode"] = r.episode; x["latency_ms"] = r.latency_ms; x["is_bp"] = r.is_bp; x["is_ticks"] = r.is_ticks; x["passive_frac"] = r.passive_frac; x["filled"] = (long long)r.filled; x["target"] = (long long)r.target; x["n_child"] = r.n_child; x["kappaT"] = r.kappaT; x["spread_bp"] = r.spread_bp; x["timing_bp"] = r.timing_bp; x["impact_bp"] = r.impact_bp; x["opportunity_bp"] = r.opp_bp; x["fee_bp"] = r.fee_bp; rj.push(x); }
    j["rows"] = rj;
    out.json = j; out.markdown = md;
    return out;
}

} // namespace lobsim
