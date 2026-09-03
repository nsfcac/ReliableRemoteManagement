# MQTT Contract — REPACSS Rack Telemetry

## 1. Purpose & scope
Single source of truth for the MQTT messages exchanged between the ESP32 edge
devices and the Radxa host services. **Both sides must obey this document.**
Firmware: `ESP32-Firmware/`. Host: `host/`.


---

## 2. Message flow (overview)

1. Device boots unconfigured → publishes `hello` (repeats every HELLO_INTERVAL_MS)
2. Host (provisioning service) receives hello → looks up MAC in registry
3. Host publishes `config` to the device's config topic
4. Device validates + applies config → stops helloing
5. Device (if configured, enabled, and Primary) publishes `telemetry`
6. Host (ingestion service) receives telemetry → resolves identity by MAC → stores


---

## 3. Topics

All topics are rooted at the base prefix `repacss` (firmware: `BASE_TOPIC`).

| Topic | Direction | Publisher | Subscriber | Retained? |
|---|---|---|---|---|
| `repacss/devices/<mac>/hello`   | device → host | ESP32 | provisioning svc | no |
| `repacss/devices/<mac>/config`  | host → device | provisioning svc | ESP32 | no |
| `repacss/devices/<mac>/telemetry` | device → host | ESP32 (active controller) | ingestion svc | no |
| `repacss/devices/<mac>/event`  | device → host | ESP32 | ingestion svc | no |
| `repacss/devices/<mac>/status` | device → host | ESP32 | ingestion svc | *reserved / future* |

- `<mac>` — see MAC format in §4.
- **All topics are keyed by `<mac>`, not `<rack_id>`.** The device generates
  every topic from its own MAC (firmware globals `gHelloTopic`, `gConfigTopic`,
  `gTelemetryTopic`, `gEventTopic`); the host does not send topic strings. The
  host resolves rack and role from the MAC via the registry on the receiving
  side. (`rack_id` is still delivered in the config for the device to report,
  it just is not part of any topic path.)
- `event` is **implemented**: failover (`primary_down` / `primary_up`) and
  `config_rejected`. `status` remains reserved.

> Trade-off:
> Retained means a rebooting device gets its config immediately on subscribe
> (before it even hellos); non-retained means it always waits for a fresh reply. <br>
> We chose the hello-triggered model (non-retained config) for debuggability.

---

## 4. Config message (host → device)

Topic: `repacss/devices/<mac>/config`

| Field | Type | Required | Meaning |
|---|---|---|---|
| `message_type` | string | yes | must be `"config"` |
| `mac` | string | yes | target device MAC; device rejects if it ≠ its own |
| `configured` | bool | yes | device is provisioned (must be `true` to be accepted) |
| `enabled` | bool | yes | device may transmit; `false` = provisioned but silent (remote off, e.g. OTA) |
| `rack_id` | string | yes | rack identity (free-form text, e.g. `rpg93`); reported by the device, not used in topic paths |
| `role` | string | yes | `"Primary"` or `"Standby"` — drives sender selection (failover) |

**Role vocabulary (exact strings):** `Primary` | `Standby`
(firmware `parseRole()` accepts these two; anything else → `Unknown` → rejected.)

**MAC format:** uppercase hex, no separators, 12 chars — e.g. `AABBCCDDEEFF`.
Firmware builds this via `macAddrCompact`; the host must match exactly so the
device's `cfg.mac == gMacSafe` self-check compares equal.

**Validation rules (what makes a config rejected):**
Evaluated on the device; a rejected config is logged and NOT applied.

- `message_type` != `"config"` → REJECT
- `mac` != this device's own MAC → REJECT (config wasn't meant for us)
- `configured` != `true` → REJECT (device is not provisioned)
- `rack_id` missing or empty → REJECT
- `role` not in {`Primary`, `Standby`} → REJECT
- `enabled` — **not** a reject condition. Must be a boolean; `false` means the
  config is applied but the device stays silent (remote disable). Implemented
  in #19; before that the firmware rejected it and kept helloing.

