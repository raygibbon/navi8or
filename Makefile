CC ?= cc
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Iinclude -Ithird_party
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror

SOURCES := $(shell find src -name '*.c' | sort)
OBJECTS := $(SOURCES:src/%.c=build/%.o)

.PHONY: all clean check core-test control-test resize-test asan
all: nav

nav: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build:
	mkdir -p $@

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

check: nav core-test control-test
	sh tests/smoke.sh ./nav

core-test: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/core_test.c src/path.c src/provider/local.c src/commander.c src/transfer/transfer.c -o build/core-test
	./build/core-test

control-test: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/control_test.c src/tdxui/field.c src/tdxui/menu_state.c src/tdxui/view_state.c -o build/control-test
	./build/control-test

resize-test: nav
	python3 tests/resize_test.py ./nav

asan: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address,undefined
asan: clean nav

clean:
	rm -rf build nav
