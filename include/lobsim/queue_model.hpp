#pragma once
// Queue-position models for a passive order sitting at a level whose total quantity we can see
// but whose order-by-order composition we cannot (L2 data).  Queue position is therefore a
// *model*, not an observation; every result in this project is reported under all three.
//
// The update rules follow hftbacktest (nkaz001/hftbacktest, src/backtest/models/queue.rs):
//
//   trade(q)            : front -= q                  (a trade at our price consumes the front)
//   depth(prev, now)    : chg = prev - now - cum_trade_since_last_depth
//       increase        : front = min(front, now)     (arrivals join behind us)
//       decrease (chg>0): prob = P(decrease is behind us) = f(back) / (f(back) + f(front))
//                         front = min(front - (1-prob)*chg + min(back - prob*chg, 0), now)
//   fill                : when front < 0, |front| lots of our order execute (bounded by leaves).
//
//   NaiveFifo  : f(x) = x        -> cancels uniformly distributed along the queue (prob = back/(front+back))
//   PowerProb  : f(x) = x^n      -> hftbacktest PowerProbQueueFunc (default n = 3)
//   RiskAverse : front = min(front, now) on any depth change -> every cancel is assumed behind us;
//                only trades advance us (hftbacktest RiskAdverseQueueModel).
#include "types.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace lobsim {

enum class QueueModelKind : uint8_t { NaiveFifo = 0, PowerProb = 1, RiskAverse = 2 };

inline const char* queue_model_name(QueueModelKind k) {
    switch (k) { case QueueModelKind::NaiveFifo: return "naive_fifo"; case QueueModelKind::PowerProb: return "power_prob"; default: return "risk_averse"; }
}
inline QueueModelKind parse_queue_model(const std::string& s) {
    if (s == "naive" || s == "naive_fifo" || s == "fifo") return QueueModelKind::NaiveFifo;
    if (s == "power" || s == "power_prob") return QueueModelKind::PowerProb;
    if (s == "risk" || s == "risk_averse") return QueueModelKind::RiskAverse;
    throw Error("unknown queue model: " + s);
}

struct QueuePos {
    double front = 0;       // estimated lots ahead of us
    double cum_trade = 0;   // trades applied since the last depth update at this level
};

struct QueueModel {
    QueueModelKind kind = QueueModelKind::NaiveFifo;
    double n = 3.0;   // exponent for PowerProb

    void new_order(QueuePos& q, Qty level_qty) const { q.front = (double)level_qty; q.cum_trade = 0; }

    void trade(QueuePos& q, Qty traded) const { q.front -= (double)traded; q.cum_trade += (double)traded; }

    void depth(QueuePos& q, Qty prev_qty, Qty new_qty) const {
        double chg = (double)prev_qty - (double)new_qty - q.cum_trade;
        q.cum_trade = 0;
        if (kind == QueueModelKind::RiskAverse || chg <= 0) { q.front = std::min(q.front, (double)new_qty); return; }
        double front = std::max(q.front, 0.0);
        double back = std::max((double)prev_qty - front, 0.0);
        double prob = probability(front, back);
        double est = front - (1.0 - prob) * chg + std::min(back - prob * chg, 0.0);
        q.front = std::min(est, (double)new_qty);
    }

    // Executable lots once the estimated front goes negative.
    Qty executable(const QueuePos& q) const { return q.front < 0 ? (Qty)std::llround(-q.front) : 0; }

    double probability(double front, double back) const {
        auto f = [&](double x) { return kind == QueueModelKind::NaiveFifo ? x : std::pow(x, n); };
        double fb = f(back), ff = f(front);
        if (fb + ff <= 0) return 1.0;
        return fb / (fb + ff);
    }
};

} // namespace lobsim
