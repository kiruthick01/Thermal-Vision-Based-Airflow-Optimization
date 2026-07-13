"""End-to-end MQTT integration test (PROJECT_PLAN.md section 8).

Spins up an in-process amqtt broker (pure Python, no root, no mosquitto
binary needed), runs a simulated frame/hotspot publisher against
controller/airflow_controller.py, and confirms the controller reacts
end-to-end by publishing decisions to site/<zone>/control/setpoint.
"""

import asyncio
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import paho.mqtt.client as mqtt
from amqtt.broker import Broker
from amqtt.contexts import BrokerConfig, ListenerConfig

from controller.airflow_controller import AirflowController

BROKER_HOST = "127.0.0.1"
BROKER_PORT = 18830
ZONE = "zone1-test"
N_MESSAGES = 20


def build_publisher():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="sim-publisher")
    client.connect(BROKER_HOST, BROKER_PORT)
    client.loop_start()
    return client


def build_setpoint_listener():
    received = []

    def on_message(client, userdata, msg):
        received.append(json.loads(msg.payload))

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="setpoint-listener")
    client.on_message = on_message
    client.connect(BROKER_HOST, BROKER_PORT)
    client.subscribe(f"site/{ZONE}/control/setpoint")
    client.loop_start()
    return client, received


async def run_test():
    config = BrokerConfig(listeners={"default": ListenerConfig(bind=f"{BROKER_HOST}:{BROKER_PORT}")})
    broker = Broker(config)
    await broker.start()
    print(f"amqtt broker listening on {BROKER_HOST}:{BROKER_PORT}")

    controller_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="airflow-controller-test")
    AirflowController(controller_client, ZONE)
    controller_client.connect(BROKER_HOST, BROKER_PORT)
    controller_client.loop_start()

    setpoint_client, received_setpoints = build_setpoint_listener()

    await asyncio.sleep(0.5)  # let connects/subscribes settle

    publisher = build_publisher()

    ambient_c = 23.0
    for i in range(N_MESSAGES):
        # Ambient drifts upward; occupancy kicks in after a few frames so the
        # test exercises both the "unoccupied/relaxed" and
        # "occupied/predictive" branches, including once enough history has
        # accumulated for the drift model to produce a forecast.
        ambient_c += 0.15
        occupied = i >= 3
        publisher.publish(f"site/{ZONE}/thermal/frame", json.dumps({"ambient_c": ambient_c}))
        hotspots = [{"row": 4, "col": 5, "peak_temp_c": ambient_c + 8.0}] if occupied else []
        publisher.publish(f"site/{ZONE}/thermal/hotspots", json.dumps(hotspots))
        await asyncio.sleep(0.05)

    await asyncio.sleep(1.0)  # drain remaining in-flight messages

    for c in (controller_client, publisher, setpoint_client):
        c.loop_stop()
        c.disconnect()

    await broker.shutdown()
    return received_setpoints


def main():
    received = asyncio.run(run_test())

    print(f"Published {N_MESSAGES} hotspot frames, received {len(received)} control/setpoint decisions")
    assert len(received) == N_MESSAGES, f"expected {N_MESSAGES} setpoint decisions, got {len(received)}"

    occupied_decisions = [d for d in received if d["occupancy_count"] > 0]
    unoccupied_decisions = [d for d in received if d["occupancy_count"] == 0]
    assert unoccupied_decisions, "expected at least one unoccupied decision"
    assert occupied_decisions, "expected at least one occupied decision"

    assert min(d["setpoint_c"] for d in unoccupied_decisions) > max(
        d["setpoint_c"] for d in occupied_decisions
    ), "unoccupied setpoint should be relaxed above every occupied setpoint"

    assert any(d["predicted_temp_c"] is not None for d in occupied_decisions), (
        "expected at least one drift-model prediction once the history window filled"
    )

    print("PASS: controller reacted end-to-end over the amqtt broker")


if __name__ == "__main__":
    main()
