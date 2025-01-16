#pragma once
// Minimal FIX 4.4 session layer (tag=value, SOH, BodyLength, CheckSum, sequence numbers, Logon /
// Heartbeat / TestRequest / Logout) plus the three application messages this project needs:
// NewOrderSingle (D), OrderCancelRequest (F), ExecutionReport (8), and MarketDataSnapshotFullRefresh
// (W) for the gateway to publish the simulated book.  Self-contained (no QuickFIX dependency); the
// wire format is standard FIX 4.4 so a QuickFIX initiator can talk to the gateway.
//
// Lockstep protocol (deterministic by construction):
//   gateway: after every simulator event, send W (top of book, 60=sim time, 262=event seq) and any
//            ExecutionReports that became visible; then wait for the client's Heartbeat 112=<seq>.
//   client : process W / 8, let the algorithm act (D / F go out immediately at that sim time),
//            then acknowledge with Heartbeat 112=<seq>.
// The algorithm therefore sees exactly the same sequence of (event, fills) as in-process, so fills
// must be identical - which fixgw --selftest checks.
#include "simulator.hpp"
#include <string>
#include <map>
#include <vector>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <deque>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define CLOSESOCK close
#endif

namespace lobsim {
namespace fix {

constexpr char SOH = '\x01';

struct Message {
    std::vector<std::pair<int, std::string>> fields;   // in order
    void set(int tag, const std::string& v) { for (auto& f : fields) if (f.first == tag) { f.second = v; return; } fields.emplace_back(tag, v); }
    void set(int tag, long long v) { set(tag, std::to_string(v)); }
    const std::string* get(int tag) const { for (auto& f : fields) if (f.first == tag) return &f.second; return nullptr; }
    std::string str(int tag) const { auto* p = get(tag); return p ? *p : ""; }
    long long i64(int tag) const { auto* p = get(tag); return p ? std::atoll(p->c_str()) : 0; }
    double num(int tag) const { auto* p = get(tag); return p ? std::atof(p->c_str()) : 0; }
    std::string type() const { return str(35); }
};

inline std::string utc_timestamp(Ts ns) {
    long long s = ns / NS_PER_S; int ms = (int)((ns / NS_PER_MS) % 1000);
    long long days = s / 86400, rem = s % 86400;
    // civil from days (Howard Hinnant)
    long long z = days + 719468; long long era = (z >= 0 ? z : z - 146096) / 146097; unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; long long y = (long long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100); unsigned mp = (5 * doy + 2) / 153; unsigned d = doy - (153 * mp + 2) / 5 + 1; unsigned m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
    char buf[40]; std::snprintf(buf, sizeof buf, "%04lld%02u%02u-%02lld:%02lld:%02lld.%03d", y, m, d, rem / 3600, (rem % 3600) / 60, rem % 60, ms);
    return buf;
}

// Serialise with header (8, 9), body, trailer (10).
inline std::string encode(const Message& body, const std::string& sender, const std::string& target, long long seq, Ts now_ns) {
    std::string b;
    b += "35=" + body.type() + SOH;
    b += "49=" + sender + SOH + "56=" + target + SOH + "34=" + std::to_string(seq) + SOH + "52=" + utc_timestamp(now_ns) + SOH;
    for (auto& f : body.fields) if (f.first != 35) b += std::to_string(f.first) + "=" + f.second + SOH;
    std::string m = std::string("8=FIX.4.4") + SOH + "9=" + std::to_string(b.size()) + SOH + b;
    unsigned sum = 0; for (unsigned char c : m) sum += c;
    char cs[8]; std::snprintf(cs, sizeof cs, "%03u", sum % 256);
    m += std::string("10=") + cs + SOH;
    return m;
}

// Parse one complete message (validates BodyLength and CheckSum).
inline bool decode(const std::string& raw, Message& out) {
    out.fields.clear();
    size_t i = 0;
    while (i < raw.size()) {
        size_t eq = raw.find('=', i); if (eq == std::string::npos) return false;
        size_t soh = raw.find(SOH, eq); if (soh == std::string::npos) return false;
        out.fields.emplace_back(std::atoi(raw.substr(i, eq - i).c_str()), raw.substr(eq + 1, soh - eq - 1));
        i = soh + 1;
    }
    auto* cs = out.get(10); if (!cs) return false;
    size_t tail = raw.rfind("10="); unsigned sum = 0; for (size_t k = 0; k < tail; ++k) sum += (unsigned char)raw[k];
    return std::atoi(cs->c_str()) == (int)(sum % 256);
}

// Blocking TCP framing of FIX messages.
class Conn {
public:
    explicit Conn(sock_t s) : s_(s) {}
    ~Conn() { if (s_ != INVALID_SOCKET) CLOSESOCK(s_); }
    bool send_raw(const std::string& m) { size_t off = 0; while (off < m.size()) { int n = ::send(s_, m.data() + off, (int)(m.size() - off), 0); if (n <= 0) return false; off += (size_t)n; } return true; }
    // Returns false on disconnect.
    bool recv_msg(std::string& out) {
        while (true) {
            // a message ends with "10=xxx<SOH>"
            size_t p = buf_.find("\x01" "10=");
            if (p != std::string::npos) { size_t e = buf_.find(SOH, p + 1); if (e != std::string::npos) { out = buf_.substr(0, e + 1); buf_.erase(0, e + 1); return true; } }
            char tmp[65536]; int n = ::recv(s_, tmp, sizeof tmp, 0);
            if (n <= 0) return false;
            buf_.append(tmp, (size_t)n);
        }
    }
    sock_t raw() const { return s_; }
private:
    sock_t s_; std::string buf_;
};

inline void net_init() {
#ifdef _WIN32
    static bool done = false; if (!done) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); done = true; }
#endif
}
inline sock_t listen_on(int port) {
    net_init();
    sock_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((unsigned short)port);
    if (::bind(s, (sockaddr*)&a, sizeof a) != 0) throw Error("bind failed on port " + std::to_string(port));
    if (::listen(s, 1) != 0) throw Error("listen failed");
    return s;
}
inline sock_t connect_to(int port) {
    net_init();
    sock_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((unsigned short)port);
    for (int tries = 0; tries < 50; ++tries) { if (::connect(s, (sockaddr*)&a, sizeof a) == 0) return s;
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000);
#endif
    }
    throw Error("connect failed");
}

