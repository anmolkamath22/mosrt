# MOSRT - Mini-OS Runtime

MOSRT is a feature-complete userspace operating-system runtime written in C17 for Linux. It models advanced process-management concepts without requiring kernel-mode development: PCBs, process states, pluggable schedulers, guarded user stacks, `ucontext` switching, safe timer preemption, deterministic workloads, blocking I/O, IPC, synchronization, tracing, metrics, benchmarks, tests, and CI quality gates.

The project is designed as a resume-quality systems project for roles that value operating-systems fundamentals, production C, performance reasoning, and reliability engineering.

## Project Overview

MOSRT is not a toy shell and not a monolithic simulator. It is a small userspace runtime with explicit module ownership:

- The process table owns PCBs and stack lifetime.
- The runtime owns ticks, dispatch, accounting, blocking, and wakeups.
- The scheduler module owns ready-queue policy.
- IPC and synchronization are independent subsystems that report block/wakeup outcomes to the runtime.
- Tracing and benchmarks produce repeatable artifacts for analysis.

## Features

| Area | Implemented |
|---|---|
| Process model | PCB, process table, NEW/READY/RUNNING/BLOCKED/EXITED states |
| Runtime | global tick engine, dispatcher, deterministic instruction execution |
| Context switching | `ucontext_t`, dedicated stacks, guard pages, `swapcontext()` tracing |
| Preemption | safe SIGALRM + `setitimer` reschedule flag, quantum management |
| Schedulers | FCFS, Round Robin, Priority with aging, MLFQ with demotion/boost |
| Accounting | CPU time, wait time, response time, turnaround, throughput, CPU utilization |
| Blocking | deterministic I/O instructions, blocked queue semantics, wakeups |
| IPC | bounded message queues, blocking send, blocking receive, direct receiver handoff |
| Synchronization | counting semaphores, mutexes, waiter grants, ownership checks |
| Shell | `run`, `ps`, `kill`, `sched`, `quantum`, `trace`, `step`, `metrics`, exports |
| Quality | unit tests, integration tests, stress tests, sanitizer target, coverage target, CI |
| Benchmarking | scheduler comparison, CPU-bound, I/O-bound, mixed, 100-process, 1000-process |

## Architecture

```text
                  +----------------------+
                  |      MOSRT Shell     |
                  | commands, scripts    |
                  +----------+-----------+
                             |
                             v
                  +----------------------+
                  |   Kernel Runtime     |
                  | ticks, dispatch,     |
                  | accounting, wakeups  |
                  +---+------+-----+-----+
                      |      |     |
        +-------------+      |     +----------------+
        v                    v                      v
+---------------+    +---------------+      +----------------+
| Process Table |    | Scheduler API |      | Trace/Metrics  |
| PCBs, stacks  |    | FCFS/RR/PRIO  |      | CSV + tables   |
| guard pages   |    | MLFQ          |      +----------------+
+-------+-------+    +---------------+
        |
        v
+----------------+      +----------------+      +----------------+
| Workload VM    | ---> | IPC Queues     | ---> | Sync Objects   |
| CPU/IO/EXIT    |      | bounded mq     |      | sem/mutex      |
+----------------+      +----------------+      +----------------+
```

### State Transition Diagram

```text
            run <workload>
                 |
                 v
              +-----+
              | NEW |
              +--+--+
                 |
                 v
             +-------+      dispatch       +---------+
       +---->| READY |-------------------->| RUNNING |
       |     +---+---+                     +----+----+
       |         ^                              |
       |         | wakeup                       | IO / IPC / sem / mutex block
       |     +---+---+                          v
       +-----|BLOCKED|<--------------------+ +-------+
             +-------+                     | | yield |
                                           | +-------+
                                           |
                       EXIT / kill         v
                         +-------------->+--------+
                                         | EXITED |
                                         +--------+
```

## Scheduler Architecture

The scheduler interface is policy-based. The runtime performs state transitions and accounting; the scheduler only manages ready entities and preemption decisions.

```text
runtime_step()
  wake blocked processes
  age priority scheduler if active
  dispatch sched_pick_next()
  execute one workload tick or blocking instruction
  ask sched_should_preempt()
  enqueue READY process if needed
```

| Scheduler | Data Structure | Preemptive | Starvation Control |
|---|---|---:|---|
| FCFS | FIFO ring queue | No | workload completion |
| RR | FIFO ring queue | Yes | bounded quantum |
| Priority | binary heap | Yes | aging lowers numeric priority |
| MLFQ | four FIFO queues | Yes | demotion plus periodic boost |

