"""Train the NumPy MLP drift-forecasting model (PROJECT_PLAN.md section 6).

Generates training data from simulation/thermal_dynamics_sim.py across many
randomized days/seeds, trains a small MLP with plain gradient descent, and
reports test-set MAE (deg C) on held-out days (not just held-out windows, to
avoid leakage between adjacent windows of the same day).
"""

import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ml.thermal_drift_model import DriftMLP
from simulation.thermal_dynamics_sim import (
    SECONDS_PER_DAY,
    bang_bang_policy,
    simulate,
)

K_HISTORY = 10          # last k temperature / HVAC readings
H_FORECAST = 10         # predict h steps ahead
DT_S = 60.0             # 1-minute resolution
N_TRAIN_DAYS = 45
N_TEST_DAYS = 15
HIDDEN_DIM = 16
EPOCHS = 300
BATCH_SIZE = 256
LR = 0.05
RESULTS_PATH = os.path.join(os.path.dirname(__file__), "drift_model_results.json")
WEIGHTS_PATH = os.path.join(os.path.dirname(__file__), "drift_model_weights.json")


def simulate_one_day(seed):
    """Run one randomized day of the dynamics model and return raw arrays."""
    rng = np.random.default_rng(seed)
    setpoint_c = rng.uniform(22.0, 26.0)
    hysteresis_c = rng.uniform(0.3, 0.8)
    tau_env_s = rng.uniform(2.0, 4.0) * 3600.0
    max_occupants = rng.integers(1, 6)
    occupancy_start_hour = rng.uniform(6.0, 9.0)
    occupancy_end_hour = rng.uniform(17.0, 21.0)
    t_outside_min = rng.uniform(20.0, 24.0)
    t_outside_max = rng.uniform(30.0, 36.0)

    policy = bang_bang_policy(setpoint_c, hysteresis_c)
    return simulate(
        duration_s=SECONDS_PER_DAY,
        dt_s=DT_S,
        hvac_policy=policy,
        T_inside_init=setpoint_c,
        tau_env_s=tau_env_s,
        max_occupants=max_occupants,
        occupancy_start_hour=occupancy_start_hour,
        occupancy_end_hour=occupancy_end_hour,
        t_outside_min=t_outside_min,
        t_outside_max=t_outside_max,
        noise_std_c_per_s=2e-4,
        seed=seed,
    )


def build_windows(day_result, k=K_HISTORY, h=H_FORECAST):
    """Slice one day's arrays into (X, y) supervised windows."""
    T_in = day_result["T_inside"]
    hvac_on = day_result["hvac_on"].astype(float)
    occupancy = day_result["occupancy"]
    n = len(T_in)

    xs, ys = [], []
    for i in range(k, n - h):
        temp_window = T_in[i - k:i]
        hvac_window = hvac_on[i - k:i]
        occ_recent = occupancy[i - k:i].mean()
        xs.append(np.concatenate([temp_window, hvac_window, [occ_recent]]))
        ys.append(T_in[i - 1 + h])
    return np.array(xs), np.array(ys)


def build_dataset(seeds):
    X_parts, y_parts = [], []
    for seed in seeds:
        day = simulate_one_day(seed)
        X_day, y_day = build_windows(day)
        X_parts.append(X_day)
        y_parts.append(y_day)
    return np.concatenate(X_parts), np.concatenate(y_parts)


def main():
    train_seeds = list(range(0, N_TRAIN_DAYS))
    test_seeds = list(range(1000, 1000 + N_TEST_DAYS))  # disjoint seed range, held-out days

    X_train, y_train = build_dataset(train_seeds)
    X_test, y_test = build_dataset(test_seeds)

    x_mean = X_train.mean(axis=0)
    x_std = X_train.std(axis=0)
    x_std[x_std == 0] = 1.0
    X_train_n = (X_train - x_mean) / x_std
    X_test_n = (X_test - x_mean) / x_std

    y_mean = y_train.mean()
    y_std = y_train.std()
    y_train_n = (y_train - y_mean) / y_std

    input_dim = X_train_n.shape[1]
    model = DriftMLP(input_dim=input_dim, hidden_dim=HIDDEN_DIM, seed=0)
    print(f"Training DriftMLP: input_dim={input_dim}, hidden_dim={HIDDEN_DIM}, "
          f"params={model.param_count()}")
    print(f"Train windows: {len(X_train_n)} (days {len(train_seeds)}), "
          f"Test windows: {len(X_test_n)} (days {len(test_seeds)})")

    rng = np.random.default_rng(0)
    n_train = len(X_train_n)
    for epoch in range(EPOCHS):
        perm = rng.permutation(n_train)
        epoch_loss = 0.0
        for start in range(0, n_train, BATCH_SIZE):
            idx = perm[start:start + BATCH_SIZE]
            loss = model.train_step(X_train_n[idx], y_train_n[idx], LR)
            epoch_loss += loss * len(idx)
        epoch_loss /= n_train
        if epoch % 50 == 0 or epoch == EPOCHS - 1:
            print(f"epoch {epoch:4d}  train_mse={epoch_loss:.5f}")

    y_pred_test_n = model.predict(X_test_n)
    y_pred_test = y_pred_test_n * y_std + y_mean
    test_mae = np.mean(np.abs(y_pred_test - y_test))
    test_rmse = np.sqrt(np.mean((y_pred_test - y_test) ** 2))

    print(f"TEST-SET MAE: {test_mae:.4f} deg C  (RMSE: {test_rmse:.4f} deg C)")

    model.save_weights(WEIGHTS_PATH)
    with open(WEIGHTS_PATH) as f:
        weights_data = json.load(f)
    weights_data["input_mean"] = x_mean.tolist()
    weights_data["input_std"] = x_std.tolist()
    weights_data["output_mean"] = float(y_mean)
    weights_data["output_std"] = float(y_std)
    weights_data["k_history"] = K_HISTORY
    weights_data["h_forecast"] = H_FORECAST
    with open(WEIGHTS_PATH, "w") as f:
        json.dump(weights_data, f)

    results = {
        "k_history": K_HISTORY,
        "h_forecast": H_FORECAST,
        "hidden_dim": HIDDEN_DIM,
        "param_count": model.param_count(),
        "n_train_days": N_TRAIN_DAYS,
        "n_test_days": N_TEST_DAYS,
        "epochs": EPOCHS,
        "test_mae_c": float(test_mae),
        "test_rmse_c": float(test_rmse),
    }
    with open(RESULTS_PATH, "w") as f:
        json.dump(results, f, indent=2)
    print(f"Saved weights to {WEIGHTS_PATH}")
    print(f"Saved results to {RESULTS_PATH}")


if __name__ == "__main__":
    main()
