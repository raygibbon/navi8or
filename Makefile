TARGET ?= native
.DEFAULT_GOAL := all
include cmake/version.mk
ifeq ($(TARGET),windows)
include cmake/windows.mk
else
CC ?= cc
PKG_CONFIG ?= pkg-config
.CURDIR := $(shell pwd)
.DEFAULT_GOAL := all

# Persistent rootless Ubuntu build environment (no custom image).
BUILD_CONTAINER ?= navi8or-build
BUILD_IMAGE ?= ubuntu:22.04
PODMAN ?= podman
.PHONY: container-create container-deps container-deps-clean container-build container-clean container-shell container-remove
container-create container-deps container-deps-clean container-build container-clean container-shell container-remove:
	@BUILD_CONTAINER='$(BUILD_CONTAINER)' BUILD_IMAGE='$(BUILD_IMAGE)' PODMAN='$(PODMAN)' sh scripts/container-dev.sh $(@:container-%=%)

# Linux owns application libraries; macOS keeps its existing native discovery.
USE_LINUX_DEPS ?= $(if $(filter Linux,$(shell uname -s)),1,0)
ifeq ($(USE_LINUX_DEPS),1)
LINUX_DEPS_PREFIX := $(CURDIR)/build/linux-deps
LINUX_DEPS_STAMP := $(LINUX_DEPS_PREFIX)/.stamp
LINUX_ARCHIVES := $(addprefix $(LINUX_DEPS_PREFIX)/lib/lib,curl.a ssl.a crypto.a z.a sodium.a smb2.a)
CURL_CFLAGS := -isystem $(LINUX_DEPS_PREFIX)/include
CURL_LIBS := $(addprefix $(LINUX_DEPS_PREFIX)/lib/lib,curl.a ssl.a crypto.a z.a) -pthread -ldl
SODIUM_LIBS := $(LINUX_DEPS_PREFIX)/lib/libsodium.a -pthread
SMB2_LIBS := $(LINUX_DEPS_PREFIX)/lib/libsmb2.a
OBJECT_DIR := build/linux
.PHONY: linux-deps linux-deps-clean linux-deps-force
linux-deps: $(LINUX_DEPS_STAMP)
$(LINUX_DEPS_STAMP): linux-deps-force
	@CC='$(CC)' sh scripts/build-linux-deps.sh
linux-deps-clean:
	@mkdir -p build
	flock build/.linux-deps.lock sh -c 'rm -rf build/linux-deps build/linux nav'
$(LINUX_ARCHIVES): | $(LINUX_DEPS_STAMP)
	@test -s $@
verify-curl verify-libsodium verify-libsmb2: $(LINUX_DEPS_STAMP)
else
OBJECT_DIR := build
# Select one working configuration source for both compile and link flags.
CURL_CONFIG := $(shell \
	if $(PKG_CONFIG) --cflags --libs libcurl >/dev/null 2>&1; then \
		printf '%s' '$(PKG_CONFIG) libcurl'; \
	elif curl-config --cflags >/dev/null 2>&1 && curl-config --libs >/dev/null 2>&1; then \
		printf '%s' 'curl-config'; \
	elif command -v brew >/dev/null 2>&1; then \
		prefix=$$(brew --prefix curl 2>/dev/null) && \
		"$$prefix/bin/curl-config" --cflags >/dev/null 2>&1 && \
		"$$prefix/bin/curl-config" --libs >/dev/null 2>&1 && \
		printf '"%s/bin/curl-config"' "$$prefix"; \
	fi)
SODIUM_CONFIG := $(shell \
	if $(PKG_CONFIG) --cflags --libs libsodium >/dev/null 2>&1; then \
		printf '%s' '$(PKG_CONFIG) libsodium'; \
	elif command -v brew >/dev/null 2>&1; then \
		prefix=$$(brew --prefix libsodium 2>/dev/null) && \
		pc_path="$$prefix/lib/pkgconfig$${PKG_CONFIG_PATH:+:$$PKG_CONFIG_PATH}" && \
		PKG_CONFIG_PATH="$$pc_path" $(PKG_CONFIG) --cflags --libs libsodium >/dev/null 2>&1 && \
		printf 'env PKG_CONFIG_PATH="%s" %s libsodium' "$$pc_path" '$(PKG_CONFIG)'; \
	fi)
