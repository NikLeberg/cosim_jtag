# vhpi_jtag
> Connect to your GHDL simulation via JTAG!

Got tired of looking at those pesky waveforms while ultimately debugging your VHDL softcore in simulation? Ever wished you could just use the glory that is the gnu debugger `GDB` without actually having to use any real hardware? Well here is your answer. With the magic that is `VHPIDIRECT`, we can interface from the GHDL simulation to other software.

The communication channel from `gdb` to your softcore roughly looks like this:

```
GDB <-TCP-> OpenOCD <-remote bitbang-> vhpi_jtag.c <-VHPI / GHDL-> vhpi_jtag.vhd <-JTAG-> your softcore
```

This repository contains a simple VHDL entity `vhpi_jtag` with a corresponding C-API that exposes a named UNIX socket.
The VHDL entity can be used wherever you want to drive the typical `tdo, tck, tms` and `tdi` JTAG signals. During simulation a named UNIX socket is created to which OpenOCD can connect via the lovely `remote bitbanging` protocol.
GDB can then connect to OpenOCD as usual and off you go!


## Getting started

> The following assumes prior knowledge about the general use of ghdl, openocd and gdb. For a more complete example see `test` subfolder. Especially examine the `run.sh` script that brings it all together.

First, instantiate the `vhpi_jtag` entity in your design (probably an testbench) and drive your JTAG TAP with its signals.

```vhdl
  -- example:
  vhpi_jtag_inst : entity work.vhpi_jtag
    port map (
      clk => clk,
      tdo => con_jtag_tdo,
      tck => con_jtag_tck,
      tms => con_jtag_tms,
      tdi => con_jtag_tdi,
      trst => open,
      srst => open
    );
```

Next, analyze and elaborate the design files. But while elaborating, also link in the C-API in `vhpi_jtag.c`.

Note: GHDL with the mcode backend does currently not support VHPIDIRECT. Elaboration is expected to fail. Use LLVM or GCC backends.

```shell
ghdl -a vhpi_jtag.vhd <other VHDL sources> # analyse design files
ghdl -e -Wl,vhpi_jtag.c <toplevel entity> # elaborate toplevel
```

Now you should have a ghdl compiled executable with the name of your toplevel, lets name it `top`. Run it as you usually do run your simulations.

```shell
./top # run simulation
```

While the simulation is running, a named UNIX socked is created in `/tmp/vhpi_jtag.sock`. Connect to that socket with OpenOCD by selecting the `remote_bitbang` adapter in your config files.

```
adapter driver remote_bitbang
remote_bitbang_port 0
remote_bitbang_host /tmp/vhpi_jtag.sock

<other config lines for jtag tap(s) and target(s)>
```

```shell
openocd -f openocd.cfg
```

Compared to running on a real target a simulated JTAG connection can be quite slow. So OpenOCD might complain about timeouts. You can increase the timeout for commands with the following in the config:

```
riscv set_reset_timeout_sec 120
riscv set_command_timeout_sec 120
```

If everything has been successful so far, OpenOCD should print out something like this:

```
[...]
Info : Initializing remote_bitbang driver
Info : Connecting to unix socket /tmp/vhpi_jtag.sock
Info : remote_bitbang driver initialized
[...]
Info : JTAG tap: riscv.cpu tap/device found: 0x00000003 (mfg: 0x001 (AMD), part: 0x0000, ver: 0x0)
[...]
Info : Examined RISC-V core; found 1 harts
Info :  hart 0: XLEN=32, misa=0x40901103
Info : starting gdb server for riscv.cpu on 3333
```

All that's left to do is start up gdb and debug away:

```shell
gdb-multiarch -ex 'target extended-remote localhost:3333'
```


## Links

### Further Documentation
- Talking about why to do this (hint: cosimulation): https://section5.ch/doc/jtag/jtag-impl-ew2012.pdf
- GHDL and VHPIDIRECT: https://ghdl.github.io/ghdl-cosim/vhpidirect/
- OpenOCD remote_bitbang: https://github.com/openocd-org/openocd/blob/master/doc/manual/jtag/drivers/remote_bitbang.txt

### Similar Projects:
- Same idea but for Verilog: https://github.com/fjullien/jtag_vpi
- remote bitbanged JTAG for the rocket-chip (Scala): https://github.com/chipsalliance/rocket-chip/blob/master/src/main/resources/csrc/remote_bitbang.cc


## License
[MIT](LICENSE) © N. Leuenberger.
