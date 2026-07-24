"""CPU reference for src/stages/tilt_compensate.cu

Rotates points into a gravity-aligned, yaw-preserved frame: the camera's
full orientation quaternion has its yaw (rotation about Z) factored out
before building the rotation matrix, so pitch/roll tilt is compensated
while heading is left untouched.
"""
import numpy as np


def tilt_compensate(points: np.ndarray, qx: float, qy: float, qz: float, qw: float) -> np.ndarray:
    """
    points: (N, 3) float32 array [x, y, z]
    qx,qy,qz,qw: camera pose orientation quaternion
    returns: (N, 3) float32 array of rotated points
    """
    norm = np.sqrt(qx * qx + qy * qy + qz * qz + qw * qw)
    if norm > 1e-6:
        qx, qy, qz, qw = qx / norm, qy / norm, qz / norm, qw / norm

    # yaw-only quaternion, conjugated, then composed with the full pose
    yaw = np.arctan2(2.0 * (qw * qz + qx * qy), 1.0 - 2.0 * (qy * qy + qz * qz))
    sy, cy = np.sin(yaw * 0.5), np.cos(yaw * 0.5)

    pr_w = qw * cy + qz * sy
    pr_x = qx * cy - qy * sy
    pr_y = qx * sy + qy * cy
    pr_z = -qw * sy + qz * cy

    r00 = 1.0 - 2.0 * (pr_y * pr_y + pr_z * pr_z)
    r01 = 2.0 * (pr_x * pr_y - pr_w * pr_z)
    r02 = 2.0 * (pr_x * pr_z + pr_w * pr_y)
    r10 = 2.0 * (pr_x * pr_y + pr_w * pr_z)
    r11 = 1.0 - 2.0 * (pr_x * pr_x + pr_z * pr_z)
    r12 = 2.0 * (pr_y * pr_z - pr_w * pr_x)
    r20 = 2.0 * (pr_x * pr_z - pr_w * pr_y)
    r21 = 2.0 * (pr_y * pr_z + pr_w * pr_x)
    r22 = 1.0 - 2.0 * (pr_x * pr_x + pr_y * pr_y)

    R = np.array([[r00, r01, r02],
                  [r10, r11, r12],
                  [r20, r21, r22]], dtype=np.float64)

    return (points.astype(np.float64) @ R.T).astype(np.float32)
