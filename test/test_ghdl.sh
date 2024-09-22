#!/usr/bin/env bash

# This script is meant to test and showcase the functionality of cosim_jtag
# together with NEORV32 from https://github.com/stnolting/neorv32 as softcore by
# utilizing the GHDL VHDL Procedural Interface (VHPIDIRECT).

set -e
set -x # local command echo

# Restart script inside docker container.
if [ -z "$IN_DOCKER" ]; then
    docker run --rm -it \
        --env IN_DOCKER=1 \
        --volume $(realpath ..):/work \
        --workdir /work/test \
        --entrypoint bash \
        gcr.io/hdl-containers/ghdl/llvm \
        -c "/work/test/test_ghdl.sh"

    exit 0
fi

# Docker image contains GHDL with llvm backend. We require additional packages
# to run the full example.
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

# Create build directory for GHDL.
mkdir -p build

# Gather and analyze NEORV32 design files.
NEORV32_LOCAL_RTL=./neorv32_src/rtl
FILE_LIST=`cat $NEORV32_LOCAL_RTL/file_list_soc.f`
CORE_SRCS="${FILE_LIST//NEORV32_RTL_PATH_PLACEHOLDER/"$NEORV32_LOCAL_RTL"}"
ghdl -a --work=neorv32 --workdir=build $CORE_SRCS

# Analyze cosim_jtag design files.
# -> GHDL implements non-standard VHPIDIRECT, so we must use cosim_jtag_ghdl.vhd
#    as package.
ghdl -a --work=cosim --workdir=build ../cosim_jtag_ghdl.vhd ../cosim_jtag.vhd

# Compile our C file into a shared library.
gcc -shared -fPIC -o cosim_jtag.so ../cosim_jtag.c

# Analyze our testbench design file.
ghdl -a -Pbuild --workdir=build tb.vhd

# Elaborate the complete design hierarchy.
ghdl -e -Pbuild --workdir=build tb

# Run the simulation in the background.
# -> Shared library "cosim_jtag.so" is automatically loaded.
# -> Flags "max-stack-alloc" and "ieee-asserts" are NEORV32 specific.
./tb --max-stack-alloc=0 --ieee-asserts=disable &

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
