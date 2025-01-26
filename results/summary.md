## Data capture

| venue | feed | duration | messages | sequence gaps | resyncs | disconnects | snapshot parity | recv − exchange time (p50 / p95) |
|---|---|---|---|---|---|---|---|---|
| Binance spot BTCUSDT | depth@100ms + trade (L2) | 2.80 h | 448,127 | 2 | 3 | 0 | 63,248 levels, 0 mismatched | 169 / 182 ms |
| Coinbase Exchange BTC-USD | level2_batch + matches (L2) | 2.81 h | 262,608 | 0 | 3 | 0 | n/a (absolute levels) | 105 / 140 ms |
| Bitstamp BTC/USD | live_orders + live_trades (L3, order ids) | 1.33 h | 470,571 orders, 2,691 trades | – | 1 | 0 | top-of-book vs diff_order_book: 91% agree | – |

## Calibration (Binance)

- events decomposed: 266,372 (limit 126,289, cancel 94,854, market 45,229); trades 347,343; AES = 20252 lots; σ(1 s) = 552 ticks; mean spread 1.15 ticks
- reference-price jumps (|Δmid| ≥ 5 ticks between batches): 3,790; rate by touch-imbalance bucket [0.55, 0.15, 0.1, 0.11, 0.56] /s; P(up) by bucket [0.02, 0.23, 0.45, 0.77, 0.98]
- Hawkes branching ratio 0.75; OFI β = 5.85e-04 ticks/lot, R² = 0.36; AC η = 4.72e-03 ticks per lot/s

## Simulator realism (live vs simulated, Binance)

| statistic (KS distance to live) | replay | queue_reactive | zero_intelligence | hawkes |
|---|---|---|---|---|
| spread | 0.00 | 0.01 | 0.11 | 0.09 |
| vol_at_best | 0.00 | 0.09 | 0.17 | 0.17 |
| depth5 | 0.00 | 0.19 | 0.12 | 0.07 |
| imbalance | 0.00 | 0.18 | 0.14 | 0.16 |
| trade_iat_ms | 0.00 | 0.32 | 0.32 | 0.09 |
| ret_1s | 0.00 | 0.03 | 0.11 | 0.11 |
| trade_size | 0.00 | 0.07 | 0.06 | 0.08 |
| σ(1 s) ticks (live 555) | 555 | 333 | 443 | 444 |
| kurtosis of 1 s returns (live 173) | 173 | 46.6 | 34.3 | 24.6 |
| ACF |r| lag 1 (live 0.127) | 0.127 | -0.0247 | -0.0028 | -0.012 |
| trade-sign ACF lag 1 (live 0.428) | 0.428 | 0.0488 | 0.00347 | 0.424 |
| P(|r| > 4σ) (live 0.00901) | 0.00901 | 0.0121 | 0.00971 | 0.0104 |
| impact response R(1)…R(100) ticks (live 34…743) | 34…743 | 5…22 | 1…-5 | -0…-29 |

## Cost ranking (Binance replay day)

parent = 300,000 lots over 300 s in 10 slices, 12 episodes, pairing unit: episode (only one day available: CI is over episodes, not days)

- latency 50 ms: **3 distinct rankings** across simulator mode × queue assumption
  - `OFIAdaptive_CKadv < AlmgrenChriss < TWAP < VWAP < OFIAdaptive_CK` in replay/naive_fifo, replay/power_prob, replay/risk_averse
  - `TWAP < VWAP < OFIAdaptive_CK < OFIAdaptive_CKadv < AlmgrenChriss` in queue_reactive/naive_fifo, queue_reactive/power_prob, queue_reactive/risk_averse
  - `VWAP < TWAP < OFIAdaptive_CKadv < AlmgrenChriss < OFIAdaptive_CK` in zero_intelligence/naive_fifo, zero_intelligence/power_prob, zero_intelligence/risk_averse
- latency 100 ms: **3 distinct rankings** across simulator mode × queue assumption
  - `OFIAdaptive_CKadv < AlmgrenChriss < TWAP < VWAP < OFIAdaptive_CK` in replay/naive_fifo, replay/power_prob, replay/risk_averse
  - `VWAP < TWAP < OFIAdaptive_CKadv < AlmgrenChriss < OFIAdaptive_CK` in queue_reactive/naive_fifo, queue_reactive/power_prob, queue_reactive/risk_averse, zero_intelligence/naive_fifo, zero_intelligence/risk_averse
  - `VWAP < TWAP < OFIAdaptive_CKadv < OFIAdaptive_CK < AlmgrenChriss` in zero_intelligence/power_prob


### Latency 50 ms one-way

