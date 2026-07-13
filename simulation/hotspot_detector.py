"""Hotspot detection algorithm (PROJECT_PLAN.md section 4). Python reference
implementation; firmware/src/HotspotDetector.cpp is a C++ port of this exact
algorithm.

    1. ambient = median(frame)
    2. hot_mask[i] = frame[i] > ambient + delta_c
    3. 4-connected iterative flood fill (BFS, explicit queue) into blobs
    4. discard blobs smaller than min_pixels
    5. report each blob's temperature-weighted centroid + peak temp
"""

from collections import deque

import numpy as np


def detect_hotspots(frame, delta_c, min_pixels):
    """frame: 2D numpy array (rows, cols) of temperatures in deg C.

    Returns a list of {row, col, peak_temp_c, pixel_count} dicts.
    """
    rows, cols = frame.shape
    ambient = np.median(frame)
    threshold = ambient + delta_c

    hot_mask = frame > threshold
    visited = np.zeros_like(hot_mask, dtype=bool)
    hotspots = []

    for r0 in range(rows):
        for c0 in range(cols):
            if not hot_mask[r0, c0] or visited[r0, c0]:
                continue

            queue = deque([(r0, c0)])
            visited[r0, c0] = True

            pixel_count = 0
            weighted_row_sum = 0.0
            weighted_col_sum = 0.0
            weight_sum = 0.0
            peak_temp = frame[r0, c0]

            while queue:
                r, c = queue.popleft()
                temp = frame[r, c]

                pixel_count += 1
                weighted_row_sum += r * temp
                weighted_col_sum += c * temp
                weight_sum += temp
                peak_temp = max(peak_temp, temp)

                for dr, dc in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    nr, nc = r + dr, c + dc
                    if 0 <= nr < rows and 0 <= nc < cols and hot_mask[nr, nc] and not visited[nr, nc]:
                        visited[nr, nc] = True
                        queue.append((nr, nc))

            if pixel_count >= min_pixels:
                hotspots.append({
                    "row": weighted_row_sum / weight_sum,
                    "col": weighted_col_sum / weight_sum,
                    "peak_temp_c": float(peak_temp),
                    "pixel_count": pixel_count,
                })

    return hotspots
