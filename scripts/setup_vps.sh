#!/bin/bash
# bpp vps setup script
# installs docker and docker-compose on debian/ubuntu

set -e

echo "📦 updating system and installing dependencies..."
apt-get update
apt-get install -y ca-certificates curl gnupg lsb-release

echo "🔑 adding docker's official gpg key..."
mkdir -p /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/debian/gpg | gpg --dearmor --yes -o /etc/apt/keyrings/docker.gpg

echo "📝 setting up the repository..."
echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/debian \
  $(lsb_release -cs) stable" | tee /etc/apt/sources.list.d/docker.list > /dev/null

echo "🚀 installing docker engine and compose..."
apt-get update
apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

echo "✅ docker installed successfully!"
docker --version
docker compose version
