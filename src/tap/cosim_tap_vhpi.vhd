-- =============================================================================
-- File:                    cosim_tap_vhpi.vhdl
--
-- Package:                 cosim_tap_pkg
--
-- Description:             Standard VHDL Procedural Interface (VHPI) from VHDL
--                          to C. Utilizes indirect binding to register
--                          procedure implementations.
--
-- Note:                    Tested to work with NVC v1.17.0 and later.
--
-- Author:                  Niklaus Leuenberger <@NikLeberg>
--
-- SPDX-License-Identifier: MIT
--
-- Version:                 0.1
--
-- Changes:                 0.1, 2025-06-30, NikLeberg
--                              initial version
-- =============================================================================

LIBRARY ieee;
USE ieee.std_logic_1164.ALL;

PACKAGE cosim_tap_pkg IS
    TYPE dr_matrix_t IS ARRAY (INTEGER RANGE <>) OF STD_ULOGIC_VECTOR;

    -- Initialize and parametrize TAP in C code.
    FUNCTION init (
        port_num    : IN INTEGER RANGE 1024 TO 65535; -- TCP port number to use
        ir_bits     : IN INTEGER RANGE 1 TO 16;       -- size of ir register
        ir_idcode   : IN INTEGER;                     -- IR address of IDCODE register
        max_dr_bits : IN INTEGER RANGE 1 TO 64;       -- maximum size of dr register
        dr_addr     : IN INTEGER_VECTOR;              -- ir addresses of dr registers
        dr_bits     : IN INTEGER_VECTOR               -- sizes of dr registers
    ) RETURN INTEGER;                             -- returns dummy value, ignore
    -- VHPI standard way of declaring foreign VHPI indirect C-function:
    --  -> "VHPI <shared_library> <c_function>"
    ATTRIBUTE foreign OF init : FUNCTION IS "VHPI cosim_tap.so cosim_tap_init";

    -- Exchange values between VHDL and C.
    PROCEDURE tick (
        VARIABLE trst      : OUT STD_LOGIC;         -- JTAG TAP reset, active-high
        VARIABLE ir_o      : OUT STD_ULOGIC_VECTOR; -- ir value to update
        VARIABLE ir_update : OUT STD_ULOGIC;        -- ir update event
        CONSTANT dr_i      : IN STD_ULOGIC_VECTOR;  -- dr value to capture
        VARIABLE dr_o      : OUT STD_ULOGIC_VECTOR; -- dr value to update
        VARIABLE dr_update : OUT STD_ULOGIC         -- dr update event
    );
    -- VHPI standard way of declaring foreign VHPI indirect C-function:
    --  -> "VHPI <shared_library> <c_function>"
    ATTRIBUTE foreign OF tick : PROCEDURE IS "VHPI cosim_tap.so cosim_tap_tick";
END PACKAGE;

PACKAGE BODY cosim_tap_pkg IS
    FUNCTION init (
        port_num    : IN INTEGER RANGE 1024 TO 65535; -- TCP port number to use
        ir_bits     : IN INTEGER RANGE 1 TO 16;       -- size of ir register
        ir_idcode   : IN INTEGER;                     -- IR address of IDCODE register
        max_dr_bits : IN INTEGER RANGE 1 TO 64;       -- maximum size of dr register
        dr_addr     : IN INTEGER_VECTOR;              -- ir addresses of dr registers
        dr_bits     : IN INTEGER_VECTOR               -- sizes of dr registers
    ) RETURN INTEGER IS                           -- returns TAP instance number
    BEGIN
        -- dummy implementation, gets overwritten by C function cosim_tap_init
        REPORT "ERROR: foreign subprogram cosim_tap_init not called" SEVERITY failure;
    END;

    PROCEDURE tick (
        VARIABLE trst      : OUT STD_LOGIC;         -- JTAG TAP reset, active-high
        VARIABLE ir_o      : OUT STD_ULOGIC_VECTOR; -- ir value to update
        VARIABLE ir_update : OUT STD_ULOGIC;        -- ir update event
        CONSTANT dr_i      : IN STD_ULOGIC_VECTOR;  -- dr value to capture
        VARIABLE dr_o      : OUT STD_ULOGIC_VECTOR; -- dr value to update
        VARIABLE dr_update : OUT STD_ULOGIC         -- dr update event
    ) IS
    BEGIN
        -- dummy implementation, gets overwritten by C function cosim_tap_tick
        REPORT "ERROR: foreign subprogram cosim_tap_tick not called" SEVERITY failure;
    END;
END PACKAGE BODY;
