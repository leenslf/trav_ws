#!/usr/bin/env python3
"""
Compare traversability polar-grid outputs across multiple polar_grid_size_theta_deg
experiments (same source recording, reprocessed once per theta value).

Usage:
    python scripts/analyze_theta.py --output-dir output --config config/config.yaml

Outputs (written to --results-dir, default analysis/<YYYYmmdd-HHMM>/):
    summary_coverage_hazard.csv   one row per theta folder (metrics 1 & 2)
    coverage_by_row.csv           NaN fraction by radial row index, per theta folder
    summary_agreement.csv         one row per non-reference theta folder (metric 3)
    coverage_vs_theta.png
    hazard_width_vs_theta.png
    over_under_marking_vs_theta.png
    README.txt                    reference-folder note + run summary
"""
import argparse
import re
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import yaml

THETA_DIR_RE = re.compile(r"^theta_([0-9]+(?:\.[0-9]+)?)$")
REQUIRED_GEOM_KEYS = ["r_min_m", "r_max_m", "polar_grid_size_r_m", "theta_min_deg", "theta_max_deg"]


# --------------------------------------------------------------------------
# Config / geometry
# --------------------------------------------------------------------------

def load_geometry(config_path: Path) -> dict:
    with open(config_path) as f:
        cfg = yaml.safe_load(f)

    print(f"[config] top-level keys in {config_path}: {sorted(cfg.keys())}")

    trav = cfg.get("traversability")
    if trav is None:
        raise SystemExit(f"config error: missing top-level key 'traversability' in {config_path}")
    print(f"[config] keys under 'traversability': {sorted(trav.keys())}")

    missing = [k for k in REQUIRED_GEOM_KEYS if k not in trav]
    if missing:
        raise SystemExit(
            f"config error: missing required key(s) under 'traversability' in {config_path}: "
            + ", ".join(missing)
        )
    geom = {k: trav[k] for k in REQUIRED_GEOM_KEYS}
    print(f"[config] using geometry: {geom}")
    return geom


# --------------------------------------------------------------------------
# Discovery / loading
# --------------------------------------------------------------------------

def discover_theta_folders(output_dir: Path) -> dict:
    """{theta_value: folder_path}, sorted ascending by theta."""
    folders = {}
    for p in sorted(output_dir.iterdir()):
        if not p.is_dir():
            continue
        m = THETA_DIR_RE.match(p.name)
        if m:
            folders[float(m.group(1))] = p
    if not folders:
        raise SystemExit(f"no theta_* folders found under {output_dir}")
    return dict(sorted(folders.items()))


def list_frames(folder: Path) -> dict:
    """{filename: path} for frame_*.csv files, sorted."""
    return {p.name: p for p in sorted(folder.glob("frame_*.csv"))}


def load_grid(path: Path) -> np.ndarray:
    return pd.read_csv(path, header=None).to_numpy(dtype=float)


def load_all_grids(frame_files: dict, folder: Path) -> dict:
    """Load every frame in a folder and verify a constant grid shape within it."""
    grids = {fname: load_grid(path) for fname, path in frame_files.items()}
    shapes = {g.shape for g in grids.values()}
    if len(shapes) > 1:
        raise SystemExit(f"inconsistent grid shapes within {folder}: found shapes {shapes}")
    return grids


# --------------------------------------------------------------------------
# Metric 1 & 2: per-folder coverage and hazard angular width
# --------------------------------------------------------------------------

def run_lengths(bool_row: np.ndarray) -> np.ndarray:
    """Contiguous run lengths (in cells) of True values in a 1D boolean array."""
    if bool_row.size == 0:
        return np.array([], dtype=int)
    padded = np.concatenate(([False], bool_row, [False]))
    edges = np.diff(padded.astype(int))
    starts = np.flatnonzero(edges == 1)
    ends = np.flatnonzero(edges == -1)
    return ends - starts


def analyze_folder(theta_val: float, grids: dict) -> dict:
    """Coverage (metric 1) and hazard angular width (metric 2) for one theta folder."""
    coverage_fracs = []
    hazard_widths_deg = []
    n_rows = next(iter(grids.values())).shape[0]
    row_nan_sum = np.zeros(n_rows)

    for grid in grids.values():
        nan_mask = np.isnan(grid)
        coverage_fracs.append(nan_mask.mean())
        row_nan_sum += nan_mask.mean(axis=1)

        hazard_mask = grid == 1
        for row in hazard_mask:
            lengths = run_lengths(row)
            if lengths.size:
                hazard_widths_deg.extend(lengths * theta_val)

    n_frames = len(grids)
    return {
        "n_frames": n_frames,
        "coverage_fracs": np.array(coverage_fracs),
        "row_nan_mean": row_nan_sum / n_frames,
        "hazard_widths_deg": np.array(hazard_widths_deg),
    }


