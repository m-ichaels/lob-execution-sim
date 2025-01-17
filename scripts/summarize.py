#!/usr/bin/env python3
"""Key numbers from results/*.json as Markdown tables (results/summary.md), used by the README and report."""
import json
import os
import sys

R = sys.argv[1] if len(sys.argv) > 1 else "results"


def load(n):
    p = os.path.join(R, n)
    return json.load(open(p, encoding="utf-8")) if os.path.exists(p) else None


out = []
P = out.append

rb = load("rebuild_binance.json"); rc = load("rebuild_coinbase.json"); l3 = load("l3check.json")
P("## Data capture\n")
P("| venue | feed | duration | messages | sequence gaps | resyncs | disconnects | snapshot parity | recv − exchange time (p50 / p95) |")
P("|---|---|---|---|---|---|---|---|---|")
if rb:
    P(f"| Binance spot BTCUSDT | depth@100ms + trade (L2) | {rb['seconds']/3600:.2f} h | {rb['ws_msgs']:,} | {rb['gaps']} | {rb['resyncs']} | {rb['disconnects']} | {rb['parity_levels_compared']:,} levels, {rb['parity_levels_mismatched']} mismatched | {rb['recv_minus_exchange_depth']['p50_ms']:.0f} / {rb['recv_minus_exchange_depth']['p95_ms']:.0f} ms |")
if rc:
    P(f"| Coinbase Exchange BTC-USD | level2_batch + matches (L2) | {rc['seconds']/3600:.2f} h | {rc['ws_msgs']:,} | {rc['gaps']} | {rc['resyncs']} | {rc['disconnects']} | n/a (absolute levels) | {rc['recv_minus_exchange_depth']['p50_ms']:.0f} / {rc['recv_minus_exchange_depth']['p95_ms']:.0f} ms |")
if l3:
    P(f"| Bitstamp BTC/USD | live_orders + live_trades (L3, order ids) | {l3['seconds']/3600:.2f} h | {l3['orders_created']:,} orders, {l3['trades']:,} trades | – | {l3['snapshots']} | {l3['reconnects']} | top-of-book vs diff_order_book: {100*l3['l2_parity']['agree_frac']:.0f}% agree | – |")

cal = load("calib_report.json"); cm = load("../data/calib_binance.json") if False else None
if cal:
    P("\n## Calibration (Binance)\n")
    P(f"- events decomposed: {cal['n_events']:,} (limit {int(cal['event_counts']['limit']):,}, cancel {int(cal['event_counts']['cancel']):,}, market {int(cal['event_counts']['market']):,}); trades {cal['n_trades']:,}; AES = {cal['aes_lots']:.0f} lots; σ(1 s) = {cal['sigma_1s_ticks']:.0f} ticks; mean spread {cal['spread_mean_ticks']:.2f} ticks")
    P(f"- reference-price jumps (|Δmid| ≥ 5 ticks between batches): {cal['n_jumps']:,}; rate by touch-imbalance bucket {[round(x,2) for x in cal['jump_rate_by_imbalance']]} /s; P(up) by bucket {[round(x,2) for x in cal['jump_up_prob_by_imbalance']]}")
    P(f"- Hawkes branching ratio {cal['hawkes_branching_ratio']:.2f}; OFI β = {cal['ofi_beta']:.2e} ticks/lot, R² = {cal['ofi_r2']:.2f}; AC η = {cal['eta']:.2e} ticks per lot/s")

v = load("validation.json")
if v:
    P("\n## Simulator realism (live vs simulated, Binance)\n")
    stats = ["spread", "vol_at_best", "depth5", "imbalance", "trade_iat_ms", "ret_1s", "trade_size"]
    P("| statistic (KS distance to live) | " + " | ".join(m for m in v["simulators"]) + " |")
    P("|---|" + "---|" * len(v["simulators"]))
    for s in stats:
        P(f"| {s} | " + " | ".join(f"{v['simulators'][m]['distance_to_live'].get(s, {}).get('ks', float('nan')):.2f}" for m in v["simulators"]) + " |")
    live = v["live"]["facts"]
    for k, lab in (("ret_1s_std", "σ(1 s) ticks"), ("ret_kurtosis", "kurtosis of 1 s returns"), ("acf_absret1", "ACF |r| lag 1"), ("acf_sign1", "trade-sign ACF lag 1"), ("tail_gt4sigma_frac", "P(|r| > 4σ)")):
        P(f"| {lab} (live {live[k]:.3g}) | " + " | ".join(f"{v['simulators'][m]['facts'][k]:.3g}" for m in v["simulators"]) + " |")
    P(f"| impact response R(1)…R(100) ticks (live {v['live']['response']['event_time'][0]['R_ticks']:.0f}…{v['live']['response']['event_time'][-1]['R_ticks']:.0f}) | " + " | ".join(f"{v['simulators'][m]['response']['event_time'][0]['R_ticks']:.0f}…{v['simulators'][m]['response']['event_time'][-1]['R_ticks']:.0f}" for m in v["simulators"]) + " |")

b = load("bench.json")
if b:
    P("\n## Cost ranking (Binance replay day)\n")
    P(f"parent = {b['config']['qty_lots']:,} lots over {b['config']['horizon_s']:.0f} s in {b['config']['slices']} slices, {b['config']['episodes_per_day']} episodes, pairing unit: {b['pairing_unit']}\n")
    for r in b["ranking_instability"]:
        P(f"- latency {r['latency_ms']:.0f} ms: **{r['distinct_rankings']} distinct rankings** across simulator mode × queue assumption")
        for x in r["rankings"]:
            P(f"  - `{x['ranking'].rstrip(' <')}` in {', '.join(x['cells'])}")
    P("")
    P(open(os.path.join(R, "bench.md"), encoding="utf-8").read())
