#!/usr/bin/env python3
"""
Raw order-book recorder.

  Binance spot      depth@100ms diff stream + trade stream, synchronised to a REST depth snapshot
                    with the documented U/u rule (L2).
  Coinbase Exchange level2_batch + matches (L2, public), and optionally the `full` order-by-order
                    channel, which since 2023 requires an API key (COINBASE_KEY / COINBASE_SECRET /
                    COINBASE_PASSPHRASE environment variables; view permission is enough).
  Bitstamp          live_orders (order_created / order_changed / order_deleted with order ids and
                    microsecond timestamps), live_trades (with buy/sell order ids) and diff_order_book.
                    Public, no key: this is the free order-by-order ground truth used by
                    `lobsim l3check` to validate the L2 queue-position models.

Design rules (see README "Data capture"):
  * every message is written to disk *raw*, before it is parsed, in the order received;
  * files are append-only NDJSON, rotated hourly:  data/raw/<venue>/<symbol>/<YYYY-MM-DD>/<HH>.ndjson
    (tools/recorder/raw_to_parquet.py converts an hour file to Parquet for the research layer);
  * the recorder keeps a live book only to (a) detect sequence gaps and trigger a resync and
    (b) run an online parity check against a fresh REST snapshot every --parity-every seconds.
    The *exact* parity test is done offline by `lobsim rebuild --parity`, which can reconstruct the
    book at precisely the snapshot's lastUpdateId.  The online check is an operational alarm only.

Line format (one JSON object per line):
  {"rx":<local receive time, ns since epoch>,"venue":"binance"|"coinbase","kind":<see below>,"msg":<raw payload verbatim>}
  kind: ws       raw websocket message
        snapshot REST depth snapshot used for synchronisation (Binance) / websocket snapshot (Coinbase)
        parity   periodic REST snapshot + online comparison summary
        meta     connect / disconnect / gap / resync / error events

Nothing here is a dependency of the C++ engine except the file format above.
"""
import argparse
import asyncio
import json
import os
import signal
import sys
import time
from collections import deque
from datetime import datetime, timezone

import base64
import hashlib
import hmac

import aiohttp
import websockets

BINANCE_WS = "wss://stream.binance.com:9443/stream?streams="
BINANCE_REST = "https://api.binance.com/api/v3/depth"
COINBASE_WS = "wss://ws-feed.exchange.coinbase.com"
BITSTAMP_WS = "wss://ws.bitstamp.net"


def now_ns() -> int:
    return time.time_ns()


class RawLogger:
    """Append-only hourly NDJSON writer. One instance per (venue, symbol)."""

    def __init__(self, root: str, venue: str, symbol: str, flush_every: float = 1.0):
        self.root, self.venue, self.symbol = root, venue, symbol
        self.fh = None
        self.cur_hour = None
        self.n_lines = 0
        self.n_bytes = 0
        self.last_flush = time.monotonic()
        self.flush_every = flush_every

    def _path_for(self, rx_ns: int) -> str:
        dt = datetime.fromtimestamp(rx_ns / 1e9, tz=timezone.utc)
        d = os.path.join(self.root, self.venue, self.symbol, dt.strftime("%Y-%m-%d"))
        os.makedirs(d, exist_ok=True)
        return os.path.join(d, dt.strftime("%H") + ".ndjson")

    def write(self, kind: str, raw: str, rx_ns=None):
        rx_ns = rx_ns if rx_ns is not None else now_ns()
        hour = rx_ns // (3600 * 10**9)
        if hour != self.cur_hour:
            if self.fh:
                self.fh.flush()
                self.fh.close()
            self.fh = open(self._path_for(rx_ns), "a", encoding="utf-8", buffering=1 << 20)
            self.cur_hour = hour
        # `raw` is spliced verbatim: it is already JSON text, so no re-escaping/re-parsing is needed.
        line = '{"rx":%d,"venue":"%s","kind":"%s","msg":%s}\n' % (rx_ns, self.venue, kind, raw.strip())
        self.fh.write(line)
        self.n_lines += 1
        self.n_bytes += len(line)
        t = time.monotonic()
        if t - self.last_flush > self.flush_every:
            self.fh.flush()
            self.last_flush = t

    def meta(self, event: str, **kw):
        kw["event"] = event
        self.write("meta", json.dumps(kw, separators=(",", ":"), default=str))

    def close(self):
        if self.fh:
            self.fh.flush()
            self.fh.close()
            self.fh = None