**Example — valid config:**
This doubles as the string you'll `mosquitto_pub` when testing, and the fixture
the firmware tests (#14) assert against — so it must be valid JSON.

```json
{
  "message_type": "config",
  "mac": "AABBCCDDEEFF",
  "configured": true,
  "enabled": true,
  "rack_id": "rack01",
  "role": "Primary"
}
```

> Note: `configured` is host-decided for now. A future revision may let the ESP32
> own that decision via a dedicated topic — tracked as an open item, not v1.

---

## 5. Hello message (device → host)

Topic: `repacss/devices/<mac>/hello`

| Field | Type | Meaning |
|---|---|---|
| `message_type` | string | `"hello"` |
| `mac` | string | announcing device's MAC (same format as §4) |
| `firmware_version` | string | running image version, from the git tag (e.g. `v0.0.2-19-gef833c6`) |

- **Cadence:** every `HELLO_INTERVAL_MS` (currently 10 s) while unconfigured; stops once configured.
- **Retained:** no.

**Example:**

```json
{
  "message_type": "hello",
  "mac": "AABBCCDDEEFF",
  "firmware_version": "v0.0.2-19-gef833c6"
}
```

---

## 6. Telemetry message (device → host)

Topic: `repacss/devices/<mac>/telemetry`

- **Identity key is the `mac`** (in both the topic and the payload). The host
  resolves rack/role/etc. from the **MAC → registry map** at ingest time.
  Telemetry does NOT need to carry `rack_id`/`role`.
- **Cadence:** 5 s per rack. **Retained:** no.
- Only the **active** controller publishes (Primary normally; Standby after failover).

**Example — authoritative shape** (matches `buildTelemetryJson` in
`ESP32-Firmware/src/main.cpp`; keep this in sync with that function):

```json
{
  "message_type": "telemetry",
  "device": {
    "mac": "AABBCCDDEEFF"
  },
  "timestamp_device_ms": 123456,
  "items": [
    {
      "kind": "heartbeat",
      "primary_alive": true,
      "standby_alive": true
    },
    {
      "kind": "sensors",
      "buses": [
        {
          "bus": "inlet",
          "temperatures_c": [21.23, 22.00, null]
        },
        {
          "bus": "exhaust",
          "temperatures_c": [30.10, 30.50, 31.00]
        }
      ]
    }
  ]
}
```

> The telemetry payload carries only `heartbeat` and `sensors` items. Failover
> and other events are published separately on `repacss/devices/<mac>/event`
> (see §6b), not embedded in telemetry.

### 6b. Event message (device → host)

Topic: `repacss/devices/<mac>/event`. Non-retained. Published on a state change,
not on a cadence.

```json
{ "message_type": "event", "event_type": "primary_down", "details": "Standby took over after Primary heartbeat timeout" }
```

| `event_type` | When |
|---|---|
| `primary_down` | Standby stops seeing the Primary's heartbeat and takes over |
| `primary_up` | Primary's heartbeat returns |
| `config_rejected` | a received config failed validation (`details` = reason) |

The payload carries only `event_type` and `details`; the host fills in `rack_id`
and `role` from the registry when it writes the `events` row.

---

## 7. Identity resolution (host side)

The host is the source of truth for identity. `device_map` (Postgres, table
`repacss_environment.device_map`) maps **MAC → (rack_id, role, enabled)**. Both
services resolve identity from the MAC in the topic/payload via a shared,
cached registry (`app/device_registry.py`, TTL cache with bounded
refresh-on-miss). The `config` message is just that registry projected onto one
device. A MAC not in `device_map` is unknown: provisioning records it in
`unknown_devices` and deliberately sends **no reply**, so the device stays
unconfigured and keeps helloing. Ingestion drops unknown data (recording it
there is still open).

---

## 8. Ownership summary

> TODO: one short table/paragraph. The principle the team settled on:
>   - **Host owns identity** — the MAC → rack/role mapping.
>   - **Firmware owns the topic convention** — builds topics from identity.
> Each config field has exactly one job (mac=addressing, configured/enabled=gates,
> rack_id=topic, role=failover). Worth stating so future-you doesn't add fields
> without a purpose.

---

## 9. Versioning & change policy

> TODO: how schema changes are made. Since firmware + host live in one repo,
> a contract change should be a single commit touching both sides + this doc.
> Note whether you'll add a `config_version` / `schema_version` field later
> (relates to #13 config_ack).

---

## 10. Open items

- [x] `event` topic implemented — failover (`primary_down`/`primary_up`) and `config_rejected`
- [ ] `config_ack` on `repacss/devices/<mac>/ack` — see #13
- [ ] align `host/docs/example.md` + firmware to this doc — see #11
- [x] firmware: config `mac` is checked against the device's own MAC (§4) — #18
- [x] firmware: `enabled: false` is accepted and applied, so remote disable works — #19
- [ ] OTA command/status topics (`ota`, `ota/status`) — Phase 3, see #37
- [ ] firmware: drop `topics{}` parsing, build the telemetry topic locally from `rack_id`
