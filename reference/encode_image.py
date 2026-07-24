"""CPU reference for src/stages/encode_image.cpp

Not CUDA in the original (already runs on the CPU via OpenCV), reproduced
here in Python for a single place to cross-check the whole pipeline.
Downscales the raw BGRA frame and JPEG-encodes it.
"""
import cv2
import numpy as np


def encode_image(bgra: np.ndarray, scale: float, jpeg_quality: int):
    """
    bgra: (H, W, 4) uint8 array, BGRA layout (as produced by the ZED SDK)
    returns: (jpeg_bytes, width, height), or (None, 0, 0) on degenerate input
    """
    h, w = bgra.shape[:2]
    if w == 0 or h == 0:
        return None, 0, 0

    dst_w = int(w * scale)
    dst_h = int(h * scale)
    if dst_w <= 0 or dst_h <= 0:
        return None, 0, 0

    small = cv2.resize(bgra, (dst_w, dst_h), interpolation=cv2.INTER_LINEAR)

    ok, buf = cv2.imencode('.jpg', small, [cv2.IMWRITE_JPEG_QUALITY, jpeg_quality])
    if not ok:
        return None, 0, 0

    return buf.tobytes(), dst_w, dst_h
