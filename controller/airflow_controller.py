"""Airflow controller (PROJECT_PLAN.md section 8).

Subscribes to a zone's thermal frame stats + hotspots, runs the trained
drift model plus the section 7 predictive logic, and publishes a control
decision to site/<zone>/control/setpoint.

MQTT topics used:
    site/<zone>/thermal/frame     -> {"ambient_c": float}   (downsampled frame stat)
    site/<zone>/thermal/hotspots  -> [{"row", "col", "peak_temp_c"}, ...]
    site/<zone>/control/setpoint  <- {"setpoint_c", "cooling_level",
                                       "occupancy_count", "predicted_temp_c"}
"""

import json
import os
import sys
from collections import deque

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ml.thermal_drift_model import DriftMLP

WEIGHTS_PATH = os.path.join(os.path.dirname(__file__), "..", "ml", "drift_model_weights.json")
PROPORTIONAL_BAND_C = 1.0


class AirflowController:
    """Reacts to a single zone's thermal/frame + thermal/hotspots topics."""

    def __init__(
        self,
        client,
        zone,
        base_setpoint_c=24.0,
        relax_margin_c=2.0,
        max_preempt_c=1.5,
        weights_path=WEIGHTS_PATH,
    ):
        self.client = client
        self.zone = zone
        self.base_setpoint_c = base_setpoint_c
        self.relax_margin_c = relax_margin_c
        self.max_preempt_c = max_preempt_c

        with open(weights_path) as f:
            data = json.load(f)
        self.model = DriftMLP.from_dict(data)
        self.input_mean = np.array(data["input_mean"])
        self.input_std = np.array(data["input_std"])
        self.output_mean = data["output_mean"]
        self.output_std = data["output_std"]
        self.k = data["k_history"]
        self.h = data["h_forecast"]

        self.T_hist = deque(maxlen=self.k)
        self.hvac_hist = deque(maxlen=self.k)
        self.occ_hist = deque(maxlen=self.k)
        self.last_level = 0.0
        self.latest_ambient_c = None

        self.topic_frame = f"site/{zone}/thermal/frame"
        self.topic_hotspots = f"site/{zone}/thermal/hotspots"
        self.topic_setpoint = f"site/{zone}/control/setpoint"

        self.decisions = []  # useful for tests/inspection

        client.on_connect = self._on_connect
        client.on_message = self._on_message

    def _on_connect(self, client, userdata, flags, reason_code, properties=None):
        client.subscribe(self.topic_frame)
        client.subscribe(self.topic_hotspots)

    def _on_message(self, client, userdata, msg):
        if msg.topic == self.topic_frame:
            self.latest_ambient_c = json.loads(msg.payload)["ambient_c"]
        elif msg.topic == self.topic_hotspots:
            self._handle_hotspots(json.loads(msg.payload))

    def _handle_hotspots(self, hotspots):
        if self.latest_ambient_c is None:
            return  # no temperature reading yet, nothing to act on

        occupancy_count = len(hotspots)
        current_T = self.latest_ambient_c

        self.T_hist.append(current_T)
        self.hvac_hist.append(self.last_level)
        self.occ_hist.append(occupancy_count)

        predicted_T = None
        if occupancy_count <= 0:
            # Section 7: only ramp airflow when occupancy is actually detected.
            setpoint_c = self.base_setpoint_c + self.relax_margin_c
            level = 0.0
        elif len(self.T_hist) < self.k:
            # Not enough history yet for the drift model -- fall back to a
            # simple threshold until the rolling window fills.
            setpoint_c = self.base_setpoint_c
            level = 1.0 if current_T > self.base_setpoint_c else 0.0
        else:
            x = np.concatenate([np.array(self.T_hist), np.array(self.hvac_hist), [np.mean(self.occ_hist)]])
            x_n = (x - self.input_mean) / self.input_std
            y_n = self.model.predict(x_n[None, :])[0]
            predicted_T = float(y_n * self.output_std + self.output_mean)

            excess_now = current_T - self.base_setpoint_c
            predicted_rise = max(predicted_T - current_T, 0.0)
            level = float(np.clip((excess_now + predicted_rise) / PROPORTIONAL_BAND_C, 0.0, 1.0))
            setpoint_c = self.base_setpoint_c - min(predicted_rise, self.max_preempt_c)

        self.last_level = level

        payload = {
            "setpoint_c": round(setpoint_c, 2),
            "cooling_level": round(level, 2),
            "occupancy_count": occupancy_count,
            "predicted_temp_c": round(predicted_T, 2) if predicted_T is not None else None,
        }
        self.decisions.append(payload)
        self.client.publish(self.topic_setpoint, json.dumps(payload))


def main():
    import argparse

    import paho.mqtt.client as mqtt

    parser = argparse.ArgumentParser()
    parser.add_argument("--broker", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--zone", default="zone1")
    args = parser.parse_args()

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="airflow-controller")
    AirflowController(client, args.zone)
    client.connect(args.broker, args.port)
    client.loop_forever()


if __name__ == "__main__":
    main()
