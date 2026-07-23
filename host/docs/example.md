## Example: Pi provisioning service
File: provisioning_service.py

```python
import json
import paho.mqtt.client as mqtt

BROKER_HOST = "192.168.50.115"
BROKER_PORT = 1883

# Temporary development registry.
# Later this should move into PostgreSQL.
DEVICE_REGISTRY = {
    "AA-BB-CC-DD-EE-FF": {
        "unit_id": "unit01",
        "role": "controllerA",
        "telemetry_interval_ms": 5000,
        "heartbeat_interval_ms": 500,
        "heartbeat_timeout_ms": 2000,
        "config_version": 1,
    },
    "11-22-33-44-55-66": {
        "unit_id": "unit01",
        "role": "controllerB",
        "telemetry_interval_ms": 5000,
        "heartbeat_interval_ms": 500,
        "heartbeat_timeout_ms": 2000,
        "config_version": 1,
    },
}


def build_config(mac: str, device: dict) -> dict:
    unit_id = device["unit_id"]
    role = device["role"]

    return {
        "message_type": "config",
        "mac": mac,
        "configured": True,
        "unit_id": unit_id,
        "role": role,
        "config_version": device["config_version"],
        "timing": {
            "telemetry_interval_ms": device["telemetry_interval_ms"],
            "heartbeat_interval_ms": device["heartbeat_interval_ms"],
            "heartbeat_timeout_ms": device["heartbeat_timeout_ms"],
        },
        "topics": {
            "telemetry": f"repacss/units/{unit_id}/{role}/telemetry",
            "status": f"repacss/units/{unit_id}/{role}/status",
            "event": f"repacss/units/{unit_id}/{role}/event",
        },
    }


def on_connect(client, userdata, flags, reason_code, properties):
    print(f"[MQTT] Connected with reason: {reason_code}")

    # Listen for hello messages from any ESP32 MAC.
    client.subscribe("repacss/devices/+/hello", qos=1)


def on_message(client, userdata, msg):
    topic = msg.topic
    payload_text = msg.payload.decode("utf-8")

    print(f"[RX] {topic} {payload_text}")

    # Topic example:
    # repacss/devices/AA-BB-CC-DD-EE-FF/hello
    parts = topic.split("/")
    mac = parts[2]

    device = DEVICE_REGISTRY.get(mac)

    if device is None:
        print(f"[WARN] Unknown device: {mac}")

        unknown_status = {
            "message_type": "config",
            "mac": mac,
            "configured": False,
            "reason": "unknown_mac",
        }

        config_topic = f"repacss/devices/{mac}/config"

        client.publish(
            config_topic,
            json.dumps(unknown_status),
            qos=1,
            retain=True,
        )

        return

    config = build_config(mac, device)
    config_topic = f"repacss/devices/{mac}/config"

    print(f"[TX] Sending config to {config_topic}")

    # retain=True means the ESP32 can reconnect later and still receive config.
    client.publish(
        config_topic,
        json.dumps(config),
        qos=1,
        retain=True,
    )


client = mqtt.Client(
    mqtt.CallbackAPIVersion.VERSION2,
    client_id="repacss-provisioning-service",
)

client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER_HOST, BROKER_PORT)
client.loop_forever()
```

What this does:

Subscribes to:
repacss/devices/+/hello

Receives:
repacss/devices/AA-BB-CC-DD-EE-FF/hello

Publishes config to:
repacss/devices/AA-BB-CC-DD-EE-FF/config

---

## Example: Pi ingestion service
File: ingestion_service.py

```python
import json
import paho.mqtt.client as mqtt

BROKER_HOST = "192.168.50.115"
BROKER_PORT = 1883


def on_connect(client, userdata, flags, reason_code, properties):
    print(f"[MQTT] Connected with reason: {reason_code}")

    # Listen to all runtime telemetry/status/events.
    client.subscribe("repacss/units/+/+/telemetry", qos=0)
    client.subscribe("repacss/units/+/+/status", qos=0)
    client.subscribe("repacss/units/+/+/event", qos=1)

    # Also listen to device-level ack/status.
    client.subscribe("repacss/devices/+/ack", qos=1)
    client.subscribe("repacss/devices/+/status", qos=0)


def on_message(client, userdata, msg):
    topic = msg.topic
    payload_text = msg.payload.decode("utf-8")

    print(f"\n[RX] Topic: {topic}")
    print(f"[RX] Payload: {payload_text}")

    try:
        data = json.loads(payload_text)
    except json.JSONDecodeError:
        print("[ERROR] Invalid JSON")
        return

    parts = topic.split("/")

    if topic.startswith("repacss/units/"):
        # Example:
        # repacss/units/unit01/controllerA/telemetry
        unit_id = parts[2]
        role = parts[3]
        message_kind = parts[4]

        print(f"[PARSED] unit={unit_id}, role={role}, type={message_kind}")

        # Later:
        # write this into PostgreSQL / TimescaleDB.
        # telemetry_samples table
        # device_status table
        # events table

    elif topic.startswith("repacss/devices/"):
        # Example:
        # repacss/devices/AA-BB-CC-DD-EE-FF/ack
        mac = parts[2]
        message_kind = parts[3]

        print(f"[PARSED] mac={mac}, type={message_kind}")

        # Later:
        # update device_status or config_ack table.


client = mqtt.Client(
    mqtt.CallbackAPIVersion.VERSION2,
    client_id="repacss-ingestion-service",
)

client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER_HOST, BROKER_PORT)
client.loop_forever()
```

