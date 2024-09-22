-- =============================================================================
-- File:                    cosim_jtag_ghdl.vhdl
--
-- Package:                 cosim_jtag_pkg

-- Description:             Standard VHDL Procedural Interface (VHPI) interface
--                          from VHDL to C. Utilizes indirect binding to
--                          register a procedure implementation.
--
-- Author:                  Niklaus Leuenberger <@NikLeberg>
--
-- SPDX-License-Identifier: MIT
--
-- Version:                 0.1
--
-- Changes:                 0.1, 2024-09-22, NikLeberg
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
    -- VHPI standard way of declaring foreign VHPI indirect C-function:
    --  -> "VHPI <shared_library> <c_function>"
    ATTRIBUTE foreign OF tick : PROCEDURE IS "VHPI cosim_jtag.so cosim_jtag_vhpi_exec";
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
