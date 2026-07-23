#!/usr/bin/env bash
# Shared config + helpers for the setup scripts. Sourced, not executed.

TELEMETRY_IP="10.10.0.1/24"
DHCP_RANGE="10.10.0.50,10.10.0.150,12h"

# Find the USB ethernet adapter, safely. Fails loudly if it cannot be
# identified with confidence (picking the wrong NIC would rogue-DHCP the
# home network).
detect_telemetry_iface() {
    local candidates=() name
    for path in /sys/class/net/*; do
        name=$(basename "$path")
        [[ -e "$path/device" ]] || continue          # skip lo, docker0, veth, bridges
        if readlink -f "$path/device" | grep -q '/usb'; then
            candidates+=("$name")
        fi
    done

    if [[ ${#candidates[@]} -ne 1 ]]; then
        echo "[ERROR] expected 1 USB NIC, found ${#candidates[@]}: ${candidates[*]:-none}" >&2
        exit 1
    fi

    local iface="${candidates[0]}" default_iface
    default_iface=$(ip route show default | awk '{print $5}' | head -n1)
    if [[ "$iface" == "$default_iface" ]]; then
        echo "[ERROR] $iface carries the default route, refusing" >&2
        exit 1
    fi
    echo "$iface"
}

# Allow override, else auto-detect.
TELEMETRY_IFACE="${TELEMETRY_IFACE:-$(detect_telemetry_iface)}"
