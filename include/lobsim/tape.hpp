#pragma once
// Event tape: the single canonical format shared by the rebuilt live data, every simulator's
// output, the validation tools, DuckDB and the kdb+ loader.
//
//   # lobsim-tape v1 symbol=BTCUSDT tick=0.01 lot=0.00001 venue=binance
//   ts_ex,ts_rx,kind,side,price,qty
//   1734000000123000000,1734000000310000000,L,b,9876543,120000
//
// kind: L level (absolute qty), T trade (side = passive side hit), S snapshot/reset marker.
// price in ticks, qty in lots (integers), so the file is exact and diff-able.
#include "types.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>

namespace lobsim {

struct Tape {
    SymbolSpec spec;
    std::string venue = "synthetic";
    std::vector<L2Event> events;

    Ts t0() const { return events.empty() ? 0 : events.front().ts_ex; }
    Ts t1() const { return events.empty() ? 0 : events.back().ts_ex; }
    double seconds() const { return (t1() - t0()) / 1e9; }
};

class TapeWriter {
public:
    TapeWriter(const std::string& path, const SymbolSpec& spec, const std::string& venue) {
        f_ = std::fopen(path.c_str(), "wb");
        if (!f_) throw Error("cannot write tape " + path);
        std::fprintf(f_, "# lobsim-tape v1 symbol=%s tick=%.10g lot=%.10g venue=%s\n",
                     spec.name.c_str(), spec.tick, spec.lot, venue.c_str());
        std::fprintf(f_, "ts_ex,ts_rx,kind,side,price,qty\n");
    }
    ~TapeWriter() { if (f_) std::fclose(f_); }
    TapeWriter(const TapeWriter&) = delete;
    TapeWriter& operator=(const TapeWriter&) = delete;

    void write(const L2Event& e) {
        char k = e.kind == EvKind::Level ? 'L' : e.kind == EvKind::Trade ? 'T' : 'S';
        std::fprintf(f_, "%lld,%lld,%c,%c,%lld,%lld\n", (long long)e.ts_ex, (long long)e.ts_rx, k,
                     e.side == Side::Bid ? 'b' : 'a', (long long)e.price, (long long)e.qty);
        ++n_;
    }
    size_t count() const { return n_; }
    void close() { if (f_) { std::fclose(f_); f_ = nullptr; } }
private:
    FILE* f_ = nullptr;
    size_t n_ = 0;
};

namespace detail {
inline long long parse_ll(const char*& p) {
    bool neg = false;
    if (*p == '-') { neg = true; ++p; }
    long long v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
    return neg ? -v : v;
}
inline std::string header_field(const std::string& line, const std::string& key) {
    auto pos = line.find(key + "=");
    if (pos == std::string::npos) return "";
    pos += key.size() + 1;
    auto end = line.find(' ', pos);
    return line.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}
} // namespace detail

inline Tape read_tape(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw Error("cannot open tape " + path);
    Tape t;
    std::vector<char> buf(1 << 20);
    std::string line;
    bool first = true;
    int lineno = 0;
    while (std::fgets(buf.data(), (int)buf.size(), f)) {
        ++lineno;
        const char* p = buf.data();
        if (first) {
            first = false;
            std::string h(p);
            if (h.rfind("# lobsim-tape", 0) != 0) { std::fclose(f); throw Error("not a lobsim tape: " + path); }
            t.spec.name = detail::header_field(h, "symbol");
            t.spec.tick = std::atof(detail::header_field(h, "tick").c_str());
            t.spec.lot  = std::atof(detail::header_field(h, "lot").c_str());
            std::string v = detail::header_field(h, "venue");
            while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
            if (!v.empty()) t.venue = v;
            continue;
        }
        if (*p == 't' || *p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue; // column header / comments
        L2Event e{};
        e.ts_ex = detail::parse_ll(p); if (*p != ',') throw Error("bad tape line " + std::to_string(lineno)); ++p;
        e.ts_rx = detail::parse_ll(p); ++p;
        e.kind = *p == 'L' ? EvKind::Level : *p == 'T' ? EvKind::Trade : EvKind::Snapshot; ++p; ++p;
        e.side = *p == 'b' ? Side::Bid : Side::Ask; ++p; ++p;
        e.price = detail::parse_ll(p); ++p;
        e.qty = detail::parse_ll(p);
        t.events.push_back(e);
    }
    std::fclose(f);
    return t;
}

// Exchange diff batches (Binance @depth@100ms, Coinbase level2_batch) carry several level updates
// with one timestamp and no intra-batch order.  Applying an increase before a decrease can make the
// book look crossed for an instant (a new best bid recorded before the old best ask is removed).
// This reorders every same-timestamp group of Level events as: decreases first, then increases.
// The end state of each batch is unchanged; only the transient path is made non-crossing.
inline void normalize_batches(std::vector<L2Event>& ev) {
    std::map<std::pair<int, Price>, Qty> book;   // (side, price) -> qty
    size_t i = 0;
    while (i < ev.size()) {
        size_t j = i;
        while (j < ev.size() && ev[j].ts_ex == ev[i].ts_ex) ++j;
        std::vector<L2Event> dec, inc, other;
        for (size_t k = i; k < j; ++k) {
            const L2Event& e = ev[k];
            if (e.kind != EvKind::Level) { other.push_back(e); continue; }
            Qty cur = 0; auto it = book.find({(int)e.side, e.price}); if (it != book.end()) cur = it->second;
            (e.qty < cur ? dec : inc).push_back(e);
        }
        size_t k = i;
        for (auto& e : other) if (e.kind == EvKind::Snapshot) { ev[k++] = e; book.clear(); }
        for (auto& e : other) if (e.kind != EvKind::Snapshot) ev[k++] = e;
        for (auto& e : dec) { ev[k++] = e; if (e.qty <= 0) book.erase({(int)e.side, e.price}); else book[{(int)e.side, e.price}] = e.qty; }
        for (auto& e : inc) { ev[k++] = e; book[{(int)e.side, e.price}] = e.qty; }
        i = j;
    }
}

// Iterate over same-timestamp batches: f(first_index, end_index).
template <class F> inline void for_each_batch(const std::vector<L2Event>& ev, F&& f) {
    size_t i = 0;
    while (i < ev.size()) { size_t j = i; while (j < ev.size() && ev[j].ts_ex == ev[i].ts_ex) ++j; f(i, j); i = j; }
}

inline void write_tape(const std::string& path, const Tape& t) {
    TapeWriter w(path, t.spec, t.venue);
    for (auto& e : t.events) w.write(e);
}

} // namespace lobsim
