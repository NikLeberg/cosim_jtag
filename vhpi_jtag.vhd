-- =============================================================================
-- File:                    vhpi_jtag.vhdl
--
-- Entity:                  vhpi_jtag
--
-- Description:             Simulation only virtual JTAG "connector". Allows to
--                          connect to the running GHDL sim through OpenOCD.
--
-- Note:                    Logic that is driven by this JTAG "connector" is
--                          usually assuming some relation between clk and tck
--                          i.e. is doing some clock crossing. Use the DELAY
--                          generic to enforce this relation. Only set DELAY = 0
--                          if the driven logic is capable of processing
--                          changing tck, tms, and tdi signals on each clk.
--
-- Author:                  Niklaus Leuenberger <@NikLeberg>
--
-- SPDX-License-Identifier: MIT
--
-- Version:                 0.2
--
-- Changes:                 0.1, 2024-08-09, NikLeberg
--                              initial version
--        :                 0.2, 2024-08-13, NikLeberg
--                              fixed warning in vhdl2008, clarified reset level
-- =============================================================================

LIBRARY ieee;
USE ieee.std_logic_1164.ALL;
USE ieee.numeric_std.ALL;

ENTITY vhpi_jtag IS
    GENERIC (
        DELAY : NATURAL := 3 -- delay in counts of clk, 0 is no delay
    );
    PORT (
        clk : IN STD_ULOGIC; -- system clock
        tdo : IN STD_ULOGIC;
        tck, tms, tdi : OUT STD_LOGIC;
        trst : OUT STD_LOGIC; -- JTAG TAP reset, active-high
        srst : OUT STD_LOGIC -- system reset, active-high
    );
END ENTITY;

ARCHITECTURE sim OF vhpi_jtag IS

    SUBTYPE state_t IS STD_ULOGIC_VECTOR(0 TO 4); -- tck, tms, tdi, trst, srst
    TYPE state_ptr_t IS ACCESS state_t;

    -- Exchange values between VHDL and C through VHPIDIRECT.
    IMPURE FUNCTION tick (
        test_data_out : STD_LOGIC -- current value of tdo
    ) RETURN state_ptr_t IS
    BEGIN
        -- dummy implementation, gets overwritten by C function vhpi_jtag_tick
        REPORT "VHPIDIRECT vhpi_jtag_tick" SEVERITY failure;
    END;
    ATTRIBUTE foreign OF tick : FUNCTION IS "VHPIDIRECT vhpi_jtag_tick";

    SIGNAL delay_count : NATURAL RANGE 0 TO DELAY := 0;

BEGIN

    delay_counter : PROCESS (clk)
    BEGIN
        IF rising_edge(clk) THEN
            IF delay_count >= DELAY THEN
                delay_count <= 0;
            ELSE
                delay_count <= delay_count + 1;
            END IF;
        END IF;
    END PROCESS delay_counter;

    jtag_tick : PROCESS (clk)
        VARIABLE state : state_ptr_t;
    BEGIN
        IF rising_edge(clk) THEN
            IF delay_count = 0 THEN
                state := tick(tdo);
                tck <= state(0);
                tms <= state(1);
                tdi <= state(2);
                trst <= state(3);
                srst <= state(4);
            END IF;
        END IF;
    END PROCESS jtag_tick;

END ARCHITECTURE;