# --------------------------------------------------------------------------
# Metric 3: cross-theta agreement against the finest (smallest-theta) grid
# --------------------------------------------------------------------------

def ref_cell_theta_centers(n_cols_ref: int, geom: dict, theta_ref: float) -> np.ndarray:
    col_idx = np.arange(n_cols_ref)
    return geom["theta_min_deg"] + (col_idx + 0.5) * theta_ref


def map_theta_to_cmp_col(theta_centers_deg: np.ndarray, geom: dict, theta_cmp: float, n_cols_cmp: int):
    cols = np.floor((theta_centers_deg - geom["theta_min_deg"]) / theta_cmp).astype(int)
    valid = (cols >= 0) & (cols < n_cols_cmp)
    return cols, valid


def agreement_counts(ref_grid: np.ndarray, cmp_grid: np.ndarray, cmp_cols: np.ndarray, valid_cols: np.ndarray) -> dict:
    n_rows, n_cols_ref = ref_grid.shape
    cmp_mapped = np.full((n_rows, n_cols_ref), np.nan)
    if valid_cols.any():
        cmp_mapped[:, valid_cols] = cmp_grid[:, cmp_cols[valid_cols]]

    ref_known = ~np.isnan(ref_grid)
    comparable = ref_known & valid_cols[np.newaxis, :]
    out_of_bounds = ref_known & ~valid_cols[np.newaxis, :]
    cmp_nan = np.isnan(cmp_mapped)

    agree = comparable & ~cmp_nan & (ref_grid == cmp_mapped)
    over_marked = comparable & ~cmp_nan & (ref_grid == 0) & (cmp_mapped == 1)
    under_marked = comparable & ~cmp_nan & (ref_grid == 1) & (cmp_mapped == 0)
    coverage_loss = comparable & cmp_nan

    return {
        "n_comparable": int(comparable.sum()),
        "n_out_of_bounds": int(out_of_bounds.sum()),
        "agree": int(agree.sum()),
        "over_marked": int(over_marked.sum()),
        "under_marked": int(under_marked.sum()),
        "coverage_loss": int(coverage_loss.sum()),
    }


def print_worked_example(ref_theta, ref_grids, cmp_theta, cmp_grids, geom):
    """Manually verifiable sanity check for the metric-3 index math."""
    common = sorted(set(ref_grids) & set(cmp_grids))
    if not common:
        print("[metric3] worked example: no common frame between reference and comparison folder, skipping")
        return
    fname = common[0]
    ref_grid = ref_grids[fname]
    cmp_grid = cmp_grids[fname]
    n_cols_cmp = cmp_grid.shape[1]

    theta_centers = ref_cell_theta_centers(ref_grid.shape[1], geom, ref_theta)
    cmp_cols, valid_cols = map_theta_to_cmp_col(theta_centers, geom, cmp_theta, n_cols_cmp)

    row = col = None
    for r in range(ref_grid.shape[0]):
        for c in range(ref_grid.shape[1]):
            if not np.isnan(ref_grid[r, c]):
                row, col = r, c
                break
        if row is not None:
            break
    if row is None:
        print("[metric3] worked example: reference frame has no non-NaN cells, skipping")
        return

    r_center = geom["r_min_m"] + (row + 0.5) * geom["polar_grid_size_r_m"]
    theta_center = theta_centers[col]
    cmp_col = cmp_cols[col]
    is_valid = valid_cols[col]
    ref_val = ref_grid[row, col]
    cmp_val = cmp_grid[row, cmp_col] if is_valid else None

    print(f"[metric3] worked example (frame={fname}, reference theta_{ref_theta} -> comparison theta_{cmp_theta}):")
    print(f"    reference cell: row={row}, col={col}, value={ref_val}")
    print(f"    physical center: r={r_center:.3f} m, theta={theta_center:.3f} deg")
    if is_valid:
        print(f"    maps to comparison cell: row={row}, col={cmp_col} (of {n_cols_cmp}), value={cmp_val}")
        if np.isnan(cmp_val):
            label = "coverage-loss"
        elif ref_val == cmp_val:
            label = "agree"
        elif ref_val == 0 and cmp_val == 1:
            label = "over-marked"
        elif ref_val == 1 and cmp_val == 0:
            label = "under-marked"
        else:
            label = "?"
        print(f"    classification: {label}")
    else:
        print(f"    mapped column {cmp_col} is out of bounds for a grid with {n_cols_cmp} columns -> skipped")
    print()


