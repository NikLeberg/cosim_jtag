/**
 * @file vhpi_jtag.c
 * @author Niklaus Leuenberger <@NikLeberg>
 * @brief Implements interface between GHDL (through VHPIDIRCET) and OpenOCD
 *        (through remote bitbanging socket).
 * @version 0.2
 * @date 2024-08-13
 *
 * SPDX-License-Identifier: MIT
 *
 * Changes:
 * Version  Date        Author     Detail
 * 0.1      2024-08-09  NikLeberg  initial version
 * 0.2      2024-08-13  NikLeberg  initialize reset signals to '0' / logic low
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

#define SOCKET_NAME "/tmp/vhpi_jtag.sock"
static int listen_socket = -1;
static int data_socket = -1;

static int create_socket(void)
{
    int ret;

    unlink(SOCKET_NAME);

    listen_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_socket == -1)
    {
        fprintf(stderr, "vhpi_jtag: create_socket failed to make socket: %s (%d)\n", strerror(errno), errno);
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
        fprintf(stderr, "vhpi_jtag: create_socket failed to bind socket: %s (%d)\n", strerror(errno), errno);
        exit(EXIT_FAILURE);
    }

    // The processing on the socket is called from within GHDL and cannot run
    // concurrently, we must not block.
    fcntl(listen_socket, F_SETFL, O_NONBLOCK);

    ret = listen(listen_socket, 0);
    if (ret == -1)
    {
        fprintf(stderr, "vhpi_jtag: create_socket failed to listen on socket: %s (%d)\n", strerror(errno), errno);
        exit(EXIT_FAILURE);
    }
}

static void accept_connection(void)
{
    data_socket = accept(listen_socket, NULL, NULL);
    if (data_socket == -1)
    {
        if (errno != EAGAIN)
        {
            fprintf(stderr, "vhpi_jtag: accept_connection failed with: %s (%d)\n", strerror(errno), errno);
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        fprintf(stderr, "vhpi_jtag: remote connected\n");
    }
}

// Possible states of an VHDL STD_ULOGIC according to GHDL.
enum HDL_LOGIC_STATES
{
    HDL_U = 0,
    HDL_X = 1,
    HDL_0 = 2,
    HDL_1 = 3,
    HDL_Z = 4,
    HDL_W = 5,
    HDL_L = 6,
    HDL_H = 7,
    HDL_D = 8
};
#define HDL_TO_INT(hdl) (hdl == HDL_1 || hdl == HDL_H)
#define INT_TO_HDL(i) (i ? HDL_1 : HDL_0)

// Current/last state of tck, tms, tdi, trst and srst.
static char state[5] = {HDL_X, HDL_X, HDL_X, HDL_0, HDL_0};

// Set the state of tck, tms and tdi.
static void set_state(int tck, int tms, int tdi)
{
    state[0] = INT_TO_HDL(tck);
    state[1] = INT_TO_HDL(tms);
    state[2] = INT_TO_HDL(tdi);
}

// Set the state of trst and srst.
static void set_reset(int trst, int srst)
{
    state[3] = INT_TO_HDL(trst);
    state[4] = INT_TO_HDL(srst);
}

static void process_socket(char tdo)
{
    int ret;
    char buffer;

    // receive data from openocd through socket
    ret = read(data_socket, &buffer, 1);
    if (ret == -1)
    {
        fprintf(stderr, "vhpi_jtag: process_socket failed to read: %s (%d)\n", strerror(errno), errno);
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
        buffer = HDL_TO_INT(tdo) ? '1' : '0';
        ret = write(data_socket, &buffer, 1);
        if (ret == -1)
        {
            fprintf(stderr, "vhpi_jtag: process_socket failed to write: %s (%d)\n", strerror(errno), errno);
            exit(EXIT_FAILURE);
        }
        break;
    case 'Q': // Quit request
        fprintf(stderr, "vhpi_jtag: remote disconnected\n");
        close(data_socket);
        data_socket = -1;
        break;
    case '0': // Write 0 0 0
        set_state(0, 0, 0);
        break;
    case '1': // Write 0 0 1
        set_state(0, 0, 1);
        break;
    case '2': // Write 0 1 0
        set_state(0, 1, 0);
        break;
    case '3': // Write 0 1 1
        set_state(0, 1, 1);
        break;
    case '4': // Write 1 0 0
        set_state(1, 0, 0);
        break;
    case '5': // Write 1 0 1
        set_state(1, 0, 1);
        break;
    case '6': // Write 1 1 0
        set_state(1, 1, 0);
        break;
    case '7': // Write 1 1 1
        set_state(1, 1, 1);
        break;
    case 'r': // Reset 0 0
        set_reset(0, 0);
        break;
    case 's': // Reset 0 1
        set_reset(0, 1);
        break;
    case 't': // Reset 1 0
        set_reset(1, 0);
        break;
    case 'u': // Reset 1 1
        set_reset(1, 1);
        break;
    default:
        break;
    }
}

// VHPI interface to VHDL. This is our "main" entrypoint. GHDL binds to this
// function and calls it on each rising edge of the simulated clock. See VHDL
// side of the interface in file "vhpi_jtag.vhd".
char *vhpi_jtag_tick(char tdo)
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
        process_socket(tdo);
    }

    return state;
}