| mode | queue model | algo | IS bp (mean) | 95% CI | vs TWAP bp | 95% CI | spread | timing | impact | opp. | fees | passive % | rank |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| replay | naive_fifo | TWAP | 6.24 | [2.61, 9.65] | +0.00 | [0.00, 0.00] | 0.03 | 6.21 | 0.00 | 0.00 | 0.00 | 0% | 3 |
| replay | naive_fifo | VWAP | 6.43 | [2.71, 9.86] | +0.19 | [-0.18, 0.63] | 0.03 | 6.40 | 0.00 | 0.00 | 0.00 | 0% | 4 |
| replay | naive_fifo | AlmgrenChriss | 2.15 | [0.81, 3.61] | -4.10 | [-6.64, -1.38] | 0.18 | 1.96 | 0.00 | 0.00 | 0.00 | 0% | 2 |
| replay | naive_fifo | OFIAdaptive_CK | 7.17 | [3.30, 11.55] | +0.93 | [-2.47, 3.93] | -0.18 | 7.36 | 0.00 | 0.00 | 0.00 | 94% | 5 |
| replay | naive_fifo | OFIAdaptive_CKadv | 2.00 | [0.67, 3.42] | -4.24 | [-6.80, -1.50] | 0.20 | 1.80 | 0.00 | 0.00 | 0.00 | 0% | 1 |
| replay | power_prob | TWAP | 6.24 | [2.61, 9.65] | +0.00 | [0.00, 0.00] | 0.03 | 6.21 | 0.00 | 0.00 | 0.00 | 0% | 3 |
| replay | power_prob | VWAP | 6.43 | [2.71, 9.86] | +0.19 | [-0.18, 0.63] | 0.03 | 6.40 | 0.00 | 0.00 | 0.00 | 0% | 4 |
| replay | power_prob | AlmgrenChriss | 2.15 | [0.81, 3.61] | -4.10 | [-6.64, -1.38] | 0.18 | 1.96 | 0.00 | 0.00 | 0.00 | 0% | 2 |
| replay | power_prob | OFIAdaptive_CK | 7.06 | [3.22, 11.41] | +0.82 | [-2.62, 3.79] | -0.19 | 7.25 | -0.01 | 0.00 | 0.00 | 94% | 5 |
| replay | power_prob | OFIAdaptive_CKadv | 2.00 | [0.67, 3.42] | -4.24 | [-6.80, -1.50] | 0.20 | 1.80 | 0.00 | 0.00 | 0.00 | 0% | 1 |
| replay | risk_averse | TWAP | 6.24 | [2.61, 9.65] | +0.00 | [0.00, 0.00] | 0.03 | 6.21 | 0.00 | 0.00 | 0.00 | 0% | 3 |
| replay | risk_averse | VWAP | 6.43 | [2.71, 9.86] | +0.19 | [-0.18, 0.63] | 0.03 | 6.40 | 0.00 | 0.00 | 0.00 | 0% | 4 |
| replay | risk_averse | AlmgrenChriss | 2.15 | [0.81, 3.61] | -4.10 | [-6.64, -1.38] | 0.18 | 1.96 | 0.00 | 0.00 | 0.00 | 0% | 2 |
| replay | risk_averse | OFIAdaptive_CK | 7.17 | [3.30, 11.55] | +0.93 | [-2.47, 3.93] | -0.18 | 7.36 | 0.00 | 0.00 | 0.00 | 94% | 5 |
| replay | risk_averse | OFIAdaptive_CKadv | 2.00 | [0.67, 3.42] | -4.24 | [-6.80, -1.50] | 0.20 | 1.80 | 0.00 | 0.00 | 0.00 | 0% | 1 |
| queue_reactive | naive_fifo | TWAP | -0.89 | [-2.91, 1.53] | +0.00 | [0.00, 0.00] | 0.00 | 0.38 | -1.28 | 0.00 | 0.00 | 0% | 1 |
| queue_reactive | naive_fifo | VWAP | -0.67 | [-3.08, 2.01] | +0.23 | [-0.96, 1.62] | 0.00 | 0.41 | -1.08 | 0.00 | 0.00 | 0% | 2 |
| queue_reactive | naive_fifo | AlmgrenChriss | 0.82 | [-0.13, 1.85] | +1.72 | [-0.26, 3.31] | 0.00 | 0.18 | 0.65 | 0.00 | 0.00 | 0% | 5 |
| queue_reactive | naive_fifo | OFIAdaptive_CK | 0.43 | [-0.23, 1.11] | +1.32 | [-1.23, 3.57] | -0.00 | 0.08 | 0.35 | 0.00 | 0.00 | 95% | 3 |
| queue_reactive | naive_fifo | OFIAdaptive_CKadv | 0.66 | [-0.25, 1.71] | +1.56 | [-0.35, 3.17] | 0.00 | 0.24 | 0.42 | 0.00 | 0.00 | 0% | 4 |
| queue_reactive | power_prob | TWAP | -0.89 | [-2.91, 1.53] | +0.00 | [0.00, 0.00] | 0.00 | 0.38 | -1.28 | 0.00 | 0.00 | 0% | 1 |
| queue_reactive | power_prob | VWAP | -0.67 | [-3.08, 2.01] | +0.23 | [-0.96, 1.62] | 0.00 | 0.41 | -1.08 | 0.00 | 0.00 | 0% | 2 |
| queue_reactive | power_prob | AlmgrenChriss | 0.82 | [-0.13, 1.85] | +1.72 | [-0.26, 3.31] | 0.00 | 0.18 | 0.65 | 0.00 | 0.00 | 0% | 5 |
| queue_reactive | power_prob | OFIAdaptive_CK | 0.31 | [-0.33, 0.98] | +1.20 | [-1.30, 3.42] | -0.00 | 0.16 | 0.15 | 0.00 | 0.00 | 95% | 3 |
| queue_reactive | power_prob | OFIAdaptive_CKadv | 0.66 | [-0.25, 1.71] | +1.56 | [-0.35, 3.17] | 0.00 | 0.24 | 0.42 | 0.00 | 0.00 | 0% | 4 |
| queue_reactive | risk_averse | TWAP | -0.89 | [-2.91, 1.53] | +0.00 | [0.00, 0.00] | 0.00 | 0.38 | -1.28 | 0.00 | 0.00 | 0% | 1 |
| queue_reactive | risk_averse | VWAP | -0.67 | [-3.08, 2.01] | +0.23 | [-0.96, 1.62] | 0.00 | 0.41 | -1.08 | 0.00 | 0.00 | 0% | 2 |
| queue_reactive | risk_averse | AlmgrenChriss | 0.82 | [-0.13, 1.85] | +1.72 | [-0.26, 3.31] | 0.00 | 0.18 | 0.65 | 0.00 | 0.00 | 0% | 5 |
| queue_reactive | risk_averse | OFIAdaptive_CK | 0.42 | [-0.23, 1.10] | +1.32 | [-1.22, 3.56] | -0.00 | 0.09 | 0.34 | 0.00 | 0.00 | 95% | 3 |
| queue_reactive | risk_averse | OFIAdaptive_CKadv | 0.66 | [-0.25, 1.71] | +1.56 | [-0.35, 3.17] | 0.00 | 0.24 | 0.42 | 0.00 | 0.00 | 0% | 4 |
| zero_intelligence | naive_fifo | TWAP | -0.98 | [-4.13, 2.08] | +0.00 | [0.00, 0.00] | 0.00 | -0.08 | -0.91 | 0.00 | 0.00 | 0% | 2 |
| zero_intelligence | naive_fifo | VWAP | -1.09 | [-4.69, 2.82] | -0.11 | [-1.16, 1.14] | 0.00 | -0.15 | -0.94 | 0.00 | 0.00 | 0% | 1 |
| zero_intelligence | naive_fifo | AlmgrenChriss | -0.24 | [-0.96, 0.56] | +0.74 | [-1.88, 3.54] | 0.00 | 0.13 | -0.37 | 0.00 | 0.00 | 0% | 4 |
| zero_intelligence | naive_fifo | OFIAdaptive_CK | -0.07 | [-0.90, 0.91] | +0.92 | [-2.10, 3.85] | -0.00 | 0.42 | -0.49 | 0.00 | 0.00 | 100% | 5 |
| zero_intelligence | naive_fifo | OFIAdaptive_CKadv | -0.41 | [-1.12, 0.41] | +0.58 | [-1.95, 3.27] | 0.00 | -0.09 | -0.31 | 0.00 | 0.00 | 0% | 3 |
| zero_intelligence | power_prob | TWAP | -0.98 | [-4.13, 2.08] | +0.00 | [0.00, 0.00] | 0.00 | -0.08 | -0.91 | 0.00 | 0.00 | 0% | 2 |
| zero_intelligence | power_prob | VWAP | -1.09 | [-4.69, 2.82] | -0.11 | [-1.16, 1.14] | 0.00 | -0.15 | -0.94 | 0.00 | 0.00 | 0% | 1 |
| zero_intelligence | power_prob | AlmgrenChriss | -0.24 | [-0.96, 0.56] | +0.74 | [-1.88, 3.54] | 0.00 | 0.13 | -0.37 | 0.00 | 0.00 | 0% | 4 |
| zero_intelligence | power_prob | OFIAdaptive_CK | -0.17 | [-1.00, 0.82] | +0.82 | [-2.22, 3.81] | -0.00 | 0.37 | -0.54 | 0.00 | 0.00 | 100% | 5 |
| zero_intelligence | power_prob | OFIAdaptive_CKadv | -0.41 | [-1.12, 0.41] | +0.58 | [-1.95, 3.27] | 0.00 | -0.09 | -0.31 | 0.00 | 0.00 | 0% | 3 |
| zero_intelligence | risk_averse | TWAP | -0.98 | [-4.13, 2.08] | +0.00 | [0.00, 0.00] | 0.00 | -0.08 | -0.91 | 0.00 | 0.00 | 0% | 2 |
| zero_intelligence | risk_averse | VWAP | -1.09 | [-4.69, 2.82] | -0.11 | [-1.16, 1.14] | 0.00 | -0.15 | -0.94 | 0.00 | 0.00 | 0% | 1 |
| zero_intelligence | risk_averse | AlmgrenChriss | -0.24 | [-0.96, 0.56] | +0.74 | [-1.88, 3.54] | 0.00 | 0.13 | -0.37 | 0.00 | 0.00 | 0% | 4 |
| zero_intelligence | risk_averse | OFIAdaptive_CK | -0.07 | [-0.90, 0.91] | +0.92 | [-2.10, 3.85] | -0.00 | 0.42 | -0.49 | 0.00 | 0.00 | 100% | 5 |
| zero_intelligence | risk_averse | OFIAdaptive_CKadv | -0.41 | [-1.12, 0.41] | +0.58 | [-1.95, 3.27] | 0.00 | -0.09 | -0.31 | 0.00 | 0.00 | 0% | 3 |

