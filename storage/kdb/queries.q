/ Bank-style queries on the tick store.  Run after load.q.

/ 1. Rebuild top of book from level events (last state per (side,price), live levels only), one row per event.
book:{[t] b:select last qty by side,price from t; b}
/ quotes table: best bid/ask sampled at each level event (O(n log n) via running update)
mkquotes:{
  bb:0Nj; ba:0Nj; bq:()!(); aq:()!();
  f:{[e] $[e[`side]="b"; bq[e`price]::e`qty; aq[e`price]::e`qty];
        bq::(where 0<bq)#bq; aq::(where 0<aq)#aq;
        `time`sym`bid`ask`bidq`askq!(e`time; e`sym; max key bq; min key aq; bq max key bq; aq min key aq)};
  quotes::f each levels}
mkquotes[]

/ 2. As-of join: the prevailing quote for every trade (aj is the canonical kdb+ tick query).
tq:aj[`sym`time; trades; quotes]
/ effective half spread paid per trade, in ticks: |price - mid|
eff:update mid:0.5*bid+ask, half_spread:abs price-0.5*bid+ask from tq

/ 3. VWAP by 1-minute bucket
vwap1m:select vwap:qty wavg price, volume:sum qty, prints:count i by sym, minute:0D00:01 xbar time from trades

/ 4. Order-flow imbalance per second (Cont-Kukanov-Stoikov 2014), from the quotes table
ofi:{[q] q:update pb:prev bid, pa:prev ask, pbq:prev bidq, paq:prev askq by sym from q;
  q:update e:((bid>=pb)*bidq)-((bid<=pb)*pbq)-((ask<=pa)*askq)+((ask>=pa)*paq) from q;
  select ofi:sum e, dmid:0.5*(last bid+ask)-first bid+ask by sym, sec:0D00:00:01 xbar time from q}
ofi1s:ofi quotes

/ 5. Mark-out of a passive fill at horizon h (ns): mid at time+h minus fill price, via aj on a shifted key
markout:{[fills;h] f:update time:time+h from fills; m:aj[`sym`time; f; select sym,time,mid:0.5*bid+ask from quotes]; update mo:mid-price from m}
