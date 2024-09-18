-- =============================================================================
-- File:                    cosim_jtag.vhdl
--
-- Entity:                  cosim_jtag
--
-- Description:             Co-simulation virtual JTAG "connector". Allows to
--                          connect to the running simulation trough foreign
--                          language interfaces like:
--                           - VHPIDIRECT (e.g. GHDL)
--                           - MTI FLI (ModelSim or QuestaSim)
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
-- Version:                 0.4
--
-- Changes:                 0.1, 2024-08-09, NikLeberg
--                              initial version
--                          0.2, 2024-08-13, NikLeberg
--                              fixed warning in vhdl2008, clarified reset level
--                          0.3, 2024-09-16, NikLeberg
--                              simplify counter logic
--                          0.4, 2024-09-18, NikLeberg
--                              integrate fli interface, rename to cosim_jtag
-- =============================================================================

LIBRARY ieee;
USE ieee.std_logic_1164.ALL;
USE ieee.numeric_std.ALL;

LIBRARY cosim;
USE cosim.cosim_jtag_pkg.ALL;

ENTITY cosim_jtag IS
    GENERIC (
        DELAY : NATURAL := 3 -- delay in counts of clk, 0 is no delay
    );
    PORT (
        clk           : IN STD_ULOGIC; -- system clock
        tdo           : IN STD_ULOGIC;
        tck, tms, tdi : OUT STD_LOGIC;
        trst          : OUT STD_LOGIC; -- JTAG TAP reset, active-high
        srst          : OUT STD_LOGIC  -- system reset, active-high
    );
END ENTITY;

ARCHITECTURE sim OF cosim_jtag IS
    SIGNAL delay_count, delay_count_next : NATURAL RANGE 0 TO DELAY := 0;
BEGIN

    -- Delay calls to tick procedure to slow down tck in respect to clk.
    delay_count_next <= 0 WHEN delay_count >= DELAY ELSE
        delay_count + 1;
    delay_count <= delay_count_next WHEN rising_edge(clk);

    -- Call into C-function and exchange current JTAG signal values.
    jtag_tick : PROCESS (clk)
        VARIABLE v_tck, v_tms, v_tdi, v_trst, v_srst : STD_ULOGIC;
    BEGIN
        IF rising_edge(clk) THEN
            IF delay_count = 0 THEN
                tick(tdo, v_tck, v_tms, v_tdi, v_trst, v_srst);
                tck <= v_tck;
                tms <= v_tms;
                tdi <= v_tdi;
                trst <= v_trst;
                srst <= v_srst;
            END IF;
        END IF;
    END PROCESS jtag_tick;

END ARCHITECTURE;
