"""Tests for app.device_registry.

Functions covered and their cases:

get_device_map(conn)
- Maps DB rows to {mac: {rack_id, role, enabled}}.
- Returns {} on a DB error (never raises).

cache_device_map(conn)
- Fetches on first call, serves cached within the TTL, refetches after the TTL.

lookup(conn, mac)
- Hit -> returns the device dict from cache with no extra fetch.
- Miss on a stale-enough cache -> one refresh, then re-check (recognises a
  just-provisioned device).
- Miss right after a refresh (within the cooldown) -> does NOT refresh again,
  so an unknown device spamming messages cannot force a query per message.

The cache module holds process-global state (_device_map / _device_map_ts), so
each test resets it explicitly rather than relying on order.
"""

from unittest.mock import MagicMock

import app.device_registry as reg


def _reset(monkeypatch, ts=0.0, cached=None):
    monkeypatch.setattr(reg, "_device_map", cached if cached is not None else {})
    monkeypatch.setattr(reg, "_device_map_ts", ts)


# --- get_device_map -----------------------------------------------------------

def test_get_device_map_maps_rows(cursor_conn):
    conn, cur = cursor_conn
    cur.fetchall.return_value = [
        ("MAC1", "rpg93", "Primary", True),
        ("MAC2", "rpg93", "Standby", False),
    ]
    assert reg.get_device_map(conn) == {
        "MAC1": {"rack_id": "rpg93", "role": "Primary", "enabled": True},
        "MAC2": {"rack_id": "rpg93", "role": "Standby", "enabled": False},
    }


def test_get_device_map_returns_empty_on_error(cursor_conn):
    conn, cur = cursor_conn
    cur.execute.side_effect = Exception("db down")
    assert reg.get_device_map(conn) == {}


# --- cache_device_map ---------------------------------------------------------

def test_cache_device_map_ttl_behaviour(monkeypatch):
    fetch = MagicMock(side_effect=[{"a": 1}, {"b": 2}])
    monkeypatch.setattr(reg, "get_device_map", fetch)
    _reset(monkeypatch)

    clock = {"now": 1000.0}
    monkeypatch.setattr(reg.time, "monotonic", lambda: clock["now"])

    # First call: cache is stale (ts 0) -> fetch.
    assert reg.cache_device_map(None) == {"a": 1}
    assert fetch.call_count == 1

    # Within the TTL -> cached, no refetch.
    clock["now"] = 1000.0 + reg._DEVICE_MAP_TTL - 1
    assert reg.cache_device_map(None) == {"a": 1}
    assert fetch.call_count == 1

    # Past the TTL -> refetch.
    clock["now"] = 1000.0 + reg._DEVICE_MAP_TTL + 1
    assert reg.cache_device_map(None) == {"b": 2}
    assert fetch.call_count == 2


# --- lookup -------------------------------------------------------------------

def test_lookup_hit_no_fetch(monkeypatch):
    fetch = MagicMock()
    monkeypatch.setattr(reg, "get_device_map", fetch)
    # Fresh cache (ts = now) holding the device.
    now = 5000.0
    monkeypatch.setattr(reg.time, "monotonic", lambda: now)
    _reset(monkeypatch, ts=now, cached={"MAC": {"rack_id": "rpg93", "role": "Primary"}})

    assert reg.lookup(None, "MAC") == {"rack_id": "rpg93", "role": "Primary"}
    fetch.assert_not_called()


def test_lookup_miss_refreshes_when_stale_enough(monkeypatch):
    # Cache is fresh for the TTL but older than the miss cooldown, and does not
    # hold the device. A refresh brings it in (just-provisioned device).
    now = 5000.0
    monkeypatch.setattr(reg.time, "monotonic", lambda: now)
    _reset(monkeypatch, ts=now - reg._MISS_REFRESH_COOLDOWN - 1, cached={})

    fetch = MagicMock(return_value={"NEW": {"rack_id": "rpg93", "role": "Primary"}})
    monkeypatch.setattr(reg, "get_device_map", fetch)

    assert reg.lookup(None, "NEW") == {"rack_id": "rpg93", "role": "Primary"}
    fetch.assert_called_once()


def test_lookup_miss_within_cooldown_does_not_refresh(monkeypatch):
    # Cache was refreshed just now and still lacks the mac: an unknown device
    # must not force a refresh on every message.
    now = 5000.0
    monkeypatch.setattr(reg.time, "monotonic", lambda: now)
    _reset(monkeypatch, ts=now, cached={})

    fetch = MagicMock()
    monkeypatch.setattr(reg, "get_device_map", fetch)

    assert reg.lookup(None, "UNKNOWN") is None
    fetch.assert_not_called()
