CC      = /c/msys64/ucrt64/bin/gcc
PYTHON  = /c/msys64/ucrt64/bin/python3
CFLAGS  = -Wall -Wextra -std=c11 -O2 -g
SRC_DIR = src
TEST_DIR = tests

PYINC    = /c/msys64/ucrt64/include/python3.11
PYLIB    = /c/msys64/ucrt64/lib
PYEXT    = $(shell $(PYTHON) -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX'))")

ALL_SRC = $(SRC_DIR)/coordinator.c $(SRC_DIR)/disk.c $(SRC_DIR)/events.c \
          $(SRC_DIR)/encoding.c $(SRC_DIR)/partition.c $(SRC_DIR)/stringdict.c \
          $(SRC_DIR)/wal.c $(SRC_DIR)/query.c

.PHONY: all clean test test-wal test-disk test-store python

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

# Python extension — compiles all sources then links into a .pyd
PYOBJS = $(ALL_SRC:$(SRC_DIR)/%.c=build/%.o) build/fastevents_module.o

build/%.o: $(SRC_DIR)/%.c
	mkdir -p build
	$(CC) $(CFLAGS) -I$(PYINC) -fPIC -c $< -o $@

build/fastevents_module.o: $(SRC_DIR)/fastevents_module.c
	mkdir -p build
	$(CC) $(CFLAGS) -I$(PYINC) -fPIC -c $< -o $@

python: $(PYOBJS)
	$(CC) -shared -o fastevents$(PYEXT) $(PYOBJS) -L$(PYLIB) -lpython3.11 -lm

clean:
	rm -f test_wal test_disk test_store *.o *.log *.pyd
	rm -rf test_store_tmp build