# ----------------------------------------------------------------------------------------------
# Binance
# ----------------------------------------------------------------------------------------------
class BinanceBook:
    """Minimal local book used only for gap detection and online parity checks."""

    def __init__(self):
        self.bids = {}
        self.asks = {}
        self.last_u = None

    def load_snapshot(self, snap):
        self.bids = {p: q for p, q in snap["bids"] if float(q) > 0}
        self.asks = {p: q for p, q in snap["asks"] if float(q) > 0}
        self.last_u = snap["lastUpdateId"]

    def apply(self, ev):
        for side, book in (("b", self.bids), ("a", self.asks)):
            for p, q in ev[side]:
                if float(q) == 0.0:
                    book.pop(p, None)
                else:
                    book[p] = q
        self.last_u = ev["u"]


async def binance_snapshot(session, symbol: str, limit: int = 5000):
    async with session.get(BINANCE_REST, params={"symbol": symbol, "limit": limit},
                           timeout=aiohttp.ClientTimeout(total=20)) as r:
        r.raise_for_status()
        raw = await r.text()
        return raw, json.loads(raw)


async def record_binance(symbol: str, log: RawLogger, stop: asyncio.Event, parity_every: float, stats: dict):
    """
    Synchronisation rule (Binance spot docs, "How to manage a local order book correctly", 2024):
      1. open the diff stream and buffer events;
      2. GET depth?limit=5000 -> lastUpdateId = L;
      3. drop buffered events with u <= L;
      4. the first processed event must satisfy U <= L+1 <= u;
      5. thereafter every event must have U == previous u + 1, otherwise resync from step 2.
    """
    sym_l = symbol.lower()
    url = BINANCE_WS + "%s@depth@100ms/%s@trade" % (sym_l, sym_l)
    backoff = 1.0
    st = stats.setdefault("binance:" + symbol, {"ws": 0, "gaps": 0, "resyncs": 0,
                                                "drift_ms": deque(maxlen=2000), "parity_mismatch": None})
    async with aiohttp.ClientSession() as session:
        while not stop.is_set():
            book = BinanceBook()
            buffer = deque()
            synced = False
            snap = None
            L = None
            last_parity = time.monotonic()
            try:
                log.meta("connect", url=url)
                async with websockets.connect(url, max_size=2**24, ping_interval=20, ping_timeout=20) as ws:
                    backoff = 1.0
                    while not stop.is_set():
                        try:
                            raw = await asyncio.wait_for(ws.recv(), timeout=30)
                        except asyncio.TimeoutError:
                            log.meta("recv_timeout")
                            break
                        rx = now_ns()
                        log.write("ws", raw, rx)  # raw first, always
                        st["ws"] += 1
                        msg = json.loads(raw)
                        stream = msg.get("stream", "")
                        data = msg.get("data", {})
                        if "E" in data:
                            st["drift_ms"].append(rx / 1e6 - data["E"])
                        if not stream.endswith("@depth@100ms"):
                            continue
                        # ---- sequence handling ---------------------------------------------
                        if synced:
                            if data["U"] != book.last_u + 1:
                                log.meta("gap", expected=book.last_u + 1, got=data["U"], u=data["u"])
                                st["gaps"] += 1
                                st["resyncs"] += 1
                                synced = False
                                buffer.clear()
                                buffer.append(data)
                                continue
                            book.apply(data)
                        else:
                            buffer.append(data)
                            if snap is None:
                                if len(buffer) < 3:
                                    continue  # let a few events accumulate before fetching the snapshot
                                snap_raw, snap = await binance_snapshot(session, symbol)
                                log.write("snapshot", snap_raw)
                                L = snap["lastUpdateId"]
                                book.load_snapshot(snap)
                            # events with u <= L are already in the snapshot
                            while buffer and buffer[0]["u"] <= L:
                                buffer.popleft()
                            if not buffer:
                                continue  # stream not yet past L; keep buffering (snapshot is retained)
                            first = buffer[0]
                            if not (first["U"] <= L + 1 <= first["u"]):
                                log.meta("resync_snapshot_too_old", L=L, U=first["U"], u=first["u"])
                                st["resyncs"] += 1
                                buffer.clear()
                                snap = None
                                await asyncio.sleep(1.0)  # depth?limit=5000 is weight 250: never hammer it
                                continue
                            ok = True
                            prev_u = None
                            for ev in buffer:
                                if prev_u is not None and ev["U"] != prev_u + 1:
                                    ok = False
                                    break
                                book.apply(ev)
                                prev_u = ev["u"]
                            buffer.clear()
                            snap = None
                            if not ok:
                                log.meta("gap_in_buffer")
                                st["gaps"] += 1
                                continue
                            synced = True
                            log.meta("synced", lastUpdateId=L, last_u=book.last_u)
                            continue
                        # ---- online parity alarm ------------------------------------------
                        if time.monotonic() - last_parity > parity_every:
                            last_parity = time.monotonic()
                            snap_raw, snap = await binance_snapshot(session, symbol, limit=1000)
                            mism = checked = 0
                            for side, local in (("bids", book.bids), ("asks", book.asks)):
                                for p, q in snap[side][:50]:
                                    checked += 1
                                    if local.get(p) != q:
                                        mism += 1
                            st["parity_mismatch"] = mism
                            summary = json.dumps({"lastUpdateId": snap["lastUpdateId"], "local_last_u": book.last_u,
                                                  "top50_checked": checked, "top50_mismatch": mism,
                                                  "note": "online alarm only; timing skew expected; exact check is offline"},
                                                 separators=(",", ":"))
                            log.write("parity", '{"summary":%s,"snapshot":%s}' % (summary, snap_raw))
            except (websockets.ConnectionClosed, aiohttp.ClientError, OSError, asyncio.TimeoutError) as e:
                log.meta("disconnect", error=repr(e))
            except asyncio.CancelledError:
                raise
            except Exception as e:  # noqa: BLE001 - keep recording no matter what
                log.meta("error", error=repr(e))
            if stop.is_set():
                break
            log.meta("reconnect_wait", seconds=backoff)
            await asyncio.sleep(backoff)
            backoff = min(backoff * 2, 30.0)


