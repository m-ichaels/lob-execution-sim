
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
