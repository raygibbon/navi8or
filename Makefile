CC ?= cc
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Iinclude -Ithird_party
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror

SOURCES := $(shell find src -name '*.c' | sort)
OBJECTS := $(SOURCES:src/%.c=build/%.o)

.PHONY: all clean check asan
all: nav

nav: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

check: nav
	./tests/smoke.sh ./nav

asan: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address,undefined
asan: clean nav

clean:
	rm -rf build nav
