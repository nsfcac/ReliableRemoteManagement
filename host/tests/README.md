# Host app test suite

Unit tests for every function in `host/app/`. No live database or MQTT broker is
needed: the database driver (`psycopg`) and the MQTT client (`paho`) are mocked, so
the suite is fast and deterministic.

## Running

From the `host/` directory:

```bash
python -m venv .venv && source .venv/bin/activate
pip install -r requirements-dev.txt
pytest
```

`pytest.ini` sets `pythonpath = .` so `import app` resolves, and points `testpaths`
at `tests/`.

## How isolation works

The service modules run side effects at import time: they call `db_connect()`, and
`provisioning_service` also builds an MQTT client and calls `loop_forever()`.
`tests/conftest.py` imports the modules once with `app.db.connect` and
`paho.mqtt.client.Client` patched, so importing them touches nothing real. Each test
then injects a mock connection (the `cursor_conn` fixture) or patches a helper, so
functions are exercised in isolation.

Shared fixtures (in `conftest.py`):
- `make_msg(topic, payload)` builds a stand-in paho message with `.topic`/`.payload`.
- `cursor_conn` yields `(conn, cur)` mocks that behave as `with conn:` and
  `with conn.cursor() as cur:`. Their `__exit__` returns `False`, so exceptions are
  not suppressed and each function's own error path runs as it would in production.

## Coverage by function

### `app/config.py`
| Function | Case |
|---|---|
| `Settings.database_dsn` | assembles a correct `postgresql://user:pass@host:port/name` URL |
| `Settings.db_password` | `SecretStr` is masked in repr/str; real value only via `get_secret_value()` |
| `Settings.broker_port` | a string port is validated and coerced to `int` |

### `app/db.py`
| Function | Case |
|---|---|
| `connect` | opens `psycopg.connect(settings.database_dsn)` and returns the connection |
| `connect` | a driver error is wrapped in `RuntimeError` (chained), never swallowed |

### `app/provisioning_service.py`
| Function | Cases |
|---|---|
| `is_enabled` | `Primary`/`Standby` -> True; any other role -> False |
| `build_config` | full config dict with `configured=True` and `enabled` from `is_enabled`; missing device key raises `KeyError` |
| `get_device_map` | maps rows to `{mac: {rack_id, role, enabled}}`; returns `{}` on DB error |
| `get_device_state` | maps rows to `{mac: {firmware_version}}`; returns `{}` on DB error |
| `upsert_device_state` | runs the `current_status` upsert with `(mac, fw)`; swallows DB errors (no raise) |
| `cache_device_map` | fetches on first call, serves cache within the TTL, refetches after it |
| `on_connect` | subscribes to the hello topic |
| `on_message` | known device -> upsert + publish config; unknown device -> `configured:false` / `unknown mac`; invalid JSON -> no-op |

### `app/ingestion_service.py`
| Function | Cases |
|---|---|
| `on_connect` | subscribes to the four device topics (telemetry/status/event/ack) |
| `is_registered` | True when a row exists, False otherwise |
| `rack_filter` | returns the rack_id when found, None when missing |
| `handle_telemetry` | inserts one telemetry row with the given parameters |
| `handle_status` / `handle_event` / `handle_ack` | raise `NotImplementedError` (documents current state) |
| `on_message` | unregistered -> ignored; telemetry -> one `handle_telemetry` per sensor reading; unknown type -> warns; invalid JSON -> ignored; no `sensors` item -> raises `StopIteration` (documented sharp edge) |

## Notes surfaced by the tests (not fixed here)
- `ingestion_service.on_message` uses an unguarded `next(...)` to find the `sensors`
  item, so a telemetry payload without one raises `StopIteration`. The test documents
  this; guarding it with a default is a small follow-up.
- `handle_status`/`handle_event`/`handle_ack` are placeholders; their tests assert the
  current `NotImplementedError` and should be updated when the handlers are written.