### Latency 100 ms one-way

| mode | queue model | algo | IS bp (mean) | 95% CI | vs TWAP bp | 95% CI | spread | timing | impact | opp. | fees | passive % | rank |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| replay | naive_fifo | TWAP | 6.24 | [2.61, 9.64] | +0.00 | [0.00, 0.00] | 0.03 | 6.22 | -0.00 | 0.00 | 0.00 | 0% | 3 |
| replay | naive_fifo | VWAP | 6.43 | [2.70, 9.86] | +0.19 | [-0.17, 0.63] | 0.03 | 6.40 | -0.00 | 0.00 | 0.00 | 0% | 4 |
| replay | naive_fifo | AlmgrenChriss | 2.14 | [0.78, 3.60] | -4.10 | [-6.64, -1.40] | 0.18 | 1.97 | -0.01 | 0.00 | 0.00 | 0% | 2 |
| replay | naive_fifo | OFIAdaptive_CK | 7.17 | [3.28, 11.55] | +0.93 | [-2.48, 3.93] | -0.19 | 7.36 | 0.00 | 0.00 | 0.00 | 93% | 5 |
| replay | naive_fifo | OFIAdaptive_CKadv | 2.00 | [0.66, 3.42] | -4.24 | [-6.80, -1.50] | 0.20 | 1.81 | -0.01 | 0.00 | 0.00 | 0% | 1 |
| replay | power_prob | TWAP | 6.24 | [2.61, 9.64] | +0.00 | [0.00, 0.00] | 0.03 | 6.22 | -0.00 | 0.00 | 0.00 | 0% | 3 |
| replay | power_prob | VWAP | 6.43 | [2.70, 9.86] | +0.19 | [-0.17, 0.63] | 0.03 | 6.40 | -0.00 | 0.00 | 0.00 | 0% | 4 |
| replay | power_prob | AlmgrenChriss | 2.14 | [0.78, 3.60] | -4.10 | [-6.64, -1.40] | 0.18 | 1.97 | -0.01 | 0.00 | 0.00 | 0% | 2 |
| replay | power_prob | OFIAdaptive_CK | 7.05 | [3.22, 11.40] | +0.82 | [-2.62, 3.79] | -0.19 | 7.25 | -0.01 | 0.00 | 0.00 | 93% | 5 |
| replay | power_prob | OFIAdaptive_CKadv | 2.00 | [0.66, 3.42] | -4.24 | [-6.80, -1.50] | 0.20 | 1.81 | -0.01 | 0.00 | 0.00 | 0% | 1 |
| replay | risk_averse | TWAP | 6.24 | [2.61, 9.64] | +0.00 | [0.00, 0.00] | 0.03 | 6.22 | -0.00 | 0.00 | 0.00 | 0% | 3 |
| replay | risk_averse | VWAP | 6.43 | [2.70, 9.86] | +0.19 | [-0.17, 0.63] | 0.03 | 6.40 | -0.00 | 0.00 | 0.00 | 0% | 4 |
| replay | risk_averse | AlmgrenChriss | 2.14 | [0.78, 3.60] | -4.10 | [-6.64, -1.40] | 0.18 | 1.97 | -0.01 | 0.00 | 0.00 | 0% | 2 |
| replay | risk_averse | OFIAdaptive_CK | 7.17 | [3.28, 11.55] | +0.93 | [-2.47, 3.93] | -0.19 | 7.36 | 0.00 | 0.00 | 0.00 | 93% | 5 |
| replay | risk_averse | OFIAdaptive_CKadv | 2.00 | [0.66, 3.42] | -4.24 | [-6.80, -1.50] | 0.20 | 1.81 | -0.01 | 0.00 | 0.00 | 0% | 1 |
| queue_reactive | naive_fifo | TWAP | 0.36 | [-1.36, 2.21] | +0.00 | [0.00, 0.00] | 0.00 | 0.40 | -0.04 | 0.00 | 0.00 | 0% | 2 |
| queue_reactive | naive_fifo | VWAP | -0.62 | [-2.18, 1.21] | -0.97 | [-2.00, -0.19] | 0.00 | 0.43 | -1.04 | 0.00 | 0.00 | 0% | 1 |
| queue_reactive | naive_fifo | AlmgrenChriss | 0.54 | [-0.31, 1.53] | +0.18 | [-1.90, 2.18] | 0.00 | 0.17 | 0.37 | 0.00 | 0.00 | 0% | 4 |
| queue_reactive | naive_fifo | OFIAdaptive_CK | 0.97 | [0.22, 1.78] | +0.61 | [-1.17, 2.41] | -0.00 | 0.12 | 0.85 | 0.00 | 0.00 | 92% | 5 |
| queue_reactive | naive_fifo | OFIAdaptive_CKadv | 0.41 | [-0.36, 1.40] | +0.06 | [-1.96, 1.99] | 0.00 | 0.16 | 0.25 | 0.00 | 0.00 | 0% | 3 |
| queue_reactive | power_prob | TWAP | 0.36 | [-1.36, 2.21] | +0.00 | [0.00, 0.00] | 0.00 | 0.40 | -0.04 | 0.00 | 0.00 | 0% | 2 |
| queue_reactive | power_prob | VWAP | -0.62 | [-2.18, 1.21] | -0.97 | [-2.00, -0.19] | 0.00 | 0.43 | -1.04 | 0.00 | 0.00 | 0% | 1 |
| queue_reactive | power_prob | AlmgrenChriss | 0.54 | [-0.31, 1.53] | +0.18 | [-1.90, 2.18] | 0.00 | 0.17 | 0.37 | 0.00 | 0.00 | 0% | 4 |
| queue_reactive | power_prob | OFIAdaptive_CK | 1.25 | [0.25, 2.39] | +0.89 | [-0.80, 2.52] | -0.00 | 0.05 | 1.19 | 0.00 | 0.00 | 94% | 5 |
| queue_reactive | power_prob | OFIAdaptive_CKadv | 0.41 | [-0.36, 1.40] | +0.06 | [-1.96, 1.99] | 0.00 | 0.16 | 0.25 | 0.00 | 0.00 | 0% | 3 |
| queue_reactive | risk_averse | TWAP | 0.36 | [-1.36, 2.21] | +0.00 | [0.00, 0.00] | 0.00 | 0.40 | -0.04 | 0.00 | 0.00 | 0% | 2 |
| queue_reactive | risk_averse | VWAP | -0.62 | [-2.18, 1.21] | -0.97 | [-2.00, -0.19] | 0.00 | 0.43 | -1.04 | 0.00 | 0.00 | 0% | 1 |
| queue_reactive | risk_averse | AlmgrenChriss | 0.54 | [-0.31, 1.53] | +0.18 | [-1.90, 2.18] | 0.00 | 0.17 | 0.37 | 0.00 | 0.00 | 0% | 4 |
| queue_reactive | risk_averse | OFIAdaptive_CK | 1.31 | [0.30, 2.44] | +0.96 | [-0.72, 2.63] | 0.00 | 0.06 | 1.25 | 0.00 | 0.00 | 92% | 5 |
| queue_reactive | risk_averse | OFIAdaptive_CKadv | 0.41 | [-0.36, 1.40] | +0.06 | [-1.96, 1.99] | 0.00 | 0.16 | 0.25 | 0.00 | 0.00 | 0% | 3 |
| zero_intelligence | naive_fifo | TWAP | -0.80 | [-3.83, 2.19] | +0.00 | [0.00, 0.00] | 0.00 | -0.03 | -0.77 | 0.00 | 0.00 | 0% | 2 |
| zero_intelligence | naive_fifo | VWAP | -1.96 | [-5.27, 1.22] | -1.16 | [-2.10, -0.36] | 0.00 | -0.19 | -1.78 | 0.00 | 0.00 | 0% | 1 |
| zero_intelligence | naive_fifo | AlmgrenChriss | -0.21 | [-0.97, 0.68] | +0.60 | [-1.82, 3.21] | 0.00 | 0.30 | -0.50 | 0.00 | 0.00 | 0% | 4 |
| zero_intelligence | naive_fifo | OFIAdaptive_CK | -0.20 | [-0.73, 0.50] | +0.60 | [-2.28, 3.39] | -0.00 | 0.23 | -0.43 | 0.00 | 0.00 | 100% | 5 |
| zero_intelligence | naive_fifo | OFIAdaptive_CKadv | -0.44 | [-1.19, 0.40] | +0.36 | [-1.99, 2.92] | 0.00 | 0.05 | -0.49 | 0.00 | 0.00 | 0% | 3 |
| zero_intelligence | power_prob | TWAP | -0.80 | [-3.83, 2.19] | +0.00 | [0.00, 0.00] | 0.00 | -0.03 | -0.77 | 0.00 | 0.00 | 0% | 2 |
| zero_intelligence | power_prob | VWAP | -1.96 | [-5.27, 1.22] | -1.16 | [-2.10, -0.36] | 0.00 | -0.19 | -1.78 | 0.00 | 0.00 | 0% | 1 |
| zero_intelligence | power_prob | AlmgrenChriss | -0.21 | [-0.97, 0.68] | +0.60 | [-1.82, 3.21] | 0.00 | 0.30 | -0.50 | 0.00 | 0.00 | 0% | 5 |
| zero_intelligence | power_prob | OFIAdaptive_CK | -0.30 | [-0.83, 0.42] | +0.50 | [-2.38, 3.35] | -0.00 | 0.18 | -0.47 | 0.00 | 0.00 | 100% | 4 |
| zero_intelligence | power_prob | OFIAdaptive_CKadv | -0.44 | [-1.19, 0.40] | +0.36 | [-1.99, 2.92] | 0.00 | 0.05 | -0.49 | 0.00 | 0.00 | 0% | 3 |
| zero_intelligence | risk_averse | TWAP | -0.80 | [-3.83, 2.19] | +0.00 | [0.00, 0.00] | 0.00 | -0.03 | -0.77 | 0.00 | 0.00 | 0% | 2 |
| zero_intelligence | risk_averse | VWAP | -1.96 | [-5.27, 1.22] | -1.16 | [-2.10, -0.36] | 0.00 | -0.19 | -1.78 | 0.00 | 0.00 | 0% | 1 |
| zero_intelligence | risk_averse | AlmgrenChriss | -0.21 | [-0.97, 0.68] | +0.60 | [-1.82, 3.21] | 0.00 | 0.30 | -0.50 | 0.00 | 0.00 | 0% | 4 |
| zero_intelligence | risk_averse | OFIAdaptive_CK | -0.20 | [-0.73, 0.50] | +0.60 | [-2.28, 3.39] | -0.00 | 0.23 | -0.43 | 0.00 | 0.00 | 100% | 5 |
| zero_intelligence | risk_averse | OFIAdaptive_CKadv | -0.44 | [-1.19, 0.40] | +0.36 | [-1.99, 2.92] | 0.00 | 0.05 | -0.49 | 0.00 | 0.00 | 0% | 3 |


