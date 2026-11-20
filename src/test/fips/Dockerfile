FROM redhat/ubi9:9.6

RUN dnf -y install libtool automake autoconf m4 cmake make gcc gcc-c++ zlib-devel openssl openssl-devel \
      libyaml-devel findutils diffutils which tar gzip \
 && dnf clean all
