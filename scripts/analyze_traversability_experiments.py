#!/usr/bin/env python3
"""Analyze traversability experiment outputs for temporal/spatial noise and
cross-method agreement.

Expects `output/<METHOD_NAME>/frame_XXXXXX.csv` folders, one per aggregation
method, each CSV a (r_bins, theta_bins) matrix of {NaN, 0.0, 1.0} values.
"""

import argparse
import os
import re
import sys
from datetime import datetime

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy import ndimage

FRAME_RE = re.compile(r"^frame_(\d{6})\.csv$")

# Categorical palette (fixed order, from the dataviz skill reference palette).
COLOR_BLUE = "#2a78d6"
COLOR_AQUA = "#1baf7a"
COLOR_YELLOW = "#eda100"
METHOD_COLORS = [COLOR_BLUE, COLOR_AQUA, COLOR_YELLOW]
INK_PRIMARY = "#0b0b0b"
INK_MUTED = "#898781"
GRID_HAIRLINE = "#e1e0d9"


def discover_method_folders(output_dir):
    """Return {method_name: folder_path} for each immediate subdirectory of output_dir."""
    if not os.path.isdir(output_dir):
        print(f"WARNING: output directory '{output_dir}' does not exist.")
        return {}
    methods = {}
    for entry in sorted(os.listdir(output_dir)):
        full_path = os.path.join(output_dir, entry)
        if os.path.isdir(full_path):
            methods[entry] = full_path
    return methods


def load_method_frames(method_name, folder_path):
    """Load all frame_XXXXXX.csv files in folder_path into one (F, R, T) array.

    Returns (array, frame_indices) or (None, []) if nothing usable was found.
    Malformed or shape-mismatched CSVs are skipped with a warning, not fatal.
    """
    candidates = []
    for fname in os.listdir(folder_path):
        m = FRAME_RE.match(fname)
        if m:
            candidates.append((int(m.group(1)), os.path.join(folder_path, fname)))
    candidates.sort(key=lambda pair: pair[0])

    if not candidates:
        print(f"WARNING: [{method_name}] no frame_XXXXXX.csv files found in {folder_path}")
        return None, []

    frames = []
    frame_indices = []
    expected_shape = None
    for idx, path in candidates:
        try:
            arr = np.loadtxt(path, delimiter=",")
        except Exception as exc:
            print(f"WARNING: [{method_name}] failed to load {os.path.basename(path)}: {exc}")
            continue
        if arr.ndim != 2:
            print(f"WARNING: [{method_name}] {os.path.basename(path)} is not 2D (shape {arr.shape}), skipping")
            continue
        if expected_shape is None:
            expected_shape = arr.shape
        elif arr.shape != expected_shape:
            print(
                f"WARNING: [{method_name}] {os.path.basename(path)} has shape {arr.shape}, "
                f"expected {expected_shape}, skipping"
            )
            continue
        frames.append(arr)
        frame_indices.append(idx)

    if not frames:
        print(f"WARNING: [{method_name}] no valid frames could be loaded from {folder_path}")
        return None, []

    return np.stack(frames, axis=0), frame_indices


def compute_flicker_rate(frames):
    """Temporal flicker rate: fraction of consecutive valid pairs where a cell's
    value toggles, aggregated per cell then summarized across cells.
    """
    valid = ~np.isnan(frames)
    both_valid = valid[:-1] & valid[1:]
    toggled = (frames[:-1] != frames[1:]) & both_valid

    valid_pair_count = both_valid.sum(axis=0)
    toggle_count = toggled.sum(axis=0)

    total_cells = valid_pair_count.size
    included_mask = valid_pair_count >= 10
    excluded_fraction = 1.0 - (included_mask.sum() / total_cells if total_cells else 0.0)

    if not np.any(included_mask):
        return {
            "flicker_mean": np.nan,
            "flicker_median": np.nan,
            "flicker_p90": np.nan,
            "flicker_excluded_cell_fraction": excluded_fraction,
        }

    with np.errstate(invalid="ignore", divide="ignore"):
        rate = toggle_count[included_mask] / valid_pair_count[included_mask]

    return {
        "flicker_mean": float(np.mean(rate)),
        "flicker_median": float(np.median(rate)),
        "flicker_p90": float(np.percentile(rate, 90)),
        "flicker_excluded_cell_fraction": float(excluded_fraction),
    }


