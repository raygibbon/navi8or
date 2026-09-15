# Shared by native, container and MinGW builds. Never maintain a version here.
VERSION_HEADER := build/generated/nav_version.h
.PHONY: version-header version-force
version-header: $(VERSION_HEADER)
$(VERSION_HEADER): VERSION scripts/generate-version.sh version-force
	@sh scripts/generate-version.sh VERSION $@