def compute_agreement(theta_folders: dict, grids_by_theta: dict, geom: dict) -> tuple:
    ref_theta = min(theta_folders)
    ref_grids = grids_by_theta[ref_theta]
    n_cols_ref = next(iter(ref_grids.values())).shape[1]
    theta_centers = ref_cell_theta_centers(n_cols_ref, geom, ref_theta)

    print(
        f"[metric3] reference folder: theta_{ref_theta} (smallest theta available) — "
        f"used as a proxy for fine angular detail, NOT literal ground truth."
    )

    rows = []
    printed_example = False
    for theta_val, cmp_grids in grids_by_theta.items():
        if theta_val == ref_theta:
            continue

        if not printed_example:
            print_worked_example(ref_theta, ref_grids, theta_val, cmp_grids, geom)
            printed_example = True

        n_cols_cmp = next(iter(cmp_grids.values())).shape[1]
        cmp_cols, valid_cols = map_theta_to_cmp_col(theta_centers, geom, theta_val, n_cols_cmp)

        common = sorted(set(ref_grids) & set(cmp_grids))
        missing = (set(ref_grids) | set(cmp_grids)) - set(common)
        if missing:
            print(f"[metric3] theta_{theta_val}: {len(missing)} frame(s) skipped (not present in both folders)")

        per_frame_rates = []
        oob_fracs = []
        zero_comparable_frames = 0
        for fname in common:
            counts = agreement_counts(ref_grids[fname], cmp_grids[fname], cmp_cols, valid_cols)
            n_comp = counts["n_comparable"]
            n_known_ref = n_comp + counts["n_out_of_bounds"]
            if n_comp == 0:
                zero_comparable_frames += 1
                continue
            per_frame_rates.append({
                "agree": counts["agree"] / n_comp,
                "over_marked": counts["over_marked"] / n_comp,
                "under_marked": counts["under_marked"] / n_comp,
                "coverage_loss": counts["coverage_loss"] / n_comp,
            })
            if n_known_ref > 0:
                oob_fracs.append(counts["n_out_of_bounds"] / n_known_ref)

        if zero_comparable_frames:
            print(f"[metric3] theta_{theta_val}: {zero_comparable_frames} matched frame(s) had zero comparable cells")

        if per_frame_rates:
            rates_df = pd.DataFrame(per_frame_rates)
            rows.append({
                "theta_deg": theta_val,
                "reference_theta_deg": ref_theta,
                "n_frames_matched": len(per_frame_rates),
                "n_frames_skipped": len(missing) + zero_comparable_frames,
                "agree_rate": rates_df["agree"].mean(),
                "over_marked_rate": rates_df["over_marked"].mean(),
                "under_marked_rate": rates_df["under_marked"].mean(),
                "coverage_loss_rate": rates_df["coverage_loss"].mean(),
                "ref_cells_out_of_bounds_frac": np.mean(oob_fracs) if oob_fracs else 0.0,
            })
        else:
            print(f"[metric3] theta_{theta_val}: no comparable frames, omitted from agreement summary")

    return ref_theta, pd.DataFrame(rows)


# --------------------------------------------------------------------------
# Plots
# --------------------------------------------------------------------------