// Session bookkeeping shared by both ends.
struct Session {
    std::string sender, target; long long out_seq = 0, in_seq = 0;
    Conn* conn = nullptr;
    bool send(const Message& m, Ts now_ns) { return conn->send_raw(encode(m, sender, target, ++out_seq, now_ns)); }
    bool recv(Message& m) {
        std::string raw; if (!conn->recv_msg(raw)) return false;
        if (!decode(raw, m)) throw Error("bad FIX message");
        long long seq = m.i64(34); if (seq != in_seq + 1) throw Error("FIX sequence gap: expected " + std::to_string(in_seq + 1) + " got " + std::to_string(seq));
        in_seq = seq; return true;
    }
};

// ---- gateway: wraps a Simulator behind FIX ---------------------------------------------------------
class Gateway {
public:
    Gateway(Simulator& sim, const SymbolSpec& spec) : sim_(sim), spec_(spec) {}
    // Serve one session until the tape / horizon ends or the client logs out.
    void serve(Conn& conn, Ts t_end = INT64_MAX) {
        Session s; s.sender = "SIMX"; s.target = "ALGO"; s.conn = &conn;
        Message m; if (!s.recv(m) || m.type() != "A") throw Error("expected Logon");
        Message logon; logon.set(35, "A"); logon.set(98, 0LL); logon.set(108, 30LL); s.send(logon, sim_.now());
        long long ev = 0;
        while (sim_.now() < t_end && sim_.step()) {
            ++ev;
            // order-state transitions are visible in-process as soon as they happen (the algorithm reads
            // Order::status / leaves directly); fills become visible after the report latency.
            for (auto& kv : ids_) {
                const Order* o = sim_.order(kv.first); if (!o) continue;
                auto& last = reported_[kv.first];
                if (last.first != (int)o->status || last.second != o->leaves) { last = {(int)o->status, o->leaves}; send_state(s, *o, kv.second); }
            }
            for (auto& f : sim_.take_visible_fills()) send_er(s, f);
            send_md(s, ev);
            // wait for the client's acknowledgement, applying orders meanwhile
            while (true) {
                if (!s.recv(m)) return;
                const std::string t = m.type();
                if (t == "0" && m.i64(112) == ev) break;
                if (t == "5") { Message lo; lo.set(35, "5"); s.send(lo, sim_.now()); return; }
                if (t == "1") { Message hb; hb.set(35, "0"); hb.set(112, m.str(112)); s.send(hb, sim_.now()); continue; }
                if (t == "D") {
                    Side side = m.i64(54) == 1 ? Side::Bid : Side::Ask; Qty q = m.i64(38);
                    uint64_t id = m.str(40) == "1" ? sim_.submit_market(side, q) : sim_.submit_limit(side, spec_.to_ticks(m.num(44)), q);
                    clord_[m.str(11)] = id; ids_[id] = m.str(11); reported_[id] = {(int)OrdStatus::PendingNew, q};
                    Message er; er.set(35, "8"); er.set(37, (long long)id); er.set(11, m.str(11)); er.set(17, (long long)++exec_id_); er.set(150, "0"); er.set(39, "0");
                    er.set(55, spec_.name); er.set(54, m.str(54)); er.set(38, q); er.set(151, q); er.set(14, 0LL); er.set(6, 0LL); er.set(60, utc_timestamp(sim_.now()));
                    s.send(er, sim_.now());
                } else if (t == "G") {   // cancel/replace: new id, loses priority
                    auto it = clord_.find(m.str(41));
                    if (it != clord_.end()) {
                        uint64_t nid = sim_.replace(it->second, m.get(44) ? spec_.to_ticks(m.num(44)) : 0, m.i64(38));
                        clord_[m.str(11)] = nid; ids_[nid] = m.str(11); reported_[nid] = {(int)OrdStatus::PendingNew, m.i64(38)};
                        Message er; er.set(35, "8"); er.set(37, (long long)nid); er.set(11, m.str(11)); er.set(41, m.str(41)); er.set(17, (long long)++exec_id_); er.set(150, "5"); er.set(39, "0"); er.set(55, spec_.name); er.set(38, m.i64(38)); er.set(60, utc_timestamp(sim_.now()));
                        s.send(er, sim_.now());
                    }
                } else if (t == "F") {
                    auto it = clord_.find(m.str(41));
                    if (it != clord_.end()) { sim_.cancel(it->second); Message er; er.set(35, "8"); er.set(37, (long long)it->second); er.set(11, m.str(11)); er.set(41, m.str(41)); er.set(17, (long long)++exec_id_); er.set(150, "6"); er.set(39, "6"); er.set(55, spec_.name); er.set(60, utc_timestamp(sim_.now())); s.send(er, sim_.now()); }
                }
            }
        }
        Message lo; lo.set(35, "5"); lo.set(58, "END"); s.send(lo, sim_.now());
    }
private:
    void send_md(Session& s, long long ev) {
        Message w; w.set(35, "W"); w.set(55, spec_.name); w.set(262, ev); w.set(60, utc_timestamp(sim_.now())); w.set(20001, (long long)sim_.now());
        if (sim_.book_valid()) {
            w.set(268, 2LL);
            w.set(20002, (long long)sim_.best_bid()); w.set(20003, (long long)sim_.best_bid_qty()); w.set(20004, (long long)sim_.best_ask()); w.set(20005, (long long)sim_.best_ask_qty());
            w.set(20006, (long long)sim_.depth(Side::Bid, 5)); w.set(20007, (long long)sim_.depth(Side::Ask, 5));
            w.set(20008, (long long)sim_.qty_at(Side::Bid, sim_.best_bid())); w.set(20009, (long long)sim_.qty_at(Side::Ask, sim_.best_ask()));   // others only
        } else w.set(268, 0LL);
        s.send(w, sim_.now());
    }
    // 150=I: order status snapshot (39 = our OrdStatus enum value, 151 = leaves, 20014 = resting flag)
    void send_state(Session& s, const Order& o, const std::string& cl) {
        Message er; er.set(35, "8"); er.set(37, (long long)o.id); er.set(11, cl); er.set(17, (long long)++exec_id_); er.set(150, "I");
        er.set(39, (long long)o.status); er.set(151, o.leaves); er.set(20014, o.resting ? 1LL : 0LL); er.set(55, spec_.name); er.set(60, utc_timestamp(sim_.now()));
        s.send(er, sim_.now());
    }
    void send_er(Session& s, const Fill& f) {
        const Order* o = sim_.order(f.order_id);
        Message er; er.set(35, "8"); er.set(37, (long long)f.order_id); er.set(11, ids_.count(f.order_id) ? ids_[f.order_id] : std::to_string(f.order_id)); er.set(17, (long long)++exec_id_);
        er.set(150, "F"); er.set(39, o && o->leaves > 0 ? "1" : "2"); er.set(55, spec_.name); er.set(54, o && o->side == Side::Bid ? "1" : "2");
        er.set(32, f.qty); er.set(31, spec_.px(f.price)); er.set(20011, (long long)f.price); er.set(151, o ? o->leaves : 0LL); er.set(60, utc_timestamp(f.ts)); er.set(20001, (long long)f.ts);
        er.set(20012, f.passive ? 1LL : 0LL); er.set(20013, f.mid_at_fill);
        s.send(er, sim_.now());
    }
    Simulator& sim_; SymbolSpec spec_;
    std::map<std::string, uint64_t> clord_; std::map<uint64_t, std::string> ids_;
    std::map<uint64_t, std::pair<int, Qty>> reported_;
    long long exec_id_ = 0;
};