# ----------------------------------------------------------------------------------------------
# Coinbase Exchange
# ----------------------------------------------------------------------------------------------
async def record_coinbase(product: str, log: RawLogger, stop: asyncio.Event, stats: dict):
    """
    Coinbase Exchange level2_batch: an initial `snapshot` message then absolute price-level `l2update`s
    batched every 50 ms. Sizes are absolute (no diff arithmetic, no U/u chain). Gaps are detected only
    via the consecutive `trade_id` on matches; a reconnect always yields a fresh snapshot.
    """
    st = stats.setdefault("coinbase:" + product, {"ws": 0, "gaps": 0, "resyncs": 0,
                                                  "drift_ms": deque(maxlen=2000), "parity_mismatch": None})
    channels = ["level2_batch", "matches", "heartbeat"]
    sub_obj = {"type": "subscribe", "product_ids": [product], "channels": channels}
    key, secret, passphrase = os.environ.get("COINBASE_KEY"), os.environ.get("COINBASE_SECRET"), os.environ.get("COINBASE_PASSPHRASE")
    if key and secret and passphrase:
        # Coinbase Exchange websocket auth: sign timestamp + 'GET' + '/users/self/verify' with the base64 secret.
        channels.append("full")
        ts = str(time.time())
        msg = ts + "GET" + "/users/self/verify"
        sig = base64.b64encode(hmac.new(base64.b64decode(secret), msg.encode(), hashlib.sha256).digest()).decode()
        sub_obj.update({"signature": sig, "key": key, "passphrase": passphrase, "timestamp": ts})
    sub = json.dumps(sub_obj)
    backoff = 1.0
    while not stop.is_set():
        last_seq = None
        try:
            log.meta("connect", url=COINBASE_WS)
            async with websockets.connect(COINBASE_WS, max_size=2**24, ping_interval=20, ping_timeout=20) as ws:
                await ws.send(sub)
                backoff = 1.0
                while not stop.is_set():
                    try:
                        raw = await asyncio.wait_for(ws.recv(), timeout=30)
                    except asyncio.TimeoutError:
                        log.meta("recv_timeout")
                        break
                    rx = now_ns()
                    msg = json.loads(raw)
                    t = msg.get("type")
                    log.write("snapshot" if t == "snapshot" else "ws", raw, rx)
                    st["ws"] += 1
                    if t == "error":
                        log.meta("error", error=msg)
                    tm = msg.get("time")
                    if tm:
                        try:
                            ev_ms = datetime.fromisoformat(tm.replace("Z", "+00:00")).timestamp() * 1e3
                            st["drift_ms"].append(rx / 1e6 - ev_ms)
                        except ValueError:
                            pass
                    tid = msg.get("trade_id")
                    if t == "match" and tid is not None:
                        # `sequence` counts every message on the product, so only trade_id is
                        # expected to be consecutive for the channels we subscribe to.
                        if last_seq is not None and tid > last_seq + 1:
                            log.meta("gap", expected=last_seq + 1, got=tid)
                            st["gaps"] += 1
                        last_seq = tid
        except (websockets.ConnectionClosed, OSError, asyncio.TimeoutError) as e:
            log.meta("disconnect", error=repr(e))
        except asyncio.CancelledError:
            raise
        except Exception as e:  # noqa: BLE001
            log.meta("error", error=repr(e))
        if stop.is_set():
            break
        st["resyncs"] += 1
        log.meta("reconnect_wait", seconds=backoff)
        await asyncio.sleep(backoff)
        backoff = min(backoff * 2, 30.0)


