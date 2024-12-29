// lobsim - command-line driver for the order-book execution simulator.
//
//   lobsim rebuild    raw NDJSON -> tape (+ operational report)
//   lobsim calibrate  tape -> market model (QR intensities, sizes, Hawkes, OFI, volume profile)
//   lobsim synth      market model -> synthetic tape (queue-reactive or Hawkes)
//   lobsim validate   stylised facts + impact response, live vs each simulator
//   lobsim fillcurve  empirical passive fill probability under the three queue models
//   lobsim ofi        Cont-Kukanov-Stoikov OFI regression
//   lobsim markout    mark-outs / adverse selection of passive fills per simulator
//   lobsim bench      the cost table: algos x modes x queue models, paired CIs, latency x2
#include "lobsim/rebuild.hpp"
#include "lobsim/market_model.hpp"
#include "lobsim/gen_sim.hpp"
#include "lobsim/validate.hpp"
#include "lobsim/analysis.hpp"
#include "lobsim/bench.hpp"
#include "lobsim/l3.hpp"
#include <iostream>
#include <map>
#include <filesystem>
#include <chrono>

using namespace lobsim;
namespace fs = std::filesystem;

struct Args {
    std::map<std::string, std::string> kv; std::vector<std::string> pos;
    std::string get(const std::string& k, const std::string& d = "") const { auto it = kv.find(k); return it == kv.end() ? d : it->second; }
    double num(const std::string& k, double d) const { auto it = kv.find(k); return it == kv.end() ? d : std::atof(it->second.c_str()); }
    bool has(const std::string& k) const { return kv.count(k) > 0; }
    std::vector<std::string> list(const std::string& k, const std::vector<std::string>& d) const {
        if (!has(k)) return d; std::vector<std::string> v; std::string s = get(k), cur;
        for (char c : s) { if (c == ',') { if (!cur.empty()) v.push_back(cur); cur.clear(); } else cur += c; } if (!cur.empty()) v.push_back(cur); return v;
    }
};
static Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 2; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) == 0) { std::string k = s.substr(2); if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) a.kv[k] = argv[++i]; else a.kv[k] = "1"; }
        else a.pos.push_back(s);
    }
    return a;
}
static void logmsg(const std::string& s) { std::cerr << "[lobsim] " << s << std::endl; }
static std::vector<std::string> expand_files(const std::vector<std::string>& in) {
    std::vector<std::string> out;
    for (auto& p : in) {
        if (fs::is_directory(p)) { for (auto& e : fs::recursive_directory_iterator(p)) if (e.is_regular_file() && e.path().extension() == ".ndjson") out.push_back(e.path().string()); }
        else out.push_back(p);
    }
    std::sort(out.begin(), out.end());
    return out;
}
static SymbolSpec spec_from(const Args& a, const std::string& def_sym) {
    SymbolSpec s; s.name = a.get("symbol", def_sym); s.tick = a.num("tick", 0.01); s.lot = a.num("lot", 0.00001); return s;
}

// ---------------------------------------------------------------------------------------------------
static int cmd_rebuild(const Args& a) {
    std::string venue = a.get("venue", "binance");
    SymbolSpec spec = spec_from(a, venue == "binance" ? "BTCUSDT" : "BTC-USD");
    auto files = expand_files(a.pos);
    if (files.empty()) { std::cerr << "no input files\n"; return 2; }
    std::vector<L2Event> ev;
    RebuildReport rep = venue == "binance" ? rebuild_binance(files, spec, ev) : rebuild_coinbase(files, spec, ev);
    Tape t; t.spec = spec; t.venue = venue; t.events = std::move(ev);
    std::string out = a.get("out", "tape.csv");
    write_tape(out, t);
    Json r = rep.to_json(); r["files"] = Json(files); r["tape"] = out;
    if (a.has("report")) write_file(a.get("report"), r.dump(1));
    std::cout << r.dump(1) << std::endl;
    return 0;
}

