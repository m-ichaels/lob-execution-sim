/ Load a lobsim tape CSV into the schema.  Usage:  q storage/kdb/load.q -tape data/tapes/x.csv -sym BTCUSDT
\l storage/kdb/schema.q
args:.Q.opt .z.x
tape:first args`tape; sym:`$first args`sym
lines:1_read0 hsym `$tape                                            / drop the "# lobsim-tape" line; next line is the CSV header
raw:("JJCCJJ";enlist",") 0: lines
t2ts:{`timestamp$1970.01.01D00+x}
levels:`time xasc select time:t2ts ts_ex, sym, side, price, qty from raw where kind="L"
trades:`time xasc select time:t2ts ts_ex, sym, side, price, qty from raw where kind="T"
/ best bid/ask per level event: keep last qty per (side,price), then max/min over live levels - done in queries.q
