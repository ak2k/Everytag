#!/usr/bin/env bash
#
# OTA flash a signed firmware image to a running Everytag beacon over BLE.
# Wraps mcumgr (Go binary) with per-RPC timeouts/retries and a wall-clock
# cap on the upload step so a stuck SMP transfer fails loudly instead of
# hanging forever.

set -euo pipefail

ADDR=$(echo "${1:-}" | tr '[:upper:]' '[:lower:]')
AUTH="${2:-}"
FILE="${3:-}"

if [ -z "$ADDR" ] || [ -z "$AUTH" ] || [ -z "$FILE" ]; then
  echo "Usage: ./flash_beacon.sh MAC_ADDR AUTH_KEY FILE_UPLOAD" >&2
  exit 1
fi

MCUMGR="${MCUMGR:-$HOME/go/bin/mcumgr}"
# Per-RPC: 5 s timeout, 5 retries. Each SMP packet (≈MTU 498) gets 5
# attempts to complete before the whole upload aborts.
MCUMGR_FLAGS=(-t 5 -r 5)
# Wall-clock cap on the full upload (SIGTERM after, SIGKILL +10 s later).
# 200 KB image at MTU 498 ≈ 30–90 s in practice; 5 min covers slow stacks
# and short retries without hiding a true hang.
UPLOAD_TIMEOUT="${UPLOAD_TIMEOUT:-300s}"

if [ ! -x "$MCUMGR" ]; then
  echo "mcumgr not found at $MCUMGR (override with MCUMGR=/path/to/mcumgr)" >&2
  exit 1
fi

echo "BLE MAC:        $ADDR"
echo "AUTH:           $AUTH"
echo "FILE:           $FILE"
echo "mcumgr:         $MCUMGR ${MCUMGR_FLAGS[*]}"
echo "upload timeout: $UPLOAD_TIMEOUT"
echo ""
echo "Requires sudo for mcumgr"
sudo -v

echo "Switching to mcumgr mode via conn_beacon.py"
python3 ./conn_beacon.py -i "$ADDR" -a "$AUTH"
echo ""

sleep 1
HCIDEV=$(sudo hcitool dev | tail -n 1 | awk '{print $1}' | sed 's/^.*\([0-9]\)/\1/')

CONN=(--conntype ble --connstring "peer_id=$ADDR")

echo "Checking image list"
sudo "$MCUMGR" "${MCUMGR_FLAGS[@]}" -i "$HCIDEV" "${CONN[@]}" image list
echo ""

echo "Erasing unused image (if any)"
sudo "$MCUMGR" "${MCUMGR_FLAGS[@]}" -i "$HCIDEV" "${CONN[@]}" image erase
echo ""

echo "Uploading new image (wall-clock cap: $UPLOAD_TIMEOUT)"
sudo timeout --kill-after=10s "$UPLOAD_TIMEOUT" \
  "$MCUMGR" "${MCUMGR_FLAGS[@]}" -i "$HCIDEV" "${CONN[@]}" image upload "$FILE"
echo ""

echo "Confirming new image"
HASH=$(sudo "$MCUMGR" "${MCUMGR_FLAGS[@]}" -i "$HCIDEV" "${CONN[@]}" image list | grep hash | tail -n 1 | awk '{print $2}')
sudo "$MCUMGR" "${MCUMGR_FLAGS[@]}" -i "$HCIDEV" "${CONN[@]}" image confirm "$HASH"
echo ""

#echo "Resetting beacon"
#sudo "$MCUMGR" "${MCUMGR_FLAGS[@]}" -i "$HCIDEV" "${CONN[@]}" reset
#echo ""
