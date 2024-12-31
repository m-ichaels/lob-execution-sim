#pragma once
// Deterministic offline book rebuild from the recorder's raw NDJSON files.
//
// Binance: diff events are chained with the documented U/u rule against the REST snapshot the
// recorder logged; any gap marks the book unsynchronised until the next snapshot line.  Periodic
// `parity` snapshots are checked *exactly*: the book is compared at the update id of the snapshot,
// excluding only the levels touched by the 100 ms diff that straddles that id (those cannot be
// resolved from L2 diffs).  Trades come from the @trade stream with their own timestamps.
// Coinbase: absolute level updates (level2_batch), no sequence chain; the websocket snapshot
// resets the book.  Matches carry the maker side.
//
// Output: one tape (sorted by exchange time) + a JSON operational report (gaps, resyncs,
// disconnects, parity results, receive-vs-exchange clock offsets).
#include "tape.hpp"
#include "book.hpp"
#include "json.hpp"
#include "stats.hpp"
#include <cstdio>
#include <vector>
#include <string>
#include <deque>
#include <functional>

namespace lobsim {

struct RebuildReport {
    std::string venue, symbol;
    size_t lines = 0, ws_msgs = 0, depth_msgs = 0, trade_msgs = 0, snapshots = 0, meta = 0;
    size_t gaps = 0, resyncs = 0, disconnects = 0, dropped_unsynced = 0, bad_lines = 0;
    std::vector<double> drift_depth_ms, drift_trade_ms;
    struct Parity { long long update_id; size_t compared, mismatched, excluded; bool exact_boundary; };
    std::vector<Parity> parity;
    size_t tape_events = 0;
    Ts first_ts = 0, last_ts = 0;
    Json to_json() const {
        Json j = Json::object();
        j["venue"] = venue; j["symbol"] = symbol; j["lines"] = (long long)lines; j["ws_msgs"] = (long long)ws_msgs;
        j["depth_msgs"] = (long long)depth_msgs; j["trade_msgs"] = (long long)trade_msgs; j["snapshots"] = (long long)snapshots;
        j["gaps"] = (long long)gaps; j["resyncs"] = (long long)resyncs; j["disconnects"] = (long long)disconnects;
        j["dropped_unsynced_msgs"] = (long long)dropped_unsynced; j["bad_lines"] = (long long)bad_lines; j["tape_events"] = (long long)tape_events;
        j["first_ts_ns"] = (long long)first_ts; j["last_ts_ns"] = (long long)last_ts; j["seconds"] = (last_ts - first_ts) / 1e9;
        auto drift = [&](const std::vector<double>& v) { Json d = Json::object(); d["n"] = (long long)v.size(); d["p05_ms"] = quantile(v, 0.05); d["p50_ms"] = quantile(v, 0.5); d["p95_ms"] = quantile(v, 0.95); d["mean_ms"] = mean(v); return d; };
        j["recv_minus_exchange_depth"] = drift(drift_depth_ms); j["recv_minus_exchange_trade"] = drift(drift_trade_ms);
        Json p = Json::array(); size_t tot_c = 0, tot_m = 0;
        for (auto& x : parity) { Json e = Json::object(); e["update_id"] = (long long)x.update_id; e["levels_compared"] = (long long)x.compared; e["mismatched"] = (long long)x.mismatched; e["excluded_straddled"] = (long long)x.excluded; e["exact_boundary"] = x.exact_boundary; p.push(e); tot_c += x.compared; tot_m += x.mismatched; }
        j["parity_checks"] = p; j["parity_levels_compared"] = (long long)tot_c; j["parity_levels_mismatched"] = (long long)tot_m;
        return j;
    }
};

namespace detail {
inline void for_each_line(const std::string& path, const std::function<void(const char*, size_t)>& f) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) throw Error("cannot open " + path);
    std::vector<char> buf(1 << 22);
    std::string carry;
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), fp)) > 0) {
        size_t start = 0;
        for (size_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                if (carry.empty()) f(buf.data() + start, i - start);
                else { carry.append(buf.data() + start, i - start); f(carry.data(), carry.size()); carry.clear(); }
                start = i + 1;
            }
        }
        carry.append(buf.data() + start, n - start);
    }
    if (!carry.empty()) f(carry.data(), carry.size());
    std::fclose(fp);
}
// Lines are processed in receive order even if the files are not (e.g. two recorder instances
// appended to the same hourly file for a while): every line is read, keyed by its "rx" field
// (a stable sort keeps the original order for equal keys), then handed to `f`.
inline void for_each_line_by_rx(const std::vector<std::string>& files, const std::function<void(const char*, size_t)>& f, long long since_rx = 0) {
    std::vector<std::string> lines; std::vector<std::pair<long long, size_t>> keys;
    for (auto& file : files) for_each_line(file, [&](const char* p, size_t n) {
        long long rx = 0; const char* q = p; const char* e = p + std::min<size_t>(n, 40);
        while (q < e && !(*q >= '0' && *q <= '9')) ++q;
        while (q < e && *q >= '0' && *q <= '9') { rx = rx * 10 + (*q - '0'); ++q; }
        if (rx < since_rx) return;
        keys.emplace_back(rx, lines.size()); lines.emplace_back(p, n);
    });
    std::stable_sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& k : keys) f(lines[k.second].data(), lines[k.second].size());
}
inline Ts iso_to_ns(const std::string& s) {
    // 2024-12-01T12:34:56.789012Z
    if (s.size() < 19) return 0;
    int Y = std::atoi(s.substr(0, 4).c_str()), M = std::atoi(s.substr(5, 2).c_str()), D = std::atoi(s.substr(8, 2).c_str());
    int h = std::atoi(s.substr(11, 2).c_str()), m = std::atoi(s.substr(14, 2).c_str()), sec = std::atoi(s.substr(17, 2).c_str());
    long long frac = 0; int digits = 0;
    if (s.size() > 20 && s[19] == '.') { for (size_t i = 20; i < s.size() && s[i] >= '0' && s[i] <= '9' && digits < 9; ++i) { frac = frac * 10 + (s[i] - '0'); ++digits; } }
    while (digits < 9) { frac *= 10; ++digits; }
    // days from civil (Howard Hinnant)
    int y = Y - (M <= 2); int era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1; unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = era * 146097LL + (long long)doe - 719468;
    return ((days * 86400 + h * 3600 + m * 60 + sec) * NS_PER_S) + frac;
}
} // namespace detail