# pkg-config also works with cross-toolchain .pc files. Homebrew is a fallback;
# user CPPFLAGS/LDFLAGS/LDLIBS remain first in the compile/link command.
SMB2_CONFIG := $(shell \
	if $(PKG_CONFIG) --cflags --libs libsmb2 >/dev/null 2>&1; then \
		printf '%s' '$(PKG_CONFIG) libsmb2'; \
	elif command -v brew >/dev/null 2>&1; then \
		prefix=$$(brew --prefix libsmb2 2>/dev/null) && \
		pc_path="$$prefix/lib/pkgconfig$${PKG_CONFIG_PATH:+:$$PKG_CONFIG_PATH}" && \
		PKG_CONFIG_PATH="$$pc_path" $(PKG_CONFIG) --cflags --libs libsmb2 >/dev/null 2>&1 && \
		printf 'env PKG_CONFIG_PATH="%s" %s libsmb2' "$$pc_path" '$(PKG_CONFIG)'; \
	fi)
# Upstream headers use compiler extensions; keep Navi8or's strict warnings
# without applying -Wpedantic to third-party headers on GCC/Clang toolchains.
SMB2_CFLAGS := $(patsubst -I%,-isystem %,$(if $(SMB2_CONFIG),$(shell $(SMB2_CONFIG) --cflags)))
# Without metadata, use the compiler's default search paths. Explicit LDLIBS
# can instead name an import library (e.g. on Windows), without adding -lsmb2.
SMB2_LIBS := $(if $(SMB2_CONFIG),$(shell $(SMB2_CONFIG) --libs),$(if $(filter undefined default,$(origin LDLIBS)),-lsmb2))
CURL_CFLAGS := $(if $(CURL_CONFIG),$(shell $(CURL_CONFIG) --cflags))
CURL_LIBS := $(if $(CURL_CONFIG),$(shell $(CURL_CONFIG) --libs))
SODIUM_CFLAGS := $(if $(SODIUM_CONFIG),$(shell $(SODIUM_CONFIG) --cflags))
SODIUM_LIBS := $(if $(SODIUM_CONFIG),$(shell $(SODIUM_CONFIG) --libs))
endif
override CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Ibuild/generated -Iinclude -Ithird_party $(CURL_CFLAGS) $(SODIUM_CFLAGS) $(SMB2_CFLAGS)
override LDLIBS += $(CURL_LIBS) $(SODIUM_LIBS) $(SMB2_LIBS)
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
CFLAGS += -MMD -MP

