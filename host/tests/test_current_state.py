"""Tests for app.current_state (the current_status table).

upsert(conn, mac, fw)
- Runs the current_status upsert with (mac, fw) parameters.
- Swallows DB errors (logs, does not raise) so a hello is never lost to a
  write blip.

get_recent(conn)
- Maps rows to {mac: {firmware_version}}, with the mac normalised to the
  compact uppercase form the devices report.
- Returns {} on a DB error.
"""

import app.current_state as cs


# --- upsert -------------------------------------------------------------------

def test_upsert_runs_upsert(cursor_conn):
    conn, cur = cursor_conn
    cs.upsert(conn, "MAC", "v1.0")
    cur.execute.assert_called_once()
    sql, params = cur.execute.call_args.args
    assert "INSERT INTO repacss_environment.current_status" in sql
    assert "ON CONFLICT (mac) DO UPDATE" in sql
    assert params == ("MAC", "v1.0")


def test_upsert_swallows_error(cursor_conn):
    conn, cur = cursor_conn
    cur.execute.side_effect = Exception("boom")
    cs.upsert(conn, "MAC", "v1.0")  # must not raise


# --- get_recent ---------------------------------------------------------------

def test_get_recent_maps_rows(cursor_conn):
    conn, cur = cursor_conn
    cur.fetchall.return_value = [("MAC1", "v0.0.2"), ("MAC2", "v0.0.3")]
    assert cs.get_recent(conn) == {
        "MAC1": {"firmware_version": "v0.0.2"},
        "MAC2": {"firmware_version": "v0.0.3"},
    }


def test_get_recent_queries_the_right_schema(cursor_conn):
    # Guards the repacss_enviroment typo that made this silently return {}.
    conn, cur = cursor_conn
    cur.fetchall.return_value = []
    cs.get_recent(conn)
    sql = cur.execute.call_args.args[0]
    assert "repacss_environment.current_status" in sql


def test_get_recent_returns_empty_on_error(cursor_conn):
    conn, cur = cursor_conn
    cur.execute.side_effect = Exception("db down")
    assert cs.get_recent(conn) == {}
