// Known-answer tests: every check has an answer that can be worked out by hand.
#include "lobsim/replay_sim.hpp"
#include "lobsim/gen_sim.hpp"
#include "lobsim/algos.hpp"
#include "lobsim/bench.hpp"
#include "lobsim/fix.hpp"
#include "lobsim/analysis.hpp"
#include <iostream>
#include <cmath>

using namespace lobsim;
static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if (!(cond)) { ++failures; std::cerr << "FAIL: " << msg << "  [" #cond "]\n"; } } while (0)
#define CHECK_NEAR(a, b, tol, msg) CHECK(std::fabs((double)(a) - (double)(b)) <= (tol), msg << " (" << (a) << " vs " << (b) << ")")

// A flat synthetic tape: constant book, bid 1000 x 500, ask 1001 x 500, levels 5 deep, a trade every second.
static Tape flat_tape(int seconds, Qty depth = 500) {
    Tape t; t.spec.name = "FLAT"; t.spec.tick = 0.01; t.spec.lot = 1; t.venue = "synthetic_flat";
    Ts t0 = 1'700'000'000'000'000'000LL;
    t.events.push_back({t0, 0, EvKind::Snapshot, Side::Bid, 0, 0});
    for (int i = 0; i < 5; ++i) { t.events.push_back({t0, 0, EvKind::Level, Side::Bid, 1000 - i, depth}); t.events.push_back({t0, 0, EvKind::Level, Side::Ask, 1001 + i, depth}); }
    for (int s = 1; s <= seconds; ++s) {
        Ts ts = t0 + s * NS_PER_S;
        t.events.push_back({ts, 0, EvKind::Trade, Side::Ask, 1001, 10});
        t.events.push_back({ts + 1, 0, EvKind::Level, Side::Ask, 1001, depth});   // level refilled
    }
    return t;
}

static void test_flat_book_twap() {
    Tape t = flat_tape(400);
    ReplaySim sim(&t, 0);
    AlgoParams p; SliceScheduler twap(SliceScheduler::Curve::Twap, p);
    ParentOrder po; po.side = Side::Bid; po.qty = 100; po.start = t.t0() + 10 * NS_PER_S; po.end = po.start + 100 * NS_PER_S; po.slices = 10;
    ExecResult r = run_episode(sim, twap, po);
    CHECK(r.filled == 100, "flat TWAP fills the full parent");
    CHECK_NEAR(r.avg_px, 1001.0, 1e-9, "flat TWAP buys at the ask");
    CHECK_NEAR(r.is_ticks, 0.5, 1e-9, "flat-book TWAP shortfall is exactly the half spread");
    CHECK_NEAR(r.timing_ticks, 0.0, 1e-9, "flat-book timing cost is zero");
    CHECK_NEAR(r.impact_ticks, 0.0, 1e-9, "replay impact is identically zero");
    CHECK_NEAR(r.opportunity_ticks, 0.0, 1e-9, "flat-book opportunity cost is zero");
    CHECK_NEAR(r.spread_ticks + r.timing_ticks + r.impact_ticks + r.opportunity_ticks, r.is_ticks, 1e-9, "attribution sums to IS");
    // benchmark against the arrival ask instead of the mid: zero
    CHECK_NEAR(sign(po.side) * (r.avg_px - 1001.0), 0.0, 1e-9, "TWAP vs arrival ask on a flat book = 0 bp");
}

static void test_market_order_walks_book() {
    Tape t; t.spec.lot = 1; Ts t0 = 1'700'000'000'000'000'000LL;
    t.events.push_back({t0, 0, EvKind::Snapshot, Side::Bid, 0, 0});
    t.events.push_back({t0, 0, EvKind::Level, Side::Bid, 999, 100});
    t.events.push_back({t0, 0, EvKind::Level, Side::Ask, 1000, 100});
    t.events.push_back({t0, 0, EvKind::Level, Side::Ask, 1001, 50});
    t.events.push_back({t0, 0, EvKind::Level, Side::Ask, 1005, 1000});
    for (int s = 1; s <= 5; ++s) t.events.push_back({t0 + s * NS_PER_S, 0, EvKind::Level, Side::Bid, 998, 100});
    ReplaySim sim(&t, 5);
    uint64_t id = sim.submit_market(Side::Bid, 120);
    sim.run_until(t0 + 2 * NS_PER_S);
    const Order* o = sim.order(id);
    CHECK(o && o->leaves == 0 && o->status == OrdStatus::Filled, "market order fully filled");
    double notional = 0; Qty q = 0; for (auto& f : sim.all_fills()) { notional += (double)f.price * f.qty; q += f.qty; }
    CHECK(q == 120, "market order quantity");
    CHECK_NEAR(notional / 120.0, (100 * 1000.0 + 20 * 1001.0) / 120.0, 1e-9, "market order average price = (100*1000 + 20*1001)/120");
    // a marketable limit stops at its limit price and rests for the remainder
    ReplaySim sim2(&t, 5);
    uint64_t id2 = sim2.submit_limit(Side::Bid, 1001, 200);
    sim2.run_until(t0 + 2 * NS_PER_S);
    const Order* o2 = sim2.order(id2);
    CHECK(o2 && o2->leaves == 50 && o2->resting, "marketable limit executes 150 and rests 50 at 1001");
}

static void test_replay_reproduces_history() {
    Tape t = flat_tape(50);
    ReplaySim sim(&t, 0);
    Book ref;
    size_t i = 0; bool ok = true;
    while (sim.step()) {
        const L2Event& e = t.events[i++];
        if (e.kind == EvKind::Snapshot) ref.clear(); else if (e.kind == EvKind::Level) ref.set(e.side, e.price, e.qty);
        if (ref.valid() && (ref.best_bid() != sim.best_bid() || ref.best_ask() != sim.best_ask() || ref.best_bid_qty() != sim.best_bid_qty())) ok = false;
    }
    CHECK(ok && i == t.events.size(), "replay without our orders reproduces the tape event by event");
    CHECK(sim.all_fills().empty(), "no orders, no fills");
    // tape round trip is exact
    write_tape("kat_roundtrip.csv", t);
    Tape t2 = read_tape("kat_roundtrip.csv");
    bool same = t2.events.size() == t.events.size();
    for (size_t k = 0; same && k < t.events.size(); ++k) same = t.events[k].ts_ex == t2.events[k].ts_ex && t.events[k].price == t2.events[k].price && t.events[k].qty == t2.events[k].qty && t.events[k].kind == t2.events[k].kind && t.events[k].side == t2.events[k].side;
    CHECK(same, "tape write/read round trip is exact");
    std::remove("kat_roundtrip.csv");
}

static void test_queue_models() {
    QueueModel naive; naive.kind = QueueModelKind::NaiveFifo;
    QueueModel power; power.kind = QueueModelKind::PowerProb; power.n = 3;
    QueueModel risk; risk.kind = QueueModelKind::RiskAverse;
    QueuePos qn, qp, qr;
    naive.new_order(qn, 100); power.new_order(qp, 100); risk.new_order(qr, 100);
    // we join behind 100; 100 more arrive behind us: nothing changes
    naive.depth(qn, 100, 200); power.depth(qp, 100, 200); risk.depth(qr, 100, 200);
    CHECK_NEAR(qn.front, 100, 1e-9, "arrivals behind us do not move us (naive)");
    CHECK_NEAR(qr.front, 100, 1e-9, "arrivals behind us do not move us (risk-averse)");
    // 50 cancelled out of 200 (100 ahead, 100 behind)
    naive.depth(qn, 200, 150); power.depth(qp, 200, 150); risk.depth(qr, 200, 150);
    CHECK_NEAR(qn.front, 75, 1e-9, "naive: cancels uniform along the queue -> half of 50 was ahead");
    CHECK_NEAR(qp.front, 100 - 50 * 0.5, 1e-9, "power n=3 with front==back: prob 1/2, same as naive here");
    CHECK_NEAR(qr.front, 100, 1e-9, "risk-averse: every cancel assumed behind us");
    // level shrinks below our estimated position: all models clamp
    risk.depth(qr, 150, 60);
    CHECK_NEAR(qr.front, 60, 1e-9, "risk-averse clamps to the visible level size");
    // power with more behind than ahead: cancels attributed mostly behind
    QueuePos q2; power.new_order(q2, 10); power.depth(q2, 10, 1010); power.depth(q2, 1010, 910);
    CHECK(q2.front > 9.99, "power n=3: with 1000 behind and 10 ahead a 100-lot cancel barely moves us");
    // trades consume the front; execution when the front is exhausted
    QueuePos q3; naive.new_order(q3, 30); naive.trade(q3, 20); CHECK(naive.executable(q3) == 0, "20 traded of 30 ahead: not yet our turn");
    naive.trade(q3, 25); CHECK(naive.executable(q3) == 15, "45 traded of 30 ahead: 15 lots executed for us");
    // depth update after the trades does not double count
    QueuePos q4; naive.new_order(q4, 30); naive.trade(q4, 20); naive.depth(q4, 30, 10); CHECK_NEAR(q4.front, 10, 1e-9, "trade then matching depth decrease: no double counting");
}

static void test_latency() {
    Tape t = flat_tape(30);
    ReplaySim sim(&t, 12);
    Latency L; L.order_in = 250 * NS_PER_MS; L.report_out = 100 * NS_PER_MS; sim.set_latency(L);
    Ts t_submit = sim.now();
    uint64_t id = sim.submit_market(Side::Bid, 10);
    sim.run_until(t_submit + 3 * NS_PER_S);
    const Order* o = sim.order(id);
    CHECK(o && o->status == OrdStatus::Filled, "order filled after latency");
    CHECK(o->active_ts >= t_submit + L.order_in, "order becomes effective no earlier than submit + order_in");
    CHECK(sim.all_fills()[0].ts >= t_submit + L.order_in, "fill time respects order latency");
}

static void test_generative_determinism_and_attribution() {
    // synthetic model: small book, constant intensities, unit-ish sizes
    MarketModel m; m.spec.lot = 1; m.K = 5; m.B = 12; m.aes = 20; m.sigma_1s = 1;
    m.lam_L.assign(5, std::vector<double>(12, 2.0)); m.lam_C.assign(5, std::vector<double>(12, 1.0)); m.lam_M.assign(12, 1.0); m.lam_L_inside.assign(64, 5.0);
    m.sizes.assign(3, std::vector<std::vector<SizeDist>>(6, std::vector<SizeDist>(12))); m.sizes_pooled.assign(3, std::vector<SizeDist>(6));
    for (auto& t : m.sizes) for (auto& l : t) for (auto& b : l) { b.init(20); for (int k = 0; k < 30; ++k) b.add(10 + k); }
    for (auto& t : m.sizes_pooled) for (auto& l : t) { l.init(20); for (int k = 0; k < 30; ++k) l.add(10 + k); }
    m.stationary.assign(6, SizeDist()); for (auto& d : m.stationary) { d.init(20); for (int k = 0; k < 50; ++k) d.add(50 + k); }
    m.zi_lam_L.assign(5, 2.0); m.zi_c.assign(5, 0.02); m.zi_lam_M = 1.0;
    m.jump_rate.assign(5, 0.0); m.jump_up_prob.assign(5, 0.5); m.jump_size.init(1);
    m.beta = {10, 1}; m.alpha.assign(6, std::vector<std::vector<double>>(6, std::vector<double>(2, 0.01))); for (auto& x : m.mu) x = 1.0;
    m.limit_level_dist = {0.1, 0.5, 0.2, 0.1, 0.05, 0.05}; m.cancel_level_dist = {0, 0.5, 0.2, 0.1, 0.1, 0.1};
    Ts t0 = 1'700'000'000'000'000'000LL;
    for (std::string mode : {"queue_reactive", "zero_intelligence", "hawkes"}) {
        std::string a, b, c;
        for (int rep = 0; rep < 2; ++rep) {
            std::unique_ptr<GenSim> s;
            if (mode == "queue_reactive") s = std::make_unique<QRSim>(m, t0, t0 + 20 * NS_PER_S, 1000, 7);
            else if (mode == "zero_intelligence") s = std::make_unique<ZISim>(m, t0, t0 + 20 * NS_PER_S, 1000, 7);
            else s = std::make_unique<HawkesSim>(m, t0, t0 + 20 * NS_PER_S, 1000, 7);
            std::string key; while (s->step()) key += std::to_string(s->best_bid()) + "," + std::to_string(s->best_ask_qty()) + ";";
            (rep == 0 ? a : b) = key;
        }
        std::unique_ptr<GenSim> s3 = mode == "queue_reactive" ? std::unique_ptr<GenSim>(new QRSim(m, t0, t0 + 20 * NS_PER_S, 1000, 8)) : mode == "zero_intelligence" ? std::unique_ptr<GenSim>(new ZISim(m, t0, t0 + 20 * NS_PER_S, 1000, 8)) : std::unique_ptr<GenSim>(new HawkesSim(m, t0, t0 + 20 * NS_PER_S, 1000, 8));
        while (s3->step()) c += std::to_string(s3->best_bid()) + ";";
        CHECK(a == b && !a.empty(), mode << ": same seed reproduces the same path");
        CHECK(a != c, mode << ": a different seed gives a different path");
    }
    // attribution sums by construction with a counterfactual path (impact != 0 in a reactive sim)
    Tape dummy; dummy.spec.lot = 1;
    QRSim cf(m, t0, t0 + 120 * NS_PER_S, 1000, 3);
    auto path = counterfactual_path(cf, t0 + 100 * NS_PER_S);
    QRSim sim(m, t0, t0 + 120 * NS_PER_S, 1000, 3);
    AlgoParams p; p.model = &m; SliceScheduler twap(SliceScheduler::Curve::Twap, p);
    ParentOrder po; po.side = Side::Ask; po.qty = 300; po.start = t0 + 10 * NS_PER_S; po.end = t0 + 70 * NS_PER_S; po.slices = 6;
    ExecResult r = run_episode(sim, twap, po, &path);
    CHECK(r.filled == 300, "QR TWAP fills the parent");
    CHECK_NEAR(r.spread_ticks + r.timing_ticks + r.impact_ticks + r.opportunity_ticks, r.is_ticks, 1e-6, "QR attribution sums to IS");
}

static void test_ofi_formula() {
    OfiTracker o;
    o.update(0, 100, 10, 101, 10);
    o.update(1, 100, 15, 101, 10);   // bid size up 5: e = +15 - 10 = +5
    CHECK_NEAR(o.sum_last(1, 10), 5, 1e-9, "OFI: bid depth +5");
    o.update(2, 100, 15, 101, 4);    // ask size down 6: e = -4 + 10 = +6
    CHECK_NEAR(o.sum_last(2, 10), 11, 1e-9, "OFI: ask depth -6 adds +6");
    o.update(3, 99, 20, 101, 4);     // bid price down: e = -15 (old bid size removed)
    CHECK_NEAR(o.sum_last(3, 10), -4, 1e-9, "OFI: bid price down removes the old bid size");
}

static void test_fix_roundtrip() {
    fix::Message m; m.set(35, "D"); m.set(11, "O1"); m.set(55, "BTCUSDT"); m.set(54, "1"); m.set(38, 100LL); m.set(40, "2"); m.set(44, "76000.01");
    std::string raw = fix::encode(m, "ALGO", "SIMX", 7, 1'700'000'000'123'000'000LL);
    fix::Message d; CHECK(fix::decode(raw, d), "FIX message decodes with a valid checksum");
    CHECK(d.str(35) == "D" && d.i64(38) == 100 && d.str(44) == "76000.01" && d.i64(34) == 7 && d.str(49) == "ALGO", "FIX fields round-trip");
    raw[raw.size() - 3] = '0'; raw[raw.size() - 2] = '0'; raw[raw.size() - 4] = '0';
    fix::Message bad; CHECK(!fix::decode(raw, bad), "corrupted checksum is rejected");
    CHECK(fix::utc_timestamp(1'700'000'000'123'000'000LL) == "20231114-22:13:20.123", "FIX UTC timestamp");
}

static void test_stats() {
    std::vector<double> a = {1, 2, 3, 4, 5}, b = {1, 2, 3, 4, 5}, c = {6, 7, 8, 9, 10};
    CHECK_NEAR(ks_distance(a, b), 0, 1e-12, "KS of identical samples is 0");
    CHECK_NEAR(ks_distance(a, c), 1, 1e-12, "KS of disjoint samples is 1");
    CHECK_NEAR(wasserstein1(a, c), 5, 1e-9, "W1 of a shifted sample is the shift");
    Ols o = ols({0, 1, 2, 3}, {1, 3, 5, 7}); CHECK_NEAR(o.b, 2, 1e-12, "OLS slope"); CHECK_NEAR(o.a, 1, 1e-12, "OLS intercept"); CHECK_NEAR(o.r2, 1, 1e-12, "OLS r2");
    CI ci = bootstrap_mean_ci({1, 1, 1, 1}); CHECK_NEAR(ci.lo, 1, 1e-12, "bootstrap CI of a constant sample collapses");
    // normalisation of a crossed transient inside a batch
    std::vector<L2Event> ev = {{0, 0, EvKind::Level, Side::Bid, 100, 5}, {0, 0, EvKind::Level, Side::Ask, 101, 5},
                               {1, 0, EvKind::Level, Side::Bid, 101, 3}, {1, 0, EvKind::Level, Side::Ask, 101, 0}, {1, 0, EvKind::Level, Side::Ask, 102, 4}};
    normalize_batches(ev);
    CHECK(ev[2].side == Side::Ask && ev[2].qty == 0, "batch normalisation applies the decrease first");
    Book bk; for (auto& e : ev) bk.set(e.side, e.price, e.qty); CHECK(bk.best_bid() == 101 && bk.best_ask() == 102, "batch normalisation preserves the end state");
}

int main() {
    test_flat_book_twap();
    test_market_order_walks_book();
    test_replay_reproduces_history();
    test_queue_models();
    test_latency();
    test_generative_determinism_and_attribution();
    test_ofi_formula();
    test_fix_roundtrip();
    test_stats();
    std::cout << (failures ? "KNOWN-ANSWER TESTS FAILED: " : "KNOWN-ANSWER TESTS PASSED: ") << (checks - failures) << "/" << checks << " checks\n";
    return failures ? 1 : 0;
}
