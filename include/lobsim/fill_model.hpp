#pragma once
// Fill-probability models for a passive order and the Cont-Kukanov limit/market split.
//
// FillModel::Structural  - in the spirit of Lokin & Yu (arXiv 2403.02572, 2024): order flow at the
//   queue is a state-dependent Poisson process (intensities lambda^{L,C,M}(q) taken from the
//   queue-reactive calibration), and the fill probability within a horizon is the first-passage
//   probability of our queue position through zero.  Lokin & Yu compute this semi-analytically;
//   here it is Monte Carlo over the single-queue birth-death process with the same cancellation
//   allocation rule as the replay (the QueueModel), cached per discrete state.
// FillModel::Logistic    - a state-conditional logistic surrogate fitted on replay outcomes
//   (`lobsim fillcurve`), features: distance, queue ahead, own size, imbalance, vol, horizon.
// FillModel::Poisson     - fallback: memoryless outflow with the observed market-order rate.
//
// ContKukanov::split     - single-venue version of Cont & Kukanov (QF 2017): choose market M and
//   limit L for a slice target d to minimise
//        h*M  -  h*L*p(L)  +  lambda_u * E[shortfall]
//   with p(L) from the fill model (binary fill of the whole limit order), h = half spread,
//   lambda_u = penalty per unfilled lot (it has to be crossed later).
#include "market_model.hpp"
#include "queue_model.hpp"
#include "stats.hpp"
#include "json.hpp"
#include <map>
#include <tuple>

namespace lobsim {

struct FillFeatures {
    int level = 1;            // 1 = at the touch on our side
    double queue_ahead = 0;   // others' lots ahead of us
    double our_size = 1;      // lots
    double imbalance = 0;     // (Q_our_side - Q_other_side)/(sum) at the best, signed towards our side
    double sigma = 0;         // 1s mid vol (ticks)
    double tau_s = 10;        // horizon in seconds
    double spread = 1;        // ticks
    double aes = 1;
    std::vector<double> x() const {
        return {1.0, (double)level, std::log1p(queue_ahead / std::max(aes, 1.0)), std::log1p(our_size / std::max(aes, 1.0)),
                imbalance, sigma, std::log(std::max(tau_s, 0.1)), spread};
    }
};

class FillModel {
public:
    enum class Kind { Structural, Logistic, Poisson };
    FillModel(Kind k, const MarketModel* m, QueueModel qm = QueueModel{}) : kind_(k), m_(m), qm_(qm) {}
    void set_logistic_weights(std::vector<double> w) { w_ = std::move(w); }
    Kind kind() const { return kind_; }
    static const char* name(Kind k) { return k == Kind::Structural ? "structural" : k == Kind::Logistic ? "logistic" : "poisson"; }

