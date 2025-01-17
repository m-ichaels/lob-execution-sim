#!/usr/bin/env bash
# Full pipeline: raw recordings -> tapes -> calibration -> validation -> empirical facts -> benchmark -> figures.
# Usage: scripts/run_all.sh [DATE] [BITSTAMP_SINCE_RX_NS]
set -euo pipefail
DATE=${1:-$(date -u +%Y-%m-%d)}
SINCE=${2:-0}
B=./build/lobsim
mkdir -p data/tapes results
$B rebuild --venue binance  --symbol BTCUSDT --tick 0.01 --lot 0.00001 --out data/tapes/binance_BTCUSDT_$DATE.csv  --report results/rebuild_binance.json  data/raw/binance/BTCUSDT  > /dev/null
$B rebuild --venue coinbase --symbol BTC-USD --tick 0.01 --lot 0.00000001 --out data/tapes/coinbase_BTC-USD_$DATE.csv --report results/rebuild_coinbase.json data/raw/coinbase/BTC-USD > /dev/null
$B l3check --since-rx $SINCE --tape-out data/tapes/bitstamp_BTCUSD_$DATE.csv --out results/l3check.json data/raw/bitstamp/btcusd > /dev/null
$B calibrate --tape data/tapes/binance_BTCUSDT_$DATE.csv --out data/calib_binance.json --report results/calib_report.json > /dev/null
cp data/calib_binance.json configs/calib_binance_BTCUSDT.json
$B validate  --live data/tapes/binance_BTCUSDT_$DATE.csv --calib data/calib_binance.json --outdir results --out results/validation.json > /dev/null
$B fillcurve --tape data/tapes/binance_BTCUSDT_$DATE.csv --calib data/calib_binance.json --probes 4000 --out results/fillcurve.json > /dev/null
$B ofi       --tape data/tapes/binance_BTCUSDT_$DATE.csv --out results/ofi.json > /dev/null
$B markout   --tape data/tapes/binance_BTCUSDT_$DATE.csv --calib data/calib_binance.json --probes 600 --tau 30 --out results/markout.json > /dev/null
# adverse mark-out charged to passive fills by the mark-out-aware scheduler: measured 10 s replay mark-out (naive FIFO), in ticks
ADV=$(python -c "import json; r=[x for x in json.load(open('results/markout.json')) if x['mode']=='replay' and x['queue_model']=='naive_fifo'][0]; print(max(0.0, -[e for e in r['markouts'] if e['horizon_s']==10][0]['markout_ticks']))")
echo "ck-adverse-ticks = $ADV"
$B bench --tapes data/tapes/binance_BTCUSDT_$DATE.csv --calib data/calib_binance.json --episodes 12 --horizon 300 --slices 10 --qty 300000 --latency-ms 50 --ck-adverse-ticks $ADV --fill-model logistic --fill-model-json results/fillcurve.json --out results/bench.json --md results/bench.md > /dev/null
for s in 1 2 3 4; do $B synth --calib data/calib_binance.json --mode qr --seconds 3600 --seed $s --start-mid 7600000 --out data/tapes/synth_qr_day$s.csv > /dev/null; done
$B bench --tapes data/tapes/synth_qr_day1.csv,data/tapes/synth_qr_day2.csv,data/tapes/synth_qr_day3.csv,data/tapes/synth_qr_day4.csv --calib data/calib_binance.json --episodes 6 --horizon 300 --slices 10 --qty 300000 --latency-ms 50 --ck-adverse-ticks $ADV --fill-model logistic --fill-model-json results/fillcurve.json --out results/bench_synth_days.json --md results/bench_synth_days.md > /dev/null
./build/fixgw --selftest --tape data/tapes/binance_BTCUSDT_$DATE.csv --calib data/calib_binance.json --horizon 120 --slices 6 --ck-adverse-ticks $ADV | tee results/fix_selftest.txt
./build/known_answer_tests | tee results/tests.txt
python scripts/plots.py results results/figures
python scripts/summarize.py results
python scripts/report.py results report.pdf
python tools/recorder/raw_to_parquet.py data/raw data/parquet/raw > /dev/null
python storage/duckdb/build_research_db.py data/tapes data/parquet/tapes data/research.duckdb > /dev/null
echo done
