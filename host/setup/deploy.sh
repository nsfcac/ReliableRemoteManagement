#!/usr/bin/env bash
set -euo pipefail

# broker first (creates the repacss_mqtt network)
docker compose -f host/broker/compose.yaml up -d

# services second (join that network)
docker compose -f host/compose.yaml up -d --build