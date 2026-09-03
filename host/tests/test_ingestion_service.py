"""Tests for app.ingestion_service.

Functions covered and their cases:

on_connect(...)
- Subscribes to the four device topics (telemetry, status, event, ack).

(Registry lookups now go through app.device_registry.lookup; see test_device_registry.py.)

handle_telemetry(conn, ...)
- Inserts one telemetry row with the given parameters.
- A DB error is logged, not raised (a raise would be swallowed by paho).

handle_event(conn, ...)
- Inserts one events row with the given parameters.
- Null details is stored as null.
- A DB error is logged, not raised.

handle_status / handle_ack
- Still placeholders that raise NotImplementedError (documents current state).

on_message(...)
- Unregistered device (lookup -> None) -> ignored (no telemetry handling).
- Telemetry -> one handle_telemetry call per sensor reading across all buses.
- Event -> pulls rack_id and role from the looked-up device, then one handle_event call.
- Event without event_type -> skipped, no handle_event call.
- Unknown message type -> warns.
- Invalid JSON -> ignored.
- SHARP EDGE: telemetry payload with no 'sensors' item currently raises
  StopIteration (unguarded next(...)); documented so it is not lost.
"""

from unittest.mock import MagicMock

import pytest

import app.ingestion_service as ing


# --- on_connect ---------------------------------------------------------------

def test_on_connect_subscribes_all_device_topics():
    client = MagicMock()
    ing.on_connect(client, None, None, 0, None)
    subscribed = {call.args[0] for call in client.subscribe.call_args_list}
    assert subscribed == {
        "repacss/devices/+/telemetry",
        "repacss/devices/+/status",
        "repacss/devices/+/event",
        "repacss/devices/+/ack",
    }


# --- handle_telemetry ---------------------------------------------------------

def test_handle_telemetry_inserts_row(cursor_conn):
    conn, cur = cursor_conn
    ing.handle_telemetry(conn, "2026-01-01T00:00:00Z", "MAC", 1, "inlet", 0, 25.5)
    cur.execute.assert_called_once()
    sql, params = cur.execute.call_args.args
    assert "INSERT INTO repacss_environment.telemetry" in sql
    assert params == ("2026-01-01T00:00:00Z", "MAC", 1, "inlet", 0, 25.5)


# --- unimplemented handlers ---------------------------------------------------

@pytest.mark.parametrize("fn_name", ["handle_status", "handle_ack"])
def test_unimplemented_handlers_raise(fn_name):
    with pytest.raises(NotImplementedError):
        getattr(ing, fn_name)("MAC", {})


# --- handle_event -------------------------------------------------------------

def test_handle_event_inserts_row(monkeypatch, cursor_conn):
    conn, cur = cursor_conn
    ing.handle_event(conn, "2026-01-01T00:00:00Z", "MAC", "primary_down",
                     "Standby took over", "rpg93", "Standby")
    cur.execute.assert_called_once()
    sql, params = cur.execute.call_args.args
    assert "INSERT INTO repacss_environment.events" in sql
    assert "VALUES (%s, %s, %s, %s, %s, %s)" in sql
    assert params == ("2026-01-01T00:00:00Z", "MAC", "primary_down",
                      "Standby took over", "rpg93", "Standby")


def test_handle_event_null_details(monkeypatch, cursor_conn):
    conn, cur = cursor_conn
    ing.handle_event(conn, "2026-01-01T00:00:00Z", "MAC", "primary_up",
                     None, "rpg93", "Standby")
    _, params = cur.execute.call_args.args
    assert params[3] is None


def test_handle_event_swallows_db_error(monkeypatch, cursor_conn):
    conn, cur = cursor_conn
    cur.execute.side_effect = Exception("boom")
    # Must log and return, not raise (a raise here is swallowed by paho).
    ing.handle_event(conn, "2026-01-01T00:00:00Z", "MAC", "primary_down",
                     "d", "rpg93", "Standby")


