"""CPU reference for src/stages/traversability.cu

Builds a polar (r, theta) occupancy/traversability grid from a polarized
point cloud: height-map binning, slope, roughness, step-height, a
weighted danger score, a ray-cast visibility mask, and the final
trav_grid (NaN = unknown, 0 = observed free, 1 = nontraversable).

Kernel-by-kernel, this mirrors the CUDA source 1:1 (including the
reflect-padding and ray-cast edge cases) rather than being vectorized,
so it can be read side-by-side with the .cu file for verification.
"""
import math

import numpy as np

HALF = 2                        # 5x5 window half-width (step_height)
N_CRIT = (2 * HALF + 1) ** 2 - 1  # 24 neighbours


def reflect_index(idx: int, size: int) -> int:
    """OpenCV-style BORDER_REFLECT (no edge repeat), used for 3x3/5x5 padding."""
    if size <= 1:
        return 0
    if idx < 0:
        return -idx - 1
    if idx >= size:
        return 2 * size - idx - 1
    return idx


def compute_bin_counts(r_min, r_max, dr, theta_min, theta_max, dtheta):
    """Mirrors the host-side arange(start, stop, step) edge/bin counting:
    edge count = iterations while x < stop; bins = edges - 1."""
    r_bins = 0
    x = r_min
    while x < r_max + dr:
        r_bins += 1
        x += dr
    theta_bins = 0
    x = theta_min
    while x < theta_max + dtheta:
        theta_bins += 1
        x += dtheta
    r_bins -= 1
    theta_bins -= 1
    return max(r_bins, 0), max(theta_bins, 0)


def build_height_map(polar_points, r_bins, theta_bins, r_min, theta_min, dr, dtheta):
    sentinel = -np.finfo(np.float32).max
    height_map = np.full((r_bins, theta_bins), sentinel, dtype=np.float32)

    inv_dr, inv_dtheta = 1.0 / dr, 1.0 / dtheta
    for r, theta, z in polar_points:
        r_bin = math.floor((r - r_min) * inv_dr)
        theta_bin = math.floor((theta - theta_min) * inv_dtheta)
        if 0 <= r_bin < r_bins and 0 <= theta_bin < theta_bins:
            if z > height_map[r_bin, theta_bin]:
                height_map[r_bin, theta_bin] = z
    return height_map, sentinel


def fill_terrain(height_map, sentinel):
    valid_mask = height_map != sentinel
    terrain = np.where(valid_mask, height_map, np.float32(-0.3)).astype(np.float32)
    return terrain, valid_mask


def gradient_slope(terrain, r_min, dr, dtheta, scrit, inf_val=math.inf):
    nr, nc = terrain.shape
    slope = np.zeros((nr, nc), dtype=np.float32)
    for i in range(nr):
        for j in range(nc):
            dzdx = 0.0
            if nr > 1:
                if i == 0:
                    dzdx = (terrain[i + 1, j] - terrain[i, j]) / dr
                elif i == nr - 1:
                    dzdx = (terrain[i, j] - terrain[i - 1, j]) / dr
                else:
                    dzdx = (terrain[i + 1, j] - terrain[i - 1, j]) / (2.0 * dr)

            dzdy = 0.0
            if nc > 1:
                if j == 0:
                    dzdy = (terrain[i, j + 1] - terrain[i, j]) / dtheta
                elif j == nc - 1:
                    dzdy = (terrain[i, j] - terrain[i, j - 1]) / dtheta
                else:
                    dzdy = (terrain[i, j + 1] - terrain[i, j - 1]) / (2.0 * dtheta)

            r_centre = r_min + (i + 0.5) * dr
            dzdy_metric = dzdy / r_centre  # arc-length correction for theta direction
            s = math.atan(math.sqrt(dzdx * dzdx + dzdy_metric * dzdy_metric))
            slope[i, j] = inf_val if s > scrit else s
    return slope


def roughness(terrain, rcrit_m, inf_val=math.inf):
    nr, nc = terrain.shape
    rough = np.zeros((nr, nc), dtype=np.float32)
    for i in range(nr):
        for j in range(nc):
            s = 0.0
            s2 = 0.0
            for di in (-1, 0, 1):
                ri = reflect_index(i + di, nr)
                for dj in (-1, 0, 1):
                    cj = reflect_index(j + dj, nc)
                    v = float(terrain[ri, cj])
                    s += v
                    s2 += v * v
            mean = s / 9.0
            var = max(0.0, s2 / 9.0 - mean * mean)
            rv = math.sqrt(var)
            rough[i, j] = inf_val if rv > rcrit_m else rv
    return rough


