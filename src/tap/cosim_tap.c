/**
 * @file cosim_tap.c
 * @author Niklaus Leuenberger <@NikLeberg>
 * @brief Implements interface between VHDL (through VHPI, VHPIDIRCET or
 *        MTI FLI) and OpenOCD (through jtag_dpi driver).
 * @version 0.1
 * @date 2025-06-30
 *
 * SPDX-License-Identifier: MIT
 *
 * Changes:
 * Version  Date        Author     Detail
 * 0.1      2025-06-30  NikLeberg  initial version
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../cosim_common.h"

typedef struct dr_reg_s
{
    int addr; // ir address of register
    int bits; // size of register
} dr_reg_t;

enum step_e
{
    STEP_OP = 0,
    STEP_REM,
    STEP_SIZE,
    STEP_DATA
};

#define MAX_NUM_DR_REGS (8) // maximum number of dr registers

#ifdef USE_VHPI
struct vhpi_inst_s; // forward declaration
#endif

typedef struct inst_s
{
    struct
    {
        int port_num; // TCP port number
        int ir_bits;  // size of ir register
        int ir_bytes;
        int ir_default; // default/reset value of ir register
        int dr_bits;    // maximum size of dr register
        int dr_bytes;
        dr_reg_t dr_conf[MAX_NUM_DR_REGS]; // configuration of dr registers
    } config;

    struct
    {
        int socket_fd; // socket file descriptor
        int data_fd;   // data socket file descriptor
    } conn;

    struct
    {
        int initialized; // = 1 if instance is initialized
        enum step_e step;
        char op;
        size_t nbits;
        char *ir; // [0] is bit0, [1] is bit1, ...
        char *dr_o;
    } state;

#ifdef USE_VHPI
    struct vhpi_inst_s *vhpi;
#endif
} inst_t;

static inst_t inst;

static int calc_dr_bits(inst_t *inst)
{
    int ir_value = 0;
    for (int i = 0; i < inst->config.ir_bits; ++i)
    {
        if (HDL_TO_INT(inst->state.ir[i]))
        {
            ir_value |= (1 << i);
        }
    }

    for (int i = 0; i < MAX_NUM_DR_REGS && inst->config.dr_conf[i].addr != 0; ++i)
    {
        if (inst->config.dr_conf[i].addr == ir_value)
        {
            return inst->config.dr_conf[i].bits;
        }
    }

    FAIL("cosim_tap: calc_dr_bits: no matching dr register found for ir value %d\n", ir_value);
    return 0;
}

static void process_step_op(inst_t *inst)
{
    // read type of operation
    ssize_t ret = read(inst->conn.data_fd, &inst->state.op, 1);
    if (ret == -1)
    {
        if (errno == EAGAIN)
        {
            return; // no data to process
        }
        FAIL("cosim_tap: process_step_op: read failed with: %s (%d)\n",
             strerror(errno), errno);
    }

    switch (inst->state.op)
    {
    case 'i':
    case 'd':
    case 'r':
        inst->state.step = STEP_REM;
        break;
    default:
        PRINT("cosim_tap: process_step_op: received invalid operation '%c'. "
              " Out of sync?",
              inst->state.op);
        // stay in STEP_OP
        break;
    }
}

static void process_step_remainder(inst_t *inst, char *trst)
{
    // read "remainder" of the op type
    int is_reset = inst->state.op == 'r';
    size_t size = is_reset ? 5 : 2; // "eset\n" or "b\n"
    char rem[5];
    if (read(inst->conn.data_fd, rem, size) != size)
    {
        if (errno == EAGAIN)
        {
            return; // no data to process
        }
        FAIL("cosim_tap: process_step_remainder: read failed with: %s (%d)\n",
             strerror(errno), errno);
    }

    if (is_reset)
    {
        // on reset, JTAG defines that IR reg shall be set to IDCODE
        for (int i = 0; i < inst->config.ir_bits; ++i)
        {
            inst->state.ir[i] = INT_TO_HDL((inst->config.ir_default >> i) & 1);
        }
        *trst = HDL_1;
    }

    inst->state.nbits = 0;
    inst->state.step = is_reset ? STEP_OP : STEP_SIZE;
}

static void process_step_size(inst_t *inst)
{
    // read size (in bits) argument
    int ret;
    char val;
    size_t *nbits = &inst->state.nbits;
    while ((ret = read(inst->conn.data_fd, &val, 1)) > 0)
    {
        if (val == '\n')
        {
            break;
        }
        *nbits = (10 * *nbits) + (val - '0');
    }
    if (ret == -1)
    {
        if (errno == EAGAIN)
        {
            return; // no data to process
        }
        FAIL("cosim_tap: process_step_size: read failed with: %s (%d)\n",
             strerror(errno), errno);
    }
    inst->state.step = STEP_DATA;
}

static void process_step_data(inst_t *inst, char *dr_i, char *ir_update, char *dr_update)
{
    // this assumes no chunked transfer i.e. that all nbytes of the current
    // operation are available in one-go
    size_t nbytes = (inst->state.nbits + 7) / 8;
    char rx_buf[nbytes];
    if (read(inst->conn.data_fd, rx_buf, nbytes) != nbytes)
    {
        if (errno == EAGAIN)
        {
            return; // no data to process
        }
        FAIL("cosim_tap: process_step_data: failed to read %zu data bytes from "
             "socket: %s (%d)\n",
             nbytes, strerror(errno), errno);
    }

    // prepare response buffer now, because we have to send the old value of
    // the registers
    int is_ir_op = inst->state.op == 'i';
    char tx_buf[nbytes];
    size_t nbits_write = 0;
    size_t nbits_max = is_ir_op ? inst->config.ir_bits : calc_dr_bits(inst);
    char *reg_i = is_ir_op ? inst->state.ir : dr_i;
    for (size_t i = 0; i < nbytes; ++i)
    {
        tx_buf[i] = 0xff;
        for (int j = 0; j < 8 && nbits_write < nbits_max; ++j, ++nbits_write)
        {
            tx_buf[i] &= ~(!HDL_TO_INT(reg_i[nbits_write]) << j);
        }
    }

    // convert received bytes to bits and write to registers
    size_t nbits_read = 0;
    char *reg_o = is_ir_op ? inst->state.ir : inst->state.dr_o;
    for (size_t i = 0; i < nbytes && nbits_read < nbits_max; ++i)
    {
        for (int j = 0; j < 8 && nbits_read < nbits_max; ++j, ++nbits_read)
        {
            reg_o[nbits_read] = INT_TO_HDL(rx_buf[i] & (1 << j));
        }
    }

    // write response back to socket
    if (write(inst->conn.data_fd, tx_buf, nbytes) != nbytes)
    {
        FAIL("cosim_tap: process_socket: failed to write %zu data bytes to "
             "socket: %s (%d)\n",
             nbytes, strerror(errno), errno);
    }

    *ir_update = is_ir_op ? HDL_1 : HDL_0;
    *dr_update = is_ir_op ? HDL_0 : HDL_1;

    inst->state.step = STEP_OP; // restart
}

static void process_socket(inst_t *inst, char *trst, char *ir_update,
                           char *dr_i, char *dr_update)
{
    // Process incomming data from TCP stream.
    // TCP socket may not have all the data available to read at once. Step
    // through the required reads one-by-one. If at any time EAGAIN is returned,
    // we can stop the current processing and continue on the next tick.
    // Protocol is:
    // "ib N\n"  -> receive N bits of ir register value
    //              respond with N bits of current ir value
    // "db N\n"  -> receive N bits of dr register value
    //              respond with N bits of current dr value
    // "reset\n" -> trigger an JTAG TAP reset
    ssize_t ret;
    char val;
    while ((ret = recv(inst->conn.data_fd, &val, 1, MSG_PEEK)) == 1)
    {
        // I/O over socket is non-blocking and may not have all the data
        // available in one-go. Step through sequence of reads/writes as long as
        // there is data and we do not require a tick.
        switch (inst->state.step)
        {
        case STEP_OP:
            process_step_op(inst);
            break;
        case STEP_REM:
            process_step_remainder(inst, trst);
            break;
        case STEP_SIZE:
            process_step_size(inst);
            break;
        case STEP_DATA:
            process_step_data(inst, dr_i, ir_update, dr_update);
            break;
        }
        if (*trst == HDL_1 || *ir_update == HDL_1 || *dr_update == HDL_1)
        {
            // return to simulator to let HDL process received event
            return;
        }
    }

    if (ret == 0)
    {
        PRINT("cosim_tap: remote disconnected\n");
        close(inst->conn.data_fd);
        inst->conn.data_fd = -1;
        return;
    }
}

void cosim_tap_release(void)
{
    if (inst.conn.socket_fd > 0)
    {
        close(inst.conn.socket_fd);
        inst.conn.socket_fd = -1;
    }
    if (inst.conn.data_fd > 0)
    {
        close(inst.conn.data_fd);
        inst.conn.data_fd = -1;
    }

    if (inst.state.ir)
    {
        free(inst.state.ir);
        inst.state.ir = NULL;
    }
    if (inst.state.dr_o)
    {
        free(inst.state.dr_o);
        inst.state.dr_o = NULL;
    }

    memset(&inst, 0, sizeof(inst_t));
}

// Interface to VHDL. This is our "init" entrypoint. Simulators bind to
// this function and call it once during elaboration. See VHDL side of the
// interface in file "cosim_tap.vhd" together with simulator specific
// "cosim_tap_<simulator_interface>.vhd" package file.
int cosim_tap_init(int port_num, int ir_bits, int ir_default, int max_dr_bits,
                   int *dr_addr, int *dr_bits)
{
    if (inst.state.initialized == 1)
    {
        // ignore if instance already created
        return 0;
    }

    inst.config.port_num = port_num;
    inst.config.ir_bits = ir_bits;
    inst.config.ir_default = ir_default;
    inst.config.dr_bits = max_dr_bits;
    for (int i = 0; i < MAX_NUM_DR_REGS && dr_addr[i] != 0; ++i)
    {
        inst.config.dr_conf[i].addr = dr_addr[i];
        inst.config.dr_conf[i].bits = dr_bits[i];
    }

    inst.conn.socket_fd = tcp_create_loopback_socket(port_num);

    inst.config.ir_bytes = (ir_bits + 7) / 8;
    inst.state.ir = malloc(inst.config.ir_bits);
    memset(inst.state.ir, HDL_0, inst.config.ir_bits);

    inst.config.dr_bytes = (max_dr_bits + 7) / 8;
    inst.state.dr_o = malloc(inst.config.dr_bits);
    memset(inst.state.dr_o, HDL_0, inst.config.dr_bits);

    inst.state.initialized = 1;
    return 0;
}

// Interface to VHDL. This is our cyclic "tick" entrypoint. Simulators bind to
// this function and call it on each rising edge of the simulated clock. See
// VHDL side of the interface in file "cosim_tap.vhd" together with simulator
// specific "cosim_tap_<simulator_interface>.vhd" package file.
void cosim_tap_tick(char *trst, char *ir_o, char *ir_update,
                    char *dr_i, char *dr_o, char *dr_update)
{
    if (inst.state.initialized != 1)
    {
        FAIL("cosim_tap: tick called on uninitialized tap\n");
    }

    if (inst.conn.data_fd <= 0)
    {
        inst.conn.data_fd = tcp_accept_connection(inst.conn.socket_fd);
    }

    *trst = HDL_0;
    *ir_update = HDL_0;
    *dr_update = HDL_0;

    if (inst.conn.data_fd > 0)
    {
        process_socket(&inst, trst, ir_update, dr_i, dr_update);
    }

    memcpy(ir_o, inst.state.ir, inst.config.ir_bits);
    memcpy(dr_o, inst.state.dr_o, inst.config.dr_bits);
}

#ifdef USE_VHPI

enum init_param_e
{
    I_PARAM_PORT_NUM = 0,
    I_PARAM_IR_BITS,
    I_PARAM_IR_IDCODE,
    I_PARAM_MAX_DR_BITS,
    I_PARAM_DR_ADDR,
    I_PARAM_DR_BITS,
    I_PARAM_MAX
};
static handle_exp_t init_param_map[I_PARAM_MAX] = {
    {"port_num", vhpiConstParamDeclK, vhpiIntVal},
    {"ir_bits", vhpiConstParamDeclK, vhpiIntVal},
    {"ir_idcode", vhpiConstParamDeclK, vhpiIntVal},
    {"max_dr_bits", vhpiConstParamDeclK, vhpiIntVal},
    {"dr_addr", vhpiConstParamDeclK, vhpiIntVecVal},
    {"dr_bits", vhpiConstParamDeclK, vhpiIntVecVal}};

enum tick_param_e
{
    T_PARAM_TRST,
    T_PARAM_IR_O,
    T_PARAM_IR_UPDATE,
    T_PARAM_DR_I,
    T_PARAM_DR_O,
    T_PARAM_DR_UPDATE,
    T_PARAM_MAX
};
static handle_exp_t tick_param_map[T_PARAM_MAX] = {
    {"trst", vhpiVarParamDeclK, vhpiLogicVal},
    {"ir_o", vhpiVarParamDeclK, vhpiLogicVecVal},
    {"ir_update", vhpiVarParamDeclK, vhpiLogicVal},
    {"dr_i", vhpiConstParamDeclK, vhpiLogicVecVal},
    {"dr_o", vhpiVarParamDeclK, vhpiLogicVecVal},
    {"dr_update", vhpiVarParamDeclK, vhpiLogicVal}};

typedef struct vhpi_inst_s
{
    int tick_initialized;
    vhpiHandleT init_handles[I_PARAM_MAX];
    vhpiHandleT tick_handles[T_PARAM_MAX];
} vhpi_inst_t;

static vhpi_inst_t vinst;

static void vhpi_release(const vhpiCbDataT *cb_data)
{
    cosim_tap_release();

    vhpi_release_handles(vinst.init_handles, I_PARAM_MAX);
    vhpi_release_handles(vinst.tick_handles, T_PARAM_MAX);

    memset(&vinst, 0, sizeof(vhpi_inst_t));
}

static void vhpi_init(const vhpiCbDataT *cb_data)
{
    if (inst.state.initialized == 1)
    {
        // ignore if instance already created
        vhpi_put_int(cb_data->obj, 0);
        return;
    }

    vhpiCbDataT end_cb = {
        .cb_rtn = vhpi_release,
        .reason = vhpiCbEndOfSimulation,
        .user_data = NULL};
    vhpi_register_cb(&end_cb, 0);

    vhpi_lookup_handles(cb_data->obj, vhpiParamDecls, init_param_map, I_PARAM_MAX, vinst.init_handles);

    int dr_addr[MAX_NUM_DR_REGS] = {0};
    vhpi_get_int_vec(vinst.init_handles[I_PARAM_DR_ADDR], dr_addr, MAX_NUM_DR_REGS);
    int dr_bits[MAX_NUM_DR_REGS] = {0};
    vhpi_get_int_vec(vinst.init_handles[I_PARAM_DR_BITS], dr_bits, MAX_NUM_DR_REGS);

    cosim_tap_init(
        vhpi_get_int(vinst.init_handles[I_PARAM_PORT_NUM]),
        vhpi_get_int(vinst.init_handles[I_PARAM_IR_BITS]),
        vhpi_get_int(vinst.init_handles[I_PARAM_IR_IDCODE]),
        vhpi_get_int(vinst.init_handles[I_PARAM_MAX_DR_BITS]),
        dr_addr, dr_bits);

    vhpi_put_int(cb_data->obj, 0);
}

static void vhpi_tick(const vhpiCbDataT *cb_data)
{
    if (vinst.tick_initialized != 1)
    {
        vhpi_lookup_handles(cb_data->obj, vhpiParamDecls, tick_param_map, T_PARAM_MAX, vinst.tick_handles);
        vinst.tick_initialized = 1;
    }

    char dr_i[inst.config.dr_bits];
    vhpi_get_logic_vec(vinst.tick_handles[T_PARAM_DR_I], dr_i, inst.config.dr_bits);

    char trst, ir_update, dr_update;

    char ir_o[inst.config.ir_bits];
    char dr_o[inst.config.dr_bits];

    cosim_tap_tick(&trst, ir_o, &ir_update, dr_i, dr_o, &dr_update);

    vhpi_put_logic(vinst.tick_handles[T_PARAM_TRST], trst);
    vhpi_put_logic(vinst.tick_handles[T_PARAM_IR_UPDATE], ir_update);
    vhpi_put_logic_vec(vinst.tick_handles[T_PARAM_IR_O], ir_o, inst.config.ir_bits);
    vhpi_put_logic(vinst.tick_handles[T_PARAM_DR_UPDATE], dr_update);
    vhpi_put_logic_vec(vinst.tick_handles[T_PARAM_DR_O], dr_o, inst.config.dr_bits);
}

static void vhpi_register(const vhpiCbDataT *cb_data)
{
    vhpiHandleT cb_h;
    vhpiForeignDataT foreign_init = {
        vhpiFuncF,
        "cosim_tap.so",   // must precisely match VHDL "foreign" attribute
        "cosim_tap_init", // must precisely match VHDL "foreign" attribute
        NULL,
        vhpi_init};
    cb_h = vhpi_register_foreignf(&foreign_init);
    if (!cb_h)
    {
        FAIL("cosim_tap: failed to register VHPI foreign procedure 'cosim_tap_init'");
    }
    vhpiForeignDataT foreign_tick = {
        vhpiProcF,
        "cosim_tap.so",   // must precisely match VHDL "foreign" attribute
        "cosim_tap_tick", // must precisely match VHDL "foreign" attribute
        NULL,
        vhpi_tick};
    cb_h = vhpi_register_foreignf(&foreign_tick);
    if (!cb_h)
    {
        FAIL("cosim_tap: failed to register VHPI foreign procedure 'cosim_tap_tick'");
    }
    vhpi_release_handle(cb_h);
}

// Interface to VHDL. This is our "init". VHPI enabled simulators call each
// function in the following list once on startup.
void (*vhpi_startup_routines[])() = {
    vhpi_register,
    NULL};

#endif // USE_VHPI
