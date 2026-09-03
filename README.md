# Reliable Remote Management of the REPACSS Cluster

Rack-level environmental telemetry and remote firmware management for the REPACSS
datacenter. Each rack runs **two ESP32 controllers** that read intake/exhaust
temperatures and publish them over **wired Ethernet (W5500) using MQTT** to a
Radxa host, which provisions the devices, ingests telemetry into a time-series
database, and can update firmware over the air.

> **If you worked from an older version of this doc:** the design has changed.
> Transport is **MQTT, not UDP**. There are **no SPDT switches, relays, or
> sensor-bus switching** (removed in #26) and **no Wi-Fi failover**. Controllers
> are now **Primary/Standby** (not "A/B"), run **one unified firmware image**,
> and are given their identity **by the host**. See "Changed from the original
> design" at the bottom.

---

## Architecture at a glance

```
  ESP32 Primary ─┐                      Radxa host                 DB host
         |       │  W5500 Ethernet   ┌──────────────────┐      ┌──────────────┐
Heartbeat|       ├── MQTT (1883) ───►│ Mosquitto broker │      │ TimescaleDB  │
  ESP32 Standby ─┘   HTTP (8080)     │ provisioning svc │─────►│ (PostgreSQL  │
        │                            │ ingestion svc    │      │  17 + TS ext)│
        └──────────────-─────────────│ fw-server (nginx)│      └──────────────┘
                                     └──────────────────┘
```

- **Two controllers per rack, Primary + Standby.** Only the active controller
  publishes telemetry. They watch each other over a UART **heartbeat**; if the
  Primary goes silent (no beat for 2000 ms) the Standby takes over. Failover is
  edge-triggered and reported as an MQTT event.
- **Unified firmware.** Both controllers flash the same binary. Role
  (Primary/Standby), rack, and enable state come from the host at runtime, so
  scaling the fleet needs **no firmware changes**, only database rows.
- **Host services** (Python) run in Docker: provisioning (assigns identity),
  ingestion (stores telemetry + events), and fw-server (serves OTA binaries).

---

## Repository layout

| Path | What's in it |
|---|---|
| `ESP32-Firmware/` | PlatformIO firmware (unified image). Key modules: `TelemetrySender` (Ethernet+MQTT), `Heartbeat`, `TemperatureBus`, `RuntimeConfig`, `OtaUpdater`. |
| `host/app/` | Services: `provisioning_service.py`, `ingestion_service.py`, shared `config.py` / `db.py` / `device_registry.py`. |
| `host/tests/` | pytest suite for the host services (DB + MQTT mocked). |
| `db/` | `init.sql` (schema) and `compose.yaml` (TimescaleDB). |
| `host/fw-server/` | nginx container that serves firmware `.bin` files for OTA. |
| `docs/mqtt-contract.md` | **Source of truth** for the MQTT messages. Both firmware and host must obey it. |

---

## How it works

### Provisioning (hello → config)
1. A device boots **unconfigured** and publishes `hello` every 10 s to
   `repacss/devices/<mac>/hello` (with its firmware version).
2. The provisioning service looks the MAC up in `device_map`.
3. It replies with a **non-retained** `config` on `repacss/devices/<mac>/config`
   carrying `rack_id`, `role`, and `enabled`.
4. The device validates and applies it, then stops helloing. An invalid config
   is rejected and reported as a `config_rejected` event.

Identity is host-assigned; the device only knows its own MAC. See the
[MQTT contract](docs/mqtt-contract.md) for exact fields and validation rules.

### Telemetry
The active controller publishes JSON every **5 s** to
`repacss/devices/<mac>/telemetry`: a heartbeat item (Primary/Standby liveness)
and a sensors item (intake + exhaust buses, 3 sensors each). Ingestion writes
**one row per sensor reading** into the `telemetry` hypertable, resolving
`rack_id` from the registry. Missing/failed sensors are stored as `NULL`.

### Failover and events
The Standby watches the Primary's heartbeat and, on the healthy→down edge,
takes over sending and publishes `primary_down` (and `primary_up` on recovery)
to `repacss/devices/<mac>/event`. Config rejections publish `config_rejected`.
Ingestion records these in the `events` hypertable with rack and role filled in
from the registry.

### OTA (over-the-air firmware update)
Pull-based: the host serves a `.bin` over HTTP (fw-server) and the device
fetches it, streaming straight into flash while computing SHA-256. The image is
committed **only if the hash matches**; a bad download never becomes bootable.

Because the stock Arduino bootloader ships **without**
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, rollback is done **in software**: a
freshly flashed image is put on probation in NVS and must reach MQTT within
60 s (or it reverts to the previous slot). This is proven on hardware
(Phase 2, #31). The MQTT-driven, fleet-automated phases are planned (#37, #38).

---

## MQTT topics (summary)

All rooted at `repacss`. `<mac>` is uppercase hex, no separators (`ECE3347C07D0`).
The device generates every topic from its own MAC; the host does not send topic
strings.

| Topic | Direction | Purpose |
|---|---|---|
| `repacss/devices/<mac>/hello` | device → host | announce (unconfigured) |
| `repacss/devices/<mac>/config` | host → device | assign identity |
| `repacss/devices/<mac>/telemetry` | device → host | sensor + heartbeat data |
| `repacss/devices/<mac>/event` | device → host | failover, config_rejected |

Full field-level contract, validation rules, and payload examples:
[`docs/mqtt-contract.md`](docs/mqtt-contract.md).

---

## Database

PostgreSQL 17 + TimescaleDB, schema `repacss_environment` (see
[`db/init.sql`](db/init.sql)). Lives on its own host in production.

| Table | Role |
|---|---|
| `device_map` | registry: mac → rack_id, role, enabled, retired (source of truth for identity) |
| `telemetry` | hypertable, one row per sensor reading (ts, mac, rack_id, bus, sensor_index, temperature_celsius) |
| `events` | hypertable of device events (failover, config_rejected, …) |
| `current_status` | latest per-device state (alive, last_seen, firmware version) |
| `alerts` | alert history + rate-limit state |
| `unknown_devices` | telemetry from unregistered MACs (first/last seen, 24 h count) |

`telemetry` and `events` are **compressed** (after 7 and 30 days). No retention
policy is set, so raw history is kept; the target is at least **1 year** of
5-second data, provisioned around **1 TB** of storage. `rack_id` is free-form
text (rack codenames like `rpg93`).

---

## Running it (local/dev)

- **Database:** `docker compose up -d` in `db/`, then load `init.sql`.
- **Broker + services:** the `host/` compose stack runs Mosquitto, provisioning,
  and ingestion. They read connection settings from `.env` via `pydantic-settings`.
- **fw-server:** `docker compose up -d` in `host/fw-server/` (serves `bin/*.bin`
  on port 8080).
- **Firmware:** `pio run` in `ESP32-Firmware/` (env `esp32_a`); version is
  injected from the git tag by `version.py`.

Watch MQTT traffic:
```bash
docker exec -it mosquitto mosquitto_sub -h localhost -t "repacss/#" -v
```

---

## Status & roadmap

- **Done:** unified firmware + role-based failover (#26), MQTT provisioning +
  telemetry, host DB pipeline (provisioning, ingestion, events), TimescaleDB
  schema + compression, **OTA Phase 2** (verified HTTP OTA + software rollback, #31).
- **Next:** `enabled:false` remote disable (#19), config-MAC verification (#18),
  **OTA Phase 3** MQTT control plane (#37) and **Phase 4** automated rollout (#38).
- **Planned:** out-of-band alerting (Prometheus + Alertmanager, #23/#39), commanded
  rollback for healthy-but-misbehaving images (#32/#33).

---

## Changed from the original design

| Old (early spec) | Now |
|---|---|
| UDP JSON | MQTT (PubSubClient / Mosquitto) |
| Controller A / B | Primary / Standby, host-assigned role |
| Per-controller firmware | one unified image |
| SPDT switches, relays, sensor-bus switching | removed (#26); two fixed buses (intake/exhaust) per esp32|
| Wi-Fi failover | none; wired Ethernet only (expecting wifi v2.0.0)|
| Device-chosen identity | host-assigned via `device_map` |
| Bus names `cool` | `inlet` / `exhaust` |
