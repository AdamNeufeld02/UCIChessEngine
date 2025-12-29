#!/usr/bin/env python3
"""
Plot history-score distributions from HistoryDistLogger JSONL output.

Input format (per line): JSON object with:
  - "by_depth": { "<depth>": {
        "<key>": { "min": -8192, "max": 8192, "step": 16, "n": ..., "bins":[...] },
        ...
    }, ... }

Keys expected (from historystats.h):
  quiet_fail_high, quiet_improve_alpha, quiet_fail_low,
  capt_fail_high,  capt_improve_alpha,  capt_fail_low

Usage:
  python plot_history_dists.py path/to/history.jsonl --outdir plots
  python plot_history_dists.py history.jsonl --depths 8 9 10 11 --trim 0.01
"""

from __future__ import annotations

import argparse
import json
import math
import os
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

import matplotlib.pyplot as plt


EXPECTED_KEYS = [
    "quiet_fail_high",
    "quiet_improve_alpha",
    "quiet_fail_low",
    "capt_fail_high",
    "capt_improve_alpha",
    "capt_fail_low",
]

QUIET_KEYS = ["quiet_fail_high", "quiet_improve_alpha", "quiet_fail_low"]
CAPT_KEYS  = ["capt_fail_high",  "capt_improve_alpha",  "capt_fail_low"]


