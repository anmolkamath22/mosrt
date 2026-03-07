# MOSRT (Mini-OS Runtime)

Userspace mini OS runtime in C for Linux.

Current scope:
- PCB + process table
- Phase 1 shell commands (`run`, `ps`, `kill`, `sched`, `quantum`, `trace`, `start`, `step`)

Build and run:
```bash
gcc -std=c11 -Wall -Wextra -pedantic src/main.c src/shell.c src/proc.c -o mosrt
./mosrt
```
