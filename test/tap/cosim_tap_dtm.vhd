-- ================================================================================ --
-- NEORV32 SoC - RISC-V-Compatible Debug Transport Module (DTM)                     --
-- -------------------------------------------------------------------------------- --
-- The NEORV32 RISC-V Processor - https://github.com/stnolting/neorv32              --
-- Copyright (c) NEORV32 contributors.                                              --
-- Copyright (c) 2020 - 2024 Stephan Nolting. All rights reserved.                  --
-- Licensed under the BSD-3-Clause license, see LICENSE for details.                --
-- SPDX-License-Identifier: BSD-3-Clause                                            --
-- ================================================================================ --

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library neorv32;
use neorv32.neorv32_package.all;

library cosim;
use cosim.cosim_tap_pkg.all;

entity neorv32_debug_dtm is
  generic (
    IDCODE_VERSION : std_ulogic_vector(3 downto 0);  -- version
    IDCODE_PARTID  : std_ulogic_vector(15 downto 0); -- part number
    IDCODE_MANID   : std_ulogic_vector(10 downto 0)  -- manufacturer id
  );
  port (
    -- global control --
    clk_i      : in  std_ulogic; -- global clock line
    rstn_i     : in  std_ulogic; -- global reset line, low-active
    -- jtag connection --
    jtag_tck_i : in  std_ulogic;
    jtag_tdi_i : in  std_ulogic;
    jtag_tdo_o : out std_ulogic;
    jtag_tms_i : in  std_ulogic;
    -- debug module interface (DMI) --
    dmi_req_o  : out dmi_req_t; -- request
    dmi_rsp_i  : in  dmi_rsp_t  -- response
  );
end neorv32_debug_dtm;

