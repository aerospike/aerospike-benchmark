#!/usr/bin/env bash
VERSION=$(git rev-parse HEAD | cut -c -8)
BUILD_DEPS_REDHAT="libtool cmake zlib zlib-devel openssl-devel libyaml-devel"
BUILD_DEPS_UBUNTU="libtool cmake zlib1g-dev libssl-dev libyaml-dev"
BUILD_DEPS_DEBIAN="libtool cmake zlib1g-dev libssl-dev libyaml-dev"
FPM_DEPS_DEBIAN="ruby-rubygems make rpm git rsync binutils"
FPM_DEPS_UBUNTU_2004="ruby make rpm git rsync binutils"
FPM_DEPS_UBUNTU="ruby-rubygems make rpm git rsync binutils"

AWS_SDK_VERSION="1.10.55"

function install_deps_debian12() {
  apt -y install $BUILD_DEPS_DEBIAN $FPM_DEPS_DEBIAN
  gem install fpm -v 1.17.0

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

function install_deps_debian13() {
  apt -y install $BUILD_DEPS_DEBIAN $FPM_DEPS_DEBIAN
  gem install fpm -v 1.17.0

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
  gem install fpm -v 1.17.0

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
  gem install fpm -v 1.17.0

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
  gem install fpm -v 1.17.0

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
function install_deps_el8() {
  dnf module enable -y ruby:2.7
  dnf -y install ruby ruby-devel redhat-rpm-config rubygems rpm-build make git
  gem install --no-document fpm -v 1.17.0

  dnf -y install $BUILD_DEPS_REDHAT python3 python3-pip rsync

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

function install_deps_el9() {
  dnf -y install $BUILD_DEPS_REDHAT ruby rpmdevtools make git python3 python3-pip rsync

  gem install fpm -v 1.17.0

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

function install_deps_el10() {
  dnf -y install $BUILD_DEPS_REDHAT ruby rpmdevtools make git python3 python3-pip rsync

  gem install fpm -v 1.17.0

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


function install_deps_amzn2023() {
  dnf -y install $BUILD_DEPS_REDHAT ruby rpmdevtools make git python3 python3-pip rsync

  gem install fpm -v 1.17.0

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


