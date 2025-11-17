CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -Werror -fno-builtin -Iinclude
LDFLAGS ?=

CMOCKA_CFLAGS := $(shell pkg-config --cflags cmocka 2>/dev/null)
CMOCKA_LIBS := $(shell pkg-config --libs cmocka 2>/dev/null)
CMOCKA_LIBS ?= -lcmocka
WRAP_FLAGS := -Wl,--wrap=getc -Wl,--wrap=putc -Wl,--wrap=fprintf -Wl,--wrap=exit

BUILD_DIR := build
OBJS := $(BUILD_DIR)/protocol.o
BIN := $(BUILD_DIR)/test_main

# Tests need bzero; emulate via memset without touching sources
TEST_DEFS := '-Dbzero(x,n)=memset(x,0,n)'
TEST_CFLAGS := $(CFLAGS) $(CMOCKA_CFLAGS) $(TEST_DEFS)

.PHONY: all clean test

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/protocol.o: src/protocol.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): test/test_main.c $(OBJS) | $(BUILD_DIR)
	$(CC) $(TEST_CFLAGS) $^ -o $@ $(WRAP_FLAGS) $(CMOCKA_LIBS) $(LDFLAGS)

test: $(BIN)
	$(BIN)

clean:
	rm -rf $(BUILD_DIR)