## Cost ranking on 4 synthetic queue-reactive days (day-paired CIs)

- latency 50 ms: 2 distinct rankings
- latency 100 ms: 3 distinct rankings

### Latency 50 ms one-way

| mode | queue model | algo | IS bp (mean) | 95% CI | vs TWAP bp | 95% CI | spread | timing | impact | opp. | fees | passive % | rank |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| replay | naive_fifo | TWAP | 0.65 | [-0.73, 2.15] | +0.00 | [0.00, 0.00] | 0.00 | 0.65 | 0.00 | 0.00 | 0.00 | 0% | 4 |
| replay | naive_fifo | VWAP | 0.65 | [-0.73, 2.15] | +0.00 | [0.00, 0.00] | 0.00 | 0.65 | 0.00 | 0.00 | 0.00 | 0% | 5 |
| replay | naive_fifo | AlmgrenChriss | 0.00 | [-0.44, 0.48] | -0.65 | [-1.67, 0.32] | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0% | 2 |
| replay | naive_fifo | OFIAdaptive_CK | 0.59 | [0.29, 0.81] | -0.06 | [-1.37, 1.04] | 0.48 | 0.10 | 0.00 | 0.00 | 0.00 | 100% | 3 |
| replay | naive_fifo | OFIAdaptive_CKadv | -0.06 | [-0.48, 0.41] | -0.71 | [-1.74, 0.25] | 0.00 | -0.06 | 0.00 | 0.00 | 0.00 | 0% | 1 |
| replay | power_prob | TWAP | 0.65 | [-0.73, 2.15] | +0.00 | [0.00, 0.00] | 0.00 | 0.65 | 0.00 | 0.00 | 0.00 | 0% | 4 |
| replay | power_prob | VWAP | 0.65 | [-0.73, 2.15] | +0.00 | [0.00, 0.00] | 0.00 | 0.65 | 0.00 | 0.00 | 0.00 | 0% | 5 |
| replay | power_prob | AlmgrenChriss | 0.00 | [-0.44, 0.48] | -0.65 | [-1.67, 0.32] | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0% | 2 |
| replay | power_prob | OFIAdaptive_CK | 0.46 | [0.13, 0.78] | -0.20 | [-1.45, 0.87] | 0.43 | 0.05 | -0.02 | 0.00 | 0.00 | 100% | 3 |
| replay | power_prob | OFIAdaptive_CKadv | -0.06 | [-0.48, 0.41] | -0.71 | [-1.74, 0.25] | 0.00 | -0.06 | 0.00 | 0.00 | 0.00 | 0% | 1 |
| replay | risk_averse | TWAP | 0.65 | [-0.73, 2.15] | +0.00 | [0.00, 0.00] | 0.00 | 0.65 | 0.00 | 0.00 | 0.00 | 0% | 4 |
| replay | risk_averse | VWAP | 0.65 | [-0.73, 2.15] | +0.00 | [0.00, 0.00] | 0.00 | 0.65 | 0.00 | 0.00 | 0.00 | 0% | 5 |
| replay | risk_averse | AlmgrenChriss | 0.00 | [-0.44, 0.48] | -0.65 | [-1.67, 0.32] | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0% | 2 |
| replay | risk_averse | OFIAdaptive_CK | 0.60 | [0.30, 0.83] | -0.05 | [-1.34, 1.04] | 0.49 | 0.11 | 0.00 | 0.00 | 0.00 | 100% | 3 |
| replay | risk_averse | OFIAdaptive_CKadv | -0.06 | [-0.48, 0.41] | -0.71 | [-1.74, 0.25] | 0.00 | -0.06 | 0.00 | 0.00 | 0.00 | 0% | 1 |
| queue_reactive | naive_fifo | TWAP | -0.20 | [-1.53, 1.13] | +0.00 | [0.00, 0.00] | 0.00 | 0.34 | -0.54 | 0.00 | 0.00 | 0% | 1 |
| queue_reactive | naive_fifo | VWAP | -0.20 | [-1.53, 1.13] | +0.00 | [0.00, 0.00] | 0.00 | 0.34 | -0.54 | 0.00 | 0.00 | 0% | 2 |
| queue_reactive | naive_fifo | AlmgrenChriss | 0.14 | [-0.26, 0.45] | +0.35 | [-0.96, 1.50] | 0.00 | -0.09 | 0.23 | 0.00 | 0.00 | 0% | 4 |
| queue_reactive | naive_fifo | OFIAdaptive_CK | 0.97 | [0.54, 1.44] | +1.17 | [0.02, 2.33] | -0.00 | -0.30 | 1.27 | 0.00 | 0.00 | 100% | 5 |
| queue_reactive | naive_fifo | OFIAdaptive_CKadv | 0.06 | [-0.40, 0.46] | +0.26 | [-1.13, 1.48] | 0.00 | -0.10 | 0.16 | 0.00 | 0.00 | 0% | 3 |
| queue_reactive | power_prob | TWAP | -0.20 | [-1.53, 1.13] | +0.00 | [0.00, 0.00] | 0.00 | 0.34 | -0.54 | 0.00 | 0.00 | 0% | 1 |
| queue_reactive | power_prob | VWAP | -0.20 | [-1.53, 1.13] | +0.00 | [0.00, 0.00] | 0.00 | 0.34 | -0.54 | 0.00 | 0.00 | 0% | 2 |
| queue_reactive | power_prob | AlmgrenChriss | 0.14 | [-0.26, 0.45] | +0.35 | [-0.96, 1.50] | 0.00 | -0.09 | 0.23 | 0.00 | 0.00 | 0% | 4 |
| queue_reactive | power_prob | OFIAdaptive_CK | 1.12 | [0.62, 1.89] | +1.33 | [0.29, 2.36] | -0.00 | -0.43 | 1.55 | 0.00 | 0.00 | 100% | 5 |
| queue_reactive | power_prob | OFIAdaptive_CKadv | 0.06 | [-0.40, 0.46] | +0.26 | [-1.13, 1.48] | 0.00 | -0.10 | 0.16 | 0.00 | 0.00 | 0% | 3 |
| queue_reactive | risk_averse | TWAP | -0.20 | [-1.53, 1.13] | +0.00 | [0.00, 0.00] | 0.00 | 0.34 | -0.54 | 0.00 | 0.00 | 0% | 1 |
| queue_reactive | risk_averse | VWAP | -0.20 | [-1.53, 1.13] | +0.00 | [0.00, 0.00] | 0.00 | 0.34 | -0.54 | 0.00 | 0.00 | 0% | 2 |
| queue_reactive | risk_averse | AlmgrenChriss | 0.14 | [-0.26, 0.45] | +0.35 | [-0.96, 1.50] | 0.00 | -0.09 | 0.23 | 0.00 | 0.00 | 0% | 4 |
| queue_reactive | risk_averse | OFIAdaptive_CK | 0.97 | [0.55, 1.44] | +1.17 | [0.03, 2.31] | -0.00 | -0.30 | 1.27 | 0.00 | 0.00 | 100% | 5 |
| queue_reactive | risk_averse | OFIAdaptive_CKadv | 0.06 | [-0.40, 0.46] | +0.26 | [-1.13, 1.48] | 0.00 | -0.10 | 0.16 | 0.00 | 0.00 | 0% | 3 |
| zero_intelligence | naive_fifo | TWAP | -1.68 | [-3.65, 0.26] | +0.00 | [0.00, 0.00] | 0.00 | -2.19 | 0.50 | 0.00 | 0.00 | 0% | 1 |
| zero_intelligence | naive_fifo | VWAP | -1.68 | [-3.65, 0.26] | +0.00 | [0.00, 0.00] | 0.00 | -2.19 | 0.50 | 0.00 | 0.00 | 0% | 2 |
| zero_intelligence | naive_fifo | AlmgrenChriss | -0.24 | [-0.59, 0.32] | +1.45 | [0.05, 3.06] | 0.00 | -0.16 | -0.08 | 0.00 | 0.00 | 0% | 4 |
| zero_intelligence | naive_fifo | OFIAdaptive_CK | 0.39 | [-0.23, 1.01] | +2.07 | [0.26, 3.70] | -0.00 | 0.11 | 0.28 | 0.00 | 0.00 | 99% | 5 |
| zero_intelligence | naive_fifo | OFIAdaptive_CKadv | -0.28 | [-0.78, 0.28] | +1.40 | [0.02, 2.86] | 0.00 | -0.26 | -0.02 | 0.00 | 0.00 | 0% | 3 |
| zero_intelligence | power_prob | TWAP | -1.68 | [-3.65, 0.26] | +0.00 | [0.00, 0.00] | 0.00 | -2.19 | 0.50 | 0.00 | 0.00 | 0% | 1 |
| zero_intelligence | power_prob | VWAP | -1.68 | [-3.65, 0.26] | +0.00 | [0.00, 0.00] | 0.00 | -2.19 | 0.50 | 0.00 | 0.00 | 0% | 2 |
| zero_intelligence | power_prob | AlmgrenChriss | -0.24 | [-0.59, 0.32] | +1.45 | [0.05, 3.06] | 0.00 | -0.16 | -0.08 | 0.00 | 0.00 | 0% | 4 |
| zero_intelligence | power_prob | OFIAdaptive_CK | 0.32 | [-0.28, 0.96] | +2.01 | [0.15, 3.66] | -0.00 | 0.04 | 0.28 | 0.00 | 0.00 | 99% | 5 |
| zero_intelligence | power_prob | OFIAdaptive_CKadv | -0.28 | [-0.78, 0.28] | +1.40 | [0.02, 2.86] | 0.00 | -0.26 | -0.02 | 0.00 | 0.00 | 0% | 3 |
| zero_intelligence | risk_averse | TWAP | -1.68 | [-3.65, 0.26] | +0.00 | [0.00, 0.00] | 0.00 | -2.19 | 0.50 | 0.00 | 0.00 | 0% | 1 |
| zero_intelligence | risk_averse | VWAP | -1.68 | [-3.65, 0.26] | +0.00 | [0.00, 0.00] | 0.00 | -2.19 | 0.50 | 0.00 | 0.00 | 0% | 2 |
| zero_intelligence | risk_averse | AlmgrenChriss | -0.24 | [-0.59, 0.32] | +1.45 | [0.05, 3.06] | 0.00 | -0.16 | -0.08 | 0.00 | 0.00 | 0% | 4 |
| zero_intelligence | risk_averse | OFIAdaptive_CK | 0.37 | [-0.23, 0.97] | +2.05 | [0.24, 3.70] | -0.00 | 0.13 | 0.24 | 0.00 | 0.00 | 99% | 5 |
| zero_intelligence | risk_averse | OFIAdaptive_CKadv | -0.28 | [-0.78, 0.28] | +1.40 | [0.02, 2.86] | 0.00 | -0.26 | -0.02 | 0.00 | 0.00 | 0% | 3 |

