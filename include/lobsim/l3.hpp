#pragma once
// Order-by-order (L3) ground truth from the Bitstamp live_orders / live_trades channels.
//
// Every real order created at the touch is tracked to its end (fill, cancel, or end of data).
// Its TRUE queue position is the resting quantity of the orders ahead of it at its price.  In
// parallel, the three L2 queue models are fed only what an L2 subscriber would see - the level
// total excluding the order itself, and public trades at that price that did not involve it -
// and their estimated position is compared with the truth.  This is the strongest check
// available for the queue-position assumption of the replay simulator.
//
// Also produces an L2 tape (levels + trades) derived from the L3 book so the same fill-probability
// probes can be run on this venue and compared with the observed fill outcomes of real orders.
#include "tape.hpp"
#include "book.hpp"
#include "json.hpp"
#include "queue_model.hpp"
#include "rebuild.hpp"
#include "stats.hpp"
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <list>
#include <deque>
#include <functional>
#include <vector>

namespace lobsim {

struct L3Stats {
    struct ModelStats {
        std::string name; double n_exp = 0;    // power exponent when applicable
        size_t n = 0;                          // tracked orders evaluated
        std::vector<double> rel_err;           // (est_front - true_front) / initial_queue at the order's end
        std::vector<double> abs_err_lots;
        size_t filled_true = 0, filled_pred_of_true = 0;    // true fills, and how many the model also executed by then
        size_t cancelled_true = 0, false_fill_on_cancelled = 0;
        std::vector<double> fill_time_err_s;   // model fill time - true fill time (s), when both filled
        Json to_json() const {
            Json j = Json::object(); j["queue_model"] = name; j["power_n"] = n_exp; j["n_orders"] = (long long)n;
            j["rel_err_mean"] = mean(rel_err); j["rel_err_p50"] = median(rel_err); j["rel_err_abs_p50"] = 0.0;
            std::vector<double> a; for (double x : rel_err) a.push_back(std::fabs(x)); j["rel_err_abs_p50"] = median(a); j["rel_err_abs_mean"] = mean(a);
            j["abs_err_lots_p50"] = median(abs_err_lots);
            j["true_fills"] = (long long)filled_true; j["fills_also_predicted"] = (long long)filled_pred_of_true;
            j["fill_recall"] = filled_true ? (double)filled_pred_of_true / filled_true : 0;
            j["true_cancels"] = (long long)cancelled_true; j["false_fills_on_cancelled"] = (long long)false_fill_on_cancelled;
            j["false_fill_rate"] = cancelled_true ? (double)false_fill_on_cancelled / cancelled_true : 0;
            j["fill_time_err_s_p50"] = median(fill_time_err_s); j["fill_time_err_s_mean"] = mean(fill_time_err_s); j["n_fill_time"] = (long long)fill_time_err_s.size();
            return j;
        }
    };
    std::vector<ModelStats> models;
    size_t orders_created = 0, orders_tracked = 0, trades = 0, snapshots = 0, reconnects = 0;
    size_t unknown_order_refs = 0, bad_lines = 0, purged_stale = 0, n_state_checks = 0, crossed_states = 0, marketable_created = 0;
    double seconds = 0;
    // observed fill probability of real touch orders by holding horizon (orders alive at least tau or filled before tau)
    Json observed_fill_curve;
    Qty median_touch_size = 0;
    Json l2_parity;   // top-of-book agreement between the L3-derived book and diff_order_book
};

inline void rebuild_bitstamp_l3(const std::vector<std::string>& files, const SymbolSpec& spec, std::vector<L2Event>& tape, L3Stats& st,
                                std::vector<double> horizons_s = {5, 30, 120}, Ts debug_dump_ts = 0, Ts since_rx = 0) {
    struct O { uint64_t id; Side side; Price px; Qty rem; Ts created; bool tracked; size_t tidx; bool from_snapshot = false; };
    Ts snap_ts = 0;   // ws events at or before this are already in the snapshot
    std::unordered_map<uint64_t, O> orders;
    std::map<Price, std::list<uint64_t>> bidq, askq;   // bids: best = rbegin()
    auto queue_of = [&](Side s, Price p) -> std::list<uint64_t>& { return s == Side::Bid ? bidq[p] : askq[p]; };
    auto level_total = [&](Side s, Price p) { Qty q = 0; auto& l = s == Side::Bid ? bidq : askq; auto it = l.find(p); if (it == l.end()) return q; for (auto id : it->second) q += orders[id].rem; return q; };
    auto best = [&](Side s) -> Price { if (s == Side::Bid) { for (auto it = bidq.rbegin(); it != bidq.rend(); ++it) if (!it->second.empty()) return it->first; return 0; } for (auto& kv : askq) if (!kv.second.empty()) return kv.first; return 0; };
    auto erase_from_queue = [&](Side s, Price p, uint64_t id) { auto& l = s == Side::Bid ? bidq : askq; auto it = l.find(p); if (it == l.end()) return; it->second.remove(id); if (it->second.empty()) l.erase(it); };

    constexpr int NM = 7;
    struct Tracked { uint64_t id; Side side; Price px; Qty size; Ts t0; Qty q0; QueuePos q[NM]; bool model_filled[NM]; Ts model_fill_ts[NM]; Ts t_end = 0; int outcome = -1; Qty true_front_end = 0; Qty filled_qty = 0; Ts fill_ts = 0; };
    std::vector<Tracked> tracked;
    QueueModel qms[NM]; qms[0].kind = QueueModelKind::NaiveFifo; qms[1].kind = QueueModelKind::PowerProb; qms[2].kind = QueueModelKind::RiskAverse;
    const double scan_n[4] = {0.25, 0.5, 2.0, 5.0};
    for (int k = 3; k < NM; ++k) { qms[k].kind = QueueModelKind::PowerProb; qms[k].n = scan_n[k - 3]; }
    st.models.resize(NM); for (int k = 0; k < NM; ++k) { st.models[k].name = queue_model_name(qms[k].kind); st.models[k].n_exp = qms[k].kind == QueueModelKind::PowerProb ? qms[k].n : 0; }
    std::unordered_map<uint64_t, size_t> tracked_by_id;
    const size_t MAX_TRACKED = 60000;
    Ts now = 0; Ts t_first = 0;

    auto true_front = [&](const Tracked& t) { Qty f = 0; auto& l = t.side == Side::Bid ? bidq : askq; auto it = l.find(t.px); if (it == l.end()) return f; for (auto id : it->second) { if (id == t.id) break; f += orders[id].rem; } return f; };
    // L2 view change at (side, px) for every tracked order resting there, excluding itself.
    auto notify_level = [&](Side s, Price p, Qty prev_total, Qty new_total, uint64_t cause_id) {
        auto& l = s == Side::Bid ? bidq : askq; auto it = l.find(p); if (it == l.end() && new_total > 0) return;
        if (it == l.end()) return;
        for (auto id : it->second) {
            auto tb = tracked_by_id.find(id); if (tb == tracked_by_id.end()) continue;
            Tracked& t = tracked[tb->second]; if (t.outcome >= 0 || id == cause_id) continue;
            Qty own = orders[id].rem;
            for (int k = 0; k < NM; ++k) { if (t.model_filled[k]) continue; qms[k].depth(t.q[k], prev_total - own, new_total - own); if (qms[k].executable(t.q[k]) >= t.size) { t.model_filled[k] = true; t.model_fill_ts[k] = now; } }
        }
    };
    // A recorded snapshot can predate the first streamed event (deletions in that hole are lost).
    // Any snapshot-origin order that a later order or trade crosses without trading against it is
    // stale and is purged; only snapshot orders can be stale, every later order has its own deletion.
    size_t purged = 0;
    auto purge_crossed = [&](Side opp, Price px) {   // remove snapshot orders on side `opp` at prices crossed by px
        std::vector<std::pair<Price, uint64_t>> victims;
        if (opp == Side::Ask) { for (auto& kv : askq) { if (kv.first > px) break; for (auto id : kv.second) if (orders[id].from_snapshot) victims.emplace_back(kv.first, id); } }
        else { for (auto it = bidq.rbegin(); it != bidq.rend(); ++it) { if (it->first < px) break; for (auto id : it->second) if (orders[id].from_snapshot) victims.emplace_back(it->first, id); } }
        for (auto& v : victims) { erase_from_queue(opp, v.first, v.second); orders.erase(v.second); ++purged; tape.push_back({now, now, EvKind::Level, opp, v.first, level_total(opp, v.first)}); }
    };
    auto finish = [&](Tracked& t, int outcome) {
        t.outcome = outcome; t.t_end = now; t.true_front_end = true_front(t);
        for (int k = 0; k < NM; ++k) {
            auto& m = st.models[k]; ++m.n;
            double est = t.model_filled[k] ? -(double)t.size : t.q[k].front;
            double tf = outcome == 1 ? -(double)t.size : (double)t.true_front_end;
            if (t.q0 > 0) m.rel_err.push_back((est - tf) / (double)t.q0);
            m.abs_err_lots.push_back(std::fabs(est - tf));
            if (outcome == 1) { ++m.filled_true; if (t.model_filled[k]) { ++m.filled_pred_of_true; m.fill_time_err_s.push_back((t.model_fill_ts[k] - t.fill_ts) / 1e9); } }
            if (outcome == 2) { ++m.cancelled_true; if (t.model_filled[k]) ++m.false_fill_on_cancelled; }
        }
    };
    bool have_book = false;
    size_t parity_n = 0, parity_ok = 0;   // top-of-book agreement with the diff_order_book channel
    std::unordered_set<uint64_t> seen_trades, deleted_ids;   // duplicate-line protection (two recorder instances overlapped)
    // One websocket event (order or trade) applied to the L3 book; also used to re-apply buffered
    // events on top of a fresh snapshot.
    std::function<void(const Json&, Ts)> handle_ws = [&](const Json& msg, Ts rx) {
        const std::string ev = msg.get("event", std::string()); const Json& d = msg["data"];
        if (ev == "data" && msg.get("channel", std::string()).rfind("diff_order_book", 0) == 0) {
            // parity: exchange L2 diff's best levels vs our L3-derived book (top of book only)
            Ts t = (Ts)(std::strtoll(d["microtimestamp"].str().c_str(), nullptr, 10)) * 1000;
            Price bb = 0, ba = 0; Qty qb = 0, qa = 0;
            for (auto& r : d["bids"].arr()) { Qty q = spec.to_lots(std::atof(r[1].str().c_str())); Price px = spec.to_ticks(std::atof(r[0].str().c_str())); if (q > 0 && px > bb) { bb = px; qb = q; } }
            for (auto& r : d["asks"].arr()) { Qty q = spec.to_lots(std::atof(r[1].str().c_str())); Price px = spec.to_ticks(std::atof(r[0].str().c_str())); if (q > 0 && (ba == 0 || px < ba)) { ba = px; qa = q; } }
            if (bb) { ++parity_n; if (level_total(Side::Bid, bb) == qb) ++parity_ok; }
            if (ba) { ++parity_n; if (level_total(Side::Ask, ba) == qa) ++parity_ok; }
            (void)t; return;
        }
        if (ev != "order_created" && ev != "order_changed" && ev != "order_deleted" && ev != "trade") return;
        Ts t = (Ts)(std::strtoll(d["microtimestamp"].str().c_str(), nullptr, 10)) * 1000;
        if (t <= snap_ts) return;   // already reflected in the snapshot
        now = std::max(now, t);
        { Price b = best(Side::Bid), a_ = best(Side::Ask); if (b && a_) { ++st.n_state_checks; if (b >= a_) ++st.crossed_states; } }
        if (debug_dump_ts && now >= debug_dump_ts) {   // one-off diagnostic dump of the top of the L3 book
            debug_dump_ts = 0; int k = 0;
            auto dump = [&](const char* tag, Price px, uint64_t id) { auto& o = orders[id]; std::fprintf(stderr, "%s %lld qty %lld id %llu snap=%d created=%lld", tag, (long long)px, (long long)o.rem, (unsigned long long)id, (int)o.from_snapshot, (long long)o.created); std::fputc(10, stderr); };
            for (auto it = bidq.rbegin(); it != bidq.rend() && k < 6; ++it, ++k) for (auto id : it->second) dump("BID", it->first, id);
            k = 0; for (auto it = askq.begin(); it != askq.end() && k < 6; ++it, ++k) for (auto id : it->second) dump("ASK", it->first, id);
        }
        if (ev == "trade") {
            { uint64_t tid = (uint64_t)d["id"].i64(); if (!seen_trades.insert(tid).second) return; if (seen_trades.size() > 200000) seen_trades.clear(); }   // duplicate lines (two recorder instances)
            ++st.trades;
            Qty q = spec.to_lots(std::atof(d["amount_str"].str().c_str())); Price px = spec.to_ticks(std::atof(d["price_str"].str().c_str()));
            int taker = (int)d["type"].i64();   // 0 = buy
            uint64_t maker_id = (uint64_t)(taker == 0 ? d["sell_order_id"].i64() : d["buy_order_id"].i64());
            Side passive = taker == 0 ? Side::Ask : Side::Bid;
            purge_crossed(passive, passive == Side::Ask ? px - 1 : px + 1);   // nothing on the passive side can sit inside the trade price
            tape.push_back({now, rx, EvKind::Trade, passive, px, q});
            auto it = orders.find(maker_id);
            if (it == orders.end()) { ++st.unknown_order_refs; return; }
            O& mo = it->second;
            Qty prev_total = level_total(passive, mo.px);
            // queue models: a public trade at this price of q lots (excluding trades against the tracked order itself)
            auto& l = passive == Side::Bid ? bidq : askq; auto lit = l.find(mo.px);
            if (lit != l.end()) for (auto id : lit->second) {
                auto tb = tracked_by_id.find(id); if (tb == tracked_by_id.end()) continue; Tracked& tr = tracked[tb->second];
                if (tr.outcome >= 0 || id == maker_id) continue;
                for (int k = 0; k < NM; ++k) { if (tr.model_filled[k]) continue; qms[k].trade(tr.q[k], q); if (qms[k].executable(tr.q[k]) >= tr.size) { tr.model_filled[k] = true; tr.model_fill_ts[k] = t; } }
            }
            mo.rem -= q; if (mo.rem < 0) mo.rem = 0;
            auto tb = tracked_by_id.find(maker_id);
            if (tb != tracked_by_id.end()) { Tracked& tr = tracked[tb->second]; tr.filled_qty += q; tr.fill_ts = t; if (tr.outcome < 0 && tr.filled_qty >= tr.size) { finish(tr, 1); erase_from_queue(passive, mo.px, maker_id); orders.erase(it); } }
            else if (mo.rem <= 0) { erase_from_queue(passive, mo.px, maker_id); orders.erase(it); }
            Qty new_total = level_total(passive, mo.px);
            // fills reduce the level but the model already accounted for them via trade(); mark cum_trade consistency by a depth call
            notify_level(passive, mo.px, prev_total, new_total, maker_id);
            tape.push_back({now, rx, EvKind::Level, passive, mo.px, new_total});
            return;
        }
        uint64_t id = (uint64_t)d["id"].i64(); Side s = d["order_type"].i64() == 0 ? Side::Bid : Side::Ask;
        Price px = spec.to_ticks(std::atof(d["price_str"].str().c_str())); Qty rem = spec.to_lots(std::atof(d["amount_str"].str().c_str()));
        if (ev == "order_created") {
            ++st.orders_created;
            if (orders.count(id) || deleted_ids.count(id)) return;   // duplicate creation, possibly after its deletion
            // An order created at or through the opposite best is marketable (it trades, and the trades
            // reference it as taker) or was rejected (Bitstamp then sends nothing further): never resting.
            Price ob = best(other(s));
            if (ob && (s == Side::Bid ? px >= ob : px <= ob)) { ++st.marketable_created; return; }
            Qty prev_total = level_total(s, px);
            bool at_touch = px == best(s) || (best(s) == 0) || (s == Side::Bid ? px > best(s) : px < best(s));
            bool track = at_touch && tracked.size() < MAX_TRACKED && rem > 0 && (best(other(s)) == 0 || (s == Side::Bid ? px < best(other(s)) : px > best(other(s))));
            orders[id] = O{id, s, px, rem, t, track, 0}; queue_of(s, px).push_back(id);
            if (track) {
                Tracked tr; tr.id = id; tr.side = s; tr.px = px; tr.size = rem; tr.t0 = t; tr.q0 = prev_total;
                for (int k = 0; k < NM; ++k) { qms[k].new_order(tr.q[k], prev_total); tr.model_filled[k] = false; tr.model_fill_ts[k] = 0; }
                tracked_by_id[id] = tracked.size(); tracked.push_back(tr); ++st.orders_tracked;
            }
            notify_level(s, px, prev_total, level_total(s, px), id);
            tape.push_back({now, rx, EvKind::Level, s, px, level_total(s, px)});
        } else if (ev == "order_changed") {
            auto it = orders.find(id); if (it == orders.end()) { ++st.unknown_order_refs; return; }
            O& o = it->second;
            if (o.px != px) {   // amended price: loses priority
                Qty prev_old = level_total(s, o.px); erase_from_queue(s, o.px, id); notify_level(s, o.px, prev_old, level_total(s, o.px), id); tape.push_back({now, rx, EvKind::Level, s, o.px, level_total(s, o.px)});
                Qty prev_new = level_total(s, px); o.px = px; o.rem = rem; queue_of(s, px).push_back(id); notify_level(s, px, prev_new, level_total(s, px), id); tape.push_back({now, rx, EvKind::Level, s, px, level_total(s, px)});
                auto tb = tracked_by_id.find(id); if (tb != tracked_by_id.end() && tracked[tb->second].outcome < 0) finish(tracked[tb->second], 2);
            } else {
                Qty prev_total = level_total(s, px); Qty delta = rem - o.rem;
                if (delta != 0) {
                    // partial fills arrive as trades first; a residual change here is an amend (treated as cancel of the difference)
                    o.rem = rem;
                    if (delta < 0) notify_level(s, px, prev_total, level_total(s, px), id); else notify_level(s, px, prev_total, level_total(s, px), id);
                    tape.push_back({now, rx, EvKind::Level, s, px, level_total(s, px)});
                }
            }
        } else { // order_deleted
            deleted_ids.insert(id); if (deleted_ids.size() > 400000) deleted_ids.clear();
            auto it = orders.find(id); if (it == orders.end()) { ++st.unknown_order_refs; return; }
            O& o = it->second; Qty prev_total = level_total(s, o.px);
            auto tb = tracked_by_id.find(id);
            if (tb != tracked_by_id.end() && tracked[tb->second].outcome < 0) { Tracked& tr = tracked[tb->second]; finish(tr, tr.filled_qty >= tr.size ? 1 : 2); }
            erase_from_queue(s, o.px, id); Price opx = o.px; orders.erase(it);
            notify_level(s, opx, prev_total, level_total(s, opx), id);
            tape.push_back({now, rx, EvKind::Level, s, opx, level_total(s, opx)});
        }
    };
    std::deque<std::pair<Ts, Json>> recent;   // last few seconds of events, for re-application after a snapshot

    {
        detail::for_each_line_by_rx(files, [&](const char* p, size_t n) {
          try {
            Json line = Json::parse(p, p + n);
            if (!line.is_object()) return;
            const std::string& kind = line["kind"].str(); Ts rx = line["rx"].i64();
            const Json& msg = line["msg"];
            if (kind == "meta") { const std::string ev = msg.get("event", std::string()); if (ev == "disconnect" || ev == "recv_timeout") ++st.reconnects; return; }
            if (kind == "snapshot") {
                ++st.snapshots;
                for (auto& kv : orders) { auto tb = tracked_by_id.find(kv.first); if (tb != tracked_by_id.end() && tracked[tb->second].outcome < 0) tracked[tb->second].outcome = 3; }
                orders.clear(); bidq.clear(); askq.clear(); tracked_by_id.clear();
                // stamp the snapshot with its exchange time, not receive time: stream events sorted by
                // exchange time must not land before the state they build on
                snap_ts = (Ts)(std::strtoll(msg["microtimestamp"].str().c_str(), nullptr, 10)) * 1000;
                now = std::max(now, snap_ts);
                tape.push_back({now, rx, EvKind::Snapshot, Side::Bid, 0, 0});
                for (Side s : {Side::Bid, Side::Ask}) for (auto& row : msg[s == Side::Bid ? "bids" : "asks"].arr()) {
                    Price px = spec.to_ticks(std::atof(row[0].str().c_str())); Qty q = spec.to_lots(std::atof(row[1].str().c_str())); uint64_t id = (uint64_t)std::strtoull(row[2].str().c_str(), nullptr, 10);
                    if (q <= 0 || orders.count(id)) continue;
                    orders[id] = O{id, s, px, q, now, false, 0, true}; queue_of(s, px).push_back(id);
                }
                for (auto& kv : bidq) tape.push_back({now, rx, EvKind::Level, Side::Bid, kv.first, level_total(Side::Bid, kv.first)});
                for (auto& kv : askq) tape.push_back({now, rx, EvKind::Level, Side::Ask, kv.first, level_total(Side::Ask, kv.first)});
                have_book = true; if (!t_first) t_first = now;
                for (auto& r : recent) if (r.first > snap_ts) handle_ws(r.second, rx);   // events newer than the snapshot, already seen
                return;
            }
            if (kind != "ws" || !have_book) return;
            { const Json& d0 = msg["data"]; if (d0.is_object() && d0.has("microtimestamp")) {
                Ts t0 = (Ts)(std::strtoll(d0["microtimestamp"].str().c_str(), nullptr, 10)) * 1000;
                recent.emplace_back(t0, msg); while (!recent.empty() && recent.front().first < t0 - 10 * NS_PER_S) recent.pop_front(); } }
            handle_ws(msg, rx);
          } catch (const std::exception&) { ++st.bad_lines; }   // torn / malformed raw line: skipped
        }, since_rx);
    }
    st.seconds = (now - t_first) / 1e9; st.purged_stale = purged;
    // Kaplan-Meier fill curve of real touch orders: P(filled by tau) with cancellation (and end of
    // data) treated as right-censoring, i.e. the fill probability an order would have had if its
    // owner had left it in the queue - the quantity a replay probe estimates.  By queue-ahead tercile.
    Json fc = Json::array();
    std::vector<double> qa; for (auto& tr : tracked) if (tr.outcome >= 0) qa.push_back((double)tr.q0);
    double q1 = quantile(qa, 1.0 / 3), q2 = quantile(qa, 2.0 / 3);
    std::vector<double> sizes; for (auto& tr : tracked) if (tr.outcome >= 0) sizes.push_back((double)tr.size);
    st.median_touch_size = (Qty)std::llround(median(sizes));
    for (int terc = -1; terc < 3; ++terc) {
        struct Ev { double dur; bool fill; };
        std::vector<Ev> evs;
        for (auto& tr : tracked) {
            if (tr.outcome < 0) continue;
            int tt = tr.q0 <= q1 ? 0 : tr.q0 <= q2 ? 1 : 2; if (terc >= 0 && tt != terc) continue;
            evs.push_back({(tr.outcome == 1 ? tr.fill_ts - tr.t0 : tr.t_end - tr.t0) / 1e9, tr.outcome == 1});
        }
        std::sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.dur < b.dur; });
        double surv = 1.0; size_t at_risk = evs.size(), i = 0;
        for (double h : horizons_s) {
            while (i < evs.size() && evs[i].dur <= h) {
                size_t j = i; size_t fills = 0, leave = 0;
                while (j < evs.size() && evs[j].dur == evs[i].dur) { fills += evs[j].fill; ++leave; ++j; }
                if (at_risk > 0) surv *= 1.0 - (double)fills / at_risk;
                at_risk -= leave; i = j;
            }
            Json e = Json::object(); e["horizon_s"] = h; e["queue_tercile"] = terc; e["n"] = (long long)evs.size(); e["p_fill_km"] = 1.0 - surv; e["at_risk"] = (long long)at_risk; fc.push(e);
        }
    }
    st.observed_fill_curve = fc;
    Json par = Json::object(); par["top_of_book_checks"] = (long long)parity_n; par["agree"] = (long long)parity_ok; par["agree_frac"] = parity_n ? (double)parity_ok / parity_n : 0; st.l2_parity = par;
    std::stable_sort(tape.begin(), tape.end(), [](const L2Event& a, const L2Event& b) { return a.ts_ex < b.ts_ex; });
}

inline Json l3_json(const L3Stats& st) {
    Json j = Json::object();
    j["seconds"] = st.seconds; j["orders_created"] = (long long)st.orders_created; j["orders_tracked_at_touch"] = (long long)st.orders_tracked; j["trades"] = (long long)st.trades;
    j["snapshots"] = (long long)st.snapshots; j["reconnects"] = (long long)st.reconnects; j["unknown_order_refs"] = (long long)st.unknown_order_refs; j["bad_lines"] = (long long)st.bad_lines; j["purged_stale_snapshot_orders"] = (long long)st.purged_stale; j["marketable_or_rejected_creations"] = (long long)st.marketable_created; j["internal_book_crossed_frac"] = st.n_state_checks ? (double)st.crossed_states / st.n_state_checks : 0;
    Json m = Json::array(); for (auto& x : st.models) m.push(x.to_json()); j["queue_models"] = m;
    j["observed_fill_curve"] = st.observed_fill_curve; j["l2_parity"] = st.l2_parity; j["median_touch_order_size_lots"] = (long long)st.median_touch_size;
    return j;
}

} // namespace lobsim
