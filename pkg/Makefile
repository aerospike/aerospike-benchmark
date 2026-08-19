# Variables required for this Makefile
PKG_DIR = $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
TOP_DIR = $(PKG_DIR)/../target
BUILD_DIR = $(PKG_DIR)/build
TARGET_DIR = $(PKG_DIR)/target
CONFIG_DIR = /etc/aerospike
# Package variables
PACKAGE_NAME = asbench
BUILD_DISTRO := $(or $(BUILD_DISTRO),$(shell $(PKG_DIR)/../.github/bin/os_version.sh))
NAME = "aerospike-$(PACKAGE_NAME)"
MAINTAINER = "Aerospike"
VERSION ?= $(shell git describe --tags --always --abbrev=9 2>/dev/null || echo 'unknown')
DESCRIPTION = "Aerospike Benchmark, a tool for performance testing Aerospike database."
LICENSE = "Apache License 2.0"
URL = "https://github.com/aerospike/aerospike-benchmark"
VENDOR = "Aerospike, Inc."
ARCH = $(shell uname -m)
# Package names never carry the "rcN" pre-release tag: rcN becomes the package
# iteration, so 2.2.10-rc2 packages as version 2.2.10 iteration 2.
PKG_RELEASE_SH = $(PKG_DIR)/../.github/bin/pkg_release.sh
PKG_VERSION := $(shell $(PKG_RELEASE_SH) '$(VERSION)' version)
# `:= $(or ...)` rather than `?=`, which is always recursively expanded and
# would re-run pkg_release.sh at every expansion; an override still wins.
ITERATION := $(or $(ITERATION),$(shell $(PKG_RELEASE_SH) '$(VERSION)' iteration))
# For names with no iteration field of their own (.tar, macOS .pkg).
RELEASE := $(PKG_VERSION)-$(ITERATION)
RPM_VERSION := $(subst -,_,$(PKG_VERSION))
# rpm Release / deb revision. The deb revision embeds the distro per Debian
# convention (e.g. 1.1.2-1debian12), like the rpm dist tag.
DEB_ITERATION := $(ITERATION)$(BUILD_DISTRO)
# pkgbuild --version rejects hyphens and underscores; collapse both to dots
# (1.1.2-1 -> 1.1.2.1), and defensively normalize any underscore-form
# inputs (env overrides) too. This is the version *inside* the pkg only;
# the file name below keeps the canonical hyphenated RELEASE.
MAC_VERSION := $(subst _,.,$(subst -,.,$(RELEASE)))
MAC_ROOT = $(BUILD_DIR)/mac-root
PKG_ID = com.aerospike.asbench
# macOS artifact file name, lower case and '-' separated so a Homebrew cask can
# interpolate it (<arch> is `uname -m`, the token a cask's `arch` stanza emits):
#
#     aerospike-asbench-<version>-<iteration>-macos<major>-<arch>.pkg
#
# macos<major> is the generation the artifact was BUILT on, not a minimum it
# supports; it keeps one release's per-runner artifacts distinct. MAC_PLATFORM
# is `:=` so check-version's guard tests the token that lands in the name.
MAC_PLATFORM := $(shell $(PKG_DIR)/../.github/bin/os_version.sh)
MAC_PKG = $(TARGET_DIR)/aerospike-asbench-$(RELEASE)-$(MAC_PLATFORM)-$(ARCH).pkg


.PHONY: all
all: deb rpm tar

.PHONY: deb
deb: prep
	fpm --force \
		--config-files $(CONFIG_DIR) \
		--input-type dir \
		--output-type deb \
		--chdir $(BUILD_DIR)/ \
		--name $(NAME) \
		--version $(PKG_VERSION) \
		--iteration $(DEB_ITERATION) \
		--maintainer $(MAINTAINER) \
		--description $(DESCRIPTION) \
		--license $(LICENSE) \
		--url $(URL) \
		--vendor $(VENDOR) \
		--package $(TARGET_DIR)/$(NAME)_$(PKG_VERSION)-$(DEB_ITERATION)_$(ARCH).deb