def compute_isolation(frames):
    """Fraction of non-traversable cells that are isolated (connected-component
    size 1, 4-connectivity), averaged over frames that contain at least one
    non-traversable cell.
    """
    structure = np.array([[0, 1, 0], [1, 1, 1], [0, 1, 0]])
    n_frames = frames.shape[0]
    per_frame_isolated_fraction = []
    zero_obstacle_frames = 0

    for i in range(n_frames):
        obstacle_mask = frames[i] == 1.0
        n_obstacle = int(obstacle_mask.sum())
        if n_obstacle == 0:
            zero_obstacle_frames += 1
            continue
        labeled, num_components = ndimage.label(obstacle_mask, structure=structure)
        if num_components == 0:
            continue
        component_sizes = ndimage.sum(obstacle_mask, labeled, index=np.arange(1, num_components + 1))
        n_isolated_cells = int(np.sum(component_sizes == 1))
        per_frame_isolated_fraction.append(n_isolated_cells / n_obstacle)

    return {
        "isolation_mean": float(np.mean(per_frame_isolated_fraction)) if per_frame_isolated_fraction else np.nan,
        "zero_obstacle_frame_fraction": zero_obstacle_frames / n_frames if n_frames else np.nan,
    }


def compute_class_distribution(frames):
    """Per-frame class percentages (traversable / non-traversable / unknown),
    summarized as mean and std across frames.
    """
    n_frames, r_bins, t_bins = frames.shape
    total_cells = r_bins * t_bins

    unknown_pct = np.isnan(frames).sum(axis=(1, 2)) / total_cells * 100.0
    trav_pct = (frames == 0.0).sum(axis=(1, 2)) / total_cells * 100.0
    nontrav_pct = (frames == 1.0).sum(axis=(1, 2)) / total_cells * 100.0

    return {
        "trav_pct_mean": float(np.mean(trav_pct)),
        "trav_pct_std": float(np.std(trav_pct)),
        "nontrav_pct_mean": float(np.mean(nontrav_pct)),
        "nontrav_pct_std": float(np.std(nontrav_pct)),
        "unknown_pct_mean": float(np.mean(unknown_pct)),
        "unknown_pct_std": float(np.std(unknown_pct)),
    }


def compute_cross_method_agreement(method_a, frames_a, indices_a, method_b, frames_b, indices_b):
    """Overall agreement % and obstacle IoU between two methods, over frame
    indices present in both. Returns None (with a warning) if shapes differ
    or there is no overlap.
    """
    if frames_a.shape[1:] != frames_b.shape[1:]:
        print(
            f"WARNING: grid shape mismatch between {method_a} {frames_a.shape[1:]} and "
            f"{method_b} {frames_b.shape[1:]}, skipping this pair"
        )
        return None

    pos_a = {idx: pos for pos, idx in enumerate(indices_a)}
    pos_b = {idx: pos for pos, idx in enumerate(indices_b)}
    common_indices = sorted(set(indices_a) & set(indices_b))

    if not common_indices:
        print(f"WARNING: no overlapping frame indices between {method_a} and {method_b}, skipping this pair")
        return None

    agreements = []
    ious = []
    for idx in common_indices:
        fa = frames_a[pos_a[idx]]
        fb = frames_b[pos_b[idx]]
        both_valid = ~np.isnan(fa) & ~np.isnan(fb)
        n_valid = int(both_valid.sum())
        if n_valid == 0:
            continue
        equal = (fa == fb) & both_valid
        agreements.append(equal.sum() / n_valid)

        obs_a = (fa == 1.0) & both_valid
        obs_b = (fb == 1.0) & both_valid
        union = obs_a | obs_b
        union_size = int(union.sum())
        if union_size == 0:
            continue
        intersection = obs_a & obs_b
        ious.append(intersection.sum() / union_size)

    return {
        "method_a": method_a,
        "method_b": method_b,
        "num_matched_frames": len(common_indices),
        "mean_agreement_pct": float(np.mean(agreements) * 100.0) if agreements else np.nan,
        "mean_obstacle_iou": float(np.mean(ious)) if ious else np.nan,
    }


