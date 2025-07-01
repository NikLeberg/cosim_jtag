-- =============================================================================
-- File:                    cosim_tap.vhdl
--
-- Entity:                  cosim_tap
--
-- Description:             Co-simulation virtual JTAG TAP. Allows to connect to
--                          the running simulation trough foreign language
--                          interfaces like:
--                           - VHPI
--                           - VHPIDIRECT (e.g. GHDL)
--                           - MTI FLI (ModelSim or QuestaSim)
--
-- Note #1:                 IR register is managed internally. On TAP reset
--                          request it is automatically set to the configured
--                          IR_IDCODE.
--
-- Note #2:                 Rapid updates to the IR and DR registers may not be
--                          supported by the logic driven by this TAP. You can
--                          delay the incoming updates with the DELAY generic.
--                          With a DELAY != 0, the registers may only change
--                          after DELAY cycles of clk. Only set DELAY = 0 if the
--                          driven logic is capable of processing changing IR
--                          and DR registers on each clk.
--
-- Author:                  Niklaus Leuenberger <@NikLeberg>
--
-- SPDX-License-Identifier: MIT
--
-- Version:                 0.1
--
-- Changes:                 0.1, 2025-08-09, NikLeberg
--                              initial version
-- =============================================================================

LIBRARY ieee;
USE ieee.std_logic_1164.ALL;
USE ieee.numeric_std.ALL;

LIBRARY cosim;
USE cosim.cosim_tap_pkg.ALL;

ENTITY cosim_tap IS
    GENERIC (
        PORT_NUM    : INTEGER RANGE 1024 TO 65535     := 5555;  -- TCP port number to use
        DELAY       : INTEGER RANGE 0 TO INTEGER'HIGH := 100;   -- delay in counts of clk, 0 is no delay
        IR_BITS     : INTEGER RANGE 1 TO 16           := 5;     -- size of IR register
        IR_IDCODE   : INTEGER                         := 16#0#; -- IR address of IDCODE register
        MAX_DR_BITS : INTEGER RANGE 1 TO 64           := 64;    -- maximum size of dr register
        DR_ADDR     : INTEGER_VECTOR;                           -- ir addresses of dr registers
        DR_BITS     : INTEGER_VECTOR                            -- sizes of dr registers
    );
    PORT (
        clk  : IN STD_ULOGIC; -- system clock
        trst : OUT STD_LOGIC; -- JTAG TAP reset, active-high

        -- IR register, read-only.
        ir_o      : OUT STD_ULOGIC_VECTOR(IR_BITS - 1 DOWNTO 0); -- new value of ir register
        ir_update : OUT STD_ULOGIC;                              -- ir update event, asserted when 'ir_o' is updated

        -- DR registers.
        dr_i      : IN dr_matrix_t(DR_ADDR'RANGE);  -- current values of dr registers
        dr_o      : OUT dr_matrix_t(DR_ADDR'RANGE); -- new values of dr registers
        dr_update : OUT STD_ULOGIC                  -- dr update event, asserted when 'dr_o' is updated
    );
END ENTITY;

ARCHITECTURE sim OF cosim_tap IS
    CONSTANT dummy              : INTEGER                  := init(PORT_NUM, IR_BITS, IR_IDCODE, MAX_DR_BITS, DR_ADDR, DR_BITS);
    SIGNAL dly_cnt, dly_cnt_nxt : INTEGER RANGE 0 TO DELAY := 0;

    SIGNAL ir : STD_ULOGIC_VECTOR(IR_BITS - 1 DOWNTO 0) := (OTHERS => '0');
BEGIN

    -- Delay calls to tick proc to slow down TAP processing in respect to clk.
    dly_cnt_nxt <= 0 WHEN dly_cnt >= DELAY ELSE
        dly_cnt + 1;
    dly_cnt <= dly_cnt_nxt WHEN rising_edge(clk);

    -- Call into C-function and exchange current JTAG TAP register values.
    tap_tick : PROCESS (clk)
        VARIABLE v_trst, v_ir_update, v_dr_update : STD_ULOGIC;
        VARIABLE v_ir                             : STD_ULOGIC_VECTOR(IR_BITS - 1 DOWNTO 0);
        VARIABLE v_dr_i, v_dr_o                   : STD_ULOGIC_VECTOR(MAX_DR_BITS - 1 DOWNTO 0);
    BEGIN
        IF rising_edge(clk) THEN
            IF dly_cnt = 0 THEN

                v_dr_i := (OTHERS => '0');
                FOR i IN DR_ADDR'RANGE LOOP
                    IF to_integer(UNSIGNED(ir)) = DR_ADDR(i) THEN
                        v_dr_i := dr_i(i);
                        EXIT;
                    END IF;
                END LOOP;

                tick(v_trst, v_ir, v_ir_update, v_dr_i, v_dr_o, v_dr_update);

                trst      <= v_trst;
                ir        <= v_ir;
                ir_o      <= v_ir;
                ir_update <= v_ir_update;

                FOR i IN DR_ADDR'RANGE LOOP
                    IF to_integer(UNSIGNED(v_ir)) = DR_ADDR(i) THEN
                        dr_o(i) <= v_dr_o;
                        EXIT;
                    END IF;
                END LOOP;

                dr_update <= v_dr_update;

            ELSE
                trst      <= '0';
                ir_update <= '0';
                dr_update <= '0';
            END IF;
        END IF;
    END PROCESS tap_tick;

END ARCHITECTURE;
