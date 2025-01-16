#!/usr/bin/env python3
"""Convert raw hourly NDJSON files to Parquet (one file per hour, columns rx / venue / kind / msg).

    python tools/recorder/raw_to_parquet.py data/raw data/parquet/raw

The raw text is kept verbatim in `msg`; nothing is parsed, so the Parquet file is a lossless,
compressed archive of exactly what the websocket delivered.
"""
import json
import os
import sys

import pyarrow as pa
import pyarrow.parquet as pq


def convert(src: str, dst: str) -> int:
    rx, venue, kind, msg = [], [], [], []
    with open(src, encoding="utf-8") as f:
        for line in f:
            try:
                d = json.loads(line)
            except ValueError:
                continue  # torn line (recorder killed mid-write): skipped, counted by the C++ rebuilder
            rx.append(d["rx"]); venue.append(d["venue"]); kind.append(d["kind"]); msg.append(json.dumps(d["msg"], separators=(",", ":")))
    table = pa.table({"rx": pa.array(rx, pa.int64()), "venue": venue, "kind": kind, "msg": msg})
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    pq.write_table(table, dst, compression="zstd")
    return len(rx)


def main():
    root, out = sys.argv[1], sys.argv[2]
    n = 0
    for dp, _, files in os.walk(root):
        for fn in files:
            if fn.endswith(".ndjson"):
                src = os.path.join(dp, fn)
                rel = os.path.relpath(src, root)[:-len(".ndjson")] + ".parquet"
                n += convert(src, os.path.join(out, rel))
                print("converted", rel)
    print("rows:", n)


if __name__ == "__main__":
    main()
