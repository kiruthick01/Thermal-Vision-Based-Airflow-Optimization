"""Shared room thermal dynamics model (PROJECT_PLAN.md section 5).

dT/dt = (T_outside - T_inside) / tau_env      # heat exchange with outside
      + occupant_heat_gain(t)                 # from occupancy schedule
      - hvac_cooling_power * hvac_on(t)        # compressor effect
      + noise

This module owns the physics only. It is imported (not duplicated) by both
ml/train_drift_model.py and simulation/energy_simulation.py.
"""

import numpy as np

SECONDS_PER_DAY = 86400.0


def outside_temperature_c(t_seconds, t_min=22.0, t_max=34.0, peak_hour=16.0):
    """Diurnal sine wave outside temperature: trough at peak_hour-12, peak at peak_hour."""
    t_seconds = np.asarray(t_seconds, dtype=float)
    phase = (t_seconds % SECONDS_PER_DAY) / SECONDS_PER_DAY
    mean = (t_min + t_max) / 2.0
    amp = (t_max - t_min) / 2.0
    return mean + amp * np.sin(2 * np.pi * (phase - peak_hour / 24.0) + np.pi / 2.0)


def occupancy_count(t_seconds, max_occupants=4, start_hour=8.0, end_hour=20.0):
    """Step occupancy schedule: max_occupants during [start_hour, end_hour), else 0."""
    t_seconds = np.asarray(t_seconds, dtype=float)
    hour = (t_seconds % SECONDS_PER_DAY) / 3600.0
    occupied = (hour >= start_hour) & (hour < end_hour)
    return np.where(occupied, max_occupants, 0).astype(float)


def occupant_heat_gain_c_per_s(occupants, heat_per_person_w=100.0, thermal_mass_j_per_c=5.0e6):
    """Convert occupant count -> room temperature rate of change (deg C / s)."""
    return occupants * heat_per_person_w / thermal_mass_j_per_c


def bang_bang_policy(setpoint_c=24.0, hysteresis_c=0.5):
    """Static thermostat: toggles hvac_on at setpoint +/- hysteresis."""

    def policy(step_index, t_s, T_in, prev_on):
        if T_in > setpoint_c + hysteresis_c:
            return True
        if T_in < setpoint_c - hysteresis_c:
            return False
        return prev_on

    return policy


def simulate(
    duration_s,
    dt_s=60.0,
    hvac_policy=None,
    T_inside_init=24.0,
    tau_env_s=3 * 3600.0,
    heat_per_person_w=100.0,
    thermal_mass_j_per_c=5.0e6,
    hvac_cooling_c_per_s=0.002,
    max_occupants=4,
    occupancy_start_hour=8.0,
    occupancy_end_hour=20.0,
    t_outside_min=22.0,
    t_outside_max=34.0,
    t_outside_peak_hour=16.0,
    noise_std_c_per_s=0.0,
    setpoint_c=24.0,
    hysteresis_c=0.5,
    t0_s=0.0,
    seed=None,
):
    """Simulate one zone's temperature trajectory.

    Returns a dict of equal-length arrays: t, T_outside, T_inside, occupancy,
    hvac_on. hvac_policy(step_index, t_s, T_in, prev_on) -> bool defaults to
    a static bang-bang thermostat at setpoint_c +/- hysteresis_c.
    """
    rng = np.random.default_rng(seed)
    if hvac_policy is None:
        hvac_policy = bang_bang_policy(setpoint_c, hysteresis_c)

    n_steps = int(round(duration_s / dt_s))
    t = t0_s + np.arange(n_steps) * dt_s

    T_out = outside_temperature_c(t, t_outside_min, t_outside_max, t_outside_peak_hour)
    occ = occupancy_count(t, max_occupants, occupancy_start_hour, occupancy_end_hour)
    occ_gain = occupant_heat_gain_c_per_s(occ, heat_per_person_w, thermal_mass_j_per_c)

    T_in = np.empty(n_steps)
    hvac_on = np.empty(n_steps, dtype=bool)
    T_in[0] = T_inside_init
    prev_on = False

    for i in range(n_steps):
        prev_on = hvac_policy(i, t[i], T_in[i], prev_on)
        hvac_on[i] = prev_on
        if i + 1 < n_steps:
            dT = (T_out[i] - T_in[i]) / tau_env_s + occ_gain[i] - hvac_cooling_c_per_s * prev_on
            if noise_std_c_per_s > 0:
                dT += rng.normal(0.0, noise_std_c_per_s)
            T_in[i + 1] = T_in[i] + dT * dt_s

    return {
        "t": t,
        "T_outside": T_out,
        "T_inside": T_in,
        "occupancy": occ,
        "hvac_on": hvac_on,
    }


if __name__ == "__main__":
    result = simulate(duration_s=SECONDS_PER_DAY, dt_s=60.0, seed=0, noise_std_c_per_s=1e-4)
    print(
        f"Simulated {len(result['t'])} steps over "
        f"{SECONDS_PER_DAY / 3600:.0f}h. "
        f"T_inside range: {result['T_inside'].min():.2f}-{result['T_inside'].max():.2f} C, "
        f"HVAC duty cycle: {result['hvac_on'].mean() * 100:.1f}%"
    )