static int cmd_calibrate(const Args& a) {
    Tape t = read_tape(a.get("tape"));
    CalibOptions o; o.K = (int)a.num("K", 10); o.B = (int)a.num("B", 12); o.dequantize_ns = (Ts)(a.num("dequantize-ms", 100) * 1e6); o.theta = a.num("theta", 0.0);
    o.hawkes_iters = (int)a.num("hawkes-iters", 40);
    Json rep;
    logmsg("decomposing events and fitting intensities / sizes / Hawkes ...");
    MarketModel m = calibrate(t, o, &rep);
    OfiResult ofi = ofi_regression(t, 1.0);
    m.ofi_beta = ofi.beta; m.ofi_r2 = ofi.r2; m.eta = std::max(ofi.eta, 1e-9);
    rep["ofi_beta"] = ofi.beta; rep["ofi_r2"] = ofi.r2; rep["eta"] = m.eta; rep["eta_r2"] = ofi.eta_r2;
    m.save(a.get("out", "calib.json"));
    if (a.has("report")) write_file(a.get("report"), rep.dump(1));
    std::cout << rep.dump(1) << std::endl;
    return 0;
}

static int cmd_synth(const Args& a) {
    MarketModel m = MarketModel::load(a.get("calib"));
    std::string mode = a.get("mode", "qr");
    double secs = a.num("seconds", 600); uint64_t seed = (uint64_t)a.num("seed", 1);
    Ts t0 = (Ts)a.num("start-ts", 1.7e18); Price mid = (Price)a.num("start-mid", 1000000);
    TapeWriter w(a.get("out", "synth.csv"), m.spec, mode == "qr" ? "synthetic_qr" : "synthetic_hawkes");
    std::unique_ptr<GenSim> sim;
    if (mode == "qr") sim = std::make_unique<QRSim>(m, t0, t0 + (Ts)(secs * 1e9), mid, seed);
    else if (mode == "zi") sim = std::make_unique<ZISim>(m, t0, t0 + (Ts)(secs * 1e9), mid, seed);
    else sim = std::make_unique<HawkesSim>(m, t0, t0 + (Ts)(secs * 1e9), mid, seed);
    sim->set_tape_writer(&w);
    // the constructor already emitted the initial book; the writer was attached after, so re-emit it
    for (auto& kv : sim->book().bids()) w.write({t0, 0, EvKind::Level, Side::Bid, kv.first, kv.second});
    for (auto& kv : sim->book().asks()) w.write({t0, 0, EvKind::Level, Side::Ask, kv.first, kv.second});
    while (sim->step()) {}
    logmsg("generated " + std::to_string(sim->generated_events()) + " events, " + std::to_string(w.count()) + " tape rows");
    return 0;
}

static int cmd_validate(const Args& a) {
    Tape live = read_tape(a.get("live"));
    MarketModel m = MarketModel::load(a.get("calib"));
    double secs = a.has("seconds") ? a.num("seconds", 0) : live.seconds();
    uint64_t seed = (uint64_t)a.num("seed", 1);
    std::string outdir = a.get("outdir", "results");
    fs::create_directories(outdir);
    StylisedFacts fl = compute_facts(live); ResponseFunction rl = response_function(live);
    Json out = Json::object();
    out["live"] = Json::object(); out["live"]["facts"] = facts_summary(fl); out["live"]["response"] = rf_json(rl);
    // starting mid = live mid at start
    Book b; Price mid = 0; for (auto& e : live.events) { if (e.kind == EvKind::Level) b.set(e.side, e.price, e.qty); else if (e.kind == EvKind::Snapshot) b.clear(); if (b.valid()) { mid = (Price)std::llround(b.mid()); break; } }
    Json sims = Json::object();
    std::vector<std::string> modes = a.list("modes", {"replay", "queue_reactive", "zero_intelligence", "hawkes"});
    for (const std::string& mode : modes) {
        Tape st;
        if (mode == "replay") st = live;   // replay without our orders is the tape itself (known answer)
        else {
            std::string path = outdir + "/sim_" + mode + ".csv";
            { TapeWriter w(path, m.spec, "synthetic_" + mode);
              std::unique_ptr<GenSim> sim;
              Ts te = live.t0() + (Ts)(secs * 1e9);
              if (mode == "queue_reactive") sim = std::make_unique<QRSim>(m, live.t0(), te, mid, seed);
              else if (mode == "zero_intelligence") sim = std::make_unique<ZISim>(m, live.t0(), te, mid, seed);
              else sim = std::make_unique<HawkesSim>(m, live.t0(), te, mid, seed);
              sim->set_tape_writer(&w);
              for (auto& kv : sim->book().bids()) w.write({live.t0(), 0, EvKind::Level, Side::Bid, kv.first, kv.second});
              for (auto& kv : sim->book().asks()) w.write({live.t0(), 0, EvKind::Level, Side::Ask, kv.first, kv.second});
              while (sim->step()) {}
              logmsg(mode + ": generated " + std::to_string(sim->generated_events()) + " events"); }
            st = read_tape(path);
        }
        StylisedFacts fs_ = compute_facts(st); ResponseFunction rs = response_function(st);
        Json sj = Json::object(); sj["facts"] = facts_summary(fs_); sj["response"] = rf_json(rs); sj["distance_to_live"] = compare_facts(fl, fs_);
        // response function error: mean abs difference over event lags (ticks)
        double err = 0; for (size_t i = 0; i < rl.R.size() && i < rs.R.size(); ++i) err += std::fabs(rl.R[i] - rs.R[i]); sj["response_mean_abs_err_ticks"] = rl.R.empty() ? 0 : err / rl.R.size();
        sims[mode] = sj;
    }
    out["simulators"] = sims;
    std::string op = a.get("out", outdir + "/validation.json");
    write_file(op, out.dump(1));
    std::cout << out.dump(1) << std::endl;
    return 0;
}

