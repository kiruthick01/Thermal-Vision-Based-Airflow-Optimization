"""Hotspot-detection accuracy eval (PROJECT_PLAN.md section 4).

Runs N synthetic frames per sensor config through hotspot_detector.py and
scores detections against thermal_scene_simulator.py's ground truth. A
detected blob is a true positive if its centroid falls within
MATCH_RADIUS_PX of an unmatched ground-truth blob (greedy nearest-neighbor,
one-to-one); everything else is a false positive/negative.

Prints precision/recall/F1 per sensor and writes
simulation/hotspot_accuracy_results.json (source of the numbers cited in
docs/results.md).
"""

import json
import os

import numpy as np

from hotspot_detector import detect_hotspots
from thermal_scene_simulator import generate_frame

N_FRAMES = 1000
MATCH_RADIUS_PX = 2.5
RESULTS_PATH = os.path.join(os.path.dirname(__file__), "hotspot_accuracy_results.json")

SENSOR_CONFIGS = {
    "MLX90640": {"rows": 24, "cols": 32, "delta_c": 4.5, "min_pixels": 2},
    "AMG8833": {"rows": 8, "cols": 8, "delta_c": 3.5, "min_pixels": 1},
}


def match_and_score(detections, ground_truth):
    """Greedy nearest-neighbor one-to-one matching within MATCH_RADIUS_PX."""
    pairs = []
    for di, d in enumerate(detections):
        for gi, g in enumerate(ground_truth):
            dist = np.hypot(d["row"] - g["row"], d["col"] - g["col"])
            if dist <= MATCH_RADIUS_PX:
                pairs.append((dist, di, gi))
    pairs.sort(key=lambda p: p[0])

    matched_d, matched_g = set(), set()
    for _, di, gi in pairs:
        if di in matched_d or gi in matched_g:
            continue
        matched_d.add(di)
        matched_g.add(gi)

    tp = len(matched_d)
    fp = len(detections) - len(matched_d)
    fn = len(ground_truth) - len(matched_g)
    return tp, fp, fn


def evaluate_sensor(name, cfg, n_frames, seed):
    rng = np.random.default_rng(seed)
    tp_total = fp_total = fn_total = 0

    for _ in range(n_frames):
        frame, ground_truth = generate_frame(cfg["rows"], cfg["cols"], rng=rng)
        detections = detect_hotspots(frame, cfg["delta_c"], cfg["min_pixels"])
        tp, fp, fn = match_and_score(detections, ground_truth)
        tp_total += tp
        fp_total += fp
        fn_total += fn

    precision = tp_total / (tp_total + fp_total) if (tp_total + fp_total) else 0.0
    recall = tp_total / (tp_total + fn_total) if (tp_total + fn_total) else 0.0
    f1 = 2 * precision * recall / (precision + recall) if (precision + recall) else 0.0

    return {
        "sensor": name,
        "rows": cfg["rows"],
        "cols": cfg["cols"],
        "delta_c": cfg["delta_c"],
        "min_pixels": cfg["min_pixels"],
        "n_frames": n_frames,
        "true_positives": tp_total,
        "false_positives": fp_total,
        "false_negatives": fn_total,
        "precision": precision,
        "recall": recall,
        "f1": f1,
    }


def main():
    results = []
    for i, (name, cfg) in enumerate(SENSOR_CONFIGS.items()):
        r = evaluate_sensor(name, cfg, N_FRAMES, seed=100 + i)
        results.append(r)
        print(
            f"{name}: delta_c={cfg['delta_c']} min_pixels={cfg['min_pixels']}  "
            f"precision={r['precision']:.3f}  recall={r['recall']:.3f}  f1={r['f1']:.3f}  "
            f"(TP={r['true_positives']} FP={r['false_positives']} FN={r['false_negatives']}, "
            f"{N_FRAMES} frames)"
        )

    with open(RESULTS_PATH, "w") as f:
        json.dump({"match_radius_px": MATCH_RADIUS_PX, "results": results}, f, indent=2)
    print(f"Saved results to {RESULTS_PATH}")


if __name__ == "__main__":
    main()
