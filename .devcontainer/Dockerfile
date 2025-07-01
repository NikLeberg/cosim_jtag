FROM ubuntu:24.04 AS openocd_builder

ARG DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    LC_CTYPE=C.UTF-8

RUN <<EOF
    set -e
    apt-get -q -y update
    apt-get -q -y install --no-install-recommends \
        ca-certificates git \
        autoconf automake libtool pkg-config gcc make libjim-dev
    apt-get clean
    rm -rf /var/lib/apt/lists/*
EOF

ARG OPENOCD_CLONE_URL=https://github.com/NikLeberg/riscv-openocd.git
ARG OPENOCD_CLONE_TAG=fix_wrap
RUN <<EOF
    set -e
    git clone $OPENOCD_CLONE_URL -b $OPENOCD_CLONE_TAG --depth 1 openocd
    cd openocd
    ./bootstrap
    ./configure --enable-jtag-dpi
    make
EOF


FROM ghcr.io/nikleberg/nvc

COPY --from=openocd_builder /openocd/src/openocd /opt/openocd/
COPY --from=openocd_builder /openocd/tcl /opt/openocd/tcl

RUN <<EOF
    set -e
    apt-get -q -y update
    apt-get -q -y install --no-install-recommends \
        ca-certificates git \
        libjim0.82 \
        gcc libc-dev gdb-multiarch
    apt-get clean
    rm -rf /var/lib/apt/lists/*
EOF