static int cmd_fillcurve(const Args& a) {
    Tape t = read_tape(a.get("tape")); MarketModel m = MarketModel::load(a.get("calib"));
    int probes = (int)a.num("probes", 3000); Qty size = (Qty)a.num("size", (double)std::llround(m.aes)); uint64_t seed = (uint64_t)a.num("seed", 3);
    std::vector<int> dist = {0, 1, 2, 3}; std::vector<double> hor = {5, 30, 120};
    Json out = Json::object();
    for (std::string qmn : {"naive_fifo", "power_prob", "risk_averse"}) {
        QueueModel qm; qm.kind = parse_queue_model(qmn);
        FillCurveResult r = fill_curve(t, qm, m, probes, dist, hor, size, seed);
        Json j = r.table; j["n_samples"] = (long long)r.samples.size(); j["logistic_w"] = Json(r.logistic_w);
        j["logistic_features"] = Json(std::vector<std::string>{"1", "level", "log1p(queue_ahead/aes)", "log1p(size/aes)", "imbalance", "sigma_1s", "logmsg(tau)", "spread"});
        // model comparison on the same bins: structural (Lokin-Yu-style), logistic, poisson
        FillModel fs(FillModel::Kind::Structural, &m, qm), fp(FillModel::Kind::Poisson, &m, qm), fl(FillModel::Kind::Logistic, &m, qm); fl.set_logistic_weights(r.logistic_w);
        Json cmp = Json::array();
        for (int d : dist) for (double h : hor) {
            double ps = 0, pp = 0, pl = 0, pe = 0; size_t n = 0;
            for (auto& s : r.samples) { if (s.distance != d || s.tau_s != h) continue; FillFeatures f; f.level = 1 + d; f.queue_ahead = s.queue_ahead; f.our_size = s.our_size; f.imbalance = s.imbalance; f.sigma = s.sigma; f.tau_s = h; f.spread = s.spread; f.aes = m.aes; ps += fs.prob(f); pp += fp.prob(f); pl += fl.prob(f); pe += s.filled; ++n; }
            if (!n) continue;
            Json e = Json::object(); e["distance_ticks"] = d; e["horizon_s"] = h; e["n"] = (long long)n; e["empirical"] = pe / n; e["structural"] = ps / n; e["logistic"] = pl / n; e["poisson"] = pp / n; cmp.push(e);
        }
        j["model_vs_empirical"] = cmp;
        out[qmn] = j;
        logmsg("fill curve " + qmn + ": " + std::to_string(r.samples.size()) + " probes");
    }
    write_file(a.get("out", "results/fillcurve.json"), out.dump(1));
    std::cout << out.dump(1) << std::endl;
    return 0;
}