### Latency 100 ms one-way

| mode | queue model | algo | IS bp (mean) | 95% CI | vs TWAP bp | 95% CI | spread | timing | impact | opp. | fees | passive % | rank |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| replay | naive_fifo | TWAP | 0.66 | [-0.73, 2.16] | +0.00 | [0.00, 0.00] | 0.00 | 0.66 | 0.00 | 0.00 | 0.00 | 0% | 4 |
| replay | naive_fifo | VWAP | 0.66 | [-0.73, 2.16] | +0.00 | [0.00, 0.00] | 0.00 | 0.66 | 0.00 | 0.00 | 0.00 | 0% | 5 |
| replay | naive_fifo | AlmgrenChriss | 0.08 | [-0.43, 0.60] | -0.59 | [-1.56, 0.34] | 0.00 | 0.07 | 0.00 | 0.00 | 0.00 | 0% | 2 |
| replay | naive_fifo | OFIAdaptive_CK | 0.59 | [0.29, 0.81] | -0.07 | [-1.38, 1.03] | 0.48 | 0.10 | 0.00 | 0.00 | 0.00 | 100% | 3 |
| replay | naive_fifo | OFIAdaptive_CKadv | 0.01 | [-0.47, 0.53] | -0.65 | [-1.63, 0.27] | 0.00 | 0.01 | 0.00 | 0.00 | 0.00 | 0% | 1 |
| replay | power_prob | TWAP | 0.66 | [-0.73, 2.16] | +0.00 | [0.00, 0.00] | 0.00 | 0.66 | 0.00 | 0.00 | 0.00 | 0% | 4 |
| replay | power_prob | VWAP | 0.66 | [-0.73, 2.16] | +0.00 | [0.00, 0.00] | 0.00 | 0.66 | 0.00 | 0.00 | 0.00 | 0% | 5 |
| replay | power_prob | AlmgrenChriss | 0.08 | [-0.43, 0.60] | -0.59 | [-1.56, 0.34] | 0.00 | 0.07 | 0.00 | 0.00 | 0.00 | 0% | 2 |
| replay | power_prob | OFIAdaptive_CK | 0.46 | [0.13, 0.78] | -0.20 | [-1.45, 0.86] | 0.43 | 0.05 | -0.02 | 0.00 | 0.00 | 100% | 3 |
| replay | power_prob | OFIAdaptive_CKadv | 0.01 | [-0.47, 0.53] | -0.65 | [-1.63, 0.27] | 0.00 | 0.01 | 0.00 | 0.00 | 0.00 | 0% | 1 |
| replay | risk_averse | TWAP | 0.66 | [-0.73, 2.16] | +0.00 | [0.00, 0.00] | 0.00 | 0.66 | 0.00 | 0.00 | 0.00 | 0% | 4 |
| replay | risk_averse | VWAP | 0.66 | [-0.73, 2.16] | +0.00 | [0.00, 0.00] | 0.00 | 0.66 | 0.00 | 0.00 | 0.00 | 0% | 5 |
| replay | risk_averse | AlmgrenChriss | 0.08 | [-0.43, 0.60] | -0.59 | [-1.56, 0.34] | 0.00 | 0.07 | 0.00 | 0.00 | 0.00 | 0% | 2 |
| replay | risk_averse | OFIAdaptive_CK | 0.60 | [0.30, 0.83] | -0.06 | [-1.35, 1.03] | 0.49 | 0.11 | 0.00 | 0.00 | 0.00 | 100% | 3 |
| replay | risk_averse | OFIAdaptive_CKadv | 0.01 | [-0.47, 0.53] | -0.65 | [-1.63, 0.27] | 0.00 | 0.01 | 0.00 | 0.00 | 0.00 | 0% | 1 |
| queue_reactive | naive_fifo | TWAP | 0.97 | [0.65, 1.28] | +0.00 | [0.00, 0.00] | 0.00 | 0.35 | 0.61 | 0.00 | 0.00 | 0% | 3 |
| queue_reactive | naive_fifo | VWAP | 0.97 | [0.65, 1.28] | +0.00 | [0.00, 0.00] | 0.00 | 0.35 | 0.61 | 0.00 | 0.00 | 0% | 4 |
| queue_reactive | naive_fifo | AlmgrenChriss | -0.08 | [-0.46, 0.30] | -1.05 | [-1.39, -0.49] | 0.00 | -0.04 | -0.04 | 0.00 | 0.00 | 0% | 2 |
| queue_reactive | naive_fifo | OFIAdaptive_CK | 0.98 | [0.21, 1.56] | +0.01 | [-0.59, 0.60] | -0.00 | -0.02 | 1.00 | 0.00 | 0.00 | 98% | 5 |
| queue_reactive | naive_fifo | OFIAdaptive_CKadv | -0.15 | [-0.52, 0.25] | -1.12 | [-1.56, -0.50] | 0.00 | -0.02 | -0.14 | 0.00 | 0.00 | 0% | 1 |
| queue_reactive | power_prob | TWAP | 0.97 | [0.65, 1.28] | +0.00 | [0.00, 0.00] | 0.00 | 0.35 | 0.61 | 0.00 | 0.00 | 0% | 3 |
| queue_reactive | power_prob | VWAP | 0.97 | [0.65, 1.28] | +0.00 | [0.00, 0.00] | 0.00 | 0.35 | 0.61 | 0.00 | 0.00 | 0% | 4 |
| queue_reactive | power_prob | AlmgrenChriss | -0.08 | [-0.46, 0.30] | -1.05 | [-1.39, -0.49] | 0.00 | -0.04 | -0.04 | 0.00 | 0.00 | 0% | 2 |
| queue_reactive | power_prob | OFIAdaptive_CK | 1.10 | [0.58, 1.59] | +0.14 | [-0.36, 0.63] | -0.00 | 0.07 | 1.04 | 0.00 | 0.00 | 99% | 5 |
| queue_reactive | power_prob | OFIAdaptive_CKadv | -0.15 | [-0.52, 0.25] | -1.12 | [-1.56, -0.50] | 0.00 | -0.02 | -0.14 | 0.00 | 0.00 | 0% | 1 |
| queue_reactive | risk_averse | TWAP | 0.97 | [0.65, 1.28] | +0.00 | [0.00, 0.00] | 0.00 | 0.35 | 0.61 | 0.00 | 0.00 | 0% | 4 |
| queue_reactive | risk_averse | VWAP | 0.97 | [0.65, 1.28] | +0.00 | [0.00, 0.00] | 0.00 | 0.35 | 0.61 | 0.00 | 0.00 | 0% | 5 |
| queue_reactive | risk_averse | AlmgrenChriss | -0.08 | [-0.46, 0.30] | -1.05 | [-1.39, -0.49] | 0.00 | -0.04 | -0.04 | 0.00 | 0.00 | 0% | 2 |
| queue_reactive | risk_averse | OFIAdaptive_CK | 0.97 | [0.18, 1.55] | -0.00 | [-0.61, 0.59] | -0.00 | -0.00 | 0.97 | 0.00 | 0.00 | 98% | 3 |
| queue_reactive | risk_averse | OFIAdaptive_CKadv | -0.15 | [-0.52, 0.25] | -1.12 | [-1.56, -0.50] | 0.00 | -0.02 | -0.14 | 0.00 | 0.00 | 0% | 1 |
| zero_intelligence | naive_fifo | TWAP | -1.95 | [-3.68, -0.22] | +0.00 | [0.00, 0.00] | 0.00 | -2.21 | 0.26 | 0.00 | 0.00 | 0% | 1 |
| zero_intelligence | naive_fifo | VWAP | -1.95 | [-3.68, -0.22] | +0.00 | [0.00, 0.00] | 0.00 | -2.21 | 0.26 | 0.00 | 0.00 | 0% | 2 |
| zero_intelligence | naive_fifo | AlmgrenChriss | -0.16 | [-0.63, 0.33] | +1.78 | [0.52, 3.05] | 0.00 | -0.09 | -0.07 | 0.00 | 0.00 | 0% | 4 |
| zero_intelligence | naive_fifo | OFIAdaptive_CK | 0.37 | [-0.41, 1.15] | +2.32 | [1.31, 3.29] | -0.00 | 0.09 | 0.28 | 0.00 | 0.00 | 100% | 5 |
| zero_intelligence | naive_fifo | OFIAdaptive_CKadv | -0.24 | [-0.81, 0.32] | +1.70 | [0.54, 2.87] | 0.00 | -0.18 | -0.07 | 0.00 | 0.00 | 0% | 3 |
| zero_intelligence | power_prob | TWAP | -1.95 | [-3.68, -0.22] | +0.00 | [0.00, 0.00] | 0.00 | -2.21 | 0.26 | 0.00 | 0.00 | 0% | 1 |
| zero_intelligence | power_prob | VWAP | -1.95 | [-3.68, -0.22] | +0.00 | [0.00, 0.00] | 0.00 | -2.21 | 0.26 | 0.00 | 0.00 | 0% | 2 |
| zero_intelligence | power_prob | AlmgrenChriss | -0.16 | [-0.63, 0.33] | +1.78 | [0.52, 3.05] | 0.00 | -0.09 | -0.07 | 0.00 | 0.00 | 0% | 4 |
| zero_intelligence | power_prob | OFIAdaptive_CK | 0.20 | [-0.47, 0.93] | +2.14 | [0.86, 3.26] | -0.00 | -0.06 | 0.26 | 0.00 | 0.00 | 99% | 5 |
| zero_intelligence | power_prob | OFIAdaptive_CKadv | -0.24 | [-0.81, 0.32] | +1.70 | [0.54, 2.87] | 0.00 | -0.18 | -0.07 | 0.00 | 0.00 | 0% | 3 |
| zero_intelligence | risk_averse | TWAP | -1.95 | [-3.68, -0.22] | +0.00 | [0.00, 0.00] | 0.00 | -2.21 | 0.26 | 0.00 | 0.00 | 0% | 1 |
| zero_intelligence | risk_averse | VWAP | -1.95 | [-3.68, -0.22] | +0.00 | [0.00, 0.00] | 0.00 | -2.21 | 0.26 | 0.00 | 0.00 | 0% | 2 |
| zero_intelligence | risk_averse | AlmgrenChriss | -0.16 | [-0.63, 0.33] | +1.78 | [0.52, 3.05] | 0.00 | -0.09 | -0.07 | 0.00 | 0.00 | 0% | 4 |
| zero_intelligence | risk_averse | OFIAdaptive_CK | 0.39 | [-0.40, 1.19] | +2.34 | [1.31, 3.31] | -0.00 | 0.07 | 0.33 | 0.00 | 0.00 | 100% | 5 |
| zero_intelligence | risk_averse | OFIAdaptive_CKadv | -0.24 | [-0.81, 0.32] | +1.70 | [0.54, 2.87] | 0.00 | -0.18 | -0.07 | 0.00 | 0.00 | 0% | 3 |