SOURCES := $(filter-out %_win32.c src/platform/windows.c,$(shell find src -name '*.c' | sort))
OBJECTS := $(SOURCES:src/%.c=$(OBJECT_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d) $(OBJECT_DIR)/toml.d
INPUT_SOURCES := $(wildcard src/input/*.c)
SODIUM_OBJECTS := $(OBJECT_DIR)/credential/vault.o

-include $(DEPS)

.PHONY: all clean check core-test control-test terminal-test viewer-test config-test input-test input-integration-test theme-test vault-test provider-test smb-path-test http-test resize-test credential-picker-test asan asan-check verify-vendor verify-curl verify-libsodium verify-libsmb2 dist dist-check
all: nav
$(OBJECTS): | $(VERSION_HEADER)
control-test: $(VERSION_HEADER)
.PHONY: version-test
check: version-test
version-test: version-header
	python3 tests/version_test.py

nav: $(OBJECTS) $(OBJECT_DIR)/toml.o $(LINUX_ARCHIVES) | verify-curl verify-libsodium verify-libsmb2
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(OBJECT_DIR)/toml.o $(LDLIBS)

build:
	mkdir -p $@

$(OBJECT_DIR)/%.o: src/%.c $(LINUX_DEPS_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Check development configuration before compiling dependency consumers.
$(OBJECT_DIR)/provider/http.o: | verify-curl
$(OBJECT_DIR)/provider/smb.o: | verify-libsmb2
$(SODIUM_OBJECTS): | verify-libsodium

$(OBJECT_DIR)/toml.o: third_party/toml.c third_party/toml.h $(LINUX_DEPS_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

check: verify-vendor verify-libsodium nav core-test control-test terminal-test viewer-test config-test input-test input-integration-test theme-test vault-test provider-test smb-path-test http-test credential-picker-test
	sh tests/smoke.sh ./nav

verify-vendor:
	test -s third_party/termbox2/termbox2.h
	test -s third_party/toml.c
	test -s third_party/toml.h

verify-curl:
	@test -n '$(strip $(CURL_LIBS))' || { echo "error: libcurl development configuration not found; tried pkg-config libcurl, PATH curl-config, and Homebrew curl-config" >&2; exit 1; }

verify-libsodium:
	@test -n '$(strip $(SODIUM_LIBS))' || { echo "error: libsodium development configuration not found; tried pkg-config libsodium and Homebrew libsodium pkgconfig" >&2; exit 1; }

# Compile and link, but do not execute: this also supports cross-compilation.
# A runtime .so alone is insufficient; both public headers and API symbols
# must be usable with the selected toolchain and the caller's standard flags.
verify-libsmb2:
	@probe=$$(mktemp -d) || exit 1; trap 'rm -rf "$$probe"' EXIT HUP INT TERM; \
	printf '%s\n' '#include <stddef.h>' '#include <stdint.h>' '#include <time.h>' \
		'#include <smb2/smb2.h>' '#include <smb2/libsmb2.h>' \
		'int main(void) { struct smb2_context *s = smb2_init_context(); if (s) smb2_destroy_context(s); return 0; }' > "$$probe/check.c"; \
	$(CC) $(CPPFLAGS) $(CFLAGS) "$$probe/check.c" $(LDFLAGS) -o "$$probe/check" $(LDLIBS) > "$$probe/log" 2>&1 || { \
		cat "$$probe/log" >&2; \
		echo "error: libsmb2 development files are required; provide pkg-config libsmb2 or set CPPFLAGS, LDFLAGS and LDLIBS for your installed headers/library" >&2; \
		exit 1; }

DIST_ARCHIVE := navi8or-source.tar.gz

dist: verify-vendor
	set -eu; temp=$$(mktemp -d); trap 'rm -rf "$$temp"' EXIT; \
	mkdir -p "$$temp/navi8or"; \
	cp -R Makefile VERSION README.md .gitignore cmake scripts include src tests themes third_party docs "$$temp/navi8or/"; \
	tar -C "$$temp" -czf "$(CURDIR)/$(DIST_ARCHIVE)" navi8or

dist-check: dist
	set -eu; temp=$$(mktemp -d); trap 'rm -rf "$$temp"' EXIT; \
	tar -xzf "$(DIST_ARCHIVE)" -C "$$temp"; \
	test -s "$$temp/navi8or/third_party/termbox2/termbox2.h"; \
	test -s "$$temp/navi8or/third_party/toml.c"; \
	test -s "$$temp/navi8or/third_party/toml.h"; \
	test -s "$$temp/navi8or/themes/classic-dos.toml"; \
	test -s "$$temp/navi8or/tests/theme_test.c"; \
	test -s "$$temp/navi8or/docs/TDX_UI_REFERENCE.md"; \
	test -s "$$temp/navi8or/docs/UI_DESIGN.md"; \
	! test -e "$$temp/navi8or/build"; \
	! test -e "$$temp/navi8or/nav"; \
	$(MAKE) -C "$$temp/navi8or" check

ifeq ($(USE_LINUX_DEPS),1)
.PHONY: linux-deps-test
linux-deps-test: nav | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/linux_deps_test.c $(LDFLAGS) -o build/linux-deps-test $(LDLIBS)
	python3 tests/linux_deps_test.py ./build/linux-deps-test
check: linux-deps-test profile-test preferences-integration-test
endif

core-test: | build
	$(CC) $(CPPFLAGS) -Itests $(CFLAGS) tests/core_test.c tests/fake_provider.c src/path.c src/provider/local.c src/commander.c src/ui/layout.c $(INPUT_SOURCES) src/transfer/transfer.c $(LDFLAGS) -o build/core-test
	./build/core-test

control-test: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/control_test.c $(INPUT_SOURCES) src/ui/core/area.c src/ui/core/field.c src/clipboard.c src/ui/core/menu_layout.c src/ui/core/menu_state.c src/ui/core/view_state.c $(LDFLAGS) -o build/control-test
	./build/control-test

terminal-test: | build
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/terminal_test.c src/terminal/termbox_input.c $(LDFLAGS) -o build/terminal-test
	./build/terminal-test

viewer-test: | build
	$(CC) $(CPPFLAGS) -Itests $(CFLAGS) tests/viewer_test.c tests/fake_provider.c src/view/source.c src/view/viewer.c $(LDFLAGS) -o build/viewer-test
	./build/viewer-test

input-integration-test: nav
	python3 tests/input_integration_test.py ./nav

input-test: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/input_test.c $(INPUT_SOURCES) $(LDFLAGS) -o build/input-test
	./build/input-test

config-test: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/config_test.c src/config.c src/theme.c src/symbols.c $(INPUT_SOURCES) src/platform/posix.c third_party/toml.c $(LDFLAGS) -o build/config-test
	./build/config-test

profile-test: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/profile_test.c src/profile.c src/config.c src/theme.c src/symbols.c $(INPUT_SOURCES) src/platform/posix.c third_party/toml.c $(LDFLAGS) -o build/profile-test
	./build/profile-test

location-test: nav
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/location_test.c $(filter-out $(OBJECT_DIR)/main.o,$(OBJECTS)) $(OBJECT_DIR)/toml.o $(LDFLAGS) -o build/location-test $(LDLIBS)
	python3 tests/location_integration_test.py ./build/location-test ./nav

.PHONY: location-test
check: location-test

clipboard-test: nav control-test terminal-test
	python3 tests/clipboard_integration_test.py ./nav

.PHONY: clipboard-test
check: clipboard-test

preferences-integration-test: nav
	python3 tests/preferences_integration_test.py ./nav

.PHONY: preferences-integration-test
.PHONY: profile-test
theme-test: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/theme_test.c $(INPUT_SOURCES) src/ui/layout.c src/config.c src/theme.c src/symbols.c src/platform/posix.c third_party/toml.c $(LDFLAGS) -o build/theme-test
	./build/theme-test

vault-test: verify-libsodium | build
	$(CC) $(CPPFLAGS) -Isrc -DNAV_SECURE_FILE_TESTING $(CFLAGS) \
		tests/vault_test.c src/credential/store.c src/credential/vault.c \
		src/platform/secure_file_posix.c $(LDFLAGS) -o build/vault-test $(SODIUM_LIBS)
	./build/vault-test

provider-test: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/provider_test.c src/provider/registry.c src/path.c $(LDFLAGS) -o build/provider-test
	./build/provider-test

smb-path-test: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/smb_path_test.c src/provider/smb_path.c $(LDFLAGS) -o build/smb-path-test
	./build/smb-path-test

HTTP_TEST_SOURCES := src/provider/smb.c src/provider/smb_path.c src/provider/registry.c src/provider/http.c src/provider/local.c src/config.c src/theme.c src/symbols.c $(INPUT_SOURCES) src/credential/store.c src/credential/vault.c src/platform/secure_file_posix.c src/platform/posix.c src/commander.c src/ui/layout.c src/path.c src/view/source.c src/view/viewer.c src/transfer/transfer.c third_party/toml.c

.PHONY: network-test
check: network-test
check: identity-test
.PHONY: identity-test
identity-test: nav control-test profile-test
	python3 tests/identity_integration_test.py ./nav
network-test: nav verify-curl verify-libsodium verify-libsmb2 | build
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/network_test.c $(HTTP_TEST_SOURCES) src/location.c src/transfer/download.c $(LDFLAGS) -o build/network-test $(LDLIBS)
	python3 tests/network_integration_test.py ./build/network-test ./nav

.PHONY: http-listing-test
http-listing-test: nav verify-curl verify-libsodium verify-libsmb2 | build
	$(CC) $(CPPFLAGS) -Itests -Isrc -DNAV_HTTP_LISTING_TESTING $(CFLAGS) tests/http_listing_test.c $(HTTP_TEST_SOURCES) $(LDFLAGS) -o build/http-listing-test $(LDLIBS)
	./build/http-listing-test
	python3 tests/http_listing_integration_test.py ./build/http-listing-test ./nav

http-test: http-listing-test verify-curl verify-libsodium verify-libsmb2 | build
	$(CC) $(CPPFLAGS) -Itests -Isrc $(CFLAGS) tests/http_provider_test.c $(HTTP_TEST_SOURCES) $(LDFLAGS) -o build/http-provider-test $(LDLIBS)
	python3 tests/http_integration_test.py ./build/http-provider-test ./nav

credential-picker-test: nav
	python3 tests/credential_picker_test.py ./nav

resize-test: nav
	python3 tests/resize_test.py ./nav

asan: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address,undefined
asan: clean nav

asan-check:
	$(MAKE) clean
	$(MAKE) CFLAGS='-O2 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' check

clean:
	@test ! -d build || find build -mindepth 1 -maxdepth 1 ! -name linux-deps ! -name .linux-deps.lock ! -name windows -exec rm -rf {} +
	rm -f nav

endif
