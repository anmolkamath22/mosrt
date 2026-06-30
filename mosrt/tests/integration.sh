#!/bin/sh
set -eu

BIN="${1:-./mosrt}"
OUT="$(mktemp)"
TRACE="$(mktemp)"
METRICS="$(mktemp)"

printf 'run workloads/consumer.wl\nrun workloads/producer.wl\nstart\nstep 20\nps\nmetrics\nexport trace %s\nexport metrics %s\nexit\n' "$TRACE" "$METRICS" | "$BIN" >"$OUT"

grep -q 'created pid=1' "$OUT"
grep -q 'created pid=2' "$OUT"
grep -q 'completed=2' "$OUT"
grep -q 'trace export' "$OUT"
grep -q 'metrics export' "$OUT"
grep -q '^tick,pid,event,detail' "$TRACE"
grep -q '^pid,state,cpu,wait,response' "$METRICS"

rm -f "$OUT" "$TRACE" "$METRICS"
