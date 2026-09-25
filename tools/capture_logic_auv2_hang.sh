#!/usr/bin/env bash
set -u

OUT="${1:-/private/tmp/arpsid_logic_auv2_live_$(date +%Y%m%d_%H%M%S)}"
mkdir -p "$OUT"
echo "$OUT" | tee /private/tmp/arpsid_logic_auv2_live_latest.txt
echo "Capture started $(date)" | tee "$OUT/README.txt"
echo "Waiting for Logic/AUHostingService. Reproduce the Logic AUv2 warning now." | tee -a "$OUT/README.txt"

START_EPOCH="$(date +%s)"
END_EPOCH="$((START_EPOCH + 300))"
SAMPLED=0

while [ "$(date +%s)" -lt "$END_EPOCH" ]; do
  ps -axo pid,ppid,state,%cpu,%mem,etime,comm,args | \
    /usr/bin/grep -E 'Logic Pro|AUHostingServiceXPC|AudioComponentRegistrar|ArpSID' | \
    /usr/bin/grep -v grep > "$OUT/ps_latest.txt" || true
  cat "$OUT/ps_latest.txt" >> "$OUT/ps_history.txt"

  AUPIDS="$(/usr/bin/pgrep -f 'AUHostingServiceXPC' 2>/dev/null || true)"
  LOGICPIDS="$(/usr/bin/pgrep -f '/Applications/Logic Pro X.app/Contents/MacOS/Logic Pro X' 2>/dev/null || true)"

  if [ -n "$AUPIDS$LOGICPIDS" ]; then
    TS="$(date +%H%M%S)"
    echo "[$TS] saw Logic/AU host" | tee -a "$OUT/README.txt"
    for pid in $AUPIDS; do
      /usr/bin/sample "$pid" 3 10 -file "$OUT/auhost_${pid}_${TS}.sample.txt" > "$OUT/auhost_${pid}_${TS}.sample.stdout" 2>&1 || true
      /bin/ps -p "$pid" -o pid,ppid,state,%cpu,%mem,etime,command > "$OUT/auhost_${pid}_${TS}.ps.txt" 2>&1 || true
    done
    for pid in $LOGICPIDS; do
      /usr/bin/sample "$pid" 2 8 -file "$OUT/logic_${pid}_${TS}.sample.txt" > "$OUT/logic_${pid}_${TS}.sample.stdout" 2>&1 || true
      /bin/ps -p "$pid" -o pid,ppid,state,%cpu,%mem,etime,command > "$OUT/logic_${pid}_${TS}.ps.txt" 2>&1 || true
    done
    SAMPLED=$((SAMPLED + 1))
  fi

  if [ "$SAMPLED" -ge 8 ]; then
    break
  fi
  /bin/sleep 1
done

/usr/bin/log show --style compact --last 15m --predicate '(process CONTAINS "Logic" OR process CONTAINS "AUHostingService" OR eventMessage CONTAINS "ArpSID" OR eventMessage CONTAINS "Audio Unit" OR eventMessage CONTAINS "AUv3Instance" OR eventMessage CONTAINS "connection interrupted" OR eventMessage CONTAINS "connection invalidated" OR eventMessage CONTAINS "reported a problem" OR eventMessage CONTAINS "Invalid property")' > "$OUT/unified_last15m.log" 2>"$OUT/unified_last15m.err" || true
find "$HOME/Library/Logs/DiagnosticReports" "/Library/Logs/DiagnosticReports" -maxdepth 1 -type f \( -name '*AUHostingService*' -o -name '*Logic*' -o -name '*ArpSID*' \) -mmin -30 -print > "$OUT/recent_diagnostic_reports.txt" 2>/dev/null || true

echo "Capture finished $(date)" | tee -a "$OUT/README.txt"
echo "$OUT"
