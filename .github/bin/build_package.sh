#!/usr/bin/env bash
set -xeuo pipefail

function assert_dynamic_deps() {
	local allowed="libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1
		libgcc_s.so.1 libz.so.1 ld-linux-x86-64.so.2 ld-linux-aarch64.so.1"
	if [ "$ENV_DISTRO" = "el8" ] || [ "$ENV_DISTRO" = "debian11" ]; then
		allowed+=" libssl.so.1.1 libcrypto.so.1.1"
	else
		allowed+=" libssl.so.3 libcrypto.so.3"
	fi

	local lib fail=0
	local needed
	needed=$(readelf -d target/asbench | awk '/\(NEEDED\)/ { gsub(/[][]/, "", $NF); print $NF }')
	echo "asbench DT_NEEDED:" $needed
	for lib in $needed; do
		if ! printf '%s\n' $allowed | grep -qxF "$lib"; then
			echo "asbench has unexpected dynamic dependency $lib; link it statically or add it to the allowlist and the package depends" >&2
			fail=1
		fi
	done
	return $fail
}

function build_packages() {
	if [ "${ENV_DISTRO:-}" = "" ]; then
		echo "ENV_DISTRO is not set" >&2
		return 1
	fi
	GIT_DIR=$(git rev-parse --show-toplevel)

	# build
	cd "$GIT_DIR" || exit 1
	echo "build_package.sh version: $(git describe --tags --always --abbrev=9)"
	VERSION=${PKG_VERSION:-$(git describe --tags --always --abbrev=9)}
	export VERSION
	make clean
	# Pass VERSION explicitly so the embedded TOOL_VERSION matches the package
	# version, even when the git tag hasn't been pushed yet (tag-last pipeline).
	make EVENT_LIB=libuv LIBUV_STATIC_PATH=/usr/local/lib VERSION="${VERSION}"

	assert_dynamic_deps

	# package
	cd "$GIT_DIR"/pkg || exit 1
	make clean
	echo "building package for $BUILD_DISTRO"

	if [[ $ENV_DISTRO == *"ubuntu"* ]]; then
		make deb
	elif [[ $ENV_DISTRO == *"debian"* ]]; then
		make deb
	elif [[ $ENV_DISTRO == *"el"* ]]; then
		make rpm
	elif [[ $ENV_DISTRO == *"amzn"* ]]; then
		make rpm
	else
		make tar
	fi

	mkdir -p /tmp/output/"$ENV_DISTRO"
	cp -a "$GIT_DIR"/pkg/target/* /tmp/output/"$ENV_DISTRO"
}
