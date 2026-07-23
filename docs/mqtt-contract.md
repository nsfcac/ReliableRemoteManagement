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
| `repacss/racks/<rack_id>/telemetry` | device → host | ESP32 (active controller) | ingestion svc | no |
| `repacss/racks/<rack_id>/status` | device → host | ESP32 | ingestion svc | *reserved / future* |
| `repacss/racks/<rack_id>/event`  | device → host | ESP32 | ingestion svc | *reserved / future* |

- `<mac>` — see MAC format in §4.
- `<rack_id>` — assigned by the host in the config; the **device builds** the
  telemetry topic from it (host does not send topic strings).

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
| `rack_id` | string | yes | rack identity; device builds `repacss/racks/<rack_id>/telemetry` from it |
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
  config is applied but the device stays silent (remote disable). See §10 —
  current firmware still rejects `enabled: false` and must be changed to match.

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

- **Cadence:** every `HELLO_INTERVAL_MS` (currently 60 s) while unconfigured; stops once configured.
- **Retained:** no.

**Example:**

```json
{
  "message_type": "hello",
  "mac": "AABBCCDDEEFF"
}
```

---

## 6. Telemetry message (device → host)

Topic: `repacss/racks/<rack_id>/telemetry`

- **Identity key is the `mac` in the payload.** The topic carries `rack_id`, but
  the host resolves rack/role/etc. from the **MAC → registry map** at ingest time.
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
      "controller_a_alive": true,
      "controller_b_alive": true
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
    },
    {
      "kind": "event",
      "type": "failover",
      "occurred": false,
      "details": ""
    }
  ]
}
```

---

## 7. Identity resolution (host side)

> TODO(you): describe the MAC → (rack_id, role, …) registry that both services
> rely on. Where it lives now (in-memory dict) and where it's going (Postgres
> `device_map`). This is the host's source of truth; the config message is just
> the registry projected onto one device.

---

## 8. Ownership summary

> TODO(you): one short table/paragraph. The principle we settled on:
>   - **Host owns identity** — the MAC → rack/role mapping.
>   - **Firmware owns the topic convention** — builds topics from identity.
> Each config field has exactly one job (mac=addressing, configured/enabled=gates,
> rack_id=topic, role=failover). Worth stating so future-you doesn't add fields
> without a purpose.

---

## 9. Versioning & change policy

> TODO(you): how schema changes are made. Since firmware + host live in one repo,
> a contract change should be a single commit touching both sides + this doc.
> Note whether you'll add a `config_version` / `schema_version` field later
> (relates to #13 config_ack).

---

## 10. Open items

- [ ] `config_ack` on `repacss/devices/<mac>/ack` — see #13
- [ ] align `host/example.md` + firmware to this doc — see #11
- [ ] firmware: implement `cfg.mac == gMacSafe` self-check (not done yet — §4)
- [ ] firmware: accept `enabled: false` instead of rejecting it, so remote disable works — see #6
- [ ] firmware: drop `topics{}` parsing, build the telemetry topic locally from `rack_id`
