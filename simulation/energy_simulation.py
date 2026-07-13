"""Energy comparison: static thermostat vs. predictive controller (PROJECT_PLAN.md section 7).

Both controllers run over the *same* dynamics model, occupancy schedule, and
outside-temperature profile (same randomized day params + same noise seed),
across >=10 random seeds. Energy is the time-integral of HVAC cooling level
(a proxy for compressor duty / power draw), so:

    % reduction = (static_energy - predictive_energy) / static_energy * 100

The predictive controller has no hotspot_detector.py to draw from yet (that
module isn't built in this stage), so it uses the simulator's ground-truth
occupancy signal as the "occupancy detected" gate -- the same information a
correctly-working hotspot detector would be approximating.
"""

import os
import sys
from collections import deque

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ml.thermal_drift_model import DriftMLP
from simulation.thermal_dynamics_sim import (
    SECONDS_PER_DAY,
    bang_bang_policy,
    simulate,
)

DT_S = 60.0
N_SEEDS = 20
PROPORTIONAL_BAND_C = 1.0  # deg C of (current + forecast) excess that saturates cooling to 100%
WEIGHTS_PATH = os.path.join(os.path.dirname(__file__), "..", "ml", "drift_model_weights.json")
RESULTS_PATH = os.path.join(os.path.dirname(__file__), "energy_results.json")


def day_params(seed):
    """Randomized-but-shared params for one day, reused by both controllers."""
    rng = np.random.default_rng(seed)
    return {
        "setpoint_c": rng.uniform(22.0, 26.0),
        "hysteresis_c": rng.uniform(0.3, 0.8),
        "tau_env_s": rng.uniform(2.0, 4.0) * 3600.0,
        "max_occupants": int(rng.integers(1, 6)),
        "occupancy_start_hour": rng.uniform(6.0, 9.0),
        "occupancy_end_hour": rng.uniform(17.0, 21.0),
        "t_outside_min": rng.uniform(20.0, 24.0),
        "t_outside_max": rng.uniform(30.0, 36.0),
    }


def load_drift_model():
    with open(WEIGHTS_PATH) as f:
        import json

        data = json.load(f)
    model = DriftMLP.from_dict(data)
    norm = {
        "input_mean": np.array(data["input_mean"]),
        "input_std": np.array(data["input_std"]),
        "output_mean": data["output_mean"],
        "output_std": data["output_std"],
    }
    return model, norm, data["k_history"], data["h_forecast"]


def make_predictive_policy(model, norm, k, setpoint_c, hysteresis_c):
    """Proportional controller: cooling level scales with (current excess +
    forecast forward-drift) above setpoint_c, gated by occupancy so it only
    ramps airflow when actual occupancy is present."""
    T_hist = deque(maxlen=k)
    hvac_hist = deque(maxlen=k)
    occ_hist = deque(maxlen=k)
    bootstrap = bang_bang_policy(setpoint_c, hysteresis_c)

    def policy(step_index, t_s, T_in, occupancy, prev_level):
        T_hist.append(T_in)
        hvac_hist.append(prev_level)
        occ_hist.append(occupancy)

        if len(T_hist) < k:
            return bootstrap(step_index, t_s, T_in, occupancy, prev_level)

        if occupancy <= 0:
            return 0.0

        x = np.concatenate([np.array(T_hist), np.array(hvac_hist), [np.mean(occ_hist)]])
        x_n = (x - norm["input_mean"]) / norm["input_std"]
        y_n = model.predict(x_n[None, :])[0]
        predicted_T = y_n * norm["output_std"] + norm["output_mean"]

        excess_now = T_in - setpoint_c
        predicted_rise = max(predicted_T - T_in, 0.0)
        level = (excess_now + predicted_rise) / PROPORTIONAL_BAND_C
        return float(np.clip(level, 0.0, 1.0))

    return policy


def run_one_seed(seed, model, norm, k):
    params = day_params(seed)
    setpoint_c = params["setpoint_c"]
    hysteresis_c = params["hysteresis_c"]

    common_kwargs = dict(
        duration_s=SECONDS_PER_DAY,
        dt_s=DT_S,
        T_inside_init=setpoint_c,
        tau_env_s=params["tau_env_s"],
        max_occupants=params["max_occupants"],
        occupancy_start_hour=params["occupancy_start_hour"],
        occupancy_end_hour=params["occupancy_end_hour"],
        t_outside_min=params["t_outside_min"],
        t_outside_max=params["t_outside_max"],
        noise_std_c_per_s=2e-4,
        seed=seed,
    )

    static_result = simulate(
        hvac_policy=bang_bang_policy(setpoint_c, hysteresis_c),
        **common_kwargs,
    )
    predictive_result = simulate(
        hvac_policy=make_predictive_policy(model, norm, k, setpoint_c, hysteresis_c),
        **common_kwargs,
    )

    static_energy = static_result["hvac_on"].sum()
    predictive_energy = predictive_result["hvac_on"].sum()
    reduction_pct = (static_energy - predictive_energy) / static_energy * 100.0

    comfort_static = np.mean(np.abs(static_result["T_inside"] - setpoint_c))
    comfort_predictive = np.mean(np.abs(predictive_result["T_inside"] - setpoint_c))

    return {
        "seed": seed,
        "static_energy": float(static_energy),
        "predictive_energy": float(predictive_energy),
        "reduction_pct": float(reduction_pct),
        "comfort_static_mae_c": float(comfort_static),
        "comfort_predictive_mae_c": float(comfort_predictive),
    }


def main():
    model, norm, k, h = load_drift_model()
    print(f"Loaded drift model (k={k}, h={h}) from {WEIGHTS_PATH}")

    per_seed = [run_one_seed(seed, model, norm, k) for seed in range(N_SEEDS)]

    reductions = np.array([r["reduction_pct"] for r in per_seed])
    comfort_static = np.array([r["comfort_static_mae_c"] for r in per_seed])
    comfort_predictive = np.array([r["comfort_predictive_mae_c"] for r in per_seed])

    for r in per_seed:
        print(
            f"seed {r['seed']:2d}: static={r['static_energy']:7.1f}  "
            f"predictive={r['predictive_energy']:7.1f}  "
            f"reduction={r['reduction_pct']:6.2f}%"
        )

    mean_reduction = reductions.mean()
    std_reduction = reductions.std()
    print(
        f"ENERGY REDUCTION over {N_SEEDS} seeds: "
        f"mean={mean_reduction:.2f}%  std={std_reduction:.2f}%"
    )
    print(
        f"Comfort (mean |T_in - setpoint|): static={comfort_static.mean():.3f} C  "
        f"predictive={comfort_predictive.mean():.3f} C"
    )

    results = {
        "n_seeds": N_SEEDS,
        "mean_reduction_pct": float(mean_reduction),
        "std_reduction_pct": float(std_reduction),
        "mean_comfort_static_mae_c": float(comfort_static.mean()),
        "mean_comfort_predictive_mae_c": float(comfort_predictive.mean()),
        "per_seed": per_seed,
    }
    import json

    with open(RESULTS_PATH, "w") as f:
        json.dump(results, f, indent=2)
    print(f"Saved results to {RESULTS_PATH}")


if __name__ == "__main__":
    main()