static int cmd_ofi(const Args& a) {
    Tape t = read_tape(a.get("tape"));
    Json out = Json::object();
    for (double bs : {1.0, 10.0}) {
        OfiResult r = ofi_regression(t, bs);
        Json j = Json::object(); j["bucket_s"] = bs; j["beta_ticks_per_lot"] = r.beta; j["alpha"] = r.alpha; j["r2"] = r.r2; j["n"] = (long long)r.n; j["by_hour_utc"] = r.by_hour; j["by_vol_regime"] = r.by_vol_regime;
        j["eta_ticks_per_lot_per_s"] = r.eta; j["eta_r2"] = r.eta_r2;
        out[bs == 1.0 ? "1s" : "10s"] = j;
    }
    write_file(a.get("out", "results/ofi.json"), out.dump(1));
    std::cout << out.dump(1) << std::endl;
    return 0;
}

static int cmd_markout(const Args& a) {
    Tape t = read_tape(a.get("tape")); MarketModel m = MarketModel::load(a.get("calib"));
    int probes = (int)a.num("probes", 400); double tau = a.num("tau", 30); Qty size = (Qty)a.num("size", (double)std::llround(m.aes)); uint64_t seed = (uint64_t)a.num("seed", 5);
    std::vector<double> hor = {0.1, 1, 10, 60};
    Ts lo = t.t0() + 60 * NS_PER_S, hi = t.t1() - (Ts)((tau + 70) * 1e9);
    Json out = Json::array();
    std::vector<std::string> modes = a.list("modes", {"replay", "queue_reactive", "zero_intelligence"});
    for (const std::string& mode : modes) for (std::string qmn : {"naive_fifo", "power_prob", "risk_averse"}) {
        QueueModel qm; qm.kind = parse_queue_model(qmn);
        auto maker = [&](Ts start, uint64_t s) -> std::unique_ptr<Simulator> {
            Price mid = 0;
            { size_t idx = (size_t)(std::lower_bound(t.events.begin(), t.events.end(), start, [](const L2Event& e, Ts x) { return e.ts_ex < x; }) - t.events.begin());
              ReplaySim pr(&t, idx); mid = pr.book_valid() ? (Price)std::llround(pr.mid()) : 1000000; }
            return make_sim(mode, t, m, std::max(start, t.t0()), mid, s, tau + 70);
        };
        int np = mode == "replay" ? probes : std::max(40, probes / 4);
        MarkoutResult r = probe_markouts(maker, lo, hi, np, tau, size, hor, qm, seed, mode == "replay");
        out.push(markout_json(r));
        logmsg("markout " + mode + "/" + qmn + ": p_fill=" + std::to_string(r.n_probes ? (double)r.n_filled / r.n_probes : 0) + " corr=" + std::to_string(r.corr_fill_return));
    }
    write_file(a.get("out", "results/markout.json"), out.dump(1));
    std::cout << out.dump(1) << std::endl;
    return 0;
}

static int cmd_bench(const Args& a) {
    BenchConfig c;
    c.tape_paths = a.list("tapes", {}); if (c.tape_paths.empty()) c.tape_paths = a.pos;
    c.calib_path = a.get("calib"); c.qty = (Qty)a.num("qty", 0); c.horizon_s = a.num("horizon", 300); c.slices = (int)a.num("slices", 10);
    c.episodes_per_day = (int)a.num("episodes", 6); c.latency_ms = a.num("latency-ms", 50); c.seed = (uint64_t)a.num("seed", 1);
    c.modes = a.list("modes", c.modes); c.queue_models = a.list("queue-models", c.queue_models); c.algos = a.list("algos", c.algos);
    c.latency_sensitivity = !a.has("no-latency-sensitivity");
    c.fill_model_json = a.get("fill-model-json", "");
    std::string fk = a.get("fill-model", "structural");
    c.params.fill_kind = fk == "logistic" ? FillModel::Kind::Logistic : fk == "poisson" ? FillModel::Kind::Poisson : FillModel::Kind::Structural;
    c.params.ac_lambda = a.num("ac-lambda", 2e-6); c.params.ac_kappaT_override = a.num("ac-kappaT", 0); c.params.ofi_tilt = a.num("ofi-tilt", 0.5);
    c.params.ck_lambda_u = a.num("ck-lambda-u", 2.0); c.params.ck_adverse_ticks = a.num("ck-adverse-ticks", 0.0);
    c.maker_bp = a.num("maker-bp", 0.0); c.taker_bp = a.num("taker-bp", 0.0);
    BenchOutput o = run_bench(c, logmsg);
    std::string out = a.get("out", "results/bench.json");
    write_file(out, o.json.dump(1));
    write_file(a.get("md", "results/bench.md"), o.markdown);
    std::cout << o.markdown << std::endl;
    std::cout << "ranking instability: " << o.json["ranking_instability"].dump() << std::endl;
    return 0;
}