@dataclass
class HistSpec:
    minv: int
    maxv: int
    step: int

    @property
    def nbins(self) -> int:
        # FixedHistogram uses inclusive range [-MAX_ABS..MAX_ABS] with step buckets,
        # but we don't need the exact formula; we just require consistent length.
        return int((self.maxv - self.minv) // self.step) + 1

    def centers(self) -> List[float]:
        return [self.minv + i * self.step for i in range(self.nbins)]


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser()
    ap.add_argument("jsonl", help="Path to JSONL history distribution log")
    ap.add_argument("--outdir", default="history_plots", help="Output directory for PNGs")
    ap.add_argument("--trim", type=float, default=0.01,
                    help="Percentile trim for x-axis (0.01 => show 1%%..99%% mass). Use 0 to disable.")
    ap.add_argument("--depths", nargs="*", type=int, default=None,
                    help="Only include these depth buckets (e.g., --depths 8 9 10). Default: all.")
    ap.add_argument("--normalize", action="store_true",
                    help="Plot probability density (normalized) instead of raw counts.")
    ap.add_argument("--logy", action="store_true",
                    help="Use log scale for Y axis (helps with heavy tails).")
    ap.add_argument("--max_lines", type=int, default=0,
                    help="Read at most N lines (0 = all). Useful for quick tests.")
    ap.add_argument("--per_depth", action="store_true",
                    help="Also emit per-depth plots (in addition to aggregated plots).")
    ap.add_argument("--per_depth_max", type=int, default=3,
                    help="Max depth to plot when --per_depth is set. Example: 3 => depths 0..3 (or 1..3 depending on your logger).")
    ap.add_argument("--condprob", action="store_true",
                    help="Also plot conditional probabilities P(outcome | history score).")
    ap.add_argument("--min_bin", type=int, default=200,
                    help="Hide bins with fewer than this many samples when plotting conditional probabilities.")
    ap.add_argument("--smooth", type=int, default=0,
                    help="Moving-average smoothing window (in bins) for conditional probabilities. 0 disables.")
    ap.add_argument("--threshold_curves", action="store_true",
                    help="Also plot pruning-threshold selection curves (kept mass, kept FH/FL, risk among kept).")
    return ap.parse_args()


def _ensure_spec_consistent(spec: Optional[HistSpec], hobj: dict) -> HistSpec:
    hspec = HistSpec(int(hobj["min"]), int(hobj["max"]), int(hobj["step"]))
    if spec is None:
        return hspec
    if (spec.minv, spec.maxv, spec.step) != (hspec.minv, hspec.maxv, hspec.step):
        raise ValueError(
            f"Inconsistent histogram spec detected: "
            f"existing={spec} vs new={hspec}. "
            f"Are you mixing logs built with different MAX_ABS/STEP?"
        )
    return spec

def load_by_depth(
    jsonl_path: str,
    max_lines: int = 0
) -> Tuple[HistSpec, Dict[int, Dict[str, List[int]]]]:
    """
    Returns (spec, depth_map) where:
      depth_map[depth][key] = aggregated bins at that depth
    """
    spec: Optional[HistSpec] = None
    depth_map: Dict[int, Dict[str, List[int]]] = {}

    with open(jsonl_path, "r", encoding="utf-8") as f:
        for idx, line in enumerate(f):
            if max_lines and idx >= max_lines:
                break
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue

            by_depth = obj.get("by_depth", {})
            if not isinstance(by_depth, dict):
                continue

            for d_str, bucket in by_depth.items():
                if not isinstance(bucket, dict):
                    continue
                try:
                    d = int(d_str)
                except ValueError:
                    continue

                if d not in depth_map:
                    depth_map[d] = {}

                for key in EXPECTED_KEYS:
                    hobj = bucket.get(key)
                    if not hobj or not isinstance(hobj, dict):
                        continue
                    bins = hobj.get("bins")
                    if not isinstance(bins, list) or len(bins) == 0:
                        continue

                    spec = _ensure_spec_consistent(spec, hobj)

                    if key not in depth_map[d]:
                        depth_map[d][key] = [0] * len(bins)

                    if len(depth_map[d][key]) != len(bins):
                        raise ValueError(f"Bin length mismatch for depth {d}, key {key}")

                    for i, c in enumerate(bins):
                        depth_map[d][key][i] += int(c)

    if spec is None:
        raise RuntimeError("No valid histograms found. Check the JSONL path and contents.")

    # Fill missing keys for each depth so plotting code can assume presence
    for d in depth_map:
        for key in EXPECTED_KEYS:
            depth_map[d].setdefault(key, [0] * spec.nbins)

    return spec, depth_map

def load_and_aggregate(
    jsonl_path: str,
    depths_filter: Optional[List[int]] = None,
    max_lines: int = 0
) -> Tuple[HistSpec, Dict[str, List[int]]]:
    """
    Returns:
      (spec, agg_bins_by_key)
    where agg_bins_by_key[key] is a list[int] length NBINS.
    """
    spec: Optional[HistSpec] = None
    agg: Dict[str, List[int]] = {}

    # Depth filter as set of string keys, because JSON uses "by_depth": {"8": {...}}
    depth_set = None
    if depths_filter is not None and len(depths_filter) > 0:
        depth_set = {str(d) for d in depths_filter}

    with open(jsonl_path, "r", encoding="utf-8") as f:
        for idx, line in enumerate(f):
            if max_lines and idx >= max_lines:
                break
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                # Skip malformed lines instead of failing hard
                continue

            by_depth = obj.get("by_depth", {})
            if not isinstance(by_depth, dict):
                continue

            for d_str, bucket in by_depth.items():
                if depth_set is not None and d_str not in depth_set:
                    continue
                if not isinstance(bucket, dict):
                    continue

                for key in EXPECTED_KEYS:
                    hobj = bucket.get(key)
                    if not hobj or not isinstance(hobj, dict):
                        continue
                    bins = hobj.get("bins")
                    if not isinstance(bins, list) or len(bins) == 0:
                        continue

                    spec = _ensure_spec_consistent(spec, hobj)

                    if key not in agg:
                        agg[key] = [0] * len(bins)
                    if len(agg[key]) != len(bins):
                        raise ValueError(f"Bin length mismatch for {key}: {len(agg[key])} vs {len(bins)}")

                    for i, c in enumerate(bins):
                        agg[key][i] += int(c)

    if spec is None:
        raise RuntimeError("No valid histograms found. Check the JSONL path and contents.")

    # Ensure all keys exist (even if empty) for consistent plotting
    for key in EXPECTED_KEYS:
        agg.setdefault(key, [0] * spec.nbins)

    return spec, agg


def find_mass_xlim(
    x: List[float],
    counts: List[int],
    trim: float
) -> Tuple[float, float]:
    """
    Choose x-limits:
      - remove completely empty tails
      - additionally trim based on cumulative mass (trim..1-trim) if trim > 0
    """
    n = sum(counts)
    if n <= 0:
        return (min(x), max(x))

    # First, crop empty tails
    left = 0
    right = len(counts) - 1
    while left < len(counts) and counts[left] == 0:
        left += 1
    while right >= 0 and counts[right] == 0:
        right -= 1
    if left >= right:
        return (min(x), max(x))

    # Optional percentile trim
    if trim and 0.0 < trim < 0.49:
        cum = 0
        lo_idx = left
        hi_idx = right
        target_lo = n * trim
        target_hi = n * (1.0 - trim)

        for i in range(left, right + 1):
            cum += counts[i]
            if cum >= target_lo:
                lo_idx = i
                break

        cum = 0
        for i in range(right, left - 1, -1):
            cum += counts[i]
            if (n - cum) <= target_hi:
                hi_idx = i
                break

        # Pad by a couple bins for breathing room
        lo_idx = max(left, lo_idx - 2)
        hi_idx = min(right, hi_idx + 2)
        return (x[lo_idx], x[hi_idx])

    # If trim disabled, just show the non-empty span (with padding)
    lo_idx = max(0, left - 2)
    hi_idx = min(len(x) - 1, right + 2)
    return (x[lo_idx], x[hi_idx])


def to_density(counts: List[int], step: int) -> List[float]:
    n = sum(counts)
    if n <= 0:
        return [0.0] * len(counts)
    # density so area ~ 1 over x, using bin width "step"
    return [(c / (n * step)) for c in counts]

def moving_average(y: List[float], w: int) -> List[float]:
    if w <= 1:
        return y
    w = int(w)
    half = w // 2
    out = [0.0] * len(y)
    for i in range(len(y)):
        lo = max(0, i - half)
        hi = min(len(y), i + half + 1)
        s = 0.0
        c = 0
        for j in range(lo, hi):
            if not math.isnan(y[j]):
                s += y[j]
                c += 1
        out[i] = (s / c) if c else float("nan")
    return out


def suffix_sums(a: List[int]) -> List[int]:
    """suffix[i] = sum_{j>=i} a[j]"""
    out = [0] * len(a)
    running = 0
    for i in range(len(a) - 1, -1, -1):
        running += a[i]
        out[i] = running
    return out


def plot_threshold_curves(
    x: List[float],
    agg: Dict[str, List[int]],
    prefix: str,  # "quiet" or "capt"
    title: str,
    outpath: str,
    trim: float,
    min_bin: int,
    smooth: int,
):
    """
    Plots curves vs threshold T (x-axis = history score), where we prune below T:

      Coverage curves (fraction of each class kept):
        - kept_total_frac(T)   = P(score >= T)
        - kept_fh_frac(T)      = P(FH & score>=T) / P(FH)
        - kept_ia_frac(T)      = P(IA & score>=T) / P(IA)
        - kept_fl_frac(T)      = P(FL & score>=T) / P(FL)

      Composition curves (probabilities within the kept set):
        - risk_kept(T)         = P(FH | score>=T)
        - ia_kept(T)           = P(IA | score>=T)
        - fl_kept(T)           = P(FL | score>=T)

    min_bin applies to the kept-set size: if total_kept(T) < min_bin => mask composition curves.
    """
    fh = agg[f"{prefix}_fail_high"]
    ia = agg[f"{prefix}_improve_alpha"]
    fl = agg[f"{prefix}_fail_low"]

    total = [fh[i] + ia[i] + fl[i] for i in range(len(fh))]

    # x-limits based on where data is (using total)
    xmin, xmax = find_mass_xlim(x, total, trim)

    # Suffix sums give us "kept if threshold is at bin i"
    suf_total = suffix_sums(total)
    suf_fh = suffix_sums(fh)
    suf_ia = suffix_sums(ia)
    suf_fl = suffix_sums(fl)

    total_all = sum(total)
    fh_all = sum(fh)
    ia_all = sum(ia)
    fl_all = sum(fl)

    # Coverage / kept fractions
    kept_total_frac = [(suf_total[i] / total_all) if total_all else float("nan") for i in range(len(x))]
    kept_fh_frac = [(suf_fh[i] / fh_all) if fh_all else float("nan") for i in range(len(x))]
    kept_ia_frac = [(suf_ia[i] / ia_all) if ia_all else float("nan") for i in range(len(x))]
    kept_fl_frac = [(suf_fl[i] / fl_all) if fl_all else float("nan") for i in range(len(x))]

    # Composition within kept set
    risk_kept = []
    ia_kept = []
    fl_kept = []
    for i in range(len(x)):
        denom = suf_total[i]
        if denom and denom >= min_bin:
            risk_kept.append(suf_fh[i] / denom)  # P(FH | kept)
            ia_kept.append(suf_ia[i] / denom)    # P(IA | kept)
            fl_kept.append(suf_fl[i] / denom)    # P(FL | kept)
        else:
            risk_kept.append(float("nan"))
            ia_kept.append(float("nan"))
            fl_kept.append(float("nan"))

    # Optional smoothing (helps if bins are small)
    if smooth and smooth > 1:
        kept_total_frac = moving_average(kept_total_frac, smooth)
        kept_fh_frac = moving_average(kept_fh_frac, smooth)
        kept_ia_frac = moving_average(kept_ia_frac, smooth)
        kept_fl_frac = moving_average(kept_fl_frac, smooth)
        risk_kept = moving_average(risk_kept, smooth)
        ia_kept = moving_average(ia_kept, smooth)
        fl_kept = moving_average(fl_kept, smooth)

    plt.figure(figsize=(12, 6))
    # Coverage curves
    plt.plot(x, kept_total_frac, label="Kept total fraction P(score>=T)")
    plt.plot(x, kept_fh_frac, label="Kept FailHigh fraction of all FH")
    plt.plot(x, kept_ia_frac, label="Kept ImproveAlpha fraction of all IA")
    plt.plot(x, kept_fl_frac, label="Kept FailLow fraction of all FL")

    # Composition curves
    plt.plot(x, risk_kept, label="Among kept: P(FH | score>=T)")
    plt.plot(x, ia_kept, label="Among kept: P(IA | score>=T)")
    plt.plot(x, fl_kept, label="Among kept: P(FL | score>=T)")

    plt.title(title)
    plt.xlabel("Threshold T (history score) — prune below T")
    plt.ylabel("Fraction / Probability")
    plt.ylim(0.0, 1.0)
    plt.xlim(xmin, xmax)
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(outpath, dpi=160)
    plt.close()


def conditional_probs_for_quiet_or_capture(
    agg: Dict[str, List[int]],
    prefix: str  # "quiet" or "capt"
) -> Tuple[List[int], List[float], List[float]]:
    """
    Returns:
      totals_per_bin, p_fail_high, p_fail_low
    computed as:
      p_fail_high[i] = FH[i] / (FH[i] + IA[i] + FL[i])  (if denom>0)
      p_fail_low[i]  = FL[i] / (FH[i] + IA[i] + FL[i])  (if denom>0)
    """
    fh = agg[f"{prefix}_fail_high"]
    ia = agg[f"{prefix}_improve_alpha"]
    fl = agg[f"{prefix}_fail_low"]

    totals = [fh[i] + ia[i] + fl[i] for i in range(len(fh))]
    p_fh = [(fh[i] / totals[i]) if totals[i] else float("nan") for i in range(len(fh))]
    p_fl = [(fl[i] / totals[i]) if totals[i] else float("nan") for i in range(len(fl))]
    return totals, p_fh, p_fl


def plot_condprob_overlay(
    x: List[float],
    totals: List[int],
    p_fail_high: List[float],
    p_fail_low: List[float],
    title: str,
    outpath: str,
    trim: float,
    min_bin: int,
    smooth: int,
):
    # Choose x-limits based on where we actually have mass (using totals)
    xmin, xmax = find_mass_xlim(x, totals, trim)

    # Mask low-sample bins so they don't create misleading spikes
    p_fh = [p_fail_high[i] if totals[i] >= min_bin else float("nan") for i in range(len(x))]
    p_fl = [p_fail_low[i]  if totals[i] >= min_bin else float("nan") for i in range(len(x))]

    # Optional smoothing
    p_fh = moving_average(p_fh, smooth) if smooth > 1 else p_fh
    p_fl = moving_average(p_fl, smooth) if smooth > 1 else p_fl

    plt.figure(figsize=(12, 6))
    plt.plot(x, p_fl, label="P(FailLow | score)")
    plt.plot(x, p_fh, label="P(FailHigh | score)")

    plt.title(title)
    plt.xlabel("History score")
    plt.ylabel("Conditional probability")
    plt.ylim(0.0, 1.0)
    plt.xlim(xmin, xmax)
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(outpath, dpi=160)
    plt.close()


def plot_overlay(
    x: List[float],
    series: Dict[str, List[int]],
    title: str,
    outpath: str,
    trim: float,
    normalize: bool,
    logy: bool,
):
    plt.figure(figsize=(12, 6))

    # Determine x-limits based on combined mass
    combined = [0] * len(x)
    for counts in series.values():
        for i, c in enumerate(counts):
            combined[i] += c
    xmin, xmax = find_mass_xlim(x, combined, trim)

    for label, counts in series.items():
        y = to_density(counts, step=int(abs(x[1] - x[0])) if len(x) > 1 else 1) if normalize else counts
        plt.plot(x, y, label=label)

    plt.title(title)
    plt.xlabel("History score")
    plt.ylabel("Density" if normalize else "Count")
    plt.xlim(xmin, xmax)
    plt.grid(True, alpha=0.3)
    if logy:
        plt.yscale("log")
    plt.legend()
    plt.tight_layout()
    plt.savefig(outpath, dpi=160)
    plt.close()


def plot_bars(
    x: List[float],
    counts: List[int],
    title: str,
    outpath: str,
    trim: float,
    normalize: bool,
    logy: bool,
):
    step = int(abs(x[1] - x[0])) if len(x) > 1 else 1
    y = to_density(counts, step=step) if normalize else counts

    plt.figure(figsize=(12, 6))
    xmin, xmax = find_mass_xlim(x, counts, trim)

    plt.bar(x, y, width=step * 0.9, align="center")
    plt.title(title)
    plt.xlabel("History score")
    plt.ylabel("Density" if normalize else "Count")
    plt.xlim(xmin, xmax)
    plt.grid(True, alpha=0.3, axis="y")
    if logy:
        plt.yscale("log")
    plt.tight_layout()
    plt.savefig(outpath, dpi=160)
    plt.close()


def main():
    args = parse_args()
    os.makedirs(args.outdir, exist_ok=True)

    spec, agg = load_and_aggregate(args.jsonl, depths_filter=args.depths, max_lines=args.max_lines)
    x = spec.centers()

    # Overlays: quiet and captures
    quiet_series = {
        "FailHigh": agg["quiet_fail_high"],
        "ImproveAlpha": agg["quiet_improve_alpha"],
        "FailLow": agg["quiet_fail_low"],
    }
    capt_series = {
        "FailHigh": agg["capt_fail_high"],
        "ImproveAlpha": agg["capt_improve_alpha"],
        "FailLow": agg["capt_fail_low"],
    }

    depth_note = ""
    if args.depths:
        depth_note = f" (depths={','.join(map(str,args.depths))})"

    plot_overlay(
        x,
        quiet_series,
        title=f"Quiet history score distributions by outcome{depth_note}",
        outpath=os.path.join(args.outdir, "quiet_overlay.png"),
        trim=args.trim,
        normalize=args.normalize,
        logy=args.logy,
    )
    plot_overlay(
        x,
        capt_series,
        title=f"Capture history score distributions by outcome{depth_note}",
        outpath=os.path.join(args.outdir, "capture_overlay.png"),
        trim=args.trim,
        normalize=args.normalize,
        logy=args.logy,
    )

    # Individual bar plots (often easier to read than overlays)
    for k in QUIET_KEYS + CAPT_KEYS:
        move_type = "quiet" if k.startswith("quiet_") else "capture"
        outcome = k.split("_", 1)[1]  # e.g. fail_high
        counts = agg[k]
        plot_bars(
            x,
            counts,
            title=f"{move_type.capitalize()} history: {outcome.replace('_',' ').title()}{depth_note}",
            outpath=os.path.join(args.outdir, f"{k}.png"),
            trim=args.trim,
            normalize=args.normalize,
            logy=args.logy,
        )

    if args.condprob:
        totals_q, p_fh_q, p_fl_q = conditional_probs_for_quiet_or_capture(agg, "quiet")
        plot_condprob_overlay(
            x,
            totals_q,
            p_fh_q,
            p_fl_q,
            title=f"Quiet: P(outcome | history score){depth_note}",
            outpath=os.path.join(args.outdir, "quiet_condprob.png"),
            trim=args.trim,
            min_bin=args.min_bin,
            smooth=args.smooth,
        )

        totals_c, p_fh_c, p_fl_c = conditional_probs_for_quiet_or_capture(agg, "capt")
        plot_condprob_overlay(
            x,
            totals_c,
            p_fh_c,
            p_fl_c,
            title=f"Capture: P(outcome | history score){depth_note}",
            outpath=os.path.join(args.outdir, "capture_condprob.png"),
            trim=args.trim,
            min_bin=args.min_bin,
            smooth=args.smooth,
        )

    if args.threshold_curves:
        plot_threshold_curves(
            x,
            agg,
            prefix="quiet",
            title=f"Quiet threshold curves (aggregate){depth_note}",
            outpath=os.path.join(args.outdir, "quiet_threshold_curves.png"),
            trim=args.trim,
            min_bin=args.min_bin,
            smooth=args.smooth,
        )
        plot_threshold_curves(
            x,
            agg,
            prefix="capt",
            title=f"Capture threshold curves (aggregate){depth_note}",
            outpath=os.path.join(args.outdir, "capture_threshold_curves.png"),
            trim=args.trim,
            min_bin=args.min_bin,
            smooth=args.smooth,
        )

    if args.per_depth:
        spec2, depth_map = load_by_depth(args.jsonl, max_lines=args.max_lines)

        # Safety: should match spec from aggregate pass
        if (spec.minv, spec.maxv, spec.step) != (spec2.minv, spec2.maxv, spec2.step):
            raise ValueError("Histogram spec mismatch between aggregate and per-depth loads.")

        x = spec.centers()

        # Plot depths <= per_depth_max
        depths = [d for d in sorted(depth_map.keys()) if d <= args.per_depth_max]
        if not depths:
            print(f"⚠️  --per_depth set, but no depths <= {args.per_depth_max} found in file.")
        else:
            for d in depths:
                agg_d = depth_map[d]

                quiet_series = {
                    "FailHigh": agg_d["quiet_fail_high"],
                    "ImproveAlpha": agg_d["quiet_improve_alpha"],
                    "FailLow": agg_d["quiet_fail_low"],
                }
                capt_series = {
                    "FailHigh": agg_d["capt_fail_high"],
                    "ImproveAlpha": agg_d["capt_improve_alpha"],
                    "FailLow": agg_d["capt_fail_low"],
                }

                plot_overlay(
                    x,
                    quiet_series,
                    title=f"Quiet history distributions by outcome (depth={d})",
                    outpath=os.path.join(args.outdir, f"quiet_overlay_depth_{d}.png"),
                    trim=args.trim,
                    normalize=args.normalize,
                    logy=args.logy,
                )
                plot_overlay(
                    x,
                    capt_series,
                    title=f"Capture history distributions by outcome (depth={d})",
                    outpath=os.path.join(args.outdir, f"capture_overlay_depth_{d}.png"),
                    trim=args.trim,
                    normalize=args.normalize,
                    logy=args.logy,
                )

                if args.condprob:
                    totals_q, p_fh_q, p_fl_q = conditional_probs_for_quiet_or_capture(agg_d, "quiet")
                    plot_condprob_overlay(
                        x,
                        totals_q,
                        p_fh_q,
                        p_fl_q,
                        title=f"Quiet: P(outcome | history score) (depth={d})",
                        outpath=os.path.join(args.outdir, f"quiet_condprob_depth_{d}.png"),
                        trim=args.trim,
                        min_bin=args.min_bin,
                        smooth=args.smooth,
                    )

                    totals_c, p_fh_c, p_fl_c = conditional_probs_for_quiet_or_capture(agg_d, "capt")
                    plot_condprob_overlay(
                        x,
                        totals_c,
                        p_fh_c,
                        p_fl_c,
                        title=f"Capture: P(outcome | history score) (depth={d})",
                        outpath=os.path.join(args.outdir, f"capture_condprob_depth_{d}.png"),
                        trim=args.trim,
                        min_bin=args.min_bin,
                        smooth=args.smooth,
                    )

                if args.threshold_curves:
                    plot_threshold_curves(
                        x,
                        agg_d,
                        prefix="quiet",
                        title=f"Quiet threshold curves (depth={d})",
                        outpath=os.path.join(args.outdir, f"quiet_threshold_curves_depth_{d}.png"),
                        trim=args.trim,
                        min_bin=args.min_bin,
                        smooth=args.smooth,
                    )
                    plot_threshold_curves(
                        x,
                        agg_d,
                        prefix="capt",
                        title=f"Capture threshold curves (depth={d})",
                        outpath=os.path.join(args.outdir, f"capture_threshold_curves_depth_{d}.png"),
                        trim=args.trim,
                        min_bin=args.min_bin,
                        smooth=args.smooth,
                    )

    print(f"✅ Wrote plots to: {os.path.abspath(args.outdir)}")
    print("   - quiet_overlay.png")
    print("   - capture_overlay.png")
    print("   - plus one PNG per key (quiet_fail_high, ...)")


if __name__ == "__main__":
    main()