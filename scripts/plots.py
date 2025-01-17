#!/usr/bin/env python3
"""Figures for the README / report from results/*.json.   python scripts/plots.py [results_dir] [fig_dir]"""
import json
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

R = sys.argv[1] if len(sys.argv) > 1 else "results"
F = sys.argv[2] if len(sys.argv) > 2 else "results/figures"
os.makedirs(F, exist_ok=True)
plt.rcParams.update({"figure.dpi": 130, "font.size": 9, "axes.grid": True, "grid.alpha": 0.3})
MODES = ["replay", "queue_reactive", "zero_intelligence", "hawkes"]
MLABEL = {"replay": "replay", "queue_reactive": "queue-reactive", "zero_intelligence": "zero-intelligence", "hawkes": "Hawkes (extra)"}
QMS = ["naive_fifo", "power_prob", "risk_averse"]


def load(name):
    p = os.path.join(R, name)
    return json.load(open(p, encoding="utf-8")) if os.path.exists(p) else None


def fig_validation():
    v = load("validation.json")
    if not v:
        return
    sims = [m for m in MODES if m in v["simulators"]]
    stats = ["spread", "vol_at_best", "depth5", "imbalance", "trade_iat_ms", "ret_1s", "trade_size"]
    fig, ax = plt.subplots(1, 3, figsize=(13, 3.8))
    w = 0.8 / len(sims)
    for i, m in enumerate(sims):
        ks = [v["simulators"][m]["distance_to_live"].get(s, {}).get("ks", np.nan) for s in stats]
        ax[0].bar(np.arange(len(stats)) + i * w, ks, w, label=MLABEL[m])
    ax[0].set_xticks(np.arange(len(stats)) + 0.4 - w / 2); ax[0].set_xticklabels(stats, rotation=35, ha="right")
    ax[0].set_ylabel("KS distance to live"); ax[0].set_title("Vyetrenko-style stylised facts"); ax[0].legend(fontsize=7)
    lags = [e["lag"] for e in v["live"]["response"]["event_time"]]
    ax[1].plot(lags, [e["R_ticks"] for e in v["live"]["response"]["event_time"]], "k-o", label="live")
    for m in sims:
        ax[1].plot(lags, [e["R_ticks"] for e in v["simulators"][m]["response"]["event_time"]], "-o", ms=3, label=MLABEL[m])
    ax[1].set_xscale("log"); ax[1].set_xlabel("lag (trades)"); ax[1].set_ylabel("R(l), ticks"); ax[1].set_title("Impact response function"); ax[1].legend(fontsize=7)
    sc = ["ret_1s_std", "ret_kurtosis", "acf_absret1", "acf_sign1", "tail_gt4sigma_frac"]
    live = v["live"]["facts"]
    for i, m in enumerate(["live"] + sims):
        f = live if m == "live" else v["simulators"][m]["facts"]
        vals = [f.get(s, np.nan) for s in sc]
        vals = [x / live[s] if live.get(s) else np.nan for x, s in zip(vals, sc)]
        ax[2].bar(np.arange(len(sc)) + i * 0.8 / (len(sims) + 1), vals, 0.8 / (len(sims) + 1), label=m if m == "live" else MLABEL[m])
    ax[2].set_xticks(np.arange(len(sc)) + 0.35); ax[2].set_xticklabels(sc, rotation=35, ha="right"); ax[2].set_ylabel("ratio to live"); ax[2].set_title("Scalar facts (ratio to live)"); ax[2].axhline(1, color="k", lw=0.8)
    ax[2].legend(fontsize=6)
    fig.tight_layout(); fig.savefig(os.path.join(F, "validation.png")); plt.close(fig)


