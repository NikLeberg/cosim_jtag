#!/usr/bin/env bash

# This script is meant to test and showcase the functionality of vhpi_jtag
# together with neorv32 from https://github.com/stnolting/neorv32 as softcore.
# The script assumes an installed ghdl with llvm (or gcc) backend (mcode won't
# work!). This could for example be inside a docker container from image
# gcr.io/hdl-containers/ghdl/llvm.

set -e

# Install openocd if not existing.
if ! [ -x "$(command -v openocd)" ]; then
    apt-get update
    apt-get install -y --no-install-recommends openocd
fi

# Install gdb if not existing.
if ! [ -x "$(command -v gdb-multiarch)" ]; then
    apt-get update
    apt-get install -y --no-install-recommends gdb-multiarch
fi

# Clone the NEORV32 softcore and roll-back to a specific stable commit.
if [ ! -d "./neorv32" ]; then
    git clone https://github.com/stnolting/neorv32
    cd neorv32
    git reset --hard ec2e2bb
    cd ..
fi

mkdir -p build

# Gather NEORV32 design files.
NEORV32_LOCAL_RTL=./neorv32/rtl
FILE_LIST=`cat $NEORV32_LOCAL_RTL/file_list_soc.f`
CORE_SRCS="${FILE_LIST//NEORV32_RTL_PATH_PLACEHOLDER/"$NEORV32_LOCAL_RTL"}"
ghdl -i --work=neorv32 --workdir=build $CORE_SRCS

# Add our own design files.
ghdl -i --work=neorv32 --workdir=build ../vhpi_jtag.vhd ./tb.vhd

# Make (i.e. analyze, elaborate and link) the design with GHDL into an exe.
ghdl -m --work=neorv32 --workdir=build -Wl,../vhpi_jtag.c tb

# Run the simulation in the background.
./tb --max-stack-alloc=0 --ieee-asserts=disable &

# Wait a bit to ensure simulation could boot and UNIX socket could be created.
sleep 2

# Run openocd in the background.
# -> If this is erroring out, see whats going wrong by adding debugging flag -d.
# -> If an invalid tap/device id is read: Try to increase DELAY generic in VHDL.
openocd -f openocd.cfg &

# Wait a bit longer to ensure openocd could examine hart and start gdb server.
sleep 10

# Run some debugging
gdb-multiarch --batch -x gdb.cfg

# Stop background openocd and GHDL simulation.
kill %2
sleep 1
kill %1
