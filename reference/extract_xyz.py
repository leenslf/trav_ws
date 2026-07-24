"""CPU reference for src/stages/extract_xyz.cu

Compacts the raw ZED point cloud (float4 per pixel) down to the pixels whose
x, y, z are all finite. Order is preserved, matching the stable compaction
produced by the CUDA prefix-sum scatter.
"""
import numpy as np


def extract_xyz(raw_points: np.ndarray) -> np.ndarray:
    """
    raw_points: (N, 4) float32 array of [x, y, z, w] (w is unused / ZED padding)
    returns:    (K, 3) float32 array of points with finite x, y, z
    """
    xyz = raw_points[:, :3]
    valid = np.all(np.isfinite(xyz), axis=1)
    return xyz[valid].astype(np.float32)
