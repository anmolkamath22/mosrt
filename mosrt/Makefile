CC ?= gcc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O2
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=
GCOV ?= gcov

SRC := $(wildcard src/*.c)
VM_SRC := src/frame.c src/page_table.c src/tlb.c src/swap.c src/replacement_fifo.c src/replacement_lru.c src/replacement_clock.c src/pager.c src/vm_alloc.c src/vm.c
CORE_SRC := src/ipc.c src/log.c src/proc.c src/runtime.c src/sched.c src/sync.c src/timer.c src/workload.c $(VM_SRC)
OBJ := $(SRC:.c=.o)
BIN := mosrt
TEST_BINS := tests/test_ipc_sync tests/test_workload tests/test_scheduler tests/test_proc tests/test_log tests/test_frame tests/test_pagetable tests/test_tlb tests/test_replacement tests/test_vm_alloc tests/test_pager
BENCH_BIN := tools/mosrt_bench

.PHONY: all clean test unit integration stress bench format format-check tidy cppcheck sanitize coverage debug release

all: $(BIN)

debug:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c17 -Wall -Wextra -Wpedantic -Werror -g -O0 -DDEBUG'

release:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c17 -Wall -Wextra -Wpedantic -Werror -O3 -DNDEBUG'

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) $(LDFLAGS) -o $@

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

unit: $(TEST_BINS)
	./tests/test_ipc_sync
	./tests/test_workload
	./tests/test_scheduler
	./tests/test_proc
	./tests/test_log
	./tests/test_frame
	./tests/test_pagetable
	./tests/test_tlb
	./tests/test_replacement
	./tests/test_vm_alloc
	./tests/test_pager

test: all unit integration stress

tests/test_ipc_sync: tests/test_ipc_sync.c src/ipc.c src/sync.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $^ -o $@

tests/test_workload: tests/test_workload.c src/workload.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $^ -o $@

tests/test_scheduler: tests/test_scheduler.c $(CORE_SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $^ -o $@

tests/test_proc: tests/test_proc.c $(CORE_SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $^ -o $@

tests/test_log: tests/test_log.c src/log.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $^ -o $@

tests/test_frame: tests/test_frame.c src/frame.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $^ -o $@

tests/test_pagetable: tests/test_pagetable.c src/page_table.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $^ -o $@

tests/test_tlb: tests/test_tlb.c src/tlb.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $^ -o $@

tests/test_replacement: tests/test_replacement.c src/replacement_fifo.c src/replacement_lru.c src/replacement_clock.c src/page_table.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $^ -o $@

tests/test_vm_alloc: tests/test_vm_alloc.c $(CORE_SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $^ -o $@

tests/test_pager: tests/test_pager.c $(CORE_SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $^ -o $@

tests/fuzz_workload: tests/fuzz_workload.c src/workload.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $^ -o $@

integration: all
	sh tests/integration.sh ./$(BIN)

stress: all
	sh tests/stress.sh ./$(BIN)

$(BENCH_BIN): tools/mosrt_bench.c $(CORE_SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $^ $(LDFLAGS) -o $@

bench: $(BENCH_BIN)
	mkdir -p benchmarks/results
	./$(BENCH_BIN) benchmarks/results/scheduler_comparison.csv benchmarks/results/scheduler_comparison.md

format:
	clang-format -i src/*.[ch] tests/*.c tools/*.c

format-check:
	clang-format --dry-run --Werror src/*.[ch] tests/*.c tools/*.c

tidy: clean
	clang-tidy src/*.c tests/*.c tools/*.c -- $(CPPFLAGS) -Isrc

cppcheck:
	cppcheck --enable=warning,style,performance,portability --std=c17 --suppress=missingIncludeSystem src tests tools

sanitize:
	$(MAKE) clean
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) test CFLAGS='-std=c17 -Wall -Wextra -Wpedantic -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined'

coverage:
	$(MAKE) clean
	$(MAKE) test CFLAGS='-std=c17 -Wall -Wextra -Wpedantic -g -O0 --coverage' LDFLAGS='--coverage'
	$(GCOV) -o src src/*.c >/dev/null

clean:
	rm -f $(BIN) $(TEST_BINS) $(BENCH_BIN) src/*.o tests/*.gcda tests/*.gcno tools/*.gcda tools/*.gcno src/*.gcda src/*.gcno *.gcov *.csv