architecture cosim_tap_dtm_rtl of neorv32_debug_dtm is

  -- DMI Configuration (fixed!) --
  constant dmi_idle_c    : std_ulogic_vector(2 downto 0) := "000";    -- no idle cycles required
  constant dmi_version_c : std_ulogic_vector(3 downto 0) := "0001";   -- debug spec. version (0.13 & 1.0)
  constant dmi_abits_c   : std_ulogic_vector(5 downto 0) := "000111"; -- number of DMI address bits (7)

  -- TAP data register addresses --
  constant addr_idcode_c : std_ulogic_vector(4 downto 0) := "00001"; -- identifier
  constant addr_dtmcs_c  : std_ulogic_vector(4 downto 0) := "10000"; -- DTM status and control
  constant addr_dmi_c    : std_ulogic_vector(4 downto 0) := "10001"; -- debug module interface

  -- tap control --
  type tap_ctrl_t is record
    trst      : std_ulogic; -- TAP reset, active-high
    ir_update : std_ulogic; -- instruction register update
    dr_update : std_ulogic; -- data register update
  end record;
  signal tap_ctrl : tap_ctrl_t;

  -- tap registers --
  type tap_reg_t is record
    ireg             : std_ulogic_vector(4 downto 0);
    idcode           : std_ulogic_vector(31 downto 0);
    dtmcs, dtmcs_nxt : std_ulogic_vector(31 downto 0);
    dmi,   dmi_nxt   : std_ulogic_vector((7+32+2)-1 downto 0); -- 7-bit address + 32-bit data + 2-bit operation
  end record;
  signal tap_reg : tap_reg_t;

  signal dr_i, dr_o : dr_matrix_t(0 to 2)(tap_reg.dmi'range) := (others => (others => '0'));

  -- debug module interface controller --
  type dmi_ctrl_t is record
    busy         : std_ulogic;
    op           : std_ulogic_vector(1 downto 0);
    dmihardreset : std_ulogic;
    dmireset     : std_ulogic;
    err          : std_ulogic;
    rdata        : std_ulogic_vector(31 downto 0);
    wdata        : std_ulogic_vector(31 downto 0);
    addr         : std_ulogic_vector(6 downto 0);
  end record;
  signal dmi_ctrl : dmi_ctrl_t;

begin

  -- Tap Register Access --------------------------------------------------------------------
  -- -------------------------------------------------------------------------------------------
  cosim_tap_inst : entity cosim.cosim_tap
    generic map (
      PORT_NUM    => 5555,
      DELAY       => 5,
      IR_BITS     => 5,
      IR_IDCODE   => to_integer(UNSIGNED(addr_idcode_c)),
      MAX_DR_BITS => 41,
      DR_ADDR     => (to_integer(UNSIGNED(addr_idcode_c)),
                      to_integer(UNSIGNED(addr_dtmcs_c)),
                      to_integer(UNSIGNED(addr_dmi_c))),
      DR_BITS     => (32, 32, 41)
    )
    port map (
      clk       => clk_i,
      trst      => tap_ctrl.trst,
      ir_o      => tap_reg.ireg,
      ir_update => tap_ctrl.ir_update,
      dr_i      => dr_i,
      dr_o      => dr_o,
      dr_update => tap_ctrl.dr_update
    );

  dr_i(0)(tap_reg.idcode'range)    <= IDCODE_VERSION & IDCODE_PARTID & IDCODE_MANID & '1';
  dr_i(1)(tap_reg.dtmcs_nxt'range) <= tap_reg.dtmcs_nxt;
  dr_i(2)(tap_reg.dmi_nxt'range)   <= tap_reg.dmi_nxt;

  tap_reg.idcode <= dr_o(0)(tap_reg.idcode'range);
  tap_reg.dtmcs  <= dr_o(1)(tap_reg.dtmcs'range);
  tap_reg.dmi    <= dr_o(2)(tap_reg.dmi'range);

  jtag_tdo_o <= '0';

  -- DTM Control and Status Register (dtmcs) --
  tap_reg.dtmcs_nxt(31 downto 18) <= (others => '0'); -- reserved
  tap_reg.dtmcs_nxt(17)           <= dmi_ctrl.dmihardreset; -- dmihardreset
  tap_reg.dtmcs_nxt(16)           <= dmi_ctrl.dmireset; -- dmireset
  tap_reg.dtmcs_nxt(15)           <= '0'; -- reserved
  tap_reg.dtmcs_nxt(14 downto 12) <= dmi_idle_c; -- minimum number of idle cycles
  tap_reg.dtmcs_nxt(11 downto 10) <= tap_reg.dmi_nxt(1 downto 0); -- dmistat
  tap_reg.dtmcs_nxt(09 downto 04) <= dmi_abits_c; -- number of DMI address bits
  tap_reg.dtmcs_nxt(03 downto 00) <= dmi_version_c; -- version

  -- DMI register read access --
  tap_reg.dmi_nxt(40 downto 34) <= dmi_ctrl.addr; -- address
  tap_reg.dmi_nxt(33 downto 02) <= dmi_ctrl.rdata; -- read data
  tap_reg.dmi_nxt(01 downto 00) <= (others => dmi_ctrl.err); -- status


  -- Debug Module Interface -----------------------------------------------------------------
  -- -------------------------------------------------------------------------------------------
  dmi_controller: process(rstn_i, clk_i)
  begin
    if (rstn_i = '0') then
      dmi_ctrl.busy         <= '0';
      dmi_ctrl.op           <= "00";
      dmi_ctrl.dmihardreset <= '1';
      dmi_ctrl.dmireset     <= '0';
      dmi_ctrl.err          <= '0';
      dmi_ctrl.rdata        <= (others => '0');
      dmi_ctrl.wdata        <= (others => '0');
      dmi_ctrl.addr         <= (others => '0');
    elsif rising_edge(clk_i) then
      -- DMI reset control --
      if (tap_ctrl.dr_update = '1') and (tap_reg.ireg = addr_dtmcs_c) then
        -- report "DMI: reset requested" severity note;
        dmi_ctrl.dmireset     <= tap_reg.dtmcs(16);
        dmi_ctrl.dmihardreset <= tap_reg.dtmcs(17);
      elsif (dmi_ctrl.busy = '0') then
        dmi_ctrl.dmihardreset <= '0';
        dmi_ctrl.dmireset     <= '0';
      end if;

      -- sticky error --
      if (dmi_ctrl.dmireset = '1') or (dmi_ctrl.dmihardreset = '1') then
        dmi_ctrl.err <= '0';
        -- report "DMI: clearing error flag" severity note;
      elsif (dmi_ctrl.busy = '1') and (tap_ctrl.dr_update = '1') and (tap_reg.ireg = addr_dmi_c) then -- access attempt while DMI is busy
        dmi_ctrl.err <= '1';
        -- report "DMI: access attempt while DMI is busy" severity failure;
      end if;

      -- DMI interface arbiter --
      dmi_ctrl.op <= dmi_req_nop_c; -- default
      if (dmi_ctrl.busy = '0') then -- idle: waiting for new request
        if (dmi_ctrl.dmihardreset = '0') then -- no DMI hard reset
          if (tap_ctrl.dr_update = '1') and (tap_reg.ireg = addr_dmi_c) then
            dmi_ctrl.addr  <= tap_reg.dmi(40 downto 34);
            dmi_ctrl.wdata <= tap_reg.dmi(33 downto 02);
            if (tap_reg.dmi(1 downto 0) = dmi_req_rd_c) or (tap_reg.dmi(1 downto 0) = dmi_req_wr_c) then
              dmi_ctrl.op   <= tap_reg.dmi(1 downto 0);
              dmi_ctrl.busy <= '1';
            end if;
            if (tap_reg.dmi(1 downto 0) = dmi_req_rd_c) then
              -- report "DMI: reading from 0x" & to_hstring(tap_reg.dmi(40 downto 34)) severity note;
            end if;
            if (tap_reg.dmi(1 downto 0) = dmi_req_wr_c) then
              -- report "DMI: writing to 0x" & to_hstring(tap_reg.dmi(40 downto 34)) & " = 0x" & to_hstring(tap_reg.dmi(33 downto 02)) severity note;
            end if;
          end if;
        end if;
      else -- busy: read/write access in progress
        dmi_ctrl.rdata <= dmi_rsp_i.data;
        if (dmi_rsp_i.ack = '1') then
          dmi_ctrl.busy <= '0';
        end if;
      end if;
    end if;
  end process dmi_controller;

  -- direct DMI output --
  dmi_req_o.op   <= dmi_ctrl.op;
  dmi_req_o.data <= dmi_ctrl.wdata;
  dmi_req_o.addr <= dmi_ctrl.addr;


end cosim_tap_dtm_rtl;
