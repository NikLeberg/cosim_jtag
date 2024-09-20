#!/usr/bin/env bash

# This script is meant to test and showcase the functionality of cosim_jtag
# together with NEORV32 from https://github.com/stnolting/neorv32 as softcore by
# utilizing the QuestaSim Foreign Language Interface (FLI).

set -e
set -x # local command echo

# Restart script inside docker container.
if [ -z "$IN_DOCKER" ]; then
    docker run --rm -it \
        --env IN_DOCKER=1 \
        --volume $(realpath ..):/work \
        --workdir /work/test \
        --mac-address=00:ab:ab:ab:ab:ab \
        --entrypoint bash \
        ghcr.io/nikleberg/questasim:22.1 \
        -c "/work/test/test_questasim.sh"

    exit 0
fi

# Docker image contains QuestaSim in version v22.1. We require additional
# packages to run the full example.
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

# Copy modelsim.ini file from QuestaSim install directory and create libraries.
vmap -c
vlib work
vlib neorv32
vmap neorv32 work
vlib cosim
vmap cosim work

# Gather and compile NEORV32 design files.
NEORV32_LOCAL_RTL=./neorv32_src/rtl
FILE_LIST=`cat $NEORV32_LOCAL_RTL/file_list_soc.f`
CORE_SRCS="${FILE_LIST//NEORV32_RTL_PATH_PLACEHOLDER/"$NEORV32_LOCAL_RTL"}"
vcom -work neorv32 -autoorder $CORE_SRCS

# Compile cosim_jtag design files.
# -> ModelSim/QuestaSim requires FLI, so we must use cosim_jtag_fli.vhd as pkt.
vcom -work cosim ../cosim_jtag_fli.vhd ../cosim_jtag.vhd

# Compile our testbench design file.
vcom tb.vhd

# Compile our C file into a shared library.
gcc -shared -fPIC -o cosim_jtag.so ../cosim_jtag.c

# Run the simulation in the background.
# -> Shared library "cosim_jtag.so" is automatically loaded.
vsim -c tb -do "run -all" &

# Wait a bit to ensure simulation could boot and UNIX socket could be created.
sleep 5

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