inline RebuildReport rebuild_binance(const std::vector<std::string>& files, const SymbolSpec& spec, std::vector<L2Event>& out) {
    RebuildReport rep; rep.venue = "binance"; rep.symbol = spec.name;
    Book book; bool synced = false; long long last_u = -1;
    std::deque<Json> buffer;              // unsynced depth events
    bool have_snap = false; Json snap; Ts snap_rx = 0;
    struct PendingParity { long long L; Json snapshot; };
    std::deque<PendingParity> parity;
    auto emit_level = [&](Ts ts_ex, Ts ts_rx, Side s, const Json& lvl) {
        Price p = spec.to_ticks(std::atof(lvl[0].str().c_str())); Qty q = spec.to_lots(std::atof(lvl[1].str().c_str()));
        book.set(s, p, q);
        out.push_back({ts_ex, ts_rx, EvKind::Level, s, p, q});
    };
    auto apply_depth = [&](const Json& d, Ts rx) {
        Ts E = d["E"].i64() * NS_PER_MS;
        std::vector<std::pair<Side, Price>> touched;
        for (auto& l : d["b"].arr()) { emit_level(E, rx, Side::Bid, l); touched.emplace_back(Side::Bid, spec.to_ticks(std::atof(l[0].str().c_str()))); }
        for (auto& l : d["a"].arr()) { emit_level(E, rx, Side::Ask, l); touched.emplace_back(Side::Ask, spec.to_ticks(std::atof(l[0].str().c_str()))); }
        last_u = d["u"].i64();
        // parity checks that this event reaches
        while (!parity.empty() && parity.front().L <= last_u) {
            PendingParity pp = parity.front(); parity.pop_front();
            bool boundary = pp.L < d["U"].i64();   // L fell between events: exact
            RebuildReport::Parity pr{pp.L, 0, 0, 0, boundary};
            for (Side s : {Side::Bid, Side::Ask}) {
                for (auto& l : pp.snapshot[s == Side::Bid ? "bids" : "asks"].arr()) {
                    Price p = spec.to_ticks(std::atof(l[0].str().c_str())); Qty q = spec.to_lots(std::atof(l[1].str().c_str()));
                    bool ex = false; if (!boundary) for (auto& t : touched) if (t.first == s && t.second == p) { ex = true; break; }
                    if (ex) { ++pr.excluded; continue; }
                    ++pr.compared; if (book.qty_at(s, p) != q) ++pr.mismatched;
                }
            }
            rep.parity.push_back(pr);
        }
    };
    auto try_sync = [&]() {
        if (!have_snap) return;
        long long L = snap["lastUpdateId"].i64();
        while (!buffer.empty() && buffer.front()["u"].i64() <= L) buffer.pop_front();
        if (buffer.empty()) return;
        const Json& first = buffer.front();
        if (!(first["U"].i64() <= L + 1 && L + 1 <= first["u"].i64())) { have_snap = false; buffer.clear(); return; }   // stale snapshot; wait for the next one
        book.clear();
        Ts E0 = first["E"].i64() * NS_PER_MS - 1;
        out.push_back({E0, snap_rx, EvKind::Snapshot, Side::Bid, 0, 0});
        for (auto& l : snap["bids"].arr()) emit_level(E0, snap_rx, Side::Bid, l);
        for (auto& l : snap["asks"].arr()) emit_level(E0, snap_rx, Side::Ask, l);
        last_u = L; synced = true; ++rep.resyncs; have_snap = false;
        while (!buffer.empty()) {
            Json d = buffer.front(); buffer.pop_front();
            if (d["U"].i64() != last_u + 1 && d["U"].i64() > last_u + 1) { ++rep.gaps; synced = false; buffer.clear(); return; }
            apply_depth(d, d["_rx"].i64());
        }
    };
    {
        detail::for_each_line_by_rx(files, [&](const char* p, size_t n) {
          try {
            ++rep.lines;
            Json line = Json::parse(p, p + n);
            if (!line.is_object()) { ++rep.bad_lines; return; }
            const std::string& kind = line["kind"].str();
            Ts rx = line["rx"].i64();
            if (kind == "meta") {
                ++rep.meta;
                const std::string ev = line["msg"].get("event", std::string());
                if (ev == "disconnect" || ev == "recv_timeout") { ++rep.disconnects; synced = false; buffer.clear(); }
                return;
            }
            if (kind == "snapshot") { ++rep.snapshots; snap = line["msg"]; snap_rx = rx; have_snap = true; try_sync(); return; }
            if (kind == "parity") { parity.push_back({line["msg"]["snapshot"]["lastUpdateId"].i64(), line["msg"]["snapshot"]}); return; }
            if (kind != "ws") return;
            ++rep.ws_msgs;
            const Json& msg = line["msg"];
            const std::string stream = msg.get("stream", std::string());
            const Json& d = msg["data"];
            if (stream.find("@depth") != std::string::npos) {
                ++rep.depth_msgs;
                rep.drift_depth_ms.push_back(rx / 1e6 - (double)d["E"].i64());
                if (!synced) {
                    Json dd = d; dd["_rx"] = (long long)rx; buffer.push_back(dd); ++rep.dropped_unsynced; try_sync(); return;
                }
                if (d["U"].i64() != last_u + 1) { ++rep.gaps; synced = false; buffer.clear(); Json dd = d; dd["_rx"] = (long long)rx; buffer.push_back(dd); return; }
                apply_depth(d, rx);
            } else if (stream.find("@trade") != std::string::npos) {
                ++rep.trade_msgs;
                Ts T = d["T"].i64() * NS_PER_MS;
                rep.drift_trade_ms.push_back(rx / 1e6 - (double)d["E"].i64());
                if (!synced) return;
                Side passive = d["m"].boolean() ? Side::Bid : Side::Ask;
                out.push_back({T, rx, EvKind::Trade, passive, spec.to_ticks(std::atof(d["p"].str().c_str())), spec.to_lots(std::atof(d["q"].str().c_str()))});
            }
          } catch (const std::exception&) { ++rep.bad_lines; }
        });
    }
    std::stable_sort(out.begin(), out.end(), [](const L2Event& a, const L2Event& b) { return a.ts_ex < b.ts_ex; });
    normalize_batches(out);
    rep.tape_events = out.size();
    if (!out.empty()) { rep.first_ts = out.front().ts_ex; rep.last_ts = out.back().ts_ex; }
    return rep;
}

