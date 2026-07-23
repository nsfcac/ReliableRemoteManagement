#!/usr/bin/env bash
# dual-homing / static IP on the telemetry NIC
set -euo pipefail

source "$(dirname "$0")/lib.sh"

echo "Telemetry interface: $TELEMETRY_IFACE"

# Only create the connection if it does not already exist (idempotent guard).
if ! nmcli -g NAME connection show | grep -qx "telemetry"; then
    nmcli connection add type ethernet ifname "$TELEMETRY_IFACE" con-name telemetry \
        ipv4.method manual ipv4.addresses "$TELEMETRY_IP" \
        ipv4.never-default yes ipv6.method disabled connection.zone internal
fi
nmcli connection up telemetry
