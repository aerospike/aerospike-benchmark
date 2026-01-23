#!/usr/bin/env bash
set -xeuo pipefail

# Build dependencies for C project
DEBIAN_DEPS="libtool automake autoconf m4 cmake make gcc g++ build-essential zlib1g-dev libssl-dev libyaml-dev curl git rsync"
UBUNTU_DEPS="libtool automake autoconf m4 cmake make gcc g++ build-essential zlib1g-dev libssl-dev libyaml-dev curl git rsync"
# FPM dependencies for packaging
FPM_DEPS_DEBIAN="ruby-rubygems rpm binutils"
FPM_DEPS_UBUNTU_2004="ruby rpm binutils"
FPM_DEPS_UBUNTU="ruby-rubygems rpm binutils"
# RHEL dependencies (no curl - UBI images have curl-minimal pre-installed which conflicts)
REDHAT_DEPS="libtool automake autoconf m4 cmake make gcc gcc-c++ zlib zlib-devel openssl-devel libyaml-devel git rsync"
FPM_DEPS_EL8="ruby rubygems redhat-rpm-config rpm-build"
FPM_DEPS_EL="ruby rpmdevtools"

function install_libuv() {
	cd /opt
	git clone https://github.com/libuv/libuv
	cd libuv
	git checkout v1.43.0
	sh autogen.sh
	./configure
	make
	make install
	cd ..
}

function install_deps_debian11() {
	rm -rf /var/lib/apt/lists/*
	apt-get clean
	apt-get update -o Acquire::Retries=5
	apt-get install -y --no-install-recommends $DEBIAN_DEPS $FPM_DEPS_DEBIAN
	gem install fpm -v 1.17.0
	install_libuv
	apt-get clean
	rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*
}

function install_deps_debian12() {
	rm -rf /var/lib/apt/lists/*
	apt-get clean
	apt-get update -o Acquire::Retries=5
	apt-get install -y --no-install-recommends $DEBIAN_DEPS $FPM_DEPS_DEBIAN
	gem install fpm -v 1.17.0
	install_libuv
	apt-get clean
	rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*
}

function install_deps_debian13() {
	rm -rf /var/lib/apt/lists/*
	apt-get clean
	apt-get update -o Acquire::Retries=5
	apt-get install -y --no-install-recommends $DEBIAN_DEPS $FPM_DEPS_DEBIAN
	gem install fpm -v 1.17.0
	install_libuv
	apt-get clean
	rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*
}

function install_deps_ubuntu20.04() {
	rm -rf /var/lib/apt/lists/*
	apt-get clean
	apt-get update -o Acquire::Retries=5
	apt-get install -y --no-install-recommends $UBUNTU_DEPS $FPM_DEPS_UBUNTU_2004
	gem install fpm -v 1.17.0
	install_libuv
	apt-get clean
	rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*
}

function install_deps_ubuntu22.04() {
	rm -rf /var/lib/apt/lists/*
	apt-get clean
	apt-get update -o Acquire::Retries=5
	apt-get install -y --no-install-recommends $UBUNTU_DEPS $FPM_DEPS_UBUNTU
	gem install fpm -v 1.17.0
	install_libuv
	apt-get clean
	rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*
}

function install_deps_ubuntu24.04() {
	rm -rf /var/lib/apt/lists/*
	apt-get clean
	apt-get update -o Acquire::Retries=5
	apt-get install -y --no-install-recommends $UBUNTU_DEPS $FPM_DEPS_UBUNTU
	gem install fpm -v 1.17.0
	install_libuv
	apt-get clean
	rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*
}

function install_deps_el8() {
	dnf -y update
	dnf module enable -y ruby:2.7
	dnf -y install $REDHAT_DEPS $FPM_DEPS_EL8
	gem install --no-document fpm -v 1.17.0
	install_libuv
	dnf clean all
}

function install_deps_el9() {
	dnf -y update
	dnf -y install $REDHAT_DEPS $FPM_DEPS_EL
	gem install fpm -v 1.17.0
	install_libuv
	dnf clean all
}

function install_deps_el10() {
	dnf -y update
	dnf -y install $REDHAT_DEPS $FPM_DEPS_EL
	gem install fpm -v 1.17.0
	install_libuv
	dnf clean all
}

function install_deps_amzn2023() {
	dnf -y update
	dnf -y install $REDHAT_DEPS $FPM_DEPS_EL
	gem install fpm -v 1.17.0
	install_libuv
	dnf clean all
}
