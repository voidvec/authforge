#!/usr/bin/env python3
"""gen-charts.py — blog charts rendered from the four-product benchmark data.

READ-ONLY tool: imports the aggregation logic from gen-comparison.py (same
newest-session policy, same steady-state definition — no hand-typed numbers)
and renders the launch blog post's figures:

  1. five-scenarios.png — grouped bars, steady-state QPS per scenario
     (log y-scale: the range spans 738 → 87,499 and linear bars would
     flatten the non-Fulla products into invisibility)
  2. cold-start.png     — horizontal bars, fresh cold-start seconds

Output: blog/2026-09-15-why-cpp-oauth2-server/ (created if missing).
Requires matplotlib (pip install matplotlib).

Usage:
    python benchmarks/reporting/gen-charts.py
"""
from __future__ import annotations

import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

# gen-comparison.py's filename contains a hyphen → plain import is not
# possible; load it by path instead.
import importlib.util  # noqa: E402

_spec = importlib.util.spec_from_file_location(
    "gen_comparison", Path(__file__).resolve().parent / "gen-comparison.py"
)
gc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gc)

OUT_DIR = gc.REPO / "blog" / "2026-09-15-why-cpp-oauth2-server"

# Palette: Fulla carries the accent; competitors stay distinct but muted so
# the story ("which bar leads") reads at thumbnail size.
COLORS = {
    "fulla": "#2563eb",     # blue
    "keycloak": "#94a3b8",  # slate
    "ory": "#d97706",       # amber
    "zitadel": "#65a30d",   # olive
}
ORDER = ["fulla", "keycloak", "ory", "zitadel"]
SCENARIOS = [s for s, _ in gc.SCENARIOS]  # scenario key order from the report


def qps_chart(ax) -> Path | None:
    width = 0.2
    xs = range(len(SCENARIOS))
    any_bar = False
    for i, product in enumerate(ORDER):
        smap = gc.staircase_map(product)
        vals = []
        for scen in SCENARIOS:
            entry = gc.steady(smap.get(scen, {}))
            vals.append(entry[1]["qps"] if entry else None)
        x = [xv + (i - 1.5) * width for xv in xs]
        for xv, v in zip(x, vals):
            if v is None:
                # Real data gap (not a rendering bug): mark the empty slot so
                # the chart never reads as "forgot to measure".
                ax.annotate(
                    "N/A",
                    (xv, 520),
                    ha="center",
                    fontsize=8,
                    color="#94a3b8",
                )
                continue
            any_bar = True
            ax.bar(xv, v, width=width * 0.92, color=COLORS[product], zorder=3)
            ax.annotate(
                gc.fmt_qps(v).replace(" ", ""),
                (xv, v),
                textcoords="offset points",
                xytext=(0, 3),
                ha="center",
                fontsize=7.5,
                rotation=0,
                color="#0f172a" if product == "fulla" else "#475569",
                fontweight="bold" if product == "fulla" else "normal",
            )
    ax.set_yscale("log")
    ax.set_ylim(500, 220000)
    ax.set_xticks(list(xs))
    ax.set_xticklabels([lbl.replace("_", "\n") for _, lbl in gc.SCENARIOS], fontsize=9)
    ax.set_ylabel("Steady-state QPS (log scale)")
    ax.set_title(
        "Steady-state throughput by scenario — same host, same PostgreSQL 17, "
        "each product on its recommended config",
        fontsize=10.5,
        pad=12,
    )
    ax.grid(axis="y", which="major", color="#e2e8f0", zorder=0)
    ax.grid(axis="y", which="minor", color="#f1f5f9", zorder=0)
    ax.spines[["top", "right"]].set_visible(False)
    ax.legend(
        [plt.Rectangle((0, 0), 1, 1, color=COLORS[p]) for p in ORDER],
        [gc.PRODUCT_LABEL[p] for p in ORDER],
        loc="upper right",
        frameon=False,
        fontsize=9,
    )
    ax.text(
        0.0,
        -0.17,
        "Steady state = highest concurrency with error rate < 0.01% · 2026-08-23 TTL=30 session "
        "profile · N/A: Zitadel machine users get no refresh tokens via the official jwt-bearer "
        "path · S4 (authorization_code + PKCE) is not in the four-product suite — its multi-step "
        "browser flow has no comparable configuration across products · Source: "
        "benchmarks/competitors/results/ (reproducible).",
        transform=ax.transAxes,
        fontsize=7,
        color="#475569",
    )
    return any_bar


def coldstart_chart(ax) -> bool:
    vals, names = [], []
    for product in ORDER:
        cs = gc.cold_start(product)
        if cs.get("fresh_s") is not None:
            vals.append(cs["fresh_s"])
            names.append(gc.PRODUCT_LABEL[product])
    if not vals:
        return False
    prod_of = {gc.PRODUCT_LABEL[p]: p for p in ORDER}
    pairs = sorted(zip(names, vals), key=lambda nv: nv[1])
    names = [n for n, _ in pairs]
    vals = [v for _, v in pairs]
    colors = [COLORS[prod_of[n]] for n in names]
    longest = max(vals)
    bars = ax.barh(names, vals, color=colors, zorder=3, height=0.62)
    for bar, v in zip(bars, vals):
        label = f"{v:.2f}s" if v < 10 else f"{v:.1f}s"
        if v >= longest * 0.3:
            # Long bar: label inside the right end, white on the bar color.
            ax.annotate(
                label,
                (v, bar.get_y() + bar.get_height() / 2),
                textcoords="offset points",
                xytext=(-6, 0),
                va="center",
                ha="right",
                fontsize=10,
                fontweight="bold",
                color="white",
            )
        else:
            ax.annotate(
                label,
                (v, bar.get_y() + bar.get_height() / 2),
                textcoords="offset points",
                xytext=(6, 0),
                va="center",
                ha="left",
                fontsize=10,
                fontweight="bold",
                color="#0f172a",
            )
    ax.set_xlabel("Seconds to /health/ready (fresh start, incl. DB init)")
    ax.set_title("Cold start from scratch — lower is better", fontsize=10.5, pad=12)
    ax.set_xlim(0, longest * 1.12)
    ax.margins(x=0.04)
    ax.tick_params(axis="x", pad=6)
    ax.grid(axis="x", color="#e2e8f0", zorder=0)
    ax.spines[["top", "right"]].set_visible(False)
    ax.text(
        0.0,
        -0.24,
        "Fresh = clean volume + auto-migration; median of recorded runs. "
        "Source: coldstart JSONs under benchmarks/*/results/.",
        transform=ax.transAxes,
        fontsize=7.5,
        color="#475569",
    )
    return True


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    fig, ax = plt.subplots(figsize=(11.5, 6.2), dpi=200)
    if qps_chart(ax):
        fig.tight_layout()
        out = OUT_DIR / "five-scenarios.png"
        fig.savefig(out, bbox_inches="tight", facecolor="white")
        print(f"wrote {out}")
    else:
        print("no staircase data found — skipped five-scenarios.png")
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(9.5, 4.4), dpi=200)
    if coldstart_chart(ax):
        fig.tight_layout()
        out = OUT_DIR / "cold-start.png"
        fig.savefig(out, bbox_inches="tight", facecolor="white")
        print(f"wrote {out}")
    else:
        print("no cold-start data found — skipped cold-start.png")
    plt.close(fig)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
