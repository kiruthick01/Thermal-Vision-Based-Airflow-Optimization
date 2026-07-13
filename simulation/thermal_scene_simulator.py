"""Synthetic thermal scene generator, used by hotspot_accuracy_eval.py.

Produces an ambient-temperature frame plus 0-3 warm blobs (occupant-like
hotspots) at randomized positions/sizes, along with ground-truth blob
locations so detection accuracy can be measured against a known answer.
"""

import numpy as np


def generate_frame(
    rows,
    cols,
    max_blobs=3,
    ambient_c=24.0,
    peak_delta_range=(2.0, 14.0),
    sigma_range=(0.6, 2.5),
    noise_std_c=0.3,
    rng=None,
):
    """Returns (frame[rows, cols], ground_truth: list of {row, col, peak_temp_c})."""
    rng = rng if rng is not None else np.random.default_rng()
    frame = np.full((rows, cols), ambient_c, dtype=float)
    frame += rng.normal(0.0, noise_std_c, size=(rows, cols))

    rr, cc = np.meshgrid(np.arange(rows), np.arange(cols), indexing="ij")
    ground_truth = []

    num_blobs = int(rng.integers(0, max_blobs + 1))
    for _ in range(num_blobs):
        blob_row = rng.uniform(0, rows - 1)
        blob_col = rng.uniform(0, cols - 1)
        peak_delta = rng.uniform(*peak_delta_range)
        sigma = rng.uniform(*sigma_range)

        dist2 = (rr - blob_row) ** 2 + (cc - blob_col) ** 2
        frame += peak_delta * np.exp(-dist2 / (2.0 * sigma * sigma))

        ground_truth.append({
            "row": blob_row,
            "col": blob_col,
            "peak_temp_c": ambient_c + peak_delta,
        })

    return frame, ground_truth
