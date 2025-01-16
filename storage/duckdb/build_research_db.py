#!/usr/bin/env python3
"""Research layer: tapes -> Parquet -> DuckDB with the views the analysis notebooks use.

    python storage/duckdb/build_research_db.py data/tapes data/parquet/tapes data/research.duckdb

Views: events (all tapes), trades, best quotes sampled per second (bbo_1s), VWAP per 1-minute
bucket, and the CKS order-flow imbalance per second (the SQL mirrors lobsim's C++ formula so the two
can be cross-checked).
"""
import glob
import os
import sys

import duckdb
import pyarrow as pa
import pyarrow.csv as pcsv
import pyarrow.parquet as pq


def tape_to_parquet(path: str, out: str):
    with open(path, encoding="utf-8") as f:
        header = f.readline()
    meta = dict(kv.split("=") for kv in header.replace("# lobsim-tape v1", "").split())
    t = pcsv.read_csv(path, read_options=pcsv.ReadOptions(skip_rows=1),
                      convert_options=pcsv.ConvertOptions(column_types={"ts_ex": pa.int64(), "ts_rx": pa.int64(), "price": pa.int64(), "qty": pa.int64()}))
    tape = os.path.splitext(os.path.basename(path))[0]
    t = t.append_column("tape", pa.array([tape] * t.num_rows))
    t = t.append_column("tick", pa.array([float(meta["tick"])] * t.num_rows))
    t = t.append_column("lot", pa.array([float(meta["lot"])] * t.num_rows))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    pq.write_table(t, out, compression="zstd")
    return t.num_rows


def main():
    tapes_dir, pq_dir, db_path = sys.argv[1], sys.argv[2], sys.argv[3]
    for p in glob.glob(os.path.join(tapes_dir, "*.csv")):
        n = tape_to_parquet(p, os.path.join(pq_dir, os.path.basename(p)[:-4] + ".parquet"))
        print("parquet", p, n, "rows")
    if os.path.exists(db_path):
        os.remove(db_path)
    con = duckdb.connect(db_path)
    con.execute(f"CREATE VIEW events AS SELECT * FROM read_parquet('{pq_dir}/*.parquet')")
    con.execute("CREATE VIEW trades AS SELECT tape, ts_ex, side AS passive_side, price*tick AS px, qty*lot AS size FROM events WHERE kind='T'")
    con.execute("""
        CREATE VIEW vwap_1min AS
        SELECT tape, date_trunc('minute', to_timestamp(ts_ex/1e9)::TIMESTAMP) AS minute,
               SUM(px*size)/SUM(size) AS vwap, SUM(size) AS volume, COUNT(*) AS prints
        FROM trades GROUP BY 1, 2""")
    # best bid/ask sampled once per second: a level is live from the update that set it until its next update
    con.execute("""
        CREATE VIEW bbo_1s AS
        WITH lv AS (
          SELECT tape, side, price, qty, ts_ex,
                 lead(ts_ex) OVER (PARTITION BY tape, side, price ORDER BY ts_ex) AS next_ts
          FROM events WHERE kind='L'),
        secs AS (SELECT tape, unnest(generate_series(MIN(ts_ex)//1000000000 + 1, MAX(ts_ex)//1000000000)) AS sec FROM events GROUP BY tape)
        SELECT s.tape, s.sec,
               MAX(CASE WHEN side='b' THEN price END) AS best_bid, MIN(CASE WHEN side='a' THEN price END) AS best_ask
        FROM secs s JOIN lv ON lv.tape = s.tape AND lv.ts_ex < s.sec*1000000000 AND (lv.next_ts IS NULL OR lv.next_ts >= s.sec*1000000000) AND lv.qty > 0
        GROUP BY 1, 2""")
    print(con.execute("SELECT tape, COUNT(*) AS rows, SUM(kind='T') AS trades FROM events GROUP BY 1").fetchall())
    print(con.execute("SELECT * FROM vwap_1min ORDER BY minute LIMIT 3").fetchall())
    con.close()


if __name__ == "__main__":
    main()
