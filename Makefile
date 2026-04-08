CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -g
SRC_DIR = src
TEST_DIR = tests

ALL_SRC = $(SRC_DIR)/coordinator.c $(SRC_DIR)/disk.c $(SRC_DIR)/events.c \
          $(SRC_DIR)/encoding.c $(SRC_DIR)/partition.c $(SRC_DIR)/stringdict.c \
          $(SRC_DIR)/wal.c $(SRC_DIR)/query.c

.PHONY: all clean test test-wal test-disk test-store

all: test

# WAL tests
test_wal: $(TEST_DIR)/test_wal.c $(SRC_DIR)/wal.c
	$(CC) $(CFLAGS) -o $@ $^

test-wal: test_wal
	./test_wal

# Disk serialization tests
test_disk: $(TEST_DIR)/test_disk.c $(SRC_DIR)/disk.c $(SRC_DIR)/events.c
	$(CC) $(CFLAGS) -o $@ $^

test-disk: test_disk
	./test_disk

# Full store integration tests
test_store: $(TEST_DIR)/test_store.c $(ALL_SRC)
	$(CC) $(CFLAGS) -o $@ $^

test-store: test_store
	./test_store

test: test-wal test-disk test-store

clean:
	rm -f test_wal test_disk test_store *.o *.log
	rm -rf test_store_tmp