def test_on_message_event_looks_up_rack_role_and_inserts(monkeypatch, make_msg):
    monkeypatch.setattr(ing.device_registry, "lookup",
                        lambda conn, mac: {"rack_id": "rpg93", "role": "Standby"})
    he = MagicMock()
    monkeypatch.setattr(ing, "handle_event", he)

    msg = make_msg(
        "repacss/devices/MAC/event",
        {"message_type": "event", "event_type": "primary_down", "details": "took over"},
    )
    ing.on_message(None, None, msg)

    he.assert_called_once()
    args = he.call_args.args   # (conn, ts, mac, event_type, details, rack_id, role)
    assert args[2] == "MAC"
    assert args[3] == "primary_down"
    assert args[4] == "took over"
    assert args[5] == "rpg93"
    assert args[6] == "Standby"


def test_on_message_event_without_type_skips(monkeypatch, make_msg):
    monkeypatch.setattr(ing.device_registry, "lookup",
                        lambda conn, mac: {"rack_id": "rpg93", "role": "Standby"})
    he = MagicMock()
    monkeypatch.setattr(ing, "handle_event", he)

    msg = make_msg("repacss/devices/MAC/event", {"message_type": "event"})
    ing.on_message(None, None, msg)

    he.assert_not_called()


# --- on_message ---------------------------------------------------------------

def test_on_message_ignores_unregistered(monkeypatch, make_msg):
    monkeypatch.setattr(ing.device_registry, "lookup", lambda conn, mac: None)
    ht = MagicMock()
    monkeypatch.setattr(ing, "handle_telemetry", ht)
    msg = make_msg("repacss/devices/MAC/telemetry", {"items": []})

    ing.on_message(None, None, msg)

    ht.assert_not_called()


def test_on_message_telemetry_calls_handle_per_sensor(monkeypatch, make_msg):
    monkeypatch.setattr(ing.device_registry, "lookup",
                        lambda conn, mac: {"rack_id": 1, "role": "Primary"})
    ht = MagicMock()
    monkeypatch.setattr(ing, "handle_telemetry", ht)

    payload = {"items": [
        {"kind": "heartbeat"},
        {"kind": "sensors", "buses": [
            {"bus": "inlet", "temperatures_c": [25.0, None, 26.0]},
            {"bus": "exhaust", "temperatures_c": [30.0]},
        ]},
    ]}
    msg = make_msg("repacss/devices/MAC/telemetry", payload)

    ing.on_message(None, None, msg)

    # 3 inlet readings + 1 exhaust reading = 4 rows.
    assert ht.call_count == 4
    first = ht.call_args_list[0].args   # (conn, ts, mac, rack_id, bus, sensor_index, temp)
    assert first[2] == "MAC"
    assert first[3] == 1
    assert first[4] == "inlet"
    assert first[5] == 0
    assert first[6] == 25.0


def test_on_message_unknown_type_warns(monkeypatch, make_msg, capsys):
    monkeypatch.setattr(ing.device_registry, "lookup",
                        lambda conn, mac: {"rack_id": 1, "role": "Primary"})
    msg = make_msg("repacss/devices/MAC/wat", {"x": 1})

    ing.on_message(None, None, msg)

    assert "Unknown message type" in capsys.readouterr().out


def test_on_message_invalid_json_ignored(monkeypatch, make_msg):
    # Invalid JSON returns before any registry lookup, so no lookup patch needed.
    ht = MagicMock()
    monkeypatch.setattr(ing, "handle_telemetry", ht)
    msg = make_msg("repacss/devices/MAC/telemetry", "not-json{")

    ing.on_message(None, None, msg)

    ht.assert_not_called()


def test_on_message_telemetry_without_sensors_raises_stopiteration(monkeypatch, make_msg):
    # DOCUMENTS CURRENT BEHAVIOUR / SHARP EDGE: the unguarded next(...) that finds
    # the 'sensors' item raises StopIteration when there is no such item. Worth
    # guarding with a default in the code; captured here so the behaviour is explicit.
    monkeypatch.setattr(ing.device_registry, "lookup",
                        lambda conn, mac: {"rack_id": 1, "role": "Primary"})
    msg = make_msg("repacss/devices/MAC/telemetry", {"items": [{"kind": "heartbeat"}]})

    with pytest.raises(StopIteration):
        ing.on_message(None, None, msg)