def fig_bench():
    b = load("bench.json")
    if not b:
        return
    cells = b["cells"]
    lats = sorted(set(c["latency_ms"] for c in cells))
    algos = list(dict.fromkeys(c["algo"] for c in cells))
    modes = [m for m in MODES if any(c["mode"] == m for c in cells)]
    for key, lo_k, hi_k, ylabel, fname, skip_twap in (("is_bp_mean", "is_bp_lo", "is_bp_hi", "implementation shortfall, bp (95% CI)", "ranking.png", False),
                                                        ("vs_twap_bp", "vs_twap_lo", "vs_twap_hi", "paired difference vs TWAP, bp (95% CI)", "ranking_vs_twap.png", True)):
        fig, axes = plt.subplots(len(lats), len(modes), figsize=(4.2 * len(modes), 3.4 * len(lats)), squeeze=False, sharey="row")
        al = [a for a in algos if not (skip_twap and a == "TWAP")]
        for li, lat in enumerate(lats):
            for mi, m in enumerate(modes):
                ax = axes[li][mi]
                w = 0.8 / len(al)
                for ai, a in enumerate(al):
                    ys, lo, hi = [], [], []
                    for q in QMS:
                        c = next((c for c in cells if c["latency_ms"] == lat and c["mode"] == m and c["queue_model"] == q and c["algo"] == a), None)
                        ys.append(c[key] if c else np.nan); lo.append(c[key] - c[lo_k] if c else 0); hi.append(c[hi_k] - c[key] if c else 0)
                    ax.bar(np.arange(3) + ai * w, ys, w, yerr=[lo, hi], capsize=2, label=a)
                ax.set_xticks(np.arange(3) + 0.4 - w / 2); ax.set_xticklabels(QMS, fontsize=8)
                ax.set_title(f"{MLABEL[m]}  ({int(lat)} ms one-way)"); ax.axhline(0, color="k", lw=0.8)
                if mi == 0: ax.set_ylabel(ylabel)
                if li == 0 and mi == 0: ax.legend(fontsize=7)
        fig.suptitle("Cost by algorithm, simulator mode and queue assumption" if not skip_twap else "Paired per-episode cost difference vs TWAP (negative = cheaper than TWAP)", y=1.0)
        fig.tight_layout(); fig.savefig(os.path.join(F, fname)); plt.close(fig)
    # attribution (base latency, naive queue model)
    lat = lats[0]
    fig, axes = plt.subplots(1, len(modes), figsize=(4.2 * len(modes), 3.4), squeeze=False, sharey=True)
    comps = [("spread_bp", "spread"), ("timing_bp", "timing"), ("impact_bp", "impact"), ("opportunity_bp", "opportunity")]
    for mi, m in enumerate(modes):
        ax = axes[0][mi]
        bottom_pos = np.zeros(len(algos)); bottom_neg = np.zeros(len(algos))
        for k, lab in comps:
            vals = np.array([next((c[k] for c in cells if c["latency_ms"] == lat and c["mode"] == m and c["queue_model"] == "naive_fifo" and c["algo"] == a), 0) for a in algos])
            pos = np.where(vals > 0, vals, 0); neg = np.where(vals < 0, vals, 0)
            ax.bar(algos, pos, bottom=bottom_pos, label=lab); ax.bar(algos, neg, bottom=bottom_neg, color=ax.patches[-1].get_facecolor())
            bottom_pos += pos; bottom_neg += neg
        tot = [next((c["is_bp_mean"] for c in cells if c["latency_ms"] == lat and c["mode"] == m and c["queue_model"] == "naive_fifo" and c["algo"] == a), 0) for a in algos]
        ax.plot(algos, tot, "k_", ms=18, mew=2, label="total IS")
        ax.set_title(f"{MLABEL[m]}: IS attribution (bp)"); ax.tick_params(axis="x", rotation=25); ax.axhline(0, color="k", lw=0.8)
        if mi == 0: ax.legend(fontsize=7)
    fig.tight_layout(); fig.savefig(os.path.join(F, "attribution.png")); plt.close(fig)


def fig_fills():
    fc = load("fillcurve.json"); l3 = load("l3check.json")
    fig, ax = plt.subplots(1, 3 if l3 else 2, figsize=(13 if l3 else 9, 3.6))
    if fc:
        for q in QMS:
            t = fc[q]["by_distance_horizon"]
            hs = sorted(set(e["horizon_s"] for e in t))
            for h in hs:
                d = [e["distance_ticks"] for e in t if e["horizon_s"] == h]; p = [e["p_fill"] for e in t if e["horizon_s"] == h]
                ax[0].plot(d, p, "-o", ms=3, label=f"{q}, {int(h)}s")
        ax[0].set_xlabel("ticks behind the touch"); ax[0].set_ylabel("P(fill)"); ax[0].set_title("Replay fill probability (Binance)"); ax[0].legend(fontsize=6, ncol=2)
        cmp_ = fc["naive_fifo"]["model_vs_empirical"]
        hs = sorted(set(e["horizon_s"] for e in cmp_))
        for h in hs:
            rows = [e for e in cmp_ if e["horizon_s"] == h]
            ax[1].plot([e["distance_ticks"] for e in rows], [e["empirical"] for e in rows], "k-o", ms=3, label=f"empirical {int(h)}s")
            ax[1].plot([e["distance_ticks"] for e in rows], [e["structural"] for e in rows], "--", label=f"state-dependent {int(h)}s")
            ax[1].plot([e["distance_ticks"] for e in rows], [e["logistic"] for e in rows], ":", label=f"logistic {int(h)}s")
        ax[1].set_xlabel("ticks behind the touch"); ax[1].set_title("Fill models vs empirical (naive FIFO)"); ax[1].legend(fontsize=6, ncol=2)
    if l3:
        km = [e for e in l3["observed_fill_curve"] if e["queue_tercile"] == -1]
        ax[2].plot([e["horizon_s"] for e in km], [e["p_fill_km"] for e in km], "k-o", label="real orders (Kaplan-Meier)")
        for q in QMS:
            t = l3["replay_probe_fill_curve"][q]["by_distance_horizon"]
            ax[2].plot([e["horizon_s"] for e in t], [e["p_fill"] for e in t], "--s", ms=3, label=f"replay probe, {q}")
        ax[2].set_xscale("log"); ax[2].set_xlabel("holding horizon (s)"); ax[2].set_ylabel("P(fill at the touch)"); ax[2].set_title("Bitstamp: order-level truth vs L2 replay"); ax[2].legend(fontsize=6)
    fig.tight_layout(); fig.savefig(os.path.join(F, "fills.png")); plt.close(fig)


