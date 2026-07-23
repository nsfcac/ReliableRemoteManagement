#!/usr/bin/env bash
# orchestrator, calls the numbered steps

set -euo pipefail

[[ $EUID -eq 0 ]] || { echo "Run with sudo" >&2; exit 1; }

DIR="$(cd "$(dirname "$0")" && pwd)"
"$DIR/01-docker.sh"
"$DIR/02-network.sh"
"$DIR/03-dnsmasq.sh"
"$DIR/deploy.sh"
echo "[INFO] Host Setup Completed && Deploy Script Has been run!!"