    double prob(const FillFeatures& f) const {
        switch (kind_) {
            case Kind::Logistic: return w_.empty() ? poisson(f) : logistic_predict(w_, f.x());
            case Kind::Structural: return structural(f);
            default: return poisson(f);
        }
    }

private:
    double poisson(const FillFeatures& f) const {
        double rate = m_ ? m_->trades_per_s * 0.5 : 0.1;   // market orders per second hitting our side
        double msize = m_ && !m_->sizes_pooled.empty() ? mean_size(m_->sizes_pooled[2][1]) : 1.0;
        double mean_out = std::max(rate * msize * f.tau_s, 1e-9);
        double need = f.queue_ahead + f.our_size + (f.level - 1) * (m_ ? m_->level_depth_mean[0] : 0.0);
        return std::exp(-need / mean_out);
    }
    static double mean_size(const SizeDist& d) {
        if (d.total <= 0) return 1.0;
        double s = 0; for (size_t b = 0; b < d.w.size(); ++b) s += d.w[b] * std::sqrt(d.edges[b] * std::min(d.edges[b + 1], d.edges[b] * 4));
        return s / d.total;
    }
    // Monte Carlo first passage under lambda^{L,C,M}(q) for a queue at level 1..K.
    double structural(const FillFeatures& f) const {
        if (!m_) return poisson(f);
        int lvl = std::min(std::max(f.level, 1), m_->K);
        int qb = std::min((int)std::floor(f.queue_ahead / std::max(m_->aes, 1.0) * 2.0), 4 * m_->B);   // finer than the intensity buckets
        int sb = std::min((int)std::floor(f.our_size / std::max(m_->aes, 1.0) * 2.0), 40);
        int tb = (int)std::floor(std::log2(std::max(f.tau_s, 0.5)) * 2.0);
        auto key = std::make_tuple(lvl, qb, sb, tb);
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        const int PATHS = 200;
        Rng rng(12345 + (uint64_t)lvl * 1000003 + (uint64_t)qb * 7919 + (uint64_t)sb * 131 + (uint64_t)tb);
        int fills = 0;
        for (int p = 0; p < PATHS; ++p) {
            QueuePos q; qm_.new_order(q, (Qty)std::llround(f.queue_ahead));
            double others = f.queue_ahead;
            double t = 0;
            // levels between us and the best must be eaten first: model them as extra queue ahead
            double inner = 0; for (int i = 1; i < lvl; ++i) inner += m_->level_depth_mean[i - 1];
            double ahead_inner = inner;
            while (t < f.tau_s) {
                int b = m_->bucket((Qty)std::llround(others + f.our_size));
                double lL = m_->lam_L[lvl - 1][b], lC = others > 0 ? m_->lam_C[lvl - 1][b] : 0, lM = ahead_inner > 0 ? m_->lam_M[m_->bucket((Qty)ahead_inner)] : m_->lam_M[b];
                double tot = lL + lC + lM;
                if (tot <= 0) break;
                t += rng.exponential(tot);
                if (t >= f.tau_s) break;
                double u = rng.uniform() * tot;
                if (u < lL) { Qty s = m_->sizes_pooled[0][lvl].sample(rng); qm_.depth(q, (Qty)std::llround(others), (Qty)std::llround(others) + s); others += (double)s; }
                else if (u < lL + lC) { Qty s = std::min<Qty>(m_->sizes_pooled[1][lvl].sample(rng), (Qty)std::llround(others)); qm_.depth(q, (Qty)std::llround(others), (Qty)std::llround(others) - s); others -= (double)s; }
                else {
                    Qty s = m_->sizes_pooled[2][std::min(lvl, m_->K)].sample(rng);
                    if (ahead_inner > 0) { double take = std::min((double)s, ahead_inner); ahead_inner -= take; s -= (Qty)take; if (s <= 0) continue; }
                    qm_.trade(q, s); others = std::max(0.0, others - (double)s);
                }
                if (q.front <= -f.our_size + 1e-9 || (others <= 0 && q.front <= 0 && qm_.executable(q) >= (Qty)f.our_size)) { ++fills; break; }
                if (qm_.executable(q) >= (Qty)std::llround(f.our_size)) { ++fills; break; }
            }
        }
        double p = (double)fills / PATHS;
        cache_[key] = p;
        return p;
    }

    Kind kind_;
    const MarketModel* m_;
    QueueModel qm_;
    std::vector<double> w_;
    mutable std::map<std::tuple<int, int, int, int>, double> cache_;
};

struct CKSplit { Qty market = 0, limit = 0; double exp_cost = 0, p_fill = 0; };

struct ContKukanov {
    double lambda_u_halfspreads = 2.0;   // penalty per unfilled lot, in half-spreads
    double adverse_ticks = 0.0;          // expected adverse mark-out of a passive fill (ticks); 0 = spread capture taken at face value
    int grid = 20;
    CKSplit split(const FillModel& fm, FillFeatures f, Qty target, double half_spread_ticks) const {
        CKSplit best; best.exp_cost = 1e300;
        if (target <= 0) { best.exp_cost = 0; return best; }
        double h = std::max(half_spread_ticks, 0.5);
        double lu = lambda_u_halfspreads * h;
        for (int i = 0; i <= grid; ++i) {
            Qty M = (Qty)std::llround((double)target * i / grid);
            Qty rest = target - M;
            for (int j = 0; j <= grid; ++j) {
                Qty L = (Qty)std::llround((double)rest * j / grid);
                if (rest > 0 && L == 0 && j > 0) continue;
                double p = 0;
                if (L > 0) { f.our_size = (double)L; p = fm.prob(f); }
                double cost = h * (double)M - (h - adverse_ticks) * (double)L * p + lu * ((1 - p) * (double)rest + p * (double)(rest - L));
                if (cost < best.exp_cost - 1e-9) { best.exp_cost = cost; best.market = M; best.limit = L; best.p_fill = p; }
            }
        }
        return best;
    }
};

} // namespace lobsim
