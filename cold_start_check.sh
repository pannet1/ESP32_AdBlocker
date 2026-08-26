#!/bin/bash
# Cold-start survival check for ESP32_AdBlocker
# Run this, then UNPLUG the device's power, wait 5s, PLUG it back in.
# The script self-synchronizes: it waits for the device to go down, then come back up,
# then monitors the blocklist load and verifies the DNS sinkhole.
set +e
resolve() {
  for h in 192.168.29.191 ESP32_AdBlocker.local; do
    if curl -s --max-time 3 "http://$h/status" >/dev/null 2>&1; then
      echo "http://$h"; return 0
    fi
  done
  return 1
}
echo "== ESP32_AdBlocker cold-start check =="
echo "[1] Waiting for device to go DOWN (please UNPLUG power now)..."
B=$(resolve)
for i in $(seq 1 60); do
  B=$(resolve)
  if [ -z "$B" ]; then echo "    down at $(date +%T)"; break; fi
  sleep 3
done
if [ -n "$B" ]; then echo "    !! device never went down — unplug it, then re-run this script."; exit 1; fi
echo "[2] Waiting for device to come back UP (please PLUG power back in)..."
BASE=""
for i in $(seq 1 80); do
  B=$(resolve)
  if [ -n "$B" ]; then BASE="$B"; echo "    up at $(date +%T) ($B)"; break; fi
  sleep 3
done
if [ -z "$BASE" ]; then echo "    !! device did not return. Check power/USB."; exit 1; fi
HOST=$(echo "$BASE" | sed 's#http://##')
echo "[3] Monitoring blocklist load (up to ~5 min)..."
for i in $(seq 1 100); do
  LOG=$(curl -s --max-time 5 "$BASE/control?displayLog=1" 2>/dev/null)
  if echo "$LOG" | grep -q "AdBlocker DNS Server started"; then echo "    DNS server started at $(date +%T)"; break; fi
  if echo "$LOG" | grep -qi "Startup Failure\|Blocklist truncated\|memory overflow"; then
    echo "    !! FAILURE:"; echo "$LOG" | grep -i "failure\|truncat\|overflow"; break; fi
  sleep 3
done
echo "[4] Boot log summary:"
LOG=$(curl -s --max-time 5 "$BASE/control?displayLog=1" 2>/dev/null)
echo "$LOG" | grep -iE "Wakeup by|Station DNS set|Initial load|Loaded .* blocked domains|Blocklist truncated|memory overflow|Startup Failure|AdBlocker DNS Server started"
echo "[5] DNS sinkhole check (expect 0.0.0.0):"
dig @"$HOST" +short doubleclick.net
echo "[6] Live counters:"
curl -s --max-time 5 "$BASE/status" | grep -oE '"(blockCnt|allowCnt|up_time|clock|loadProg)"[^,}]*'
echo "== done =="
