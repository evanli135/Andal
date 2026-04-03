CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -g
SRC_DIR = src
TEST_DIR = tests

# Targets
.PHONY: all clean test test-stringdict test-eventblock test-append test-wal

all: test-wal

# Append tests
test_append: $(TEST_DIR)/test_append.c $(SRC_DIR)/eventstore.c
	$(CC) $(CFLAGS) -o $@ $^

test-append: test_append
	./test_append

# EventBlock tests
test_eventblock: $(TEST_DIR)/test_eventblock.c $(SRC_DIR)/eventstore.c
	$(CC) $(CFLAGS) -o $@ $^

test-eventblock: test_eventblock
	./test_eventblock

# String dictionary tests
test_stringdict: $(TEST_DIR)/test_stringdict.c $(SRC_DIR)/stringdict.c
	$(CC) $(CFLAGS) -o $@ $^

test-stringdict: test_stringdict
	./test_stringdict

# Original storage tests (currently broken - need full storage.c)
test_storage: $(TEST_DIR)/test_storage.c $(SRC_DIR)/storage.c
	$(CC) $(CFLAGS) -o $@ $^

# WAL tests
test_wal: $(TEST_DIR)/test_wal.c $(SRC_DIR)/wal.c
	$(CC) $(CFLAGS) -o $@ $^

test-wal: test_wal
	./test_wal

test: test-wal

clean:
	rm -f test_wal test_append test_eventblock test_stringdict test_storage *.o *.log