def write_plots(summary_df, output_path):
    method_names = summary_df["method"].tolist()
    colors = [METHOD_COLORS[i % len(METHOD_COLORS)] for i in range(len(method_names))]

    def style_axes(ax):
        ax.spines["top"].set_visible(False)
        ax.spines["right"].set_visible(False)
        ax.spines["left"].set_visible(False)
        ax.spines["bottom"].set_color(GRID_HAIRLINE)
        ax.tick_params(colors=INK_MUTED)
        ax.yaxis.grid(True, color=GRID_HAIRLINE, linewidth=1)
        ax.set_axisbelow(True)
        ax.xaxis.label.set_color(INK_PRIMARY)
        ax.yaxis.label.set_color(INK_PRIMARY)
        ax.title.set_color(INK_PRIMARY)

    # 1. Flicker rate by method (mean, with median/p90 as secondary markers).
    fig, ax = plt.subplots(figsize=(6, 4.5))
    bars = ax.bar(method_names, summary_df["flicker_mean"], color=colors, width=0.6)
    ax.scatter(method_names, summary_df["flicker_median"], color=INK_PRIMARY, marker="_", s=200, zorder=3, label="median")
    ax.scatter(method_names, summary_df["flicker_p90"], color=INK_PRIMARY, marker="x", s=60, zorder=3, label="p90")
    for bar, value in zip(bars, summary_df["flicker_mean"]):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(), f"{value:.3f}",
                ha="center", va="bottom", fontsize=9, color=INK_PRIMARY)
    ax.set_ylabel("Temporal flicker rate")
    ax.set_title("Temporal flicker rate by method")
    ax.legend(frameon=False, labelcolor=INK_PRIMARY)
    style_axes(ax)
    plt.xticks(rotation=15, ha="right")
    fig.tight_layout()
    fig.savefig(os.path.join(output_path, "flicker_rate_by_method.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)

    # 2. Isolation fraction by method.
    fig, ax = plt.subplots(figsize=(6, 4.5))
    bars = ax.bar(method_names, summary_df["isolation_mean"], color=colors, width=0.6)
    for bar, value in zip(bars, summary_df["isolation_mean"]):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(), f"{value:.3f}",
                ha="center", va="bottom", fontsize=9, color=INK_PRIMARY)
    ax.set_ylabel("Mean isolated-obstacle-cell fraction")
    ax.set_title("Spatial isolation (salt-and-pepper) by method")
    style_axes(ax)
    plt.xticks(rotation=15, ha="right")
    fig.tight_layout()
    fig.savefig(os.path.join(output_path, "isolation_fraction_by_method.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)

    # 3. Class distribution by method (stacked bar of mean %).
    fig, ax = plt.subplots(figsize=(6, 4.5))
    trav = summary_df["trav_pct_mean"].to_numpy()
    nontrav = summary_df["nontrav_pct_mean"].to_numpy()
    unknown = summary_df["unknown_pct_mean"].to_numpy()
    ax.bar(method_names, trav, color=COLOR_AQUA, label="traversable", width=0.6)
    ax.bar(method_names, nontrav, bottom=trav, color=COLOR_YELLOW, label="non-traversable", width=0.6)
    ax.bar(method_names, unknown, bottom=trav + nontrav, color=GRID_HAIRLINE, label="unknown", width=0.6)
    ax.set_ylabel("Mean class share (%)")
    ax.set_title("Class distribution by method")
    ax.legend(frameon=False, labelcolor=INK_PRIMARY, loc="upper right", bbox_to_anchor=(1.35, 1.0))
    style_axes(ax)
    plt.xticks(rotation=15, ha="right")
    fig.tight_layout()
    fig.savefig(os.path.join(output_path, "class_distribution_by_method.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)


def dataframe_to_markdown(df):
    """Minimal DataFrame -> GitHub-flavored markdown table (no external deps)."""
    def fmt(value):
        if isinstance(value, float):
            return f"{value:.4f}" if not np.isnan(value) else "NaN"
        return str(value)

    headers = list(df.columns)
    header_line = "| " + " | ".join(headers) + " |"
    sep_line = "| " + " | ".join("---" for _ in headers) + " |"
    row_lines = []
    for _, row in df.iterrows():
        row_lines.append("| " + " | ".join(fmt(row[h]) for h in headers) + " |")
    return "\n".join([header_line, sep_line] + row_lines)


def write_report(summary_df, agreement_df, output_path, frame_counts):
    summary_csv_path = os.path.join(output_path, "summary.csv")
    agreement_csv_path = os.path.join(output_path, "cross_method_agreement.csv")
    summary_df.to_csv(summary_csv_path, index=False)
    agreement_df.to_csv(agreement_csv_path, index=False)

    lowest_flicker = None
    lowest_isolation = None
    if not summary_df["flicker_mean"].isna().all():
        lowest_flicker = summary_df.loc[summary_df["flicker_mean"].idxmin(), "method"]
    if not summary_df["isolation_mean"].isna().all():
        lowest_isolation = summary_df.loc[summary_df["isolation_mean"].idxmin(), "method"]

    lines = []
    lines.append("# Traversability Experiment Analysis\n")
    lines.append(f"Generated: {datetime.now().isoformat(timespec='seconds')}\n")
    lines.append("## Frame counts\n")
    for method, count in frame_counts.items():
        lines.append(f"- {method}: {count} frames loaded")
    lines.append("")

    lines.append("## Per-method summary\n")
    lines.append(dataframe_to_markdown(summary_df))
    lines.append("")

    lines.append("## Cross-method agreement\n")
    lines.append(dataframe_to_markdown(agreement_df))
    lines.append("")

    lines.append("## Findings\n")
    if lowest_flicker is not None and lowest_isolation is not None:
        lines.append(
            f"**{lowest_flicker}** had the lowest mean temporal flicker rate, and "
            f"**{lowest_isolation}** had the lowest mean spatial isolation (salt-and-pepper) "
            "fraction among the three methods on this data."
        )
    lines.append(
        "\nThese flicker-rate and isolation metrics are **noise/stability proxies only** — "
        "they measure how much a method's output changes frame-to-frame and how "
        "speckled its obstacle cells are. There is **no ground truth** in this dataset, "
        "so a lower score here does not imply a more *correct* or more *accurate* "
        "traversability classification, only a more temporally and spatially stable one. "
        "The cross-method agreement figures likewise describe how much the methods agree "
        "with each other, not which (if any) is right."
    )
    lines.append("")

    with open(os.path.join(output_path, "report.md"), "w") as f:
        f.write("\n".join(lines))

    return lowest_flicker, lowest_isolation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", default="output", help="Directory containing one subfolder per method")
    parser.add_argument("--analysis-dir", default="analysis", help="Directory under which the timestamped report folder is created")
    args = parser.parse_args()

    method_folders = discover_method_folders(args.output_dir)
    if not method_folders:
        print(f"No method folders found under '{args.output_dir}'. Nothing to do.")
        sys.exit(1)

    print(f"Discovered {len(method_folders)} method folder(s): {', '.join(method_folders)}")

    loaded = {}
    frame_counts = {}
    for method_name, folder_path in method_folders.items():
        frames, indices = load_method_frames(method_name, folder_path)
        if frames is None:
            frame_counts[method_name] = 0
            continue
        loaded[method_name] = (frames, indices)
        frame_counts[method_name] = frames.shape[0]
        print(f"  [{method_name}] loaded {frames.shape[0]} frames, grid shape {frames.shape[1:]}")

    summary_rows = []
    for method_name, (frames, indices) in loaded.items():
        row = {"method": method_name, "num_frames": frames.shape[0]}
        row.update(compute_flicker_rate(frames))
        row.update(compute_isolation(frames))
        row.update(compute_class_distribution(frames))
        summary_rows.append(row)

    summary_df = pd.DataFrame(summary_rows)

    agreement_rows = []
    method_names = list(loaded.keys())
    for i in range(len(method_names)):
        for j in range(i + 1, len(method_names)):
            method_a = method_names[i]
            method_b = method_names[j]
            frames_a, indices_a = loaded[method_a]
            frames_b, indices_b = loaded[method_b]
            result = compute_cross_method_agreement(method_a, frames_a, indices_a, method_b, frames_b, indices_b)
            if result is not None:
                agreement_rows.append(result)

    agreement_df = pd.DataFrame(agreement_rows)

    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    output_path = os.path.join(args.analysis_dir, f"outlier-analysis-{timestamp}")
    os.makedirs(output_path, exist_ok=True)

    if not summary_df.empty:
        write_plots(summary_df, output_path)
    else:
        print("WARNING: no methods produced usable data, skipping plots")

    lowest_flicker, lowest_isolation = write_report(summary_df, agreement_df, output_path, frame_counts)

    print(f"\nLowest flicker rate: {lowest_flicker}")
    print(f"Lowest isolation fraction: {lowest_isolation}")
    print(f"\nReport written to: {output_path}")


if __name__ == "__main__":
    main()
