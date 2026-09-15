# Linux -> Windows, explicit target libraries only. Never query host pkg-config.
.DEFAULT_GOAL := all
CROSS_COMPILE ?= x86_64-w64-mingw32-
ifeq ($(origin CC),default)
CC := $(CROSS_COMPILE)gcc
endif
AR := $(CROSS_COMPILE)ar
RANLIB := $(CROSS_COMPILE)ranlib
STRIP := $(CROSS_COMPILE)strip
OBJDUMP := $(CROSS_COMPILE)objdump
WINDOWS_DEPS_PREFIX ?= $(CURDIR)/.deps/windows
# libsmb2's public headers require _WINDOWS for the Windows socket ABI.
CPPFLAGS += -D_WINDOWS -D_WIN32_WINNT=0x0601 -DCURL_STATICLIB -DSODIUM_STATIC \
    -Ibuild/windows -Ibuild/generated -Iinclude -Ithird_party -isystem $(WINDOWS_DEPS_PREFIX)/include
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -MMD -MP
OBJECT_DIR := build/windows
ifeq ($(BUILD_MODE),release)
OBJECT_DIR := build/release/objects/windows
CFLAGS += -ffunction-sections -fdata-sections -ffile-prefix-map=$(CURDIR)=/src
LDFLAGS += -Wl,--gc-sections -Wl,--no-insert-timestamp
endif
APP_BINARY ?= build/windows/nav.exe
SMB2_LIBS ?= $(WINDOWS_DEPS_PREFIX)/lib/libsmb2.a
LDLIBS += -lshell32 $(WINDOWS_DEPS_PREFIX)/lib/libcurl.a $(WINDOWS_DEPS_PREFIX)/lib/libsodium.a \
    $(SMB2_LIBS) -lws2_32 -lcrypt32 -lbcrypt -ladvapi32 -lsecur32 -liphlpapi -lshlwapi -lwldap32 -luser32
SOURCES := $(filter-out src/platform/posix.c src/platform/secure_file_posix.c src/path.c src/provider/local.c,$(shell find src -name '*.c' | sort))
OBJECTS := $(SOURCES:src/%.c=$(OBJECT_DIR)/%.o) $(OBJECT_DIR)/toml.o
-include $(OBJECTS:.o=.d)

.PHONY: all clean dist windows-deps verify-windows-deps verify-windows-toolchain verify-windows-host-tools windows-inspect windows-error-test
all: $(APP_BINARY)
windows-deps: verify-windows-host-tools
	WINDOWS_DEPS_PREFIX='$(WINDOWS_DEPS_PREFIX)' sh scripts/build-windows-deps.sh
verify-windows-host-tools:
	@for tool in patch; do \
		command -v "$$tool" >/dev/null || { echo "error: required host tool '$$tool' not found" >&2; exit 1; }; \
	done
verify-windows-toolchain:
	@command -v '$(CC)' >/dev/null || { echo 'error: Windows cross compiler $(CC) not found; install MinGW-w64' >&2; exit 1; }
verify-windows-deps: verify-windows-host-tools verify-windows-toolchain
	@test -s '$(WINDOWS_DEPS_PREFIX)/lib/libsodium.a' -a -s '$(WINDOWS_DEPS_PREFIX)/include/sodium.h' || { echo 'error: Windows libsodium not found; run make TARGET=windows windows-deps' >&2; exit 1; }
	@test -s '$(WINDOWS_DEPS_PREFIX)/lib/libcurl.a' -a -s '$(WINDOWS_DEPS_PREFIX)/include/curl/curl.h' || { echo 'error: Windows libcurl not found; run make TARGET=windows windows-deps' >&2; exit 1; }
	@test -s '$(WINDOWS_DEPS_PREFIX)/lib/libsmb2.a' || { echo 'error: Windows libsmb2 not found (existing SMB provider); run make TARGET=windows windows-deps' >&2; exit 1; }

build/windows/termbox2/termbox2.h: third_party/termbox2/termbox2.h third_party/termbox2-patches/0001-windows-console-support.patch | verify-windows-host-tools
	@mkdir -p $(dir $@)
	@set -eu; trap 'rm -f "$@.tmp"' EXIT HUP INT TERM; \
		patch -s -o "$@.tmp" $< < third_party/termbox2-patches/0001-windows-console-support.patch; \
		mv "$@.tmp" "$@"
$(OBJECTS): | verify-windows-deps
$(OBJECTS): | $(VERSION_HEADER)
$(OBJECT_DIR)/terminal/termbox_backend.o $(OBJECT_DIR)/terminal/termbox_input.o: build/windows/termbox2/termbox2.h
$(OBJECT_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(OBJECT_DIR)/toml.o: third_party/toml.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(APP_BINARY): $(OBJECTS) cmake/windows.mk
	@mkdir -p $(dir $@)
	# NavApp and nested configuration locals exceed MinGW's default 2 MiB stack.
	$(CC) $(LDFLAGS) -Wl,--stack,8388608 -static-libgcc -o $@ $(OBJECTS) $(LDLIBS)
# Compile the real Windows backend with its private test hook in a separate binary.
# Optional runner command, e.g. WINDOWS_TEST_RUNNER=umu-run.
WINDOWS_TEST_RUNNER ?=
windows-error-test: verify-windows-toolchain
	@mkdir -p build/windows
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -Isrc -DNAV_SECURE_FILE_TESTING \
		tests/windows_error_test.c $(filter src/platform/secure_file_win32.c src/platform/windows.c src/platform/external_url.c,$(SOURCES)) \
		$(LDFLAGS) -static-libgcc -o build/windows/windows-error-test.exe -lbcrypt -ladvapi32 -lshell32
	@if [ -n "$(WINDOWS_TEST_RUNNER)" ]; then \
		$(WINDOWS_TEST_RUNNER) build/windows/windows-error-test.exe; \
	elif command -v wine >/dev/null 2>&1; then \
		wine build/windows/windows-error-test.exe; \
	elif command -v wine64 >/dev/null 2>&1; then \
		wine64 build/windows/windows-error-test.exe; \
	else \
		echo 'Windows error test compiled; runtime execution unavailable because Wine is not installed.'; \
	fi
windows-inspect: $(APP_BINARY)
	file $<
	$(OBJDUMP) -p $< | sed -n '/DLL Name:/p'
dist:
	python3 scripts/release.py source
clean:
	rm -rf build/windows dist/windows
