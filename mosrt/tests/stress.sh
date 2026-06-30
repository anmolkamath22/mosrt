#!/bin/sh
set -eu

BIN="${1:-./mosrt}"
INPUT="$(mktemp)"
OUT="$(mktemp)"

i=0
while [ "$i" -lt 100 ]; do
    printf 'run benchmarks/workloads/tiny_cpu.wl\n' >>"$INPUT"
    i=$((i + 1))
done
printf 'sched mlfq\nquantum 2\nstart\nstep 250\nmetrics\nexit\n' >>"$INPUT"

"$BIN" <"$INPUT" >"$OUT"
grep -q 'completed=100' "$OUT"

rm -f "$INPUT" "$OUT"
