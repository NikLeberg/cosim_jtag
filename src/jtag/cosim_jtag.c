/**
 * @file cosim_jtag.c
 * @author Niklaus Leuenberger <@NikLeberg>
 * @brief Implements interface between VHDL (through VHPIDIRCET or MTI FLI) and
 *        OpenOCD (through remote bitbanging socket).
 * @version 0.6
 * @date 2025-08-09
 *
 * SPDX-License-Identifier: MIT
 *
 * Changes:
 * Version  Date        Author     Detail
 * 0.1      2024-08-09  NikLeberg  initial version
 * 0.2      2024-08-13  NikLeberg  initialize reset signals to '0' / logic low
 * 0.3      2024-09-17  NikLeberg  integrate with ModelSim / QuestaSim FLT
 *                                 interface and rename to cosim_jtag
 * 0.4      2024-08-20  NikLeberg  print success message on socket creation
 * 0.5      2024-08-22  NikLeberg  implement standard VHPI interface
 * 0.6      2025-08-15  NikLeberg  refactor to use cosim_common.h, bitbang is
 *                                 now using TCP socket and not named socket
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../cosim_common.h"

#define SOCKET_PORT 5555
static int listen_socket = -1;
static int data_socket = -1;

typedef struct
{
    // tdo is received from VHDL on every tick, not required to keep state
    char tck;
    char tms;
    char tdi;
    char trst;
    char srst;
} state_t;

// Current/last state of tck, tms, tdi, trst and srst.
static state_t state = {HDL_X, HDL_X, HDL_X, HDL_0, HDL_0};

static void drive_from_state(state_t *state, char *tck, char *tms, char *tdi, char *trst, char *srst)
{
    *tck = state->tck;
    *tms = state->tms;
    *tdi = state->tdi;
    *trst = state->trst;
    *srst = state->srst;
}

static void process_socket(char tdo, state_t *state)
{
    int ret;
    char buffer, val;

    // receive data from openocd through socket
    ret = read(data_socket, &buffer, 1);
    if (ret == -1)
    {
        if (errno == EAGAIN)
        {
            return; // no data to process
        }
        FAIL("cosim_jtag: process_socket read failed with: %s (%d)\n", strerror(errno), errno);
    }
    if (ret == 0)
    {
        PRINT("cosim_jtag: remote disconnected\n");
        close(data_socket);
        data_socket = -1;
        return;
    }

    // process received byte, protocol according to openocd docs:
    // https://github.com/openocd-org/openocd/blob/master/doc/manual/jtag/drivers/remote_bitbang.txt
    switch (buffer)
    {
    case 'B': // Blink on
    case 'b': // Blink off
        break;
    case 'R': // Read request
        val = HDL_TO_INT(tdo) ? '1' : '0';
        ret = write(data_socket, &val, 1);
        if (ret == -1)
        {
            FAIL("cosim_jtag: process_socket failed to write: %s (%d)\n", strerror(errno), errno);
        }
        break;
    case 'Q': // Quit request
        PRINT("cosim_jtag: remote disconnected\n");
        close(data_socket);
        data_socket = -1;
        break;
    case '0': // Write 0 0 0
    case '1': // Write 0 0 1
    case '2': // Write 0 1 0
    case '3': // Write 0 1 1
    case '4': // Write 1 0 0
    case '5': // Write 1 0 1
    case '6': // Write 1 1 0
    case '7': // Write 1 1 1
        val = buffer - '0';
        state->tck = INT_TO_HDL(val & 0b100);
        state->tms = INT_TO_HDL(val & 0b010);
        state->tdi = INT_TO_HDL(val & 0b001);
        break;
    case 'r': // Reset 0 0
    case 's': // Reset 0 1
    case 't': // Reset 1 0
    case 'u': // Reset 1 1
        val = buffer - 'r';
        state->trst = INT_TO_HDL(val & 0b10);
        state->srst = INT_TO_HDL(val & 0b01);
    default:
        break;
    }
}

// Interface to VHDL. This is our cyclic "tick" entrypoint. Simulators bind to
// this function and call it on each rising edge of the simulated clock. See
// VHDL side of the interface in file "cosim_jtag.vhd" together with simulator
// specific "cosim_jtag_<simulator_interface>.vhd" package file.
void cosim_jtag_tick(char tdo, char *tck, char *tms, char *tdi, char *trst, char *srst)
{
    // Create and open a named file socked if not already open.
    if (listen_socket == -1)
    {
        listen_socket = tcp_create_loopback_socket(SOCKET_PORT);
    }

    // Accept any incoming connections from OpenOCD (if any).
    if (data_socket == -1)
    {
        data_socket = tcp_accept_connection(listen_socket);
    }

    // Process data from socket.
    if (data_socket != -1)
    {
        process_socket(tdo, &state);
    }

    // Always "drive" the output signals.
    drive_from_state(&state, tck, tms, tdi, trst, srst);
}

#ifdef USE_VHPI

enum tick_param_e
{
    T_PARAM_TDO,
    T_PARAM_TCK,
    T_PARAM_TMS,
    T_PARAM_TDI,
    T_PARAM_TRST,
    T_PARAM_SRST,
    T_PARAM_MAX
};
static handle_exp_t param_handle_exp[T_PARAM_MAX] = {
    {"tdo", vhpiConstParamDeclK, vhpiLogicVal},
    {"tck", vhpiVarParamDeclK, vhpiLogicVal},
    {"tms", vhpiVarParamDeclK, vhpiLogicVal},
    {"tdi", vhpiVarParamDeclK, vhpiLogicVal},
    {"trst", vhpiVarParamDeclK, vhpiLogicVal},
    {"srst", vhpiVarParamDeclK, vhpiLogicVal}};
static vhpiHandleT handles[T_PARAM_MAX];

static void get_vhpi_input(const vhpiHandleT *handles, char *tdo)
{
    *tdo = vhpi_get_logic(handles[T_PARAM_TDO]);
}

static void set_vhpi_outputs(const vhpiHandleT *handles, char tck, char tms, char tdi, char trst, char srst)
{
    vhpi_put_logic(handles[T_PARAM_TCK], tck);
    vhpi_put_logic(handles[T_PARAM_TMS], tms);
    vhpi_put_logic(handles[T_PARAM_TDI], tdi);
    vhpi_put_logic(handles[T_PARAM_TRST], trst);
    vhpi_put_logic(handles[T_PARAM_SRST], srst);
}

static void exec_vhpi(const vhpiCbDataT *cb_data)
{
    if (vhpiProcDeclK != vhpi_get(vhpiKindP, cb_data->obj))
    {
        FAIL("cosim_jtag: callback expected VHPI object of kind 'vhpiProcDeclK' aka 'PROCEDURE'\n");
    }

    if (NULL == handles[0])
    {
        vhpi_lookup_handles(cb_data->obj, vhpiParamDecls, param_handle_exp, T_PARAM_MAX, handles);
    }

    char tdo, tck, tms, tdi, trst, srst;
    get_vhpi_input(handles, &tdo);
    cosim_jtag_tick(tdo, &tck, &tms, &tdi, &trst, &srst);
    set_vhpi_outputs(handles, tck, tms, tdi, trst, srst);
}

static void end_vhpi(const vhpiCbDataT *cb_data)
{
    vhpiHandleT *handles = (vhpiHandleT *)cb_data->user_data;
    for (int i = 0; i < T_PARAM_MAX; ++i)
    {
        if (NULL != handles[i])
        {
            vhpi_release_handle(handles[i]);
            handles[i] = NULL;
        }
    }
}

static void register_vhpi(const vhpiCbDataT *cb_data)
{
    vhpiForeignDataT foreign_data = {
        vhpiProcF,
        "cosim_jtag.so",        // must precisely match VHDL "foreign" attribute
        "cosim_jtag_vhpi_exec", // must precisely match VHDL "foreign" attribute
        NULL,
        exec_vhpi};
    vhpiHandleT cb_h = vhpi_register_foreignf(&foreign_data);
    if (!cb_h)
    {
        FAIL("cosim_jtag: failed to register VHPI foreign function");
    }
    vhpi_release_handle(cb_h);

    vhpiCbDataT end_cb = {
        .cb_rtn = end_vhpi,
        .reason = vhpiCbEndOfSimulation,
        .user_data = (void *)handles};
    vhpi_register_cb(&end_cb, 0);
}

// Interface to VHDL. This is our "init". VHPI enabled simulators call each
// function in the following list once on startup.
void (*vhpi_startup_routines[])() = {
    register_vhpi,
    NULL};

#endif // USE_VHPI