## Passive fill probability (Binance replay probes)

| queue model | horizon | P(fill) at touch | +1 tick | +2 | +3 | touch by queue tercile (low/mid/high) |
|---|---|---|---|---|---|---|
| naive_fifo | 5 s | 0.36 | 0.35 | 0.31 | 0.29 | 0.73/0.62/0.53 |
| naive_fifo | 30 s | 0.67 | 0.66 | 0.64 | 0.67 | 0.73/0.62/0.53 |
| naive_fifo | 120 s | 0.84 | 0.82 | 0.80 | 0.87 | 0.73/0.62/0.53 |
| power_prob | 5 s | 0.39 | 0.35 | 0.31 | 0.29 | 0.71/0.62/0.56 |
| power_prob | 30 s | 0.67 | 0.66 | 0.64 | 0.67 | 0.71/0.62/0.56 |
| power_prob | 120 s | 0.84 | 0.82 | 0.80 | 0.87 | 0.71/0.62/0.56 |
| risk_averse | 5 s | 0.36 | 0.35 | 0.31 | 0.29 | 0.71/0.61/0.53 |
| risk_averse | 30 s | 0.66 | 0.66 | 0.64 | 0.67 | 0.71/0.61/0.53 |
| risk_averse | 120 s | 0.83 | 0.82 | 0.80 | 0.87 | 0.71/0.61/0.53 |