// Order-level ground truth: Bitstamp live_orders -> L3 book -> (a) queue-position model errors on real
// orders, (b) observed fill curve of real touch orders vs the replay probes on the derived L2 tape.
static int cmd_l3check(const Args& a) {
    SymbolSpec spec; spec.name = a.get("symbol", "BTCUSD"); spec.tick = a.num("tick", 0.01); spec.lot = a.num("lot", 1e-8);
    auto files = expand_files(a.pos);
    if (files.empty()) { std::cerr << "no input files" << std::endl; return 2; }
    std::vector<L2Event> ev; L3Stats st;
    rebuild_bitstamp_l3(files, spec, ev, st, {5, 30, 120}, (Ts)a.num("debug-dump-ts", 0), (Ts)a.num("since-rx", 0));
    Tape t; t.spec = spec; t.venue = "bitstamp_l3"; t.events = std::move(ev);   // event-by-event: no batch normalisation
    std::string tape_out = a.get("tape-out", "data/tapes/bitstamp_BTCUSD.csv");
    write_tape(tape_out, t);
    Json out = l3_json(st); out["tape"] = tape_out;
    logmsg("L3: " + std::to_string(st.orders_created) + " orders, " + std::to_string(st.orders_tracked) + " tracked at touch, " + std::to_string(t.events.size()) + " tape events");
    // replay probes on the derived L2 tape, same horizons, at the touch, size = median tracked order size proxy (AES)
    CalibOptions o; Json rep; MarketModel m = calibrate(t, o, &rep);
    Json probes = Json::object();
    for (std::string qmn : {"naive_fifo", "power_prob", "risk_averse"}) {
        QueueModel qm; qm.kind = parse_queue_model(qmn);
        FillCurveResult r = fill_curve(t, qm, m, (int)a.num("probes", 3000), {0}, {5, 30, 120}, std::max<Qty>(1, st.median_touch_size), 11);
        probes[qmn] = r.table;
    }
    // power-exponent scan is only meaningful via the tracked-order errors; probes use the three headline models
    out["replay_probe_fill_curve"] = probes; out["aes_lots"] = m.aes;
    write_file(a.get("out", "results/l3check.json"), out.dump(1));
    std::cout << out.dump(1) << std::endl;
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: lobsim <rebuild|calibrate|synth|validate|fillcurve|ofi|markout|bench|l3check> [--key value ...]\n"; return 1; }
    std::string cmd = argv[1];
    Args a = parse_args(argc, argv);
    try {
        if (cmd == "rebuild") return cmd_rebuild(a);
        if (cmd == "calibrate") return cmd_calibrate(a);
        if (cmd == "synth") return cmd_synth(a);
        if (cmd == "validate") return cmd_validate(a);
        if (cmd == "fillcurve") return cmd_fillcurve(a);
        if (cmd == "ofi") return cmd_ofi(a);
        if (cmd == "markout") return cmd_markout(a);
        if (cmd == "bench") return cmd_bench(a);
        if (cmd == "l3check") return cmd_l3check(a);
        std::cerr << "unknown command " << cmd << "\n"; return 1;
    } catch (const std::exception& e) { std::cerr << "error: " << e.what() << "\n"; return 1; }
}