## IPC Architecture

MOSRT message queues are bounded FIFO channels. Sends block when the queue is full. Receives block when the queue is empty. If a sender arrives while receivers are waiting, MOSRT performs a direct receiver handoff so another process cannot steal the wakeup.

```text
SEND q value
  receiver waiting? -> direct grant + wake receiver
  queue has space?  -> enqueue message
  otherwise         -> BLOCKED on send

RECV q
  direct grant?     -> consume granted message
  queue has data?   -> dequeue message + wake sender
  otherwise         -> BLOCKED on receive
```

## Synchronization Architecture

Counting semaphores and mutexes are implemented as runtime resources with explicit waiter queues. Semaphore posts grant a token to the selected waiter before wakeup. Mutex unlock transfers ownership to a selected waiter, preventing races where a third process acquires the mutex between wakeup and dispatch.

```text
SEM_WAIT id
  count > 0  -> decrement and continue
  count == 0 -> BLOCKED

SEM_POST id
  waiter? -> grant token + wake
  none    -> increment count

LOCK id
  free    -> owner = pid
  owned   -> BLOCKED

UNLOCK id
  owner mismatch -> error
  waiter         -> owner = waiter + wake
  no waiter      -> free
```

## Repository Layout

```text
.
├── Makefile
├── README.md
├── .github/workflows/ci.yml
├── mosrt/
│   ├── Makefile
│   ├── src/
│   │   ├── main.c          # entry point
│   │   ├── shell.c         # interactive shell
│   │   ├── runtime.c       # tick engine, dispatch, accounting
│   │   ├── proc.c          # PCB table and guarded stacks
│   │   ├── sched.c         # FCFS, RR, Priority, MLFQ
│   │   ├── workload.c      # deterministic workload parser
│   │   ├── ipc.c           # bounded message queues
│   │   ├── sync.c          # semaphores and mutexes
│   │   ├── timer.c         # SIGALRM safe-preemption flag
│   │   └── log.c           # trace and CSV export
│   ├── workloads/          # interactive demo workloads
│   ├── benchmarks/
│   │   ├── workloads/      # benchmark workload set
│   │   └── results/        # generated CSV and Markdown tables
│   ├── tests/              # unit, integration, stress tests
│   └── tools/
│       └── mosrt_bench.c   # benchmark runner
```

## Build Instructions

Requirements:

- Linux
- GCC or Clang
- POSIX libc with `ucontext`
- Optional quality tools: `clang-format`, `clang-tidy`, `cppcheck`, `gcov`

```bash
make
make test
make bench
```

Useful quality targets:

```bash
make format-check
make cppcheck
make tidy
make sanitize
make coverage
```

## Running Instructions

From the repository root:

```bash
make
./mosrt/mosrt
```

Example session:

```text
mosrt> run workloads/mixed.wl
mosrt> sched mlfq
mosrt> quantum 4
mosrt> trace all
mosrt> start
mosrt> step 40
mosrt> ps
mosrt> metrics
mosrt> export trace trace.csv
mosrt> export metrics metrics.csv
mosrt> exit
```

## Shell Commands

| Command | Description |
|---|---|
| `help` | list commands |
| `run <workload> [priority]` | create a process from a workload |
| `ps` | dump PCB table |
| `kill <pid>` | terminate a process |
| `sched <fcfs|rr|prio|mlfq>` | switch scheduler |
| `quantum <ticks>` | set RR/MLFQ quantum |
| `nice <pid> <nice>` | adjust process nice value |
| `prio <pid> <priority>` | set dynamic/base priority |
| `trace <pid|all>` | enable trace output |
| `start` | start runtime/timer |
| `stop` | stop runtime/timer |
| `step <n>` | advance deterministic ticks |
| `queues` | show scheduler queues |
| `metrics` | print aggregate metrics |
| `export trace <path.csv>` | export trace timeline |
| `export metrics <path.csv>` | export per-process metrics |
| `bench` | run built-in scheduler comparison |
| `exit` | leave shell |

## Example Workloads

Workloads are deterministic scripts.

```text
# CPU-bound
CPU 25
CPU 20
EXIT
```

```text
# I/O-bound
CPU 2
IO 5
CPU 2
IO 5
CPU 2
EXIT
```

```text
# IPC producer
CPU 1
SEND 1 42
CPU 1
SEND 1 84
EXIT
```

Supported instructions:

| Instruction | Meaning |
|---|---|
| `CPU N` | consume N scheduled CPU ticks |
| `IO N` | block until `current_tick + N` |
| `SEND Q VALUE` | send to bounded message queue Q |
| `RECV Q` | receive from bounded message queue Q |
| `SEM_WAIT ID` | decrement semaphore or block |
| `SEM_POST ID` | increment semaphore or wake waiter |
| `LOCK ID` | acquire mutex or block |
| `UNLOCK ID` | release mutex |
| `EXIT` | terminate process |

## Benchmarks

Run:

```bash
make bench
```

Outputs:

- `mosrt/benchmarks/results/scheduler_comparison.csv`
- `mosrt/benchmarks/results/scheduler_comparison.md`

### Performance Table

| Scenario | Scheduler | Processes | Ticks | Completed | CPU Util % | Throughput | Avg Turnaround | Avg Wait | Avg Response |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cpu_bound | fcfs | 4 | 401 | 4 | 99.75 | 0.009975 | 250.00 | 150.00 | 150.00 |
| cpu_bound | rr | 4 | 401 | 4 | 99.75 | 0.009975 | 400.00 | 300.00 | 6.00 |
| cpu_bound | prio | 4 | 401 | 4 | 99.75 | 0.009975 | 379.00 | 279.00 | 9.00 |
| cpu_bound | mlfq | 4 | 401 | 4 | 99.75 | 0.009975 | 268.00 | 168.00 | 6.00 |
| io_bound | fcfs | 4 | 29 | 4 | 82.76 | 0.137931 | 25.00 | 3.00 | 3.00 |
| mixed | fcfs | 4 | 89 | 4 | 98.88 | 0.044944 | 76.00 | 47.00 | 12.00 |
| mixed | rr | 4 | 93 | 4 | 94.62 | 0.043011 | 92.00 | 63.00 | 6.00 |
| 100_process | mlfq | 100 | 201 | 100 | 99.50 | 0.497512 | 113.50 | 110.50 | 86.50 |
| 1000_process | fcfs | 1000 | 1001 | 1000 | 99.90 | 0.999001 | 501.22 | 500.22 | 499.50 |
| 1000_process | mlfq | 1000 | 1001 | 1000 | 99.90 | 0.999001 | 500.50 | 499.50 | 499.50 |

## Implementation Details

- C17 with strict warnings.
- No async context switching inside signal handlers.
- SIGALRM handler only sets a `sig_atomic_t` reschedule flag.
- Process stacks are allocated with `mmap` and protected by guard pages.
- Exited PCBs are retained for observability while owned stack memory is released.
- Ready queues use ring-buffer storage to avoid per-tick heap allocation.
- Priority scheduling uses a binary heap and periodic aging.
- MLFQ uses multiple FIFO queues with demotion and global priority boost.
- Workload execution is deterministic, which makes tests and benchmarks repeatable.
- CSV exports are generated for trace timelines, per-process metrics, and scheduler comparisons.

## Testing

```text
make test
  unit:
    test_ipc_sync
    test_workload
    test_scheduler
  integration:
    shell-driven producer/consumer run
  stress:
    100-process MLFQ run

make sanitize
  AddressSanitizer + UndefinedBehaviorSanitizer

make coverage
  gcov-compatible coverage build
```

## CI

GitHub Actions runs:

- build
- unit/integration/stress tests
- benchmarks
- clang-format check
- cppcheck
- clang-tidy
- sanitizers
- coverage build

## Screenshots Placeholders

```text
docs/screenshots/shell-demo.png
docs/screenshots/trace-export.png
docs/screenshots/benchmark-table.png
docs/screenshots/queue-visualization.png
```

## Resume Bullet

Built MOSRT, a C17 userspace operating-system runtime implementing guarded-stack green processes, deterministic workload execution, FCFS/RR/Priority/MLFQ schedulers, SIGALRM-safe preemption, blocking IPC, semaphores, mutexes, tracing, metrics, CSV/Markdown benchmarks, stress tests, sanitizers, and CI quality gates.

## Future Work

Memory management:

- virtual address-space simulator
- page tables and TLB model
- page faults and replacement policies: FIFO, LRU, Clock
- per-process heap simulator
- copy-on-write fork model

Storage management:

- block-device simulator
- disk scheduling: FCFS, SSTF, SCAN, C-SCAN
- inode-style filesystem model
- buffer cache and write-back policy
- journaling and crash-recovery simulation

Production polish:

- richer benchmark visualizations
- trace viewer
- fuzzing harness
- code coverage badges
- manpage-style CLI documentation