def step_height(terrain, r_min, theta_min, dr, dtheta, hcrit_m, scrit, inf_val=math.inf):
    nr, nc = terrain.shape
    sh_grid = np.zeros((nr, nc), dtype=np.float32)
    for i in range(nr):
        for j in range(nc):
            r_c = r_min + (i + 0.5) * dr
            t_c = theta_min + (j + 0.5) * dtheta
            x0, y0 = r_c * math.cos(t_c), r_c * math.sin(t_c)
            z0 = float(terrain[i, j])

            st_count = 0
            h_max = 0.0
            for di in range(-HALF, HALF + 1):
                for dj in range(-HALF, HALF + 1):
                    if di == 0 and dj == 0:
                        continue
                    ri = reflect_index(i + di, nr)
                    cj = reflect_index(j + dj, nc)

                    r_n = r_min + (ri + 0.5) * dr
                    t_n = theta_min + (cj + 0.5) * dtheta
                    xn, yn = r_n * math.cos(t_n), r_n * math.sin(t_n)

                    dz = abs(z0 - float(terrain[ri, cj]))
                    dxy = math.hypot(x0 - xn, y0 - yn)
                    if dxy == 0.0:
                        continue

                    pair_slope = math.atan2(dz, dxy)
                    if dz > hcrit_m and pair_slope > scrit:
                        st_count += 1
                        h_max = max(h_max, dz)

            scaled = h_max * st_count / N_CRIT
            sh = min(h_max, scaled)
            sh_grid[i, j] = inf_val if sh > hcrit_m else sh
    return sh_grid


def danger_nontraversable(slope, roughness_grid, step_height_grid, valid_mask,
                           scrit, rcrit_m, hcrit_m, danger_threshold):
    danger = (0.3 * slope / scrit +
              0.3 * roughness_grid / rcrit_m +
              0.4 * step_height_grid / hcrit_m)
    # invalid cells are never flagged nontraversable so they don't block ray-cast
    return (danger > danger_threshold) & valid_mask


def ray_cast(nontraversable):
    """Per angular column: scan outward in r, mark cells before the first
    obstacle as observed-free. If no obstacle exists, nothing is marked
    (range(-1) below is empty, matching the C `for (i=0;i<closest;++i)`
    when closest stays -1)."""
    nr, nc = nontraversable.shape
    observed = np.zeros((nr, nc), dtype=bool)
    for j in range(nc):
        closest = -1
        for i in range(nr):
            if nontraversable[i, j]:
                closest = i
                break
        for i in range(closest):
            observed[i, j] = True
    return observed


def assemble_trav_grid(observed, nontraversable):
    nr, nc = observed.shape
    grid = np.full((nr, nc), np.nan, dtype=np.float32)
    grid[observed & ~nontraversable] = 0.0
    grid[nontraversable] = 1.0  # nontraversable always wins
    return grid


def traversability(polar_points, cfg: dict):
    """
    polar_points: (N, 3) array [r, theta, z] (theta in radians)
    cfg: dict with keys matching TraversabilityConfig (see include/traversability/config.hpp):
        r_min_m, r_max_m, theta_min_deg, theta_max_deg,
        polar_grid_size_r_m, polar_grid_size_theta_deg,
        scrit_deg, rcrit_m, hcrit_m, danger_threshold
    returns: (trav_grid, height_map) both shaped (r_bins, theta_bins)
    """
    r_min = cfg['r_min_m']
    r_max = cfg['r_max_m']
    theta_min = math.radians(cfg['theta_min_deg'])
    theta_max = math.radians(cfg['theta_max_deg'])
    dr = cfg['polar_grid_size_r_m']
    dtheta = math.radians(cfg['polar_grid_size_theta_deg'])
    scrit = math.radians(cfg['scrit_deg'])
    rcrit_m = cfg['rcrit_m']
    hcrit_m = cfg['hcrit_m']
    danger_threshold = cfg['danger_threshold']

    r_bins, theta_bins = compute_bin_counts(r_min, r_max, dr, theta_min, theta_max, dtheta)
    if r_bins == 0 or theta_bins == 0:
        empty = np.zeros((0, 0), dtype=np.float32)
        return empty, empty

    height_map, sentinel = build_height_map(polar_points, r_bins, theta_bins, r_min, theta_min, dr, dtheta)
    terrain, valid_mask = fill_terrain(height_map, sentinel)
    slope = gradient_slope(terrain, r_min, dr, dtheta, scrit)
    rough = roughness(terrain, rcrit_m)
    sh = step_height(terrain, r_min, theta_min, dr, dtheta, hcrit_m, scrit)
    nontrav = danger_nontraversable(slope, rough, sh, valid_mask, scrit, rcrit_m, hcrit_m, danger_threshold)
    observed = ray_cast(nontrav)
    trav_grid = assemble_trav_grid(observed, nontrav)

    return trav_grid, terrain
