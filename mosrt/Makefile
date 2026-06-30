CC ?= gcc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -g -O2
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=
GCOV ?= gcov

SRC := $(wildcard src/*.c)
CORE_SRC := src/ipc.c src/log.c src/proc.c src/runtime.c src/sched.c src/sync.c src/timer.c src/workload.c
OBJ := $(SRC:.c=.o)
BIN := mosrt
TEST_BINS := tests/test_ipc_sync tests/test_workload tests/test_scheduler
BENCH_BIN := tools/mosrt_bench

.PHONY: all clean test unit integration stress bench format format-check tidy cppcheck sanitize coverage

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) $(LDFLAGS) -o $@

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

unit: $(TEST_BINS)
	./tests/test_ipc_sync
	./tests/test_workload
	./tests/test_scheduler

test: all unit integration stress

tests/test_ipc_sync: tests/test_ipc_sync.c src/ipc.c src/sync.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $^ -o $@

tests/test_workload: tests/test_workload.c src/workload.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $^ -o $@

tests/test_scheduler: tests/test_scheduler.c src/proc.c src/sched.c
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
