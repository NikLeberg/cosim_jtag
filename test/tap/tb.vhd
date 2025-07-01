-- =============================================================================
-- File:                    tb.vhdl
--
-- Entity:                  tb
--
-- Description:             Testbench for cosim_tap functionality. Instantiates
--                          a NEORV32 softcore and lets us connect to it via
--                          OpenOCD to ultimately debug it with GDB.
--
-- Author:                  Niklaus Leuenberger <@NikLeberg>
--
-- SPDX-License-Identifier: MIT
--
-- Version:                 0.1
--
-- Changes:                 0.1, 2025-07-01, NikLeberg
--                              initial version
-- =============================================================================

LIBRARY ieee;
USE ieee.std_logic_1164.ALL;
USE ieee.numeric_std.ALL;

LIBRARY neorv32;
USE neorv32.neorv32_package.ALL;

ENTITY tb IS
END ENTITY;

ARCHITECTURE sim OF tb IS
    CONSTANT CLK_PERIOD : DELAY_LENGTH := 20 ns; -- 50 MHz
    SIGNAL clk : STD_LOGIC := '1';
    SIGNAL rstn : STD_ULOGIC := '0';
BEGIN

    -- Infinite clock.
    clk <= NOT clk AFTER 0.5 * CLK_PERIOD;

    -- Reset for first three clock cycles.
    rstn <= '0', '1' AFTER 3 * CLK_PERIOD;

    neorv32_inst : ENTITY neorv32.neorv32_top
        GENERIC MAP(
            -- General --
            CLOCK_FREQUENCY => (1 sec / CLK_PERIOD),
            JEDEC_ID => "00000000000",
            INT_BOOTLOADER_EN => true,
            -- On-Chip Debugger (OCD) --
            ON_CHIP_DEBUGGER_EN => true,
            -- Internal Instruction memory --
            MEM_INT_IMEM_EN => true,
            MEM_INT_IMEM_SIZE => 64 * 1024,
            -- Internal Data memory --
            MEM_INT_DMEM_EN => true,
            MEM_INT_DMEM_SIZE => 64 * 1024
        )
        PORT MAP(
            -- Global control --
            clk_i => clk,
            rstn_i => rstn
        );

END ARCHITECTURE;
