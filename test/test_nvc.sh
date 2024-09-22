#!/usr/bin/env bash

# This script is meant to test and showcase the functionality of cosim_jtag
# together with NEORV32 from https://github.com/stnolting/neorv32 as softcore by
# utilizing the NVC VHDL Procedural Interface (VHPIDIRECT).

# Note: NVC implements the same non-standard way of VHPIDIRECT as GHDL does. As
# such both are compatible with eachother.

set -e
set -x # local command echo

# Restart script inside docker container.
if [ -z "$IN_DOCKER" ]; then
    docker run --rm -it \
        --env IN_DOCKER=1 \
        --volume $(realpath ..):/work \
        --workdir /work/test \
        --entrypoint bash \
        ghcr.io/nikleberg/nvc:master \
        -c "/work/test/test_nvc.sh"

    exit 0
fi

# Docker image simply contains NVC. We require additional packages to run the
# full example.
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    git \
    gcc \
    libc-dev \
    openocd \
    gdb-multiarch

# Clone the NEORV32 softcore and roll-back to a specific stable commit.
if [ ! -d "./neorv32_src" ]; then
    git clone https://github.com/stnolting/neorv32 neorv32_src
    cd neorv32_src
    git reset --hard ec2e2bb
    cd ..
fi

# Gather and analyze NEORV32 design files.
NEORV32_LOCAL_RTL=./neorv32_src/rtl
FILE_LIST=`cat $NEORV32_LOCAL_RTL/file_list_soc.f`
CORE_SRCS="${FILE_LIST//NEORV32_RTL_PATH_PLACEHOLDER/"$NEORV32_LOCAL_RTL"}"
nvc --work=neorv32 -a $CORE_SRCS

# Analyze cosim_jtag design files.
# -> NVC supports the same non-standard VHPIDIRECT as GHDL, so we can use
#    cosim_jtag_ghdl.vhd as package.
nvc --work=cosim -a ../cosim_jtag_ghdl.vhd ../cosim_jtag.vhd

# Compile our C file into a shared library.
gcc -shared -fPIC -o cosim_jtag.so ../cosim_jtag.c

# Analyze our testbench design file.
nvc -L. -a tb.vhd

# Elaborate the complete design hierarchy.
nvc -L. -e tb

# Run the simulation in the background.
# -> Shared library "cosim_jtag.so" must be manually loaded.
# -> Flag "ieee-warnings" is NEORV32 specific.
nvc -L. -r --load ./cosim_jtag.so --ieee-warnings=off tb &

# Wait a bit to ensure simulation could boot and UNIX socket could be created.
sleep 2

# Run openocd in the background.
# -> If this errors out, see whats going wrong by adding debugging flag -d.
# -> If an invalid tap/device id is read: Try to increase DELAY generic in VHDL.
openocd -f openocd.cfg &

# Wait a bit longer to ensure openocd could examine hart and start gdb server.
sleep 10

# Run some debugging.
gdb-multiarch --batch -x gdb.cfg

# Stop background openocd and GHDL simulation.
kill %2
sleep 1
kill %1