// ---- client: a Simulator facade driven by the gateway ------------------------------------------------
class ClientSim : public Simulator {
public:
    ClientSim(Conn& conn, const SymbolSpec& spec) : spec_(spec) {
        s_.sender = "ALGO"; s_.target = "SIMX"; s_.conn = &conn;
        Message logon; logon.set(35, "A"); logon.set(98, 0LL); logon.set(108, 30LL); s_.send(logon, 0);
        Message m; if (!s_.recv(m) || m.type() != "A") throw Error("logon rejected");
    }
    std::string mode() const override { return "fix_client"; }
    Price best_bid() const override { return bb_; }
    Price best_ask() const override { return ba_; }
    Qty best_bid_qty() const override { return qb_; }
    Qty best_ask_qty() const override { return qa_; }
    Qty qty_at(Side s, Price p) const override { return (s == Side::Bid && p == bb_) ? ob_ : (s == Side::Ask && p == ba_) ? oa_ : 0; }
    Qty depth(Side s, int) const override { return s == Side::Bid ? db_ : da_; }
    bool book_valid() const override { return valid_; }
    Ts start_ts() const override { return 0; }
    Ts end_ts() const override { return INT64_MAX; }

    bool step() override {
        if (ended_) return false;
        if (ev_ > 0) { Message hb; hb.set(35, "0"); hb.set(112, ev_); s_.send(hb, now_); }
        Message m;
        while (true) {
            if (!s_.recv(m)) { ended_ = true; return false; }
            const std::string t = m.type();
            if (t == "W") {
                ev_ = m.i64(262); now_ = m.i64(20001);
                valid_ = m.i64(268) > 0;
                if (valid_) { bb_ = m.i64(20002); qb_ = m.i64(20003); ba_ = m.i64(20004); qa_ = m.i64(20005); db_ = m.i64(20006); da_ = m.i64(20007); ob_ = m.i64(20008); oa_ = m.i64(20009); }
                return true;
            }
            if (t == "8") on_er(m);
            else if (t == "5") { ended_ = true; return false; }
            else if (t == "1") { Message hb; hb.set(35, "0"); hb.set(112, m.str(112)); s_.send(hb, now_); }
        }
    }
    uint64_t submit_limit(Side s, Price p, Qty q) override { return send_new(s, OrdType::Limit, p, q); }
    uint64_t submit_market(Side s, Qty q) override { return send_new(s, OrdType::Market, 0, q); }
    void cancel(uint64_t id) override {
        auto it = orders_.find(id); if (it == orders_.end()) return;
        Message f; f.set(35, "F"); f.set(41, clord_of_[id]); f.set(11, "C" + std::to_string(++next_)); f.set(55, spec_.name); f.set(54, it->second.side == Side::Bid ? "1" : "2"); f.set(60, utc_timestamp(now_));
        s_.send(f, now_);
    }
    uint64_t replace(uint64_t id, Price new_price, Qty new_qty) override {
        auto it = orders_.find(id); if (it == orders_.end()) return 0;
        std::string cl = "R" + std::to_string(++next_);
        Message g; g.set(35, "G"); g.set(41, clord_of_[id]); g.set(11, cl); g.set(55, spec_.name); g.set(54, it->second.side == Side::Bid ? "1" : "2"); g.set(38, new_qty);
        g.set(40, it->second.type == OrdType::Market ? "1" : "2"); if (it->second.type == OrdType::Limit) { char b[32]; std::snprintf(b, sizeof b, "%.8f", spec_.px(new_price)); g.set(44, b); }
        g.set(60, utc_timestamp(now_)); s_.send(g, now_);
        Message m;
        while (true) { if (!s_.recv(m)) throw Error("disconnected while awaiting replace ack"); if (m.type() == "8" && m.str(11) == cl && m.str(150) == "5") break; if (m.type() == "8") on_er(m); }
        uint64_t nid = (uint64_t)m.i64(37);
        Order o = it->second; o.id = nid; o.price = new_price; o.qty = new_qty; o.leaves = new_qty; o.status = OrdStatus::PendingNew; o.resting = false; orders_[nid] = o; clord_of_[nid] = cl;
        return nid;
    }
    const Order* order(uint64_t id) const override { auto it = orders_.find(id); return it == orders_.end() ? nullptr : &it->second; }
    std::vector<const Order*> resting_orders() const override { std::vector<const Order*> v; for (auto& kv : orders_) if (kv.second.resting) v.push_back(&kv.second); return v; }
    std::vector<Fill> take_visible_fills() override { std::vector<Fill> out; out.swap(pending_); return out; }
    const std::vector<Fill>& all_fills() const override { return fills_; }
    Qty executed() const override { Qty q = 0; for (auto& f : fills_) q += f.qty; return q; }
    void logout() { Message lo; lo.set(35, "5"); s_.send(lo, now_); }

protected:
    Qty execute_taker(Order&, Price) override { return 0; }

private:
    uint64_t send_new(Side s, OrdType t, Price p, Qty q) {
        std::string cl = "O" + std::to_string(++next_);
        Message d; d.set(35, "D"); d.set(11, cl); d.set(55, spec_.name); d.set(54, s == Side::Bid ? "1" : "2"); d.set(38, q); d.set(40, t == OrdType::Market ? "1" : "2");
        if (t == OrdType::Limit) { char b[32]; std::snprintf(b, sizeof b, "%.8f", spec_.px(p)); d.set(44, b); }
        d.set(59, "0"); d.set(60, utc_timestamp(now_));
        s_.send(d, now_);
        // read the acknowledgement (ExecutionReport 150=0) to learn the gateway order id
        Message m;
        while (true) {
            if (!s_.recv(m)) throw Error("disconnected while awaiting ack");
            if (m.type() == "8" && m.str(11) == cl && m.str(150) == "0") break;
            if (m.type() == "8") on_er(m);
        }
        uint64_t id = (uint64_t)m.i64(37);
        Order o; o.id = id; o.side = s; o.type = t; o.price = p; o.qty = q; o.leaves = q; o.submit_ts = now_; o.status = OrdStatus::PendingNew; o.resting = false;
        orders_[id] = o; clord_of_[id] = cl;
        return id;
    }
    void on_er(const Message& m) {
        uint64_t id = (uint64_t)m.i64(37); auto it = orders_.find(id); if (it == orders_.end()) return;
        Order& o = it->second; const std::string et = m.str(150);
        if (et == "F") {
            Fill f{id, m.i64(20001), m.i64(20011), m.i64(32), m.i64(20012) == 1, m.num(20013)};
            fills_.push_back(f); pending_.push_back(f);
        } else if (et == "I") { o.status = (OrdStatus)m.i64(39); o.leaves = m.i64(151); o.resting = m.i64(20014) == 1; }
    }
    SymbolSpec spec_; Session s_;
    Price bb_ = 0, ba_ = 0; Qty qb_ = 0, qa_ = 0, db_ = 0, da_ = 0, ob_ = 0, oa_ = 0; bool valid_ = false; bool ended_ = false;
    long long ev_ = 0; uint64_t next_ = 0;
    std::map<uint64_t, Order> orders_; std::map<uint64_t, std::string> clord_of_;
    std::vector<Fill> fills_, pending_;
};

} // namespace fix
} // namespace lobsim
