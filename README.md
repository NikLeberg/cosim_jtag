# cosim_jtag
> Connect to your VHDL simulation via JTAG!

Got tired of looking at those pesky waveforms while ultimately debugging your VHDL softcore in simulation? Ever wished you could just use the glory that is the gnu debugger `GDB` without actually having to use any real hardware? Well here is your answer. With the magic that are procedural interfaces like `VHPI` or the proprietary `MTI FLI`, we can interface from the running simulation to other software. That is Co-Simulation.

The communication channel from `GDB` to your softcore roughly looks like this:

```
+-----+     +---------+     +--------------+     +----------------+     +---------------------+
| GDB | <-> | OpenOCD | <-> | cosim_json.c | <-> | cosim_json.vhd | <-> | JTAG TAP / softcore |
+-----+  :  +---------+  :  +--------------+  :  +----------------+  :  +---------------------+
        TCP         UNIX socket             VHPI                   JTAG
                                         or VHPIDIRECT
                                         or MTI FLI

[   outside simulator <<] [>> inside simulator                                                ]
```

This repository contains a simple VHDL entity `cosim_jtag` with a corresponding C-API that exposes a named UNIX socket.
The VHDL entity can be used wherever you want to drive the typical `tdo, tck, tms` and `tdi` JTAG signals. During simulation a named UNIX socket is created to which OpenOCD can connect via the lovely `remote bitbanging` protocol.
GDB can then connect to OpenOCD as usual and off you go!


## Supported Simulators

