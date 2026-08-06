# MOSRT — Mini-OS Runtime



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
| Virtual Memory | 16-bit space, 256B pages, demand paging, TLB, FIFO/LRU/Clock policies, swap simulation |
| Heap Allocator | Per-process dynamic heap (malloc/free simulation) with split/coalesce logic |
| Accounting | CPU time, wait time, response time, turnaround, throughput, CPU utilization |
| Blocking | deterministic I/O instructions, blocked queue semantics, wakeups |
| IPC | bounded message queues, blocking send, blocking receive, direct receiver handoff |
| Synchronization | counting semaphores, mutexes, waiter grants, ownership checks |
| Shell | run, ps, kill, sched, nice/prio, VM inspection (vmmap, pte, frames, tlb, policy), exports |
| Quality | unit tests, integration tests, stress tests, fuzz harness, sanitizers, coverage, CI |
| Benchmarking | scheduler comparison, CPU-bound, I/O-bound, mixed, 100-process, 1000-process |

## Architecture

```text
                  +----------------------+
                  |      MOSRT Shell     |
                  | table-driven cmds    |
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
| guard pages   |    | MLFQ          |      | overflow track |
| O(1) PID map  |    +---------------+      +----------------+
+-------+-------+
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

## Design Decisions

This section documents key engineering decisions and their rationale.

**Deterministic tick engine over real-time scheduling.** MOSRT uses a deterministic tick model rather than wall-clock time. This makes tests, benchmarks, and trace exports perfectly repeatable. The SIGALRM timer only sets a flag; the runtime decides when to check it.

**Guard-page stacks via mmap + mprotect.** Process stacks are allocated with `mmap(MAP_PRIVATE|MAP_ANONYMOUS)` and protected with a guard page at the bottom via `mprotect(PROT_NONE)`. Stack overflow causes a `SIGSEGV` rather than silent heap corruption.

**Signal handler only sets a flag.** The SIGALRM handler only writes a `sig_atomic_t` variable — no context switching, no malloc, no stdio. The runtime polls this flag at safe yield points. This avoids all async-signal-safety issues.

**Binary heap for priority scheduling.** The priority scheduler uses a min-heap rather than a sorted list. Enqueue is O(log n), dequeue is O(log n). Priority aging is applied externally; the heap is rebuilt when priorities change.

**O(1) PID-to-slot mapping.** PIDs are sequential integers starting from 1. A direct `pid_to_slot[]` array provides O(1) lookup instead of O(n) linear scans, which matters for 1000-process benchmarks.

**Retained PCBs after exit.** `proc_mark_exited()` keeps the PCB allocated (for `ps` and metrics) but releases the stack. `proc_destroy()` fully deallocates the slot. This separation enables post-mortem analysis.

**Table-driven shell dispatch.** Shell commands are dispatched via a `{name, handler}` table rather than a long if-else chain. This makes adding new commands trivial and eliminates a code smell.

**Direct-grant IPC handoff.** When a sender writes to a queue with a waiting receiver, the message is placed in a per-receiver grant slot, preventing message theft by a third process that wakes between the receiver being readied and dispatched.

## Repository Layout

```text
.
├── LICENSE
├── Makefile
├── README.md
├── .github/workflows/ci.yml
├── .clang-format
├── .clang-tidy
├── .cppcheck-suppressions
├── mosrt/
│   ├── Makefile
│   ├── src/
│   │   ├── main.c          # entry point
│   │   ├── shell.c/.h      # table-driven interactive shell
│   │   ├── runtime.c/.h    # tick engine, dispatch, accounting
│   │   ├── proc.c/.h       # PCB table, guarded stacks, O(1) PID map
│   │   ├── sched.c/.h      # FCFS, RR, Priority, MLFQ
│   │   ├── workload.c/.h   # deterministic workload parser
│   │   ├── ipc.c/.h        # bounded message queues
│   │   ├── sync.c/.h       # semaphores and mutexes
│   │   ├── timer.c/.h      # SIGALRM safe-preemption flag
│   │   └── log.c/.h        # trace log, CSV export, overflow tracking
│   ├── workloads/           # interactive demo workloads
│   ├── benchmarks/
│   │   ├── workloads/       # benchmark workload set
│   │   └── results/         # generated CSV and Markdown tables
│   ├── tests/
│   │   ├── test_proc.c      # process table + state machine tests
│   │   ├── test_scheduler.c # all 4 scheduler policies
│   │   ├── test_ipc_sync.c  # IPC + sync primitives
│   │   ├── test_workload.c  # parser + error paths
│   │   ├── test_log.c       # trace log + overflow
│   │   ├── fuzz_workload.c  # deterministic fuzz harness
│   │   ├── integration.sh   # shell-driven IPC test
│   │   └── stress.sh        # 100-process MLFQ stress test
│   └── tools/
│       └── mosrt_bench.c    # benchmark runner
```

## Build Instructions

Requirements:

- Linux
- GCC or Clang
- POSIX libc with `ucontext`
- Optional quality tools: `clang-format`, `clang-tidy`, `cppcheck`, `gcov`, `valgrind`

```bash
make            # default build (with -Werror)
make test       # unit + integration + stress
make bench      # scheduler comparison benchmarks
```

Build profiles:

```bash
make debug      # -O0, -DDEBUG, assertions enabled
make release    # -O3, -DNDEBUG
```

Quality targets:

```bash
make format-check   # clang-format dry run
make cppcheck       # static analysis
make tidy           # clang-tidy
make sanitize       # ASan + UBSan build and test
make coverage       # gcov coverage build
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
mosrt> reset
mosrt> exit
```

## Shell Commands

| Command | Description |
|---|---|
| `help` | list commands |
| `run <workload> [priority]` | create a process from a workload |
| `ps` | dump PCB table |
| `kill <pid>` | terminate a process |
| `sched <fcfs\|rr\|prio\|mlfq>` | switch scheduler |
| `quantum <ticks>` | set RR/MLFQ quantum |
| `nice <pid> <nice>` | adjust process nice value |
| `prio <pid> <priority>` | set dynamic/base priority |
| `trace <pid\|all>` | enable trace output |
| `start` | start runtime/timer |
| `stop` | stop runtime/timer |
| `step <n>` | advance deterministic ticks |
| `queues` | show scheduler queues |
| `metrics` | print aggregate metrics |
| `export trace <path.csv>` | export trace timeline |
| `export metrics <path.csv>` | export per-process metrics |
| `bench` | run built-in scheduler comparison |
| `reset` | reinitialize runtime without exiting |
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

- C17 with strict warnings (`-Wall -Wextra -Wpedantic -Werror`).
- No async context switching inside signal handlers.
- SIGALRM handler only sets a `sig_atomic_t` reschedule flag.
- Process stacks are allocated with `mmap` and protected by guard pages.
- Exited PCBs are retained for observability while owned stack memory is released.
- Ready queues use ring-buffer storage to avoid per-tick heap allocation.
- Priority scheduling uses a binary heap and periodic aging.
- MLFQ uses multiple FIFO queues with demotion and global priority boost.
- O(1) PID-to-slot lookup via direct-mapped array.
- Workload execution is deterministic, which makes tests and benchmarks repeatable.
- CSV exports properly quote detail fields for standards compliance.
- Trace log tracks overflow count for observability.
- Shell uses table-driven command dispatch for extensibility.

## Testing

```text
make test
  unit:
    test_proc           # process table, state machine, lifecycle
    test_scheduler      # FCFS, RR, Priority, MLFQ policies
    test_ipc_sync       # message queues, semaphores, mutexes
    test_workload       # parser, error paths, edge cases
    test_log            # trace events, overflow, CSV export
  integration:
    shell-driven producer/consumer run
  stress:
    100-process MLFQ run

make sanitize
  AddressSanitizer + UndefinedBehaviorSanitizer

make coverage
  gcov-compatible coverage build
```

### Fuzz Testing

```bash
cd mosrt
make tests/fuzz_workload
./tests/fuzz_workload 10000        # 10k iterations
./tests/fuzz_workload 10000 42     # with seed
```


### Developer Quick Start

```bash
git clone <repo-url>
cd mosrt/mosrt
make debug          # build with debug symbols and assertions
make unit           # run unit tests only
make sanitize       # full sanitizer check
```





## Future Work

Storage management:

- block-device simulator
- disk scheduling: FCFS, SSTF, SCAN, C-SCAN
- inode-style filesystem model
- buffer cache and write-back policy
- journaling and crash-recovery simulation

Production polish:

- richer benchmark visualizations
- trace viewer
- code coverage badges
- manpage-style CLI documentation
- deadlock detection for mutexes
- priority inheritance protocol
