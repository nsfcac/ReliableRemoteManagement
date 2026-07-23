#!/usr/bin/env bash
# DHCP for the isolated telemetry segment
set -euo pipefail

source "$(dirname "$0")/lib.sh"

dnf install -y dnsmasq

# Overwrite the whole file (idempotent). Serves DHCP only on the telemetry NIC.
cat > /etc/dnsmasq.d/telemetry.conf <<EOF
interface=$TELEMETRY_IFACE
bind-interfaces
except-interface=lo
port=0
dhcp-range=$DHCP_RANGE
dhcp-option=3
dhcp-authoritative
EOF

systemctl enable dnsmasq
systemctl restart dnsmasq

firewall-cmd --permanent --zone=internal --add-service=dhcp
firewall-cmd --permanent --zone=internal --add-port=1883/tcp
firewall-cmd --reload