| Simulator | Interface | Tested | Speed<sup><a href="#sup1" id="ref1">[1]</a></sup> |
|---|---|---|---|
| ModelSim | `MTI FLI` | :x: | ? |
| QuestaSim | `MTI FLI` | :white_check_mark: | _8m 46s_ |
| [ghdl](https://github.com/ghdl/ghdl) | `GHDL`<sup><a href="#sup2" id="ref2">[2]</a></sup> | :white_check_mark: | _6m 38s_ |
| [nvc](https://github.com/nickg/nvc) | `VHPI` or `GHDL`<sup><a href="#sup3" id="ref3">[3]</a></sup> | :white_check_mark: | _1m 32s_ |

<sup id="sup1">[1] Time it took to analyze, elaborate, simulate and debug with GDB an example softcore-system based on [NEORV32](https://github.com/stnolting/neorv32). See `test_<simulator>.sh` scripts.<a href="#ref1" title="Jump back.">↩</a></sup>

<sup id="sup2">[2] _ghdl_ implements a non-standard compliant `VHPIDIRECT` interface where instead of passing a single `const vhpiCbDataT*`, all arguments are passed more or less _1:1_.<a href="#ref2" title="Jump back.">↩</a></sup>

<sup id="sup3">[3] For portability, _nvc_ implements the same non-standard compliant `VHPIDIRECT` interface as _ghdl_ does. But it also implements standard compliant `VHPI`.<a href="#ref3" title="Jump back.">↩</a></sup>

Feel free to open an issue to request support for additional simulators or interfaces.


## Getting started

> [!NOTE]
> The following assumes prior knowledge about the general use of simulators, OpenOCD and GDB. For a more complete example see `test` subfolder. Especially examine the various simulator specific `test_<simulator>.sh` scripts that bring it all together.

First, analyze the design files into the `cosim` library. But besides the main `cosim_jtag.vhd` choose also **one** of the additional `cosim_jtag_<interface>.vhd` files, depending on your simulator.

| Simulator | additional `.vhd`-file | Example command |
|---|---|---|
| ModelSim | `cosim_jtag_fli.vhd` | `vcom -work cosim cosim_jtag.vhd cosim_jtag_fli.vhd` |
| QuestaSim | `cosim_jtag_fli.vhd` | `vcom -work cosim cosim_jtag.vhd cosim_jtag_fli.vhd` |
| ghdl<sup><a href="#sup4" id="ref4">[4]</a></sup> | `cosim_jtag_ghdl.vhd` | `ghdl -a --work=cosim cosim_jtag.vhd cosim_jtag_ghdl.vhd` |
| nvc | <center>`cosim_jtag_vhpi.vhd`<br>OR<sup><a href="#sup5" id="ref5">[5]</a></sup><br>`cosim_jtag_ghdl.vhd`</center> | <center>`nvc --work=cosim -a cosim_jtag.vhd cosim_jtag_vhpi.vhd`<br>OR<br>`nvc --work=cosim -a cosim_jtag.vhd cosim_jtag_ghdl.vhd`</center> |

<sup id="sup4">[4] ghdl with the mcode backend does currently not support `VHPIDIRECT`. Elaboration is expected to fail. Use LLVM or GCC backends.<a href="#ref4" title="Jump back.">↩</a></sup>

<sup id="sup5">[5] nvc supports both, standard `VHPI` or ghdl specific `VHPIDIRECT`. The _ghdl-way_ is a bit simpler and may be a tiny bit faster to simulate.<a href="#ref5" title="Jump back.">↩</a></sup>

Now, compile the C interface in `cosim_jtag.c` into a shared library:

```bash
gcc -shared -fPIC -o cosim_jtag.so cosim_jtag.c
```

If you use standard VHPI, you have to add precompiler flag `-DUSE_VHPI` to enable the (complex) VHPI abstraction layer. You may also have to add an include path to the `vhpi_user.h` header file of your simulator vendor with `-I<path_to_simulator>/include`.

Next, add the `cosim` library to your design (probably a testbench) and instantiate the `cosim_jtag` entity. Drive your JTAG TAP with its signals.

```vhdl
library cosim;

-- [...]

cosim_jtag_inst : entity cosim.cosim_jtag
  port map (
    clk => clk,
    tdo => con_jtag_tdo,
    tck => con_jtag_tck,
    tms => con_jtag_tms,
    tdi => con_jtag_tdi,
    trst => open, -- optional
    srst => open  -- optional
  );
```

> [!NOTE]
> Currently, only a single instance of the `cosim_jtag` entity is supported. If you require multiple simulated JTAG _connectors_ then, first, I want to see your use-case, seems pretty cool, second, open an issue. It will not be that hard to implement but complicate things a bit.

After also analyzing your own VHDL sources, elaborate your toplevel (assuming here file `tb.vhd` with toplevel `tb`). This process is simulator specific. For example with ghdl:

```shell
ghdl -a tb.vhd # analyze
ghdl -e tb     # elaborate
```

Now you should be ready to simulate. When starting the simulation some simulators (e.g. _nvc_) require your to explicitly load the previously compiled shared library `cosim_jtag.so`. Others load it automatically<sup><a href="#sup4" id="ref4">[4]</a></sup>. To start the simulation (with e.g. _nvc_) run:

```shell
nvc -r --load ./cosim_jtag.so tb # run simulation
```

<sup id="sup4">[4] Most simulators require the `./` for the current working cirectory prior to the library name. As otherwise the default dynamic linking mechanisms in i.e. Linux will only search the system path(s).<a href="#ref4" title="Jump back.">↩</a></sup>

If all went well, then your simulator output should contain:

```shell
cosim_jtag: created unix socket at: /tmp/cosim_jtag.sock
```

Connect to that socket with OpenOCD by selecting the `remote_bitbang` adapter in your config files.

```
adapter driver remote_bitbang
remote_bitbang_port 0
remote_bitbang_host /tmp/cosim_jtag.sock

<other config lines for jtag tap(s) and target(s)>
```

```shell
openocd -f openocd.cfg
```

Compared to running on a real target a simulated JTAG connection can be quite slow. So OpenOCD might complain about timeouts. You can increase the timeout for commands with the following in the config:

```
# timeout in seconds
riscv set_reset_timeout_sec 120
riscv set_command_timeout_sec 120
```

If everything has been successful so far, OpenOCD should print out something like this:

```
[...]
Info : Initializing remote_bitbang driver
Info : Connecting to unix socket /tmp/cosim_jtag.sock
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
- ModelSim FLI Reference Manual: https://users.ece.cmu.edu/~kbiswas/modelsim/se_fli.pdf
- OpenOCD remote_bitbang: https://github.com/openocd-org/openocd/blob/master/doc/manual/jtag/drivers/remote_bitbang.txt

### Similar Projects:
- Same idea but for Verilog: https://github.com/fjullien/jtag_vpi
- Same idea but for SystemVerilog: https://github.com/rdiez/jtag_dpi
- remote bitbanged JTAG for the rocket-chip (Scala): https://github.com/chipsalliance/rocket-chip/blob/master/src/main/resources/csrc/remote_bitbang.cc


## License
[MIT](LICENSE) © N. Leuenberger.
