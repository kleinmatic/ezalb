FROM fedora:42

RUN dnf install -y gcc make pkgconf-pkg-config SDL2-devel zlib-devel rpm-build \
    && dnf clean all

WORKDIR /work