# ----------------------------------------------------------------------------------------------
# Bitstamp (order-by-order, public)
# ----------------------------------------------------------------------------------------------
async def record_bitstamp(pair: str, log: RawLogger, stop: asyncio.Event, stats: dict):
    """
    live_orders_<pair>: every order creation / change / deletion with order id, price, amount and
    microtimestamp; live_trades_<pair>: trades with buy_order_id / sell_order_id; diff_order_book_<pair>:
    L2 diffs for parity between the L3-rebuilt book and the exchange's own L2 view.  On reconnect the
    L3 book must be re-seeded: the recorder logs a REST order_book snapshot (group=2, order-level).
    """
    st = stats.setdefault("bitstamp:" + pair, {"ws": 0, "gaps": 0, "resyncs": 0,
                                                "drift_ms": deque(maxlen=2000), "parity_mismatch": None})
    chans = ["live_orders_%s" % pair, "live_trades_%s" % pair, "diff_order_book_%s" % pair]
    backoff = 1.0
    async with aiohttp.ClientSession() as session:
        while not stop.is_set():
            try:
                log.meta("connect", url=BITSTAMP_WS)
                async with websockets.connect(BITSTAMP_WS, max_size=2**24, ping_interval=20, ping_timeout=20) as ws:
                    for ch in chans:
                        await ws.send(json.dumps({"event": "bts:subscribe", "data": {"channel": ch}}))
                    backoff = 1.0
                    st["resyncs"] += 1
                    need_snapshot = True
                    n_order_events = 0
                    while not stop.is_set():
                        if need_snapshot and n_order_events >= 20:
                            # order-level snapshot (group=2 rows are [price, amount, order_id]) to seed the L3 book.
                            # Fetched only once the stream is demonstrably live, so the snapshot is newer than the
                            # first streamed event and no deletion can fall into a hole before the subscription.
                            async with session.get("https://www.bitstamp.net/api/v2/order_book/%s/" % pair, params={"group": 2},
                                                   timeout=aiohttp.ClientTimeout(total=20)) as r:
                                r.raise_for_status()
                                log.write("snapshot", await r.text())
                            need_snapshot = False
                        try:
                            raw = await asyncio.wait_for(ws.recv(), timeout=30)
                        except asyncio.TimeoutError:
                            log.meta("recv_timeout")
                            break
                        rx = now_ns()
                        log.write("ws", raw, rx)
                        st["ws"] += 1
                        msg = json.loads(raw)
                        ev = msg.get("event")
                        if ev == "bts:request_reconnect":
                            log.meta("request_reconnect")
                            break
                        d = msg.get("data") or {}
                        mt = d.get("microtimestamp")
                        if mt:
                            st["drift_ms"].append(rx / 1e6 - int(mt) / 1e3)
                        if ev in ("order_created", "order_deleted", "order_changed"):
                            n_order_events += 1
            except (websockets.ConnectionClosed, aiohttp.ClientError, OSError, asyncio.TimeoutError) as e:
                log.meta("disconnect", error=repr(e))
            except asyncio.CancelledError:
                raise
            except Exception as e:  # noqa: BLE001
                log.meta("error", error=repr(e))
            if stop.is_set():
                break
            log.meta("reconnect_wait", seconds=backoff)
            await asyncio.sleep(backoff)
            backoff = min(backoff * 2, 30.0)


