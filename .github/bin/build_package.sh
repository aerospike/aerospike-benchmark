#!/usr/bin/env bash
set -xeuo pipefail

function assert_dynamic_deps() {
	local allowed="libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1
		libgcc_s.so.1 libz.so.1 ld-linux-x86-64.so.2 ld-linux-aarch64.so.1"
	if [ "$ENV_DISTRO" = "el8" ]; then
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

function assert_system_openssl() {
	local lib fail=0
	local needed
	needed=$(readelf -d target/asbench | awk '/\(NEEDED\)/ { gsub(/[][]/, "", $NF); print $NF }')
	for lib in libssl libcrypto; do
		if ! grep -q "^$lib\.so" <<< "$needed"; then
			echo "asbench does not link $lib dynamically; it must load the host's OpenSSL" >&2
			fail=1
		fi
	done

	local above_floor
	above_floor=$(objdump -T target/asbench | grep -oE 'OPENSSL_3\.[0-9]+' | sort -u | awk -F. '$2 > 0' | tr '\n' ' ')
	if [ -n "$above_floor" ]; then
		echo "asbench needs OpenSSL symbols above 3.0 ($above_floor), but its packages accept any OpenSSL 3. Build against OpenSSL 3.0, or declare the minimum in pkg/Makefile and raise the floor here" >&2
		fail=1
	fi

	if [ "$fail" -eq 0 ]; then
		echo "assert_system_openssl: asbench loads the host's OpenSSL"
	fi
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
	VERSION=${BUILD_VERSION:-${PKG_VERSION:-$(git describe --tags --always --abbrev=9)}}
	export VERSION
	make clean
	# Pass VERSION explicitly so the embedded TOOL_VERSION matches the package
	# version, even when the git tag hasn't been pushed yet (tag-last pipeline).
	make EVENT_LIB=libuv LIBUV_STATIC_PATH=/usr/local/lib VERSION="${VERSION}"

	assert_dynamic_deps
	assert_system_openssl

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
