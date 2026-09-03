CREATE SCHEMA IF NOT EXISTS repacss_environment;
CREATE TABLE IF NOT EXISTS repacss_environment.device_map (
    mac macaddr PRIMARY KEY,
    rack_id text NOT NULL,
    role text NOT NULL,
    enabled boolean NOT NULL,
    retired boolean NOT NULL DEFAULT false
);

CREATE TABLE IF NOT EXISTS repacss_environment.telemetry (
    ts_host timestamptz NOT NULL,
    mac macaddr NOT NULL,
    rack_id text NOT NULL,
    bus text NOT NULL CHECK (bus IN ('inlet', 'exhaust')),
    sensor_index smallint NOT NULL,
    temperature_celsius real
);
SELECT create_hypertable('repacss_environment.telemetry', 'ts_host');

-- Compression. Telemetry is the only table with real volume: one rack emits
-- 6 rows every 5 seconds, about 37.8 million rows and roughly 3 GB per year,
-- and most of each row is Postgres tuple overhead rather than data.
--
-- segmentby groups rows into one physical row per sensor series, so the
-- repeated mac/bus/sensor_index values are stored once instead of per reading,
-- and the remaining timestamp and temperature columns compress well (delta
-- encoding on time, and slowly changing floats). rack_id is left out of
-- segmentby because it is functionally determined by mac and would only
-- multiply the number of segments.
ALTER TABLE repacss_environment.telemetry SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'mac, bus, sensor_index',
    timescaledb.compress_orderby   = 'ts_host DESC'
);

-- Compress chunks once they stop being written to. Recent data stays
-- uncompressed so ingestion writes stay fast.
SELECT add_compression_policy('repacss_environment.telemetry', INTERVAL '7 days');

CREATE TABLE IF NOT EXISTS repacss_environment.events (
    ts_host timestamptz NOT NULL,
    mac macaddr NOT NULL,
    event_type text NOT NULL,
    details text,
    rack_id text NOT NULL,
    role text NOT NULL
);
SELECT create_hypertable('repacss_environment.events', 'ts_host');

-- Events are low volume, but they are kept indefinitely and cost nothing to
-- compress. A longer window because events are read while investigating
-- recent incidents.
ALTER TABLE repacss_environment.events SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'mac, event_type',
    timescaledb.compress_orderby   = 'ts_host DESC'
);
SELECT add_compression_policy('repacss_environment.events', INTERVAL '30 days');

-- No retention policy is set on purpose. Nothing is dropped automatically,
-- so raw history is kept until someone decides otherwise. If raw 5 second
-- data stops being worth its space, the pattern is a continuous aggregate
-- holding per minute and per hour averages, kept forever, plus a retention
-- policy on the raw table. That is a deliberate data loss decision and is
-- not made here.

CREATE TABLE IF NOT EXISTS repacss_environment.current_status(
    mac macaddr PRIMARY KEY,
    alive boolean NOT NULL,
    last_seen timestamptz NOT NULL,
    last_ip INET,
    running_firmware_version text NOT NULL
);

CREATE TABLE IF NOT EXISTS repacss_environment.alerts (
    ts_host timestamptz NOT NULL,
    source text NOT NULL,
    severity text NOT NULL CHECK (severity IN ('info', 'warning', 'critical', 'error')),
    alert_type text NOT NULL,
    details text,
    resolved boolean NOT NULL
);

CREATE TABLE IF NOT EXISTS repacss_environment.unknown_devices (
    mac macaddr PRIMARY KEY,
    first_seen timestamptz NOT NULL,
    last_seen timestamptz NOT NULL,
    -- Running total since first_seen, not a rolling window: nothing trims it.
    -- The 24 hour summary the README describes is a query over this plus
    -- last_seen, not a maintained counter.
    hit_count int NOT NULL,
    -- Last payload seen, truncated by the writer. Helps identify what a stray
    -- device actually is before deciding whether to register or block it.
    last_payload text
);