Fill models vs empirical at the touch (naive FIFO): 5s: emp 0.36, state-dependent 0.24, logistic 0.36, Poisson 0.60; 30s: emp 0.67, state-dependent 0.89, logistic 0.67, Poisson 0.90; 120s: emp 0.84, state-dependent 1.00, logistic 0.85, Poisson 0.97

## Order-level check of the L2 queue models (Bitstamp)

21,687 real orders created at the touch, tracked to fill/cancel; median size 0.1020 BTC.

| queue model | mean rel. error of queue-ahead estimate | mean |rel. error| | recall of true fills | false fills on cancelled orders |
|---|---|---|---|---|
| naive_fifo | +0.21 | 0.32 | 0.14 | 0.000 |
| power_prob (n=3) | +0.24 | 0.32 | 0.17 | 0.011 |
| risk_averse | +0.29 | 0.31 | 0.00 | 0.000 |
| power_prob (n=0.25) | -0.21 | 0.71 | 0.31 | 0.023 |
| power_prob (n=0.5) | +0.14 | 0.37 | 0.29 | 0.018 |
| power_prob (n=2) | +0.24 | 0.31 | 0.17 | 0.008 |
| power_prob (n=5) | +0.24 | 0.32 | 0.17 | 0.011 |

Kaplan–Meier fill probability of real touch orders (cancellation = censoring): 5s 0.07, 30s 0.30, 120s 0.62
Replay probes at the touch on the same tape: naive_fifo: 5s 0.17, 30s 0.58, 120s 0.75; power_prob: 5s 0.18, 30s 0.59, 120s 0.76; risk_averse: 5s 0.17, 30s 0.58, 120s 0.75