inline RebuildReport rebuild_coinbase(const std::vector<std::string>& files, const SymbolSpec& spec, std::vector<L2Event>& out) {
    RebuildReport rep; rep.venue = "coinbase"; rep.symbol = spec.name;
    bool synced = false; bool have_pending = false; Json pending_snap; Ts pending_snap_rx = 0;
    {
        detail::for_each_line_by_rx(files, [&](const char* p, size_t n) {
          try {
            ++rep.lines;
            Json line = Json::parse(p, p + n);
            if (!line.is_object()) { ++rep.bad_lines; return; }
            const std::string& kind = line["kind"].str();
            Ts rx = line["rx"].i64();
            const Json& msg = line["msg"];
            if (kind == "meta") { ++rep.meta; const std::string ev = msg.get("event", std::string()); if (ev == "disconnect" || ev == "recv_timeout") { ++rep.disconnects; synced = false; } return; }
            if (kind == "snapshot") {
                // the ws snapshot has no exchange time: hold it and stamp it 1 ns before the first l2update
                ++rep.snapshots; ++rep.resyncs;
                pending_snap = msg; pending_snap_rx = rx; have_pending = true; synced = false; return;
            }
            if (have_pending && msg.get("type", std::string()) == "l2update") {
                Ts t0 = detail::iso_to_ns(msg["time"].str()) - 1;
                out.push_back({t0, pending_snap_rx, EvKind::Snapshot, Side::Bid, 0, 0});
                for (auto& l : pending_snap["bids"].arr()) out.push_back({t0, pending_snap_rx, EvKind::Level, Side::Bid, spec.to_ticks(std::atof(l[0].str().c_str())), spec.to_lots(std::atof(l[1].str().c_str()))});
                for (auto& l : pending_snap["asks"].arr()) out.push_back({t0, pending_snap_rx, EvKind::Level, Side::Ask, spec.to_ticks(std::atof(l[0].str().c_str())), spec.to_lots(std::atof(l[1].str().c_str()))});
                have_pending = false; synced = true;
            }
            if (kind != "ws") return;
            ++rep.ws_msgs;
            const std::string type = msg.get("type", std::string());
            if (type == "l2update") {
                ++rep.depth_msgs;
                Ts t = detail::iso_to_ns(msg["time"].str());
                rep.drift_depth_ms.push_back((rx - t) / 1e6);
                if (!synced) { ++rep.dropped_unsynced; return; }
                for (auto& c : msg["changes"].arr()) {
                    Side s = c[0].str() == "buy" ? Side::Bid : Side::Ask;
                    out.push_back({t, rx, EvKind::Level, s, spec.to_ticks(std::atof(c[1].str().c_str())), spec.to_lots(std::atof(c[2].str().c_str()))});
                }
            } else if (type == "match" || type == "last_match") {
                ++rep.trade_msgs;
                Ts t = detail::iso_to_ns(msg["time"].str());
                rep.drift_trade_ms.push_back((rx - t) / 1e6);
                if (!synced) return;
                Side passive = msg["side"].str() == "sell" ? Side::Ask : Side::Bid;   // `side` is the maker side
                out.push_back({t, rx, EvKind::Trade, passive, spec.to_ticks(std::atof(msg["price"].str().c_str())), spec.to_lots(std::atof(msg["size"].str().c_str()))});
            }
          } catch (const std::exception&) { ++rep.bad_lines; }
        });
    }
    std::stable_sort(out.begin(), out.end(), [](const L2Event& a, const L2Event& b) { return a.ts_ex < b.ts_ex; });
    normalize_batches(out);
    rep.tape_events = out.size();
    if (!out.empty()) { rep.first_ts = out.front().ts_ex; rep.last_ts = out.back().ts_ex; }
    return rep;
}

} // namespace lobsim