# ----------------------------------------------------------------------------------------------
async def status_printer(stats: dict, logs, stop: asyncio.Event, every: float = 30.0):
    t0 = time.monotonic()
    last = {}
    while not stop.is_set():
        try:
            await asyncio.wait_for(stop.wait(), timeout=every)
        except asyncio.TimeoutError:
            pass
        el = time.monotonic() - t0
        parts = []
        for k, s in stats.items():
            rate = (s["ws"] - last.get(k, 0)) / every
            last[k] = s["ws"]
            d = sorted(s["drift_ms"])
            med = d[len(d) // 2] if d else float("nan")
            parts.append("%s: %d msgs (%.0f/s) gaps=%d resyncs=%d drift_med=%.0fms parity_mism=%s"
                         % (k, s["ws"], rate, s["gaps"], s["resyncs"], med, s["parity_mismatch"]))
        mb = sum(l.n_bytes for l in logs) / 1e6
        print("[%6.1f min] %8.1f MB | " % (el / 60, mb) + " | ".join(parts), flush=True)


async def main_async(args):
    stop = asyncio.Event()
    stats = {}
    logs = []
    tasks = []
    for sym in args.binance:
        lg = RawLogger(args.out, "binance", sym)
        logs.append(lg)
        tasks.append(asyncio.create_task(record_binance(sym, lg, stop, args.parity_every, stats)))
    for prod in args.coinbase:
        lg = RawLogger(args.out, "coinbase", prod)
        logs.append(lg)
        tasks.append(asyncio.create_task(record_coinbase(prod, lg, stop, stats)))
    for pair in args.bitstamp:
        lg = RawLogger(args.out, "bitstamp", pair)
        logs.append(lg)
        tasks.append(asyncio.create_task(record_bitstamp(pair, lg, stop, stats)))
    tasks.append(asyncio.create_task(status_printer(stats, logs, stop)))

    def _stop(*_):
        stop.set()

    try:
        signal.signal(signal.SIGINT, _stop)
        signal.signal(signal.SIGTERM, _stop)
    except Exception:
        pass
    if args.minutes > 0:
        async def timer():
            await asyncio.sleep(args.minutes * 60)
            stop.set()
        tasks.append(asyncio.create_task(timer()))
    await stop.wait()
    for t in tasks:
        t.cancel()
    await asyncio.gather(*tasks, return_exceptions=True)
    for lg in logs:
        lg.meta("shutdown")
        lg.close()
    print("recorder stopped", flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="data/raw", help="root directory for raw NDJSON files")
    ap.add_argument("--binance", nargs="*", default=["BTCUSDT"], help="Binance spot symbols")
    ap.add_argument("--coinbase", nargs="*", default=["BTC-USD"], help="Coinbase Exchange products")
    ap.add_argument("--bitstamp", nargs="*", default=[], help="Bitstamp pairs (order-by-order channels)")
    ap.add_argument("--minutes", type=float, default=0, help="stop after N minutes (0 = run until Ctrl-C)")
    ap.add_argument("--parity-every", type=float, default=300, help="seconds between online REST parity checks")
    args = ap.parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
