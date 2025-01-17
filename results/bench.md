
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
