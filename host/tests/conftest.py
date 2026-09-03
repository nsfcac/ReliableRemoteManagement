"""Shared pytest fixtures and import-time setup for the app test suite.

The service modules (provisioning_service, ingestion_service) run side effects at
import time: they call db_connect() and, for provisioning, build an MQTT client and
call loop_forever(). We import them once here with those neutralised, so the whole
suite can import and unit-test their functions without a live database or broker.
"""

import importlib
import json
from unittest.mock import MagicMock, patch

import pytest

# Neutralise import-time side effects, then import the modules once (cached).
with patch("app.db.connect", return_value=MagicMock(name="conn")), \
     patch("paho.mqtt.client.Client", MagicMock(name="MQTTClient")):
    importlib.import_module("app.provisioning_service")
    importlib.import_module("app.ingestion_service")


@pytest.fixture
def make_msg():
    """Factory for a stand-in paho MQTTMessage exposing .topic and .payload (bytes)."""
    def _make(topic, payload):
        if isinstance(payload, (dict, list)):
            payload = json.dumps(payload)
        msg = MagicMock()
        msg.topic = topic
        msg.payload = payload.encode("utf-8") if isinstance(payload, str) else payload
        return msg
    return _make


@pytest.fixture
def cursor_conn():
    """A mock DB connection usable as ``with conn:`` and ``with conn.cursor() as cur:``.

    Returns ``(conn, cur)``. ``__exit__`` returns False so exceptions are NOT
    suppressed by the context managers, which lets each function's own try/except
    error path execute exactly as it would against a real connection.
    """
    cur = MagicMock(name="cursor")
    conn = MagicMock(name="conn")
    conn.__enter__.return_value = conn
    conn.__exit__.return_value = False
    cm = conn.cursor.return_value
    cm.__enter__.return_value = cur
    cm.__exit__.return_value = False
    return conn, cur
