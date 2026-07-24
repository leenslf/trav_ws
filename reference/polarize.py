"""CPU reference for src/stages/polarize.cu

Filters points to a height band and minimum range, then converts the
survivors from Cartesian [x, y, z] to polar [r, theta, z]. Order is
preserved, matching the stable compaction produced by the CUDA
prefix-sum scatter.
"""
import numpy as np


def polarize(points: np.ndarray, z_threshold: float, min_range: float) -> np.ndarray:
    """
    points: (N, 3) float32 array [x, y, z]
    returns: (K, 3) float32 array [r, theta, z] where |z| < z_threshold and r > min_range
    """
    x, y, z = points[:, 0], points[:, 1], points[:, 2]
    r = np.sqrt(x * x + y * y)
    valid = (np.abs(z) < z_threshold) & (r > min_range)

    theta = np.arctan2(y, x)
    out = np.stack([r, theta, z], axis=1)
    return out[valid].astype(np.float32)
