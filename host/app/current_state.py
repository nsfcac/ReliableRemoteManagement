"""Data access for the current_status table: device liveness and firmware."""

def upsert(conn, mac: str, fw: str) -> None:
    try:
        with conn.transaction():
            with conn.cursor() as cur:
                cur.execute(
                    """
                    INSERT INTO repacss_environment.current_status
                        (mac, alive, last_seen, running_firmware_version)
                    VALUES (%s, true, now(), %s)
                    ON CONFLICT (mac) DO UPDATE
                    SET alive = EXCLUDED.alive,
                        last_seen = EXCLUDED.last_seen,
                        running_firmware_version = EXCLUDED.running_firmware_version
                    """, (mac, fw),
                    )
    except Exception as error:
        print(f"[ERROR] Failed to upsert the current status for {mac}: {error}")

def get_recent(conn) -> dict:
    """Return {mac: {firmware}} for devices seen in the last minute."""

    try:
        with conn.transaction():
            with conn.cursor() as cur:
                cur.execute(
                    """
                    SELECT upper(replace(mac::text, ':', '')), running_firmware_version
                    FROM repacss_environment.current_status
                    WHERE last_seen > now() - interval '1 minutes'
                    """
                )
                return {row[0]: {"firmware_version": row[1]} for row in cur.fetchall()}
    except Exception as error:
        print(f"[ERROR] Failed to fetch current status: {error}")
        return {}


                