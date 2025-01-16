// fixgw - FIX 4.4 gateway in front of the simulator, and the identical-fills self-test.
//
//   fixgw --serve   --tape T --calib C [--mode replay|queue_reactive|zero_intelligence] [--port 9878] [--latency-ms 50] [--queue-model naive]
//   fixgw --selftest --tape T --calib C [--port 9878] [--horizon 120] [--slices 6]
//        runs TWAP, VWAP, AlmgrenChriss and OFIAdaptive_CK both in-process and through a FIX session
//        (gateway thread + client thread on localhost) on the same replay day and asserts that the
//        fill lists (time, price, quantity, passive flag) are identical.
#include "lobsim/fix.hpp"
#include "lobsim/bench.hpp"
#include <thread>
#include <iostream>
#include <map>

using namespace lobsim;

static std::map<std::string, std::string> parse(int argc, char** argv) {
    std::map<std::string, std::string> kv;
    for (int i = 1; i < argc; ++i) { std::string s = argv[i]; if (s.rfind("--", 0) == 0) { std::string k = s.substr(2); if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) kv[k] = argv[++i]; else kv[k] = "1"; } }
    return kv;
}
static double num(const std::map<std::string, std::string>& kv, const std::string& k, double d) { auto it = kv.find(k); return it == kv.end() ? d : std::atof(it->second.c_str()); }
static std::string str(const std::map<std::string, std::string>& kv, const std::string& k, const std::string& d) { auto it = kv.find(k); return it == kv.end() ? d : it->second; }

struct EpisodeSpec { Ts t_start; Price start_mid; Side side; Qty qty; double horizon; int slices; };

static std::unique_ptr<Simulator> build_sim(const std::string& mode, const Tape& tape, const MarketModel& model, const EpisodeSpec& e, double lat_ms, QueueModel qm, uint64_t seed) {
    auto sim = make_sim(mode, tape, model, e.t_start - 60 * NS_PER_S, e.start_mid, seed, e.horizon);
    sim->set_queue_model(qm);
    Latency L; L.order_in = (Ts)(lat_ms * 1e6); L.report_out = (Ts)(lat_ms * 1e6); sim->set_latency(L);
    return sim;
}

static std::string fills_key(const std::vector<Fill>& fs) {
    std::string k; for (auto& f : fs) k += std::to_string(f.ts) + ":" + std::to_string(f.price) + ":" + std::to_string(f.qty) + ":" + (f.passive ? "p" : "a") + ";"; return k;
}

int main(int argc, char** argv) {
    auto kv = parse(argc, argv);
    try {
        Tape tape = read_tape(str(kv, "tape", "")); MarketModel model = MarketModel::load(str(kv, "calib", ""));
        int port = (int)num(kv, "port", 9878); double lat = num(kv, "latency-ms", 50);
        QueueModel qm; qm.kind = parse_queue_model(str(kv, "queue-model", "naive"));
        std::string mode = str(kv, "mode", "replay");
        double horizon = num(kv, "horizon", 120); int slices = (int)num(kv, "slices", 6);
        EpisodeSpec ep; ep.t_start = tape.t0() + 120 * NS_PER_S; ep.horizon = horizon; ep.slices = slices; ep.side = Side::Bid;
        { size_t idx = (size_t)(std::lower_bound(tape.events.begin(), tape.events.end(), ep.t_start, [](const L2Event& e, Ts t) { return e.ts_ex < t; }) - tape.events.begin()); ReplaySim pr(&tape, idx); ep.start_mid = (Price)std::llround(pr.mid()); }
        ep.qty = (Qty)num(kv, "qty", (double)std::llround(10 * model.aes));

        if (kv.count("serve")) {
            auto sim = build_sim(mode, tape, model, ep, lat, qm, 1);
            sock_t ls = fix::listen_on(port);
            std::cerr << "[fixgw] listening on 127.0.0.1:" << port << " mode=" << mode << std::endl;
            sock_t cs = ::accept(ls, nullptr, nullptr);
            fix::Conn conn(cs);
            fix::Gateway gw(*sim, tape.spec);
            gw.serve(conn);
            std::cerr << "[fixgw] session ended" << std::endl;
            return 0;
        }

        // ---- self-test -----------------------------------------------------------------------------
        std::vector<std::string> algos = {"TWAP", "VWAP", "AlmgrenChriss", "OFIAdaptive_CK", "OFIAdaptive_CKadv"};
        int failures = 0;
        for (auto& an : algos) {
            AlgoParams p; p.model = &model; p.qm = qm; p.fill_kind = FillModel::Kind::Structural; p.ck_adverse_ticks = num(kv, "ck-adverse-ticks", 100);
            ParentOrder po; po.side = ep.side; po.qty = ep.qty; po.start = ep.t_start; po.end = ep.t_start + (Ts)(horizon * 1e9); po.slices = slices;
            // in-process
            auto sim_in = build_sim(mode, tape, model, ep, lat, qm, 1);
            auto algo_in = make_algo(an, p);
            ExecResult r_in = run_episode(*sim_in, *algo_in, po);
            // through FIX: gateway thread owns an identical simulator
            auto sim_gw = build_sim(mode, tape, model, ep, lat, qm, 1);
            sock_t ls = fix::listen_on(port);
            std::thread server([&]() { sock_t cs = ::accept(ls, nullptr, nullptr); fix::Conn conn(cs); fix::Gateway gw(*sim_gw, tape.spec); gw.serve(conn, po.end + 30 * NS_PER_S); });
            ExecResult r_fix;
            {
                fix::Conn conn(fix::connect_to(port));
                fix::ClientSim client(conn, tape.spec);
                client.set_latency(sim_in->latency()); client.set_queue_model(qm);
                auto algo_fix = make_algo(an, p);
                r_fix = run_episode(client, *algo_fix, po);
                client.logout();
            }
            server.join(); CLOSESOCK(ls);
            bool same = fills_key(r_in.fills) == fills_key(r_fix.fills) && r_in.filled == r_fix.filled;
            std::printf("%-16s in-process: %3d fills, IS %+8.3f bp | FIX: %3d fills, IS %+8.3f bp | %s\n", an.c_str(), (int)r_in.fills.size(), r_in.is_bp, (int)r_fix.fills.size(), r_fix.is_bp, same ? "IDENTICAL" : "MISMATCH");
            if (!same) ++failures;
        }
        std::printf("%s\n", failures ? "FIX SELF-TEST FAILED" : "FIX SELF-TEST PASSED: identical fills in-process and through FIX 4.4");
        return failures ? 1 : 0;
    } catch (const std::exception& e) { std::cerr << "error: " << e.what() << std::endl; return 1; }
}
