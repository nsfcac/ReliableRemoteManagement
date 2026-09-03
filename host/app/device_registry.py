import time

_device_map = {}
_device_map_ts = 0.0
_DEVICE_MAP_TTL = 60.0
_MISS_REFRESH_COOLDOWN = 10.0

def get_device_map(conn) -> dict:
    try:
        with conn.transaction():
            with conn.cursor() as cur:
                cur.execute(
                    """
                    SELECT upper(replace(mac::text, ':', '')), rack_id, role, enabled
                    FROM repacss_environment.device_map WHERE retired = false
                    """
                )
                return {r[0]: {"rack_id": r[1], "role": r[2], "enabled": r[3]} for r in cur.fetchall()}
    except Exception as error:
        print(f"[ERROR] Failed to fetch device registry: {error}")
        return {}

def _refresh(conn) -> dict:
    global _device_map, _device_map_ts
    _device_map = get_device_map(conn)
    _device_map_ts = time.monotonic()
    return _device_map

def cache_device_map(conn) -> dict:
    if time.monotonic() - _device_map_ts > _DEVICE_MAP_TTL:
        _refresh(conn)
    return _device_map

def lookup(conn, mac):
    device = cache_device_map(conn).get(mac)
    if device is not None:
        return device

    if time.monotonic() - _device_map_ts >= _MISS_REFRESH_COOLDOWN:
        device = _refresh(conn).get(mac)
    return device