.PHONY: rpm
rpm: prep
	fpm --force \
		--config-files $(CONFIG_DIR) \
		--input-type dir \
		--output-type rpm \
		--chdir $(BUILD_DIR)/ \
		--name $(NAME) \
		--version $(RPM_VERSION) \
		--iteration $(ITERATION) \
		--maintainer $(MAINTAINER) \
		--depends "openssl-libs" \
		--description $(DESCRIPTION) \
		--license $(LICENSE) \
		--url $(URL) \
		--vendor $(VENDOR) \
		--package $(TARGET_DIR)/$(NAME)-$(RPM_VERSION)-$(ITERATION).$(BUILD_DISTRO).$(ARCH).rpm

.PHONY: tar
tar: prep
	fpm --force \
		--config-files $(CONFIG_DIR) \
		--input-type dir \
		--output-type tar \
		--chdir $(BUILD_DIR)/ \
		--name $(NAME) \
		--version $(PKG_VERSION) \
		--maintainer $(MAINTAINER) \
		--description $(DESCRIPTION) \
		--license $(LICENSE) \
		--url $(URL) \
		--vendor $(VENDOR) \
		--package $(TARGET_DIR)/$(NAME)-$(RELEASE)-$(BUILD_DISTRO)-$(ARCH).tar

# PKG_VERSION, ITERATION and MAC_PLATFORM all come from $(shell ...), which
# yields the empty string on failure without failing the build; unguarded, an
# empty one ships aerospike-asbench_-<distro>_<arch>.deb past every later check.
.PHONY: check-version
check-version:
	@test -n '$(VERSION)' && test '$(VERSION)' != 'unknown' \
		|| { echo "ERROR: VERSION is '$(VERSION)' -- git describe failed and none was passed; refusing to build an unversioned package" >&2; exit 1; }
	@test -n '$(PKG_VERSION)' \
		|| { echo "ERROR: empty package version -- $(PKG_RELEASE_SH) failed for VERSION='$(VERSION)'" >&2; exit 1; }
	@test -n '$(ITERATION)' \
		|| { echo "ERROR: empty package iteration -- $(PKG_RELEASE_SH) failed for VERSION='$(VERSION)'" >&2; exit 1; }

.PHONY: prep
prep: check-version
	install -d $(TARGET_DIR)
	install -d $(BUILD_DIR)/$(CONFIG_DIR)
	install -d $(BUILD_DIR)/opt/aerospike/bin
	install -pm 755 $(TOP_DIR)/asbench $(BUILD_DIR)/opt/aerospike/bin
	install -d $(BUILD_DIR)/usr/bin
	ln -sf /opt/aerospike/bin/asbench $(BUILD_DIR)/usr/bin/asbench


# Staging for macOS installer: install to /usr/local/aerospike/bin with symlink at /usr/local/bin.
.PHONY: prep-mac
prep-mac: check-version
	@echo '$(MAC_PLATFORM)' | grep -qE '^macos[0-9]+$$' \
		|| { echo "ERROR: macOS platform token is '$(MAC_PLATFORM)', not macos<major> -- .github/bin/os_version.sh failed; refusing to build a misnamed pkg" >&2; exit 1; }
	install -d $(TARGET_DIR)
	rm -rf $(MAC_ROOT)
	install -d $(MAC_ROOT)/usr/local/aerospike/bin
	install -d $(MAC_ROOT)/usr/local/bin
	install -pm 755 $(TOP_DIR)/asbench $(MAC_ROOT)/usr/local/aerospike/bin/asbench
	ln -sf ../aerospike/bin/asbench $(MAC_ROOT)/usr/local/bin/asbench

.PHONY: osx-pkg
osx-pkg: prep-mac
	pkgbuild \
		--root $(MAC_ROOT) \
		--identifier $(PKG_ID) \
		--version $(MAC_VERSION) \
		--install-location / \
		--scripts $(PKG_DIR)/mac-scripts \
		$(MAC_PKG)

.PHONY: clean
clean:
	rm -rf $(TARGET_DIR)
	rm -rf $(BUILD_DIR)/opt
	rm -rf $(BUILD_DIR)/var
	rm -rf $(MAC_ROOT)

