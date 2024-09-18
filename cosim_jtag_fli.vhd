-- =============================================================================
-- File:                    cosim_jtag_fli.vhdl
--
-- Package:                 cosim_jtag_pkg
--
-- Description:             ModelSim / QuestaSim specific implementation of
--                          foreign interface from VHDL to C. Utilizes the
--                          proprietary Model Technology Incorporated (MIT)
--                          Foreign Language Interface (FLI). It behaves almost
--                          identical to standart VHPIDIRECT if used on
--                          procedures. The only difference is the value of the
--                          "foreign" attribute.
--
-- Author:                  Niklaus Leuenberger <@NikLeberg>
--
-- SPDX-License-Identifier: MIT
--
-- Version:                 0.1
--
-- Changes:                 0.1, 2024-09-17, NikLeberg
--                              initial version
-- =============================================================================

LIBRARY ieee;
USE ieee.std_logic_1164.ALL;

PACKAGE cosim_jtag_pkg IS
    -- Exchange values between VHDL and C.
    PROCEDURE tick (
        tdo                       : IN STD_ULOGIC; -- current value of tdo
        tck, tms, tdi, trst, srst : OUT STD_ULOGIC
    );
    -- ModelSim/QuestaSim specific way of declaring foreign MTI FLI C-function:
    --  -> "<c_function> <shared_library>"
    ATTRIBUTE foreign OF tick : PROCEDURE IS "cosim_jtag_tick ./cosim_jtag.so";
END PACKAGE;

PACKAGE BODY cosim_jtag_pkg IS
    PROCEDURE tick (
        tdo                       : IN STD_ULOGIC; -- current value of tdo
        tck, tms, tdi, trst, srst : OUT STD_ULOGIC
    ) IS
    BEGIN
        -- dummy implementation, gets overwritten by C function cosim_jtag_tick
        REPORT "ERROR: foreign subprogram cosim_jtag_tick not called" SEVERITY failure;
    END;
END PACKAGE BODY;
