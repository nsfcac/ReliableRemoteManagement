"""Tests for app.provisioning_service.

Functions covered and their cases:

build_config(mac, device)
- Returns the full config dict, with enabled taken straight from the registry
  row (the old is_enabled role filter is gone: device_map.enabled is the truth).
- Missing a required device key raises KeyError (surfaces bad registry data).

on_connect(...)
- Subscribes to the hello topic.

on_message(...)
- Known device -> upserts current_status and publishes a config.
- Unknown device -> records it in unknown_devices and publishes NOTHING.
  The silence is intended: the device stays unconfigured and keeps helloing,
  and the host keeps a record instead of replying.
- Invalid JSON -> does nothing (no upsert, no publish).
- Oversized payload -> dropped before parsing.

The per-table SQL lives in app.current_state / app.unknown_devices /
app.device_registry and is tested in their own files.
"""

import json
from unittest.mock import MagicMock

import pytest

import app.provisioning_service as prov


# --- build_config -------------------------------------------------------------

def test_build_config_full_shape():
    # rack_id is free-form text (rack codenames like "rpg93"), not a number.
    cfg = prov.build_config("MAC", {"rack_id": "rpg93", "role": "Primary", "enabled": True})
    assert cfg == {
        "message_type": "config",
        "mac": "MAC",
        "configured": True,
        "enabled": True,
        "rack_id": "rpg93",
        "role": "Primary",
    }


def test_build_config_passes_enabled_false_through():
    # enabled:false is a valid applied config (remote disable), not a rejection.
    cfg = prov.build_config("MAC", {"rack_id": "rpg93", "role": "Standby", "enabled": False})
    assert cfg["enabled"] is False
    assert cfg["configured"] is True


def test_build_config_missing_key_raises_keyerror():
    with pytest.raises(KeyError):
        prov.build_config("MAC", {"role": "Primary", "enabled": True})  # no rack_id


# --- on_connect ---------------------------------------------------------------

def test_on_connect_subscribes_to_hello():
    client = MagicMock()
    prov.on_connect(client, None, None, 0, None)
    client.subscribe.assert_called_once_with("repacss/devices/+/hello", qos=1)


# --- on_message ---------------------------------------------------------------

def test_on_message_known_device_publishes_config(monkeypatch, make_msg):
    upsert = MagicMock()
    monkeypatch.setattr(prov.current_state, "upsert", upsert)
    monkeypatch.setattr(
        prov.device_registry, "lookup",
        MagicMock(return_value={"rack_id": "rpg93", "role": "Primary", "enabled": True}),
    )
    client = MagicMock()
    msg = make_msg(
        "repacss/devices/ECE3347C07D0/hello",
        {"message_type": "hello", "mac": "ECE3347C07D0", "firmware_version": "v0.0.2"},
    )

    prov.on_message(client, None, msg)

    assert upsert.call_args.args[1:] == ("ECE3347C07D0", "v0.0.2")
    client.publish.assert_called_once()
    topic, payload = client.publish.call_args.args[0], client.publish.call_args.args[1]
    assert topic == "repacss/devices/ECE3347C07D0/config"
    body = json.loads(payload)
    assert body["configured"] is True
    assert body["role"] == "Primary"
    assert body["rack_id"] == "rpg93"


def test_on_message_unknown_device_records_and_stays_silent(monkeypatch, make_msg):
    monkeypatch.setattr(prov.current_state, "upsert", MagicMock())
    monkeypatch.setattr(prov.device_registry, "lookup", MagicMock(return_value=None))
    record = MagicMock()
    monkeypatch.setattr(prov.unknown_devices, "record", record)
    client = MagicMock()
    payload = {"message_type": "hello", "mac": "AABBCCDDEEFF"}
    msg = make_msg("repacss/devices/AABBCCDDEEFF/hello", payload)

    prov.on_message(client, None, msg)

    # Recorded with the raw payload text, and deliberately NOT answered:
    # the device keeps helloing rather than being told it is unknown.
    record.assert_called_once()
    assert record.call_args.args[1] == "AABBCCDDEEFF"
    assert json.loads(record.call_args.args[2]) == payload
    client.publish.assert_not_called()


def test_on_message_invalid_json_does_nothing(monkeypatch, make_msg):
    upsert = MagicMock()
    monkeypatch.setattr(prov.current_state, "upsert", upsert)
    client = MagicMock()
    msg = make_msg("repacss/devices/ECE3347C07D0/hello", "not-json{")

    prov.on_message(client, None, msg)

    client.publish.assert_not_called()
    upsert.assert_not_called()


def test_on_message_oversized_payload_dropped(monkeypatch, make_msg):
    upsert = MagicMock()
    monkeypatch.setattr(prov.current_state, "upsert", upsert)
    client = MagicMock()
    msg = make_msg("repacss/devices/ECE3347C07D0/hello", "x" * (prov.MAX_HELLO_BYTES + 1))

    prov.on_message(client, None, msg)

    upsert.assert_not_called()
    client.publish.assert_not_called()
