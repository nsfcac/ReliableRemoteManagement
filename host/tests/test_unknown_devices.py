"""Tests for app.unknown_devices (the unknown_devices table).

record(conn, mac, payload_txt)
- Upserts one row per mac: inserts on first sight, bumps hit_count and
  last_seen on repeat.
- Truncates the stored payload to _MAX_PAYLOAD_CHARS so a chatty or hostile
  device cannot write unbounded text.
- Passes mac and payload as query parameters, never interpolated (the mac
  comes off the MQTT topic and is untrusted).
- Swallows DB errors (logs, does not raise).
"""

import app.unknown_devices as ud


def test_record_upserts_row(cursor_conn):
    conn, cur = cursor_conn
    ud.record(conn, "AABBCCDDEEFF", '{"message_type":"hello"}')
    cur.execute.assert_called_once()
    sql, params = cur.execute.call_args.args
    assert "INSERT INTO repacss_environment.unknown_devices" in sql
    assert "ON CONFLICT (mac) DO UPDATE" in sql
    assert params == ("AABBCCDDEEFF", '{"message_type":"hello"}')


def test_record_bumps_hit_count_on_conflict(cursor_conn):
    conn, cur = cursor_conn
    ud.record(conn, "AABBCCDDEEFF", "{}")
    sql = cur.execute.call_args.args[0]
    assert "hit_count = unknown_devices.hit_count + 1" in sql


def test_record_truncates_payload(cursor_conn):
    conn, cur = cursor_conn
    ud.record(conn, "AABBCCDDEEFF", "x" * (ud._MAX_PAYLOAD_CHARS + 500))
    _, params = cur.execute.call_args.args
    assert len(params[1]) == ud._MAX_PAYLOAD_CHARS


def test_record_uses_parameters_not_interpolation(cursor_conn):
    # The mac comes off the MQTT topic and is untrusted, so it must never be
    # baked into the SQL string.
    conn, cur = cursor_conn
    ud.record(conn, "'; DROP TABLE unknown_devices; --", "{}")
    sql, params = cur.execute.call_args.args
    assert "DROP TABLE" not in sql
    assert params[0] == "'; DROP TABLE unknown_devices; --"


def test_record_swallows_error(cursor_conn):
    conn, cur = cursor_conn
    cur.execute.side_effect = Exception("boom")
    ud.record(conn, "AABBCCDDEEFF", "{}")  # must not raise
