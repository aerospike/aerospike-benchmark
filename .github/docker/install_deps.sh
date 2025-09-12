#!/usr/bin/env bash
VERSION=$(git rev-parse HEAD | cut -c -8)
BUILD_DEPS_REDHAT=""
BUILD_DEPS_UBUNTU="libtool"
BUILD_DEPS_DEBIAN="libtool"
FPM_DEPS_DEBIAN="ruby-rubygems make rpm git rsync binutils"
FPM_DEPS_UBUNTU_2004="ruby make rpm git rsync binutils"
FPM_DEPS_UBUNTU="ruby-rubygems make rpm git rsync binutils"

AWS_SDK_VERSION="1.10.55"
function install_deps_debian11() {
  apt -y install $BUILD_DEPS_DEBIAN $FPM_DEPS_DEBIAN
  gem install fpm

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

function install_deps_debian12() {
  apt -y install $BUILD_DEPS_DEBIAN $FPM_DEPS_DEBIAN
  gem install fpm

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

function install_deps_ubuntu20.04() {
  apt -y install $BUILD_DEPS_UBUNTU $FPM_DEPS_UBUNTU_2004
  gem install fpm

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

function install_deps_ubuntu22.04() {
  apt -y install $BUILD_DEPS_UBUNTU $FPM_DEPS_UBUNTU
  gem install fpm

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

function install_deps_ubuntu24.04() {
  apt -y install $BUILD_DEPS_UBUNTU $FPM_DEPS_UBUNTU
  gem install fpm

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

function install_deps_redhat-ubi9() {
  #todo redhat ubi9 does not have flex or readline-devel available in the yum repos

  dnf -y install $BUILD_DEPS_REDHAT ruby rpmdevtools make git python3 python3-pip rsync

  gem install fpm
}