This service catches messages like:<br>
repacss/units/unit01/controllerA/telemetry<br>
repacss/units/unit01/controllerA/status<br>
repacss/units/unit01/controllerA/event<br>

---

## Fake ESP32 simulator in Python
File: fake_esp32.py
```python
import json
import time
import paho.mqtt.client as mqtt

BROKER_HOST = "192.168.50.115"
BROKER_PORT = 1883

MAC = "AA-BB-CC-DD-EE-FF"

config = None


def on_connect(client, userdata, flags, reason_code, properties):
    print(f"[MQTT] Connected with reason: {reason_code}")

    config_topic = f"repacss/devices/{MAC}/config"

    # ESP32 listens only to its own config topic.
    client.subscribe(config_topic, qos=1)

    hello_topic = f"repacss/devices/{MAC}/hello"

    hello = {
        "message_type": "hello",
        "mac": MAC,
        "firmware": "fake-esp32-dev",
        "configured": False,
    }

    print(f"[TX] hello → {hello_topic}")

    client.publish(
        hello_topic,
        json.dumps(hello),
        qos=1,
        retain=False,
    )


def on_message(client, userdata, msg):
    global config

    topic = msg.topic
    payload = msg.payload.decode("utf-8")

    print(f"[RX] {topic} {payload}")

    data = json.loads(payload)

    if not data.get("configured"):
        print("[DEVICE] No valid config. Staying unconfigured.")
        return

    config = data

    ack_topic = f"repacss/devices/{MAC}/ack"

    ack = {
        "message_type": "config_ack",
        "mac": MAC,
        "config_version": config["config_version"],
        "accepted": True,
    }

    print(f"[TX] config ack → {ack_topic}")

    client.publish(
        ack_topic,
        json.dumps(ack),
        qos=1,
        retain=False,
    )


client = mqtt.Client(
    mqtt.CallbackAPIVersion.VERSION2,
    client_id=f"fake-esp32-{MAC}",
)

client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER_HOST, BROKER_PORT)
client.loop_start()

while True:
    if config is None:
        print("[DEVICE] Waiting for config...")
        time.sleep(3)
        continue

    telemetry_topic = config["topics"]["telemetry"]

    telemetry = {
        "message_type": "telemetry",
        "mac": MAC,
        "unit_id": config["unit_id"],
        "role": config["role"],
        "temperature_c": 23.5,
        "humidity_rh": 40.0,
        "timestamp_device_ms": int(time.time() * 1000),
    }

    print(f"[TX] telemetry → {telemetry_topic}")

    client.publish(
        telemetry_topic,
        json.dumps(telemetry),
        qos=0,
        retain=False,
    )

    time.sleep(config["timing"]["telemetry_interval_ms"] / 1000)
```
---

fake ESP32 publishes hello<br>
provisioning service receives hello<br>
provisioning service publishes config<br>
fake ESP32 receives config<br>
fake ESP32 publishes ack<br>
fake ESP32 starts telemetry<br>
ingestion service receives telemetry<br>

---

## Tiny ESP32-style MQTT concept
This is not final W5500 firmware, but this shows the logic.

```cpp
// Concept only: ESP32 MQTT flow
//
// 1. Connect to network
// 2. Connect to MQTT broker at 192.168.50.115
// 3. Publish hello
// 4. Subscribe to config
// 5. After config, publish telemetry to assigned topic

const char* MQTT_HOST = "192.168.50.115";
const int MQTT_PORT = 1883;

String mac = "AA-BB-CC-DD-EE-FF";

String helloTopic  = "repacss/devices/" + mac + "/hello";
String configTopic = "repacss/devices/" + mac + "/config";
String ackTopic    = "repacss/devices/" + mac + "/ack";

String telemetryTopic = "";
bool configured = false;

void onMqttMessage(String topic, String payload) {
  if (topic == configTopic) {
    // Parse JSON config here.
    //
    // Example received:
    // {
    //   "configured": true,
    //   "unit_id": "unit01",
    //   "role": "controllerA",
    //   "topics": {
    //     "telemetry": "repacss/units/unit01/controllerA/telemetry"
    //   }
    // }

    telemetryTopic = "repacss/units/unit01/controllerA/telemetry";
    configured = true;

    // Publish config acknowledgement.
    publish(ackTopic, "{\"message_type\":\"config_ack\",\"accepted\":true}");
  }
}

void afterMqttConnected() {
  subscribe(configTopic);

  publish(
    helloTopic,
    "{\"message_type\":\"hello\",\"mac\":\"AA-BB-CC-DD-EE-FF\",\"configured\":false}"
  );
}

void loop() {
  if (!configured) {
    // Do not assume controllerA or controllerB.
    // Wait for config.
    return;
  }

  // Now the ESP32 has a role and telemetry topic.
  publish(
    telemetryTopic,
    "{\"message_type\":\"telemetry\",\"temperature_c\":23.5}"
  );

  delay(5000);
}
```
