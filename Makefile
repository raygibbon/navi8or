CC ?= cc
PKG_CONFIG ?= pkg-config
.DEFAULT_GOAL := all
CURL_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libcurl 2>/dev/null)
CURL_LIBS ?= $(shell $(PKG_CONFIG) --libs libcurl 2>/dev/null)
SODIUM_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libsodium 2>/dev/null)
SODIUM_LIBS ?= $(shell $(PKG_CONFIG) --libs libsodium 2>/dev/null)
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Iinclude -Ithird_party $(CURL_CFLAGS) $(SODIUM_CFLAGS)
LDLIBS += $(CURL_LIBS) $(SODIUM_LIBS)
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
CFLAGS += -MMD -MP

SOURCES := $(shell find src -name '*.c' | sort)
OBJECTS := $(SOURCES:src/%.c=build/%.o)
DEPS := $(OBJECTS:.o=.d) build/toml.d
SODIUM_OBJECTS := build/credential/vault.o

-include $(DEPS)

.PHONY: all clean check core-test control-test terminal-test viewer-test config-test theme-test vault-test http-test resize-test asan asan-check verify-vendor verify-libsodium dist dist-check
all: nav

nav: $(OBJECTS) build/toml.o | verify-libsodium
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build:
	mkdir -p $@

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Check discovery before compiling any translation unit that includes sodium.h.
$(SODIUM_OBJECTS): | verify-libsodium

build/toml.o: third_party/toml.c third_party/toml.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

check: verify-vendor verify-libsodium nav core-test control-test terminal-test viewer-test config-test theme-test vault-test http-test
	sh tests/smoke.sh ./nav

verify-vendor:
	test -s third_party/termbox2/termbox2.h
	test -s third_party/toml.c
	test -s third_party/toml.h

verify-libsodium:
	@$(PKG_CONFIG) --exists libsodium || { echo "error: libsodium development files not found" >&2; exit 1; }

DIST_ARCHIVE := navi8or-source.tar.gz

dist: verify-vendor
	set -eu; temp=$$(mktemp -d); trap 'rm -rf "$$temp"' EXIT; \
	mkdir -p "$$temp/navi8or"; \
	cp -R Makefile README.md include src tests themes third_party docs "$$temp/navi8or/"; \
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

core-test: | build
	$(CC) $(CPPFLAGS) -Itests $(CFLAGS) tests/core_test.c tests/fake_provider.c src/path.c src/provider/local.c src/commander.c src/transfer/transfer.c -o build/core-test
	./build/core-test

control-test: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/control_test.c src/ui/core/area.c src/ui/core/field.c src/ui/core/menu_layout.c src/ui/core/menu_state.c src/ui/core/view_state.c -o build/control-test
	./build/control-test

terminal-test: | build
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/terminal_test.c src/terminal/termbox_input.c -o build/terminal-test
	./build/terminal-test

viewer-test: | build
	$(CC) $(CPPFLAGS) -Itests $(CFLAGS) tests/viewer_test.c tests/fake_provider.c src/view/source.c src/view/viewer.c -o build/viewer-test
	./build/viewer-test

config-test: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/config_test.c src/config.c src/platform/posix.c third_party/toml.c -o build/config-test
	./build/config-test

theme-test: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/theme_test.c src/theme.c src/symbols.c src/platform/posix.c third_party/toml.c -o build/theme-test
	./build/theme-test

vault-test: verify-libsodium | build
	$(CC) $(CPPFLAGS) -Isrc -DNAV_SECURE_FILE_TESTING $(CFLAGS) \
		tests/vault_test.c src/credential/store.c src/credential/vault.c \
		src/platform/secure_file_posix.c -o build/vault-test $(SODIUM_LIBS)
	./build/vault-test

http-test: verify-libsodium | build
	$(CC) $(CPPFLAGS) -Itests -Isrc $(CFLAGS) tests/http_provider_test.c src/provider/http.c src/provider/local.c src/config.c src/credential/store.c src/credential/vault.c src/platform/secure_file_posix.c src/platform/posix.c src/commander.c src/path.c src/view/source.c src/view/viewer.c src/transfer/transfer.c third_party/toml.c -o build/http-provider-test $(CURL_LIBS) $(SODIUM_LIBS)
	python3 tests/http_integration_test.py ./build/http-provider-test ./nav

resize-test: nav
	python3 tests/resize_test.py ./nav

asan: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address,undefined
asan: clean nav

asan-check:
	$(MAKE) clean
	$(MAKE) CFLAGS='-O2 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' check

clean:
	rm -rf build nav
