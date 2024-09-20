/**
 * @file cosim_jtag.c
 * @author Niklaus Leuenberger <@NikLeberg>
 * @brief Implements interface between VHDL (through VHPIDIRCET or MTI FLI) and
 *        OpenOCD (through remote bitbanging socket).
 * @version 0.4
 * @date 2024-09-20
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
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define SOCKET_NAME "/tmp/cosim_jtag.sock"
static int listen_socket = -1;
static int data_socket = -1;

static int create_socket(void)
{
    int ret;

    unlink(SOCKET_NAME);

    listen_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_socket == -1)
    {
        fprintf(stderr, "cosim_jtag: create_socket failed to make socket: %s (%d)\n", strerror(errno), errno);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_NAME, sizeof(addr.sun_path) - 1);
    ret = bind(listen_socket, (const struct sockaddr *)&addr,
               sizeof(struct sockaddr_un));
    if (ret == -1)
    {
        fprintf(stderr, "cosim_jtag: create_socket failed to bind socket: %s (%d)\n", strerror(errno), errno);
        exit(EXIT_FAILURE);
    }

    // The processing on the socket is called from within GHDL and cannot run
    // concurrently, we must not block.
    fcntl(listen_socket, F_SETFL, O_NONBLOCK);

    ret = listen(listen_socket, 0);
    if (ret == -1)
    {
        fprintf(stderr, "cosim_jtag: create_socket failed to listen on socket: %s (%d)\n", strerror(errno), errno);
        exit(EXIT_FAILURE);
    }
    
    fputs("cosim_jtag: created unix socket at: " SOCKET_NAME "\n", stderr);
}

static void accept_connection(void)
{
    data_socket = accept(listen_socket, NULL, NULL);
    if (data_socket == -1)
    {
        if (errno != EAGAIN)
        {
            fprintf(stderr, "cosim_jtag: accept_connection failed with: %s (%d)\n", strerror(errno), errno);
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        fprintf(stderr, "cosim_jtag: remote connected\n");
    }
}

// Possible states of an VHDL STD_ULOGIC enumeration.
enum HDL_LOGIC_STATES
{
    HDL_U = 0, // Uninitialized
    HDL_X = 1, // Forcing Unknown
    HDL_0 = 2, // Forcing 0
    HDL_1 = 3, // Forcing 1
    HDL_Z = 4, // High Impedance
    HDL_W = 5, // Weak Unknown
    HDL_L = 6, // Weak 0
    HDL_H = 7, // Weak 1
    HDL_D = 8  // Don't care
};
#define HDL_TO_INT(hdl) ((hdl) == HDL_1 || (hdl) == HDL_H)
#define INT_TO_HDL(i) (((i) != 0) ? HDL_1 : HDL_0)

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
        fprintf(stderr, "cosim_jtag: process_socket failed to read: %s (%d)\n", strerror(errno), errno);
        exit(EXIT_FAILURE);
    }

    if (ret == 0)
    {
        return; // no data to process
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
            fprintf(stderr, "cosim_jtag: process_socket failed to write: %s (%d)\n", strerror(errno), errno);
            exit(EXIT_FAILURE);
        }
        break;
    case 'Q': // Quit request
        fprintf(stderr, "cosim_jtag: remote disconnected\n");
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
        create_socket();
    }

    // Accept any incoming connections from OpenOCD (if any).
    if (data_socket == -1)
    {
        accept_connection();
    }

    // Process data from socket.
    if (data_socket != -1)
    {
        process_socket(tdo, &state);
    }

    // Always "drive" the output signals.
    drive_from_state(&state, tck, tms, tdi, trst, srst);
}
