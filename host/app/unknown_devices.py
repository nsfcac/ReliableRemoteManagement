"""Data Access for the unknown_devices table: unrecognized MACs seen on hello."""

_MAX_PAYLOAD_CHARS = 2048

def record(conn, mac: str, payload_txt) -> None:
    """Upsert an unknown device: one row per mac, bumped on each string"""
    try:
        with conn.transaction():
            with conn.cursor() as cur:
                cur.execute(
                    """
                    INSERT INTO repacss_environment.unknown_devices
                        (mac, first_seen, last_seen, hit_count, last_payload)
                    VALUES (%s, now(), now(), 1, %s)
                    ON CONFLICT (mac) DO UPDATE 
                    SET last_seen = now(),
                        hit_count = unknown_devices.hit_count + 1,
                        last_payload = EXCLUDED.last_payload
                    """,
                    (mac, payload_txt[:_MAX_PAYLOAD_CHARS]),
                )
    except Exception as error:
        print(f"[ERROR] failed to record to unknown device {mac}: {error}")