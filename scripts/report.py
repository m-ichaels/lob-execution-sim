#!/usr/bin/env python3
"""report.pdf from results/summary.md and results/figures/*.png (fpdf2).   python scripts/report.py"""
import os
import re
import sys

from fpdf import FPDF

R = sys.argv[1] if len(sys.argv) > 1 else "results"
OUT = sys.argv[2] if len(sys.argv) > 2 else "report.pdf"

INTRO = """Question. How much does the choice of order-book simulator change the ranking of execution algorithms, and how much implementation shortfall can an order-flow-aware scheduler save once queue position and fills are modelled honestly?

Method. A C++ event-driven engine replays self-recorded Binance BTCUSDT L2 data with three queue-position models (naive FIFO, hftbacktest power-probability, risk-averse), and simulates the same market with a calibrated queue-reactive model (Huang-Lehalle-Rosenbaum 2015 with Bodor-Carlier order sizes and a data-driven reference-price jump layer) and a zero-intelligence Poisson null (Abergel-Jedidi 2013). TWAP, VWAP, Almgren-Chriss and an OFI-adaptive Cont-Kukanov scheduler run unchanged in every mode, in-process and through a FIX 4.4 gateway. Realism is scored with Vyetrenko-style stylised facts and the impact response function; fill probabilities and adverse selection are measured from replay probes and checked against order-level Bitstamp data; implementation shortfall is attributed into spread, timing, impact and opportunity cost with common-random-number counterfactuals.

Caveats. Crypto is a proxy for equities/FX (24/7, maker/taker fees, no last look, no internalisation). One recording day; confidence intervals over episodes within the day plus a four-day synthetic check. Coinbase's order-level channel now needs an API key, so Bitstamp's public order-level feed is the L3 ground truth."""

FIGS = [("validation.png", "Simulator realism: KS distance per stylised fact, impact response function, scalar facts relative to live."),
        ("ranking.png", "Implementation shortfall by algorithm, simulator mode and queue assumption (95% bootstrap CI)."),
        ("ranking_vs_twap.png", "Paired difference vs TWAP; negative = cheaper than TWAP."),
        ("attribution.png", "Shortfall attribution: spread, timing, impact, opportunity (sums to total by construction)."),
        ("fills.png", "Passive fill probability: replay probes on Binance, fill models vs empirical, and Bitstamp order-level truth vs L2 replay."),
        ("markout.png", "Adverse selection: mark-outs of passive fills and the correlation between being filled and the post-placement return."),
        ("ofi_l3.png", "OFI regression R-squared by hour and volatility regime; queue-position model errors against order-level truth.")]


class PDF(FPDF):
    def header(self):
        self.set_font("Helvetica", "B", 9); self.set_text_color(120); self.cell(0, 6, "lob-execution-sim - order-book execution simulator and TCA", align="R"); self.ln(8); self.set_text_color(0)

    def footer(self):
        self.set_y(-12); self.set_font("Helvetica", "", 8); self.set_text_color(120); self.cell(0, 6, f"{self.page_no()}", align="C")


def clean(s):
    return (s.replace("–", "-").replace("—", "-").replace("−", "-").replace("σ", "sigma").replace("β", "beta").replace("η", "eta").replace("Δ", "d").replace("×", "x").replace("≥", ">=").replace("…", "...")
             .replace("²", "^2").replace("±", "+/-").replace("**", "").replace("`", ""))


def md_table(pdf, rows):
    cols = [c.strip() for c in rows[0].strip("|").split("|")]
    data = [[clean(c.strip()) for c in r.strip("|").split("|")] for r in rows[2:]]
    pdf.set_font("Helvetica", "", 6.5)
    n = len(cols); w = (pdf.w - 20) / n
    widths = [w] * n
    pdf.set_font("Helvetica", "B", 6.5)
    for c, wd in zip(cols, widths): pdf.cell(wd, 5, clean(c)[:40], border=1)
    pdf.ln(5); pdf.set_font("Helvetica", "", 6.5)
    for r in data:
        if pdf.get_y() > pdf.h - 20: pdf.add_page()
        for c, wd in zip(r, widths): pdf.cell(wd, 4.5, c[:40], border=1)
        pdf.ln(4.5)
    pdf.ln(2)


def main():
    pdf = PDF(); pdf.set_auto_page_break(auto=True, margin=15); pdf.add_page()
    pdf.set_font("Helvetica", "B", 16); pdf.cell(0, 10, "Order-Book Execution Simulator and TCA", new_x="LMARGIN", new_y="NEXT")
    pdf.set_font("Helvetica", "", 9)
    for para in INTRO.split("\n\n"):
        pdf.multi_cell(0, 4.5, clean(para)); pdf.ln(2)
    for fn, cap in FIGS:
        p = os.path.join(R, "figures", fn)
        if not os.path.exists(p): continue
        if pdf.get_y() > pdf.h - 90: pdf.add_page()
        pdf.image(p, w=pdf.w - 20); pdf.set_font("Helvetica", "I", 8); pdf.multi_cell(0, 4, clean(cap)); pdf.ln(3); pdf.set_font("Helvetica", "", 9)
    # summary tables
    sm = os.path.join(R, "summary.md")
    if os.path.exists(sm):
        pdf.add_page()
        lines = open(sm, encoding="utf-8").read().splitlines()
        i = 0
        while i < len(lines):
            l = lines[i]
            if l.startswith("## "):
                pdf.set_font("Helvetica", "B", 11); pdf.ln(2); pdf.cell(0, 7, clean(l[3:]), new_x="LMARGIN", new_y="NEXT"); pdf.set_font("Helvetica", "", 9); i += 1
            elif l.startswith("### "):
                pdf.set_font("Helvetica", "B", 9); pdf.cell(0, 6, clean(l[4:]), new_x="LMARGIN", new_y="NEXT"); pdf.set_font("Helvetica", "", 9); i += 1
            elif l.startswith("|"):
                j = i
                while j < len(lines) and lines[j].startswith("|"): j += 1
                if j - i >= 2: md_table(pdf, lines[i:j])
                i = j
            elif l.startswith("```"):
                j = i + 1
                while j < len(lines) and not lines[j].startswith("```"): j += 1
                pdf.set_font("Courier", "", 7)
                for t in lines[i + 1:j]: pdf.set_x(pdf.l_margin); pdf.multi_cell(0, 3.5, clean(t))
                pdf.set_font("Helvetica", "", 9); i = j + 1
            elif l.strip():
                pdf.set_x(pdf.l_margin); pdf.multi_cell(0, 4.5, clean(l.strip())); i += 1
            else:
                i += 1
    pdf.output(OUT)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
