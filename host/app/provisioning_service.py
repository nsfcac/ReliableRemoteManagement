import json
import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion
from app.config import settings
from app.db import connect as db_connect
from app import unknown_devices, current_state, device_registry

conn = db_connect()
MAX_HELLO_BYTES= 2048

def build_config(mac, device: dict) -> dict:
# FUTURE: When a general system is working with DB and a dashboard implement a modifiable telemetry collect timing
    return{
        "message_type": "config",
        "mac": mac,
        "configured": True,
        "enabled": device["enabled"],
        "rack_id": device["rack_id"],
        "role": device["role"]
    }

def on_connect(client: mqtt.Client, userdata, flags, reason_code, properties):
    print(f"[MQTT] Connected with reason: {reason_code}") # Convert to a log to register to docker log file

    client.subscribe("repacss/devices/+/hello", qos=1)

def on_message(client: mqtt.Client, userdata, msg):
    if len(msg.payload) > MAX_HELLO_BYTES:
        print(f"[WARN] Overloaded payload on {msg.topic}: {len(msg.payload)}")
        return
      
    topic = msg.topic
    payload_txt = msg.payload.decode("utf-8")

    print(f"[RX] {topic} {payload_txt}")
    
    parts = topic.split("/")
    mac = parts[2]

    try:
        hello = json.loads(payload_txt)
    except json.JSONDecodeError:
        print(f"[WARN] Invalid JSON payload from {mac}: {payload_txt}")
        return

    fw = hello.get("firmware_version", "unknown")
    print(f"[INFO] Device {mac} running firmware version: {fw}")
    current_state.upsert(conn, mac, fw)

    device = device_registry.lookup(conn, mac)
    config_topic = f"repacss/devices/{mac}/config"

    if device is None:
        print(f"[WARN] Unknown device: {mac}")
        unknown_devices.record(conn, mac, payload_txt)
        return

    config = build_config(mac, device)
    print(f"[TX] Sending config to {config_topic}")

    client.publish(config_topic, json.dumps(config), qos=1, retain=False)


client = mqtt.Client(
    CallbackAPIVersion.VERSION2,
    client_id="repacss-provisioning-service"
)

client.on_connect = on_connect
client.on_message = on_message

client.connect(settings.broker_host, settings.broker_port)
client.loop_forever()
