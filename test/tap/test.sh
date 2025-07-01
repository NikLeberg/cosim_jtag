#!/usr/bin/env bash

# This script is meant to test and showcase the functionality of cosim_tap
# together with NEORV32 from https://github.com/stnolting/neorv32 as softcore by
# utilizing the standard VHDL Procedural Interface (VHPI) with NVC.

# Note: NVC implements the official IEEE standard VHPI if used with VHDL
# "foreign" attribute "VHPI". If the attribute is set to "VHPIDIRECT" or "GHDL",
# NVC is actually implementing the same non-standard way of VHPIDIRECT as GHDL
# does. As such, if you want to use cosim_tap with NVC you get to choose!

set -e
set -x # local command echo

if [ -z "$IN_DOCKER" ]; then
    # Start a parallel container with jtag_dpi enabled OpenOCD.
    docker run --rm \
        --name openocd \
        --volume $(realpath ../..):/work \
        --workdir /work/test/tap \
        --detach \
        ghcr.io/nikleberg/dpi_openocd \
        -f "/work/test/tap/openocd.cfg"
    sleep 1
    # Restart script inside docker container.
    docker run --rm -it \
        --env IN_DOCKER=1 \
        --volume $(realpath ../..):/work \
        --workdir /work/test/tap \
        --entrypoint bash \
        --network container:openocd \
        ghcr.io/nikleberg/nvc:master \
        -c "/work/test/tap/test.sh"
    docker rm --force openocd
    exit 0
fi

# Docker image simply contains NVC. We require additional packages to run the
# full example.
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates \
    git \
    gcc \
    libc-dev \
    gdb-multiarch

# The docker image may have outdated certificates. Update them.
update-ca-certificates

# Clone the NEORV32 softcore and roll-back to a specific stable commit.
if [ ! -d "./neorv32_src" ]; then
    git clone https://github.com/stnolting/neorv32 neorv32_src
    cd neorv32_src
    git reset --hard ec2e2bb
    cd ..
fi

# Gather and analyze NEORV32 design files.
NEORV32_LOCAL_RTL=./neorv32_src/rtl
FILE_LIST=(`cat $NEORV32_LOCAL_RTL/file_list_soc.f`)
FILE_LIST=(${FILE_LIST[@]//*neorv32_debug_dtm.vhd/}) # ignore original DTM
FILE_LIST=${FILE_LIST[@]//*neorv32_top.vhd/}
CORE_SRCS="${FILE_LIST//NEORV32_RTL_PATH_PLACEHOLDER/"$NEORV32_LOCAL_RTL"}"
nvc --work=neorv32 -a $CORE_SRCS

# Source root for cosim_tap.
TAP=../../src/tap

# Analyze cosim_tap design files.
# -> NVC supports the VHPI standard, so we can use cosim_tap_vhpi.vhd as pkg.
nvc --work=cosim -a $TAP/cosim_tap_vhpi.vhd $TAP/cosim_tap.vhd

# Compile our C file into a shared library.
# -> Precompiler flag "USE_VHPI" enables the (complex) VHPI implementation.
# -> If NVC is installed system-wide then the following will find the
#    "vhpi_user.h" header file. Otherwise add "-I<nvc_install_path>/include".
gcc -shared -fPIC -DUSE_VHPI -O0 -o cosim_tap.so $TAP/cosim_tap.c

# Snoop in our custom DTM.
nvc -L. --work=neorv32 -a cosim_tap_dtm.vhd $NEORV32_LOCAL_RTL/core/neorv32_top.vhd

# Analyze our testbench design file.
nvc -L. -a tb.vhd

# Elaborate the design and run the simulation in the background.
# -> Shared library "cosim_tap.so" must be manually loaded.
# -> Elaboration and simulation must be ran as a single step. Otherwise the
#    shared library will be re-initialized with default (wrong) values.
# -> Flag "ieee-warnings" is NEORV32 specific.
nvc -L. --load ./cosim_tap.so --messages=compact --ieee-warnings=off -e tb -r &

# Wait a bit to ensure simulation could boot.
sleep 2

# OpenOCD is already running in the background as a standalone docker container.
# It is constantly probing TCP port 5555 and trying to connect. If the below gdb
# command errors out you may:
# -> Inspect the container logs with "docker logs openocd". You will see the
#    output of the probing done by OpenOCD.
# -> If an invalid tap/device id is read: Try to increase DELAY generic in VHDL.

# Wait a bit longer to ensure OpenOCD could examine hart and start gdb server.
sleep 10

# Run some debugging.
gdb-multiarch --batch -x gdb.cfg

# Stop background NVC simulation.
kill %1
