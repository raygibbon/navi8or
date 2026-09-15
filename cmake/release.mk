# Deliberate packaging only; ordinary all remains a development build.
.PHONY: release-linux release-windows release release-check release-check-test
release-linux:
	@BUILD_CONTAINER='$(BUILD_CONTAINER)' PODMAN='$(PODMAN)' sh scripts/release-build.sh linux
release-windows:
	@sh scripts/release-build.sh windows
release: release-linux release-windows
	python3 scripts/release.py kit
	python3 scripts/release.py checksums
release-check:
	@BUILD_CONTAINER='$(BUILD_CONTAINER)' PODMAN='$(PODMAN)' sh scripts/release-build.sh check
release-check-test:
	python3 tests/release_test.py