def fig_markout():
    mo = load("markout.json")
    if not mo:
        return
    fig, ax = plt.subplots(1, 2, figsize=(10, 3.6))
    for r in mo:
        if r["queue_model"] != "naive_fifo":
            continue
        ax[0].errorbar([e["horizon_s"] for e in r["markouts"]], [e["markout_ticks"] for e in r["markouts"]], yerr=[e["se"] for e in r["markouts"]], fmt="-o", ms=3, capsize=2, label=MLABEL.get(r["mode"], r["mode"]))
    ax[0].set_xscale("log"); ax[0].axhline(0, color="k", lw=0.8); ax[0].set_xlabel("horizon after fill (s)"); ax[0].set_ylabel("mark-out of passive fills (ticks, + = favourable)"); ax[0].set_title("Adverse selection of passive fills"); ax[0].legend(fontsize=7)
    modes = [m for m in MODES if any(r["mode"] == m for r in mo)]
    w = 0.8 / 3
    for qi, q in enumerate(QMS):
        vals = [next((r["corr_fill_vs_return"] for r in mo if r["mode"] == m and r["queue_model"] == q), np.nan) for m in modes]
        ax[1].bar(np.arange(len(modes)) + qi * w, vals, w, label=q)
    ax[1].set_xticks(np.arange(len(modes)) + 0.4 - w / 2); ax[1].set_xticklabels([MLABEL[m] for m in modes], fontsize=8); ax[1].axhline(0, color="k", lw=0.8)
    ax[1].set_ylabel("corr(filled, post-placement return)"); ax[1].set_title("The fills you get are the ones you did not want"); ax[1].legend(fontsize=7)
    fig.tight_layout(); fig.savefig(os.path.join(F, "markout.png")); plt.close(fig)


def fig_ofi_l3():
    ofi = load("ofi.json"); l3 = load("l3check.json")
    fig, ax = plt.subplots(1, 3, figsize=(13, 3.6))
    if ofi:
        for k, st in (("1s", "-o"), ("10s", "-s")):
            bh = ofi[k]["by_hour_utc"]
            if bh: ax[0].plot([e["hour_utc"] for e in bh], [e["r2"] for e in bh], st, label=f"{k} buckets")
        ax[0].set_xlabel("hour (UTC)"); ax[0].set_ylabel("R²"); ax[0].set_title("OFI regression R² by hour"); ax[0].legend(fontsize=7)
        for k, st in (("1s", "-o"), ("10s", "-s")):
            bv = ofi[k]["by_vol_regime"]
            if bv: ax[1].plot([e["vol_tercile"] for e in bv], [e["r2"] for e in bv], st, label=f"{k} buckets")
        ax[1].set_xticks([0, 1, 2]); ax[1].set_xticklabels(["low vol", "mid", "high vol"]); ax[1].set_ylabel("R²"); ax[1].set_title("OFI R² by volatility regime"); ax[1].legend(fontsize=7)
    if l3:
        ms = l3["queue_models"]
        names = [f"{m['queue_model']}" + (f" n={m['power_n']:g}" if m["queue_model"] == "power_prob" else "") for m in ms]
        x = np.arange(len(ms))
        ax[2].bar(x - 0.2, [m["rel_err_mean"] for m in ms], 0.4, label="mean rel. error of queue ahead")
        ax[2].bar(x + 0.2, [m["fill_recall"] for m in ms], 0.4, label="recall of true fills")
        ax[2].set_xticks(x); ax[2].set_xticklabels(names, rotation=35, ha="right", fontsize=7); ax[2].axhline(0, color="k", lw=0.8)
        ax[2].set_title(f"L2 queue models vs {ms[0]['n_orders']} real touch orders (Bitstamp)", fontsize=9); ax[2].legend(fontsize=7)
    fig.tight_layout(); fig.savefig(os.path.join(F, "ofi_l3.png")); plt.close(fig)


if __name__ == "__main__":
    fig_validation(); fig_bench(); fig_fills(); fig_markout(); fig_ofi_l3()
    print("figures written to", F)