def make_plots(summary_df: pd.DataFrame, agreement_df: pd.DataFrame, ref_theta: float, results_dir: Path):
    summary_df = summary_df.sort_values("theta_deg")

    fig, ax = plt.subplots(figsize=(7, 5))
    ax.errorbar(
        summary_df["theta_deg"], summary_df["coverage_nan_frac_mean"],
        yerr=summary_df["coverage_nan_frac_std"], marker="o", capsize=4,
    )
    ax.set_xlabel("polar_grid_size_theta_deg (deg)")
    ax.set_ylabel("Mean NaN fraction (coverage)")
    ax.set_title("Grid Coverage vs Angular Bin Width\n(error bars: std across frames)")
    fig.tight_layout()
    fig.savefig(results_dir / "coverage_vs_theta.png", dpi=150)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(7, 5))
    ax.plot(summary_df["theta_deg"], summary_df["hazard_width_deg_median"], marker="o", label="median")
    ax.plot(summary_df["theta_deg"], summary_df["hazard_width_deg_p90"], marker="s", label="p90")
    ax.set_xlabel("polar_grid_size_theta_deg (deg)")
    ax.set_ylabel("Hazard run angular width (deg)")
    ax.set_title("Hazard Angular Width vs Angular Bin Width")
    ax.legend()
    fig.tight_layout()
    fig.savefig(results_dir / "hazard_width_vs_theta.png", dpi=150)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(7, 5))
    if not agreement_df.empty:
        adf = agreement_df.sort_values("theta_deg")
        ax.plot(adf["theta_deg"], adf["over_marked_rate"], marker="o", label="over-marked rate")
        ax.plot(adf["theta_deg"], adf["under_marked_rate"], marker="s", label="under-marked rate")
    ax.set_xlabel("polar_grid_size_theta_deg (deg)")
    ax.set_ylabel("Rate (fraction of comparable reference cells)")
    ax.set_title(f"Over/Under-Marking Rate vs Angular Bin Width\n(reference: theta_{ref_theta}, a proxy not ground truth)")
    ax.legend()
    fig.tight_layout()
    fig.savefig(results_dir / "over_under_marking_vs_theta.png", dpi=150)
    plt.close(fig)


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output-dir", type=Path, default=Path("output"))
    parser.add_argument("--config", type=Path, default=Path("config/config.yaml"))
    parser.add_argument("--results-dir", type=Path, default=None)
    args = parser.parse_args()

    results_dir = args.results_dir or Path("analysis") / datetime.now().strftime("%Y%m%d-%H%M")
    results_dir.mkdir(parents=True, exist_ok=True)

    geom = load_geometry(args.config)
    theta_folders = discover_theta_folders(args.output_dir)
    print(f"[discover] found theta folders: {list(theta_folders.keys())}")

    grids_by_theta = {}
    frame_counts = {}
    for theta_val, folder in theta_folders.items():
        frame_files = list_frames(folder)
        if not frame_files:
            raise SystemExit(f"no frame_*.csv files found in {folder}")
        grids_by_theta[theta_val] = load_all_grids(frame_files, folder)
        frame_counts[theta_val] = len(frame_files)
        print(f"[load] theta_{theta_val}: {len(frame_files)} frames, shape={next(iter(grids_by_theta[theta_val].values())).shape}")

    # --- Metrics 1 & 2 ---
    summary_rows = []
    row_breakdown_rows = []
    for theta_val, grids in grids_by_theta.items():
        m = analyze_folder(theta_val, grids)
        cov = m["coverage_fracs"]
        haz = m["hazard_widths_deg"]
        summary_rows.append({
            "theta_deg": theta_val,
            "n_frames": m["n_frames"],
            "coverage_nan_frac_mean": cov.mean(),
            "coverage_nan_frac_median": np.median(cov),
            "coverage_nan_frac_std": cov.std(),
            "n_hazard_runs": haz.size,
            "hazard_width_deg_mean": haz.mean() if haz.size else np.nan,
            "hazard_width_deg_median": np.median(haz) if haz.size else np.nan,
            "hazard_width_deg_p90": np.percentile(haz, 90) if haz.size else np.nan,
            "hazard_width_deg_max": haz.max() if haz.size else np.nan,
        })
        for row_i, val in enumerate(m["row_nan_mean"]):
            row_breakdown_rows.append({"theta_deg": theta_val, "row_index": row_i, "nan_frac_mean": val})

    summary_df = pd.DataFrame(summary_rows).sort_values("theta_deg")
    row_breakdown_df = pd.DataFrame(row_breakdown_rows)
    summary_df.to_csv(results_dir / "summary_coverage_hazard.csv", index=False)
    row_breakdown_df.to_csv(results_dir / "coverage_by_row.csv", index=False)
    print(f"[write] {results_dir / 'summary_coverage_hazard.csv'}")
    print(f"[write] {results_dir / 'coverage_by_row.csv'}")

    # --- Metric 3 ---
    ref_theta, agreement_df = compute_agreement(theta_folders, grids_by_theta, geom)
    agreement_df.to_csv(results_dir / "summary_agreement.csv", index=False)
    print(f"[write] {results_dir / 'summary_agreement.csv'}")

    # --- Plots ---
    make_plots(summary_df, agreement_df, ref_theta, results_dir)
    print(f"[write] {results_dir / 'coverage_vs_theta.png'}")
    print(f"[write] {results_dir / 'hazard_width_vs_theta.png'}")
    print(f"[write] {results_dir / 'over_under_marking_vs_theta.png'}")

    readme = (
        f"theta analysis run: {datetime.now().isoformat(timespec='seconds')}\n"
        f"output dir: {args.output_dir}\n"
        f"config: {args.config}\n"
        f"geometry used: {geom}\n"
        f"theta folders analyzed: {list(theta_folders.keys())}\n"
        f"frame counts per folder: {frame_counts}\n\n"
        f"Metric 3 (cross-theta agreement) reference folder: theta_{ref_theta}\n"
        f"  -> chosen as the smallest available theta (finest angular bins).\n"
        f"  -> This is a proxy for 'fine angular detail', NOT literal ground truth.\n"
    )
    (results_dir / "README.txt").write_text(readme)
    print(f"[write] {results_dir / 'README.txt'}")

    print(f"\nDone. All outputs written to {results_dir}")


if __name__ == "__main__":
    main()