## Adverse selection of passive fills (mark-outs, ticks, + = favourable)

| mode | queue model | P(fill) | 100 ms | 1 s | 10 s | 60 s | corr(filled, post-placement return) |
|---|---|---|---|---|---|---|---|
| replay | naive_fifo | 0.67 | -464 ± 24 | -813 ± 38 | -1332 ± 100 | -1732 ± 266 | -0.51 |
| replay | power_prob | 0.68 | -414 ± 28 | -740 ± 42 | -1216 ± 103 | -1544 ± 267 | -0.49 |
| replay | risk_averse | 0.66 | -477 ± 24 | -831 ± 38 | -1376 ± 100 | -1788 ± 267 | -0.52 |
| queue_reactive | naive_fifo | 0.79 | -357 ± 42 | -370 ± 45 | -439 ± 82 | -780 ± 248 | -0.59 |
| queue_reactive | power_prob | 0.85 | -263 ± 46 | -287 ± 49 | -318 ± 86 | -592 ± 236 | -0.49 |
| queue_reactive | risk_averse | 0.79 | -372 ± 42 | -389 ± 45 | -426 ± 82 | -711 ± 247 | -0.57 |
| zero_intelligence | naive_fifo | 0.88 | -400 ± 35 | -430 ± 40 | -432 ± 132 | -465 ± 278 | -0.49 |
| zero_intelligence | power_prob | 0.89 | -281 ± 39 | -292 ± 46 | -300 ± 126 | -404 ± 272 | -0.45 |
| zero_intelligence | risk_averse | 0.89 | -403 ± 35 | -432 ± 39 | -428 ± 131 | -490 ± 275 | -0.48 |

Queue-position value (Moallemi–Yuan, measured): P(fill) × (half-spread + 60 s mark-out), by tercile of queue ahead at placement, naive FIFO:

| mode | tercile | mean queue ahead (lots) | P(fill) | 60 s mark-out | value (ticks) |
|---|---|---|---|---|---|
| replay | 0 | 34,317 | 0.83 | -1698 | -1417.5 |
| replay | 1 | 155,180 | 0.62 | -1053 | -655.7 |
| replay | 2 | 463,995 | 0.54 | -2564 | -1384.4 |
| queue_reactive | 0 | 75,083 | 0.82 | -996 | -816.3 |
| queue_reactive | 1 | 194,052 | 0.76 | -304 | -230.4 |
| queue_reactive | 2 | 420,329 | 0.78 | -1017 | -792.8 |
| zero_intelligence | 0 | 38,551 | 0.82 | -443 | -362.7 |
| zero_intelligence | 1 | 188,808 | 0.96 | -519 | -497.4 |
| zero_intelligence | 2 | 381,611 | 0.86 | -427 | -366.9 |

## OFI regression (Cont–Kukanov–Stoikov)

- 1s buckets: β = 5.85e-04 ticks/lot, R² = 0.36, n = 10,079; by hour UTC: 19h 0.38, 20h 0.44, 21h 0.26, 22h 0.46; by volatility tercile: 0.25, 0.38, 0.41
- 10s buckets: β = 6.93e-04 ticks/lot, R² = 0.57, n = 1,008; by hour UTC: 19h 0.61, 20h 0.65, 21h 0.37; by volatility tercile: 0.40, 0.63, 0.62

## Known-answer tests and FIX self-test

```
KNOWN-ANSWER TESTS PASSED: 52/52 checks
```
```
TWAP             in-process:  49 fills, IS  +18.815 bp | FIX:  49 fills, IS  +18.815 bp | IDENTICAL
VWAP             in-process:  49 fills, IS  +18.815 bp | FIX:  49 fills, IS  +18.815 bp | IDENTICAL
AlmgrenChriss    in-process: 111 fills, IS   +4.895 bp | FIX: 111 fills, IS   +4.895 bp | IDENTICAL
OFIAdaptive_CK   in-process: 120 fills, IS   +6.432 bp | FIX: 120 fills, IS   +6.432 bp | IDENTICAL
OFIAdaptive_CKadv in-process: 111 fills, IS   +4.925 bp | FIX: 111 fills, IS   +4.925 bp | IDENTICAL
FIX SELF-TEST PASSED: identical fills in-process and through FIX 4.4
```