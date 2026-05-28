#!/usr/bin/env bash
set -xeuo pipefail

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
