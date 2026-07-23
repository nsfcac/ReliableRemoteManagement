#!/usr/bin/env bash
# install Docker Engine + compose
set -euo pipefail

# Install Docker only if it is not already present.
if ! command -v docker >/dev/null 2>&1; then
    echo "Installing Docker..."
    sudo dnf install -y dnf-plugins-core 
    sudo dnf config-manager --add-repo https://download.docker.com/linux/centos/docker-ce.repo 
    sudo dnf install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
else
    echo "Docker already installed, skipping"
fi

# Enable and start the daemon (idempotent).
systemctl enable --now docker

# Add the real login user (not root) to the docker group.
TARGET_USER="${SUDO_USER:-$USER}"
if ! id -nG "$TARGET_USER" | grep -qw docker; then
    usermod -aG docker "$TARGET_USER"
    echo "Added $TARGET_USER to docker group (log out/in for it to take effect)"
fi
