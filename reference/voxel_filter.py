"""CPU reference for src/stages/voxel_filter.cu

Downsamples a point cloud onto a regular voxel grid: each point is bucketed
by (ix, iy, iz), voxels with fewer than min_points are dropped, and
surviving voxels are replaced by their centre. Output order follows
ascending encoded key (matches the CUB radix sort used on-device).
"""
import numpy as np

OFFSET21 = 1 << 20          # bias so signed indices fit in 21 unsigned bits (+/-1,048,575 range)
MASK21 = (1 << 21) - 1


def encode_voxel(ix: np.ndarray, iy: np.ndarray, iz: np.ndarray) -> np.ndarray:
    ix = (ix.astype(np.int64) + OFFSET21).astype(np.uint64)
    iy = (iy.astype(np.int64) + OFFSET21).astype(np.uint64)
    iz = (iz.astype(np.int64) + OFFSET21).astype(np.uint64)
    return (iz << np.uint64(42)) | (iy << np.uint64(21)) | ix


def decode_voxel(key: np.ndarray):
    key = key.astype(np.uint64)
    ix = (key & np.uint64(MASK21)).astype(np.int64) - OFFSET21
    iy = ((key >> np.uint64(21)) & np.uint64(MASK21)).astype(np.int64) - OFFSET21
    iz = ((key >> np.uint64(42)) & np.uint64(MASK21)).astype(np.int64) - OFFSET21
    return ix, iy, iz


def voxel_filter(points: np.ndarray, vx: float, vy: float, vz: float, min_points: int) -> np.ndarray:
    """
    points: (N, 3) float32 array [x, y, z]
    returns: (K, 3) float32 array of surviving voxel centres
    """
    if points.shape[0] == 0:
        return np.empty((0, 3), dtype=np.float32)

    ix = np.floor(points[:, 0] / vx).astype(np.int64)
    iy = np.floor(points[:, 1] / vy).astype(np.int64)
    iz = np.floor(points[:, 2] / vz).astype(np.int64)

    keys = encode_voxel(ix, iy, iz)

    unique_keys, counts = np.unique(keys, return_counts=True)  # np.unique sorts ascending

    survivors = unique_keys[counts >= min_points]
    if survivors.size == 0:
        return np.empty((0, 3), dtype=np.float32)

    cix, ciy, ciz = decode_voxel(survivors)
    centers = np.stack([
        (cix.astype(np.float32) + 0.5) * vx,
        (ciy.astype(np.float32) + 0.5) * vy,
        (ciz.astype(np.float32) + 0.5) * vz,
    ], axis=1)
    return centers.astype(np.float32)
