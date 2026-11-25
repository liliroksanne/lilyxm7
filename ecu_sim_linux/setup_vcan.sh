#!/usr/bin/env bash
set -euo pipefail

IFACE="${1:-vcan0}"

echo "[lilyxm7] Cargando módulo vcan y levantando interfaz ${IFACE}..."
sudo modprobe vcan

if ip link show "${IFACE}" >/dev/null 2>&1; then
  echo "[lilyxm7] ${IFACE} ya existe, asegurándola en estado UP..."
else
  sudo ip link add dev "${IFACE}" type vcan
fi

sudo ip link set up "${IFACE}"
ip -brief link show "${IFACE}"
echo "[lilyxm7] Listo. Puedes monitorear tráfico con: candump ${IFACE}"