bs = load("bench_synth_days.json")
if bs:
    P("\n## Cost ranking on 4 synthetic queue-reactive days (day-paired CIs)\n")
    for r in bs["ranking_instability"]:
        P(f"- latency {r['latency_ms']:.0f} ms: {r['distinct_rankings']} distinct rankings")
    P(open(os.path.join(R, "bench_synth_days.md"), encoding="utf-8").read())

fc = load("fillcurve.json")
if fc:
    P("\n## Passive fill probability (Binance replay probes)\n")
    P("| queue model | horizon | P(fill) at touch | +1 tick | +2 | +3 | touch by queue tercile (low/mid/high) |")
    P("|---|---|---|---|---|---|---|")
    for q, d in fc.items():
        for h in sorted(set(e["horizon_s"] for e in d["by_distance_horizon"])):
            row = {e["distance_ticks"]: e["p_fill"] for e in d["by_distance_horizon"] if e["horizon_s"] == h}
            terc = "/".join(f"{e['p_fill']:.2f}" for e in d.get("touch_by_queue_tercile", []))
            P(f"| {q} | {h:.0f} s | " + " | ".join(f"{row.get(k, float('nan')):.2f}" for k in (0, 1, 2, 3)) + f" | {terc} |")
    cmp_ = fc["naive_fifo"]["model_vs_empirical"]
    P("\nFill models vs empirical at the touch (naive FIFO): " + "; ".join(f"{e['horizon_s']:.0f}s: emp {e['empirical']:.2f}, state-dependent {e['structural']:.2f}, logistic {e['logistic']:.2f}, Poisson {e['poisson']:.2f}" for e in cmp_ if e["distance_ticks"] == 0))

if l3:
    P("\n## Order-level check of the L2 queue models (Bitstamp)\n")
    P(f"{l3['orders_tracked_at_touch']:,} real orders created at the touch, tracked to fill/cancel; median size {l3['median_touch_order_size_lots']/1e8:.4f} BTC.\n")
    P("| queue model | mean rel. error of queue-ahead estimate | mean |rel. error| | recall of true fills | false fills on cancelled orders |")
    P("|---|---|---|---|---|")
    for m in l3["queue_models"]:
        name = m["queue_model"] + (f" (n={m['power_n']:g})" if m["queue_model"] == "power_prob" else "")
        P(f"| {name} | {m['rel_err_mean']:+.2f} | {m['rel_err_abs_mean']:.2f} | {m['fill_recall']:.2f} | {m['false_fill_rate']:.3f} |")
    km = [e for e in l3["observed_fill_curve"] if e["queue_tercile"] == -1]
    P("\nKaplan–Meier fill probability of real touch orders (cancellation = censoring): " + ", ".join(f"{e['horizon_s']:.0f}s {e['p_fill_km']:.2f}" for e in km))
    P("Replay probes at the touch on the same tape: " + "; ".join(q + ": " + ", ".join(f"{e['horizon_s']:.0f}s {e['p_fill']:.2f}" for e in d["by_distance_horizon"]) for q, d in l3["replay_probe_fill_curve"].items()))

mo = load("markout.json")
if mo:
    P("\n## Adverse selection of passive fills (mark-outs, ticks, + = favourable)\n")
    P("| mode | queue model | P(fill) | 100 ms | 1 s | 10 s | 60 s | corr(filled, post-placement return) |")
    P("|---|---|---|---|---|---|---|---|")
    for r in mo:
        P(f"| {r['mode']} | {r['queue_model']} | {r['p_fill']:.2f} | " + " | ".join(f"{e['markout_ticks']:+.0f} ± {e['se']:.0f}" for e in r["markouts"]) + f" | {r['corr_fill_vs_return']:+.2f} |")
    P("\nQueue-position value (Moallemi–Yuan, measured): P(fill) × (half-spread + 60 s mark-out), by tercile of queue ahead at placement, naive FIFO:\n")
    P("| mode | tercile | mean queue ahead (lots) | P(fill) | 60 s mark-out | value (ticks) |")
    P("|---|---|---|---|---|---|")
    for r in mo:
        if r["queue_model"] != "naive_fifo":
            continue
        for t in r["queue_position_value"]:
            P(f"| {r['mode']} | {t['tercile']} | {t['mean_queue_ahead_lots']:,.0f} | {t['p_fill']:.2f} | {t['markout_60s_ticks']:+.0f} | {t['queue_position_value_ticks']:+.1f} |")

ofi = load("ofi.json")
if ofi:
    P("\n## OFI regression (Cont–Kukanov–Stoikov)\n")
    for k in ("1s", "10s"):
        o = ofi[k]
        P(f"- {k} buckets: β = {o['beta_ticks_per_lot']:.2e} ticks/lot, R² = {o['r2']:.2f}, n = {o['n']:,}; by hour UTC: " + ", ".join(f"{e['hour_utc']}h {e['r2']:.2f}" for e in o["by_hour_utc"]) + "; by volatility tercile: " + ", ".join(f"{e['r2']:.2f}" for e in o["by_vol_regime"]))

fx = os.path.join(R, "fix_selftest.txt"); ts = os.path.join(R, "tests.txt")
P("\n## Known-answer tests and FIX self-test\n")
if os.path.exists(ts): P("```\n" + open(ts).read().strip() + "\n```")
if os.path.exists(fx): P("```\n" + open(fx).read().strip() + "\n```")

open(os.path.join(R, "summary.md"), "w", encoding="utf-8").write("\n".join(out))
print("written", os.path.join(R, "summary.md"))
