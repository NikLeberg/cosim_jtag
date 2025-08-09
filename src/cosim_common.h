/**
 * @file cosim_common.h
 * @author Niklaus Leuenberger <@NikLeberg>
 * @brief Common code shared between jtag and tap impl. Mainly for VHPI.
 * @version 0.1
 * @date 2025-08-08
 *
 * SPDX-License-Identifier: MIT
 *
 * Changes:
 * Version  Date        Author     Detail
 * 0.1      2025-08-08  NikLeberg  initial version
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifdef USE_VHPI
#include <vhpi_user.h> // this header is provided by the simulator
#include <strings.h>   // for strcasecmp()
#endif                 // USE_VHPI

/**
 * @def PRINT(...)
 * @brief Prints formatted output to the appropriate output stream.
 *
 * If USE_VHPI is defined, this prints messages using the VHPI simulator's
 * `vhpi_printf` function. Otherwise simply to stderr.
 *
 * @param ... Format string and arguments, as with printf.
 */
#ifdef USE_VHPI
#define PRINT(...) vhpi_printf(__VA_ARGS__)
#else
#define PRINT(...) fprintf(stderr, __VA_ARGS__)
#endif // USE_VHPI

/**
 * @def FAIL(...)
 * @brief Prints an error message and terminates execution.
 *
 * If USE_VHPI is defined, this prints messages using the VHPI simulator's
 * `vhpi_printf` function. Otherwise simply to stderr.
 *
 * @param ... Format string and arguments, as with printf.
 */
#ifdef USE_VHPI
#define FAIL(...)                              \
    {                                          \
        vhpi_assert(vhpiFailure, __VA_ARGS__); \
        vhpi_control(vhpiStop);                \
    }
#else
#define FAIL(...)           \
    {                       \
        PRINT(__VA_ARGS__); \
        exit(EXIT_FAILURE); \
    }
#endif // USE_VHPI

/**
 * @enum hdl_e
 * @brief Represents the possible states of a VHDL STD_(U)LOGIC enumeration.
 *
 * This enumeration is assumed to fit into a char type. So a single char is
 * equivalent to a STD_(U)LOGIC and a char* array is equivalent to a
 * STD_(U)LOGIC_VECTOR.
 *
 * The values defined here are required to match the definition of the
 * simulators. The below matches the behaviour of GHDL, NVC and
 * ModelSim / QuestaSim.
 */
enum hdl_e
{
    HDL_U = 0, //!< Uninitialized
    HDL_X = 1, //!< Forcing Unknown (U)
    HDL_0 = 2, //!< Forcing 0
    HDL_1 = 3, //!< Forcing 1
    HDL_Z = 4, //!< High Impedance (Z)
    HDL_W = 5, //!< Weak Unknown (W)
    HDL_L = 6, //!< Weak 0
    HDL_H = 7, //!< Weak 1
    HDL_D = 8  //!< Don't care (-)
};

/**
 * @def HDL_TO_INT(hdl)
 * @brief Converts an HDL logic state to a C integer value.
 *
 * Evaluates to 1 if the given HDL logic state is either HDL_1 (forcing 1) or
 * HDL_H (weak 1), otherwise to 0.
 *
 * @param hdl The HDL logic state to convert (of enum type).
 * @return 1 if `hdl` is HDL_1 or HDL_H, 0 otherwise.
 */
#define HDL_TO_INT(hdl) ((hdl) == HDL_1 || (hdl) == HDL_H)

/**
 * @def INT_TO_HDL(i)
 * @brief Converts a C integer value to an HDL logic state.
 *
 * Evaluates to HDL_1 if the given integer `i` is non-zero, otherwise to HDL_0.
 *
 * @param i The integer value to convert.
 * @return HDL_1 if @p i is non-zero, HDL_0 otherwise.
 */
#define INT_TO_HDL(i) (((i) != 0) ? HDL_1 : HDL_0)

/**
 * @brief Creates a TCP loopback socket on the specified port.
 *
 * @param port_num The port number to bind the loopback socket to.
 * @return int File descriptor of the created socket on success,
 *             or -1 on failure.
 */
static int tcp_create_loopback_socket(const int port_num)
{
    int ret;
    int fd;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
    {
        FAIL("cosim_common: tcp_create_loopback_socket: failed to make socket: "
             "%s (%d)\n",
             strerror(errno), errno);
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_num);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret == -1)
    {
        FAIL("cosim_common: tcp_create_loopback_socket: failed to bind socket: "
             "%s (%d)\n",
             strerror(errno), errno);
    }

    // The processing on the socket is called from within simulator and cannot
    // run concurrently, we must not block.
    fcntl(fd, F_SETFL, O_NONBLOCK);

    ret = listen(fd, 0);
    if (ret == -1)
    {
        FAIL("cosim_common: tcp_create_loopback_socket: failed to listen on "
             "socket: %s (%d)\n",
             strerror(errno), errno);
    }

    PRINT("cosim_common: created tcp socket at port: %i\n", port_num);
    return fd;
}

/**
 * @brief Accepts an incoming TCP connection on the given socket.
 *
 * @param socket_fd The file descriptor of the listening socket.
 * @return int The file descriptor for the accepted connection,
 *             or -1 if no connection was incoming.
 */
static int tcp_accept_connection(const int socket_fd)
{
    int fd = accept(socket_fd, NULL, NULL);
    if (fd == -1)
    {
        if (errno != EAGAIN)
        {
            FAIL("cosim_common: tcp_accept_connection: failed with: %s (%d)\n",
                 strerror(errno), errno);
        }
    }
    else
    {
        PRINT("cosim_common: remote connected\n");
    }
    return fd;
}

#ifdef USE_VHPI
/**
 * @def VHPI_LOGIC_TO_ENUM(i)
 * @brief Converts a VHPI logic value to our internal enum value.
 *
 * @warning Only "logic one" (1) and "logic high" (H) map to HDL_1. Everything
 *          else maps to HDL_0. So unknown (U), high-imp (Z) etc. are swallowed.
 *
 * @param logic The VHPI logic value to convert.
 * @return The internal enum value representing the logic state.
 */
#define VHPI_LOGIC_TO_ENUM(l) (((l) == vhpi1 || (l) == vhpiH) ? HDL_1 : HDL_0)

/**
 * @def ENUM_TO_VHPI_LOGIC(e)
 * @brief Converts an internal enum value to a VHPI logic value.
 *
 * @param enum_value The enum value to be converted.
 * @return The corresponding VHPI logic value.
 */
#define ENUM_TO_VHPI_LOGIC(e) (((e) == HDL_1 || (e) == HDL_H) ? vhpi1 : vhpi0)

/**
 * @brief Expected values of a VHPI handle. Used for handle lookup.
 */
typedef struct
{
    const char *name;          //!< name, case-insensitive
    const vhpiClassKindT kind; //!< class kind e.g. vhpiVarParamDeclK for
                               //!< `VARIABLE` or vhpiConstParamDeclK for inputs
                               //!< of any kind
    const vhpiFormatT format;  //!< format e.g. vhpiIntVecVal for `INTEGER` or
                               //!< vhpiLogicVal for `STD_(U)LOGIC`
} handle_exp_t;

/**
 * @brief Looks up and retrieves VHPI handles for specified objects.
 *
 * This looks up a specified kind of handle (the `what`) inside a `parent`
 * referenced by handle. Lookup happens per-index. I.e. first element of
 * `expected` should match the first found candidate by VHPI.
 *
 * For example, to look up all parameters of a VHDL PROCEDURE `parent`
 * should be a handle to the procedure itself (i.e from (vhpiCbDataT *)->obj)
 * and `what` should be vhpiParamDecls for all parameters:
 *     vhpi_lookup_handles(cb_data->obj, vhpiParamDecls, ...)
 *
 * @param parent       The VHPI handle to the parent object in which to search.
 * @param what         VHPI relation specifying what kind of objects to look up.
 * @param expected     Expected specification of handles. Must match exactly.
 * @param length       The length of the `expected` array.
 * @param[out] handles Found handles will be stored here. Must be `length` long.
 */
static void vhpi_lookup_handles(vhpiHandleT parent, vhpiOneToManyT what,
                                handle_exp_t *expected, int length,
                                vhpiHandleT *handles)
{
    for (int i = 0; i < length; ++i)
    {
        vhpiHandleT h = vhpi_handle_by_index(what, parent, i);
        const char *name = vhpi_get_str(vhpiNameP, h);
        const vhpiIntT kind = vhpi_get(vhpiKindP, h);
        vhpiValueT val = {.format = vhpiObjTypeVal};
        vhpi_get_value(h, &val);
        if ((0 == strcasecmp(name, expected[i].name)) &&
            (kind == expected[i].kind) &&
            (val.format == expected[i].format))
        {
            handles[i] = h;
        }
        else
        {
            FAIL("cosim_common: lookup_handles: handle for '%s' not found\n",
                 expected[i].name);
        }
    }
}

/**
 * @brief Releases all VHPI handles in the given list.
 *
 * @param handles Array of handles i.e. as populated from `vhpi_lookup_handles`.
 * @param length  Amount of handles.
 */
static void vhpi_release_handles(vhpiHandleT *handles, int length)
{
    for (int i = 0; i < length; ++i)
    {
        if (NULL != handles[i])
        {
            vhpi_release_handle(handles[i]);
            handles[i] = NULL;
        }
    }
}

/**
 * @brief Get the integer value from a VHPI object.
 *
 * This retrieves the (32-bit) integer value from the given VHPI object.
 *
 * @param handle The VHPI handle of the object.
 * @return The integer value of the object.
 */
static int vhpi_get_int(vhpiHandleT handle)
{
    vhpiValueT val = {.format = vhpiIntVal};
    if (vhpi_get_value(handle, &val) != 0)
    {
        FAIL("cosim_common: vhpi_get_int: failed to get value for handle %s\n",
             vhpi_get_str(vhpiNameP, handle));
    }
    return (int)val.value.intg;
}

/**
 * @brief Write integer value of VHPI object.
 *
 * This sets the (32-bit) integer value of the given VHPI object using the mode
 * vhpiDepositPropagate.
 *
 * @param handle The VHPI handle of the object.
 * @param value  Integer value to write.
 */
static void vhpi_put_int(const vhpiHandleT handle, int value)
{
    vhpiValueT val = {.format = vhpiIntVal, .value.intg = (vhpiIntT)value};
    if (vhpi_put_value(handle, &val, vhpiDepositPropagate) != 0)
    {
        FAIL("cosim_common: vhpi_put_int: failed to set value for handle %s\n",
             vhpi_get_str(vhpiNameP, handle));
    }
}

/**
 * @brief Get the STD_(U)LOGIC_VECTOR value from a VHPI object.
 *
 * This retrieves the STD_(U)LOGIC_VECTOR value from the given VHPI object.
 * Returned value array is in the internal enum type i.e. HDL_0 and HDL_1.
 *
 * @note Direction of vector is assumed to be DOWNTO.
 *
 * @param handle     The VHPI handle of the object.
 * @param[out] value The STD_(U)LOGIC_VECTOR value in internal enum repr.
 * @param length     Size of `value` array.
 */
static void vhpi_get_logic_vec(const vhpiHandleT handle, char *value, int length)
{
    vhpiEnumT buf[sizeof(vhpiEnumT) * length];
    vhpiValueT val = {.value.enumvs = buf,
                      .bufSize = sizeof(vhpiEnumT) * length,
                      .format = vhpiLogicVecVal};
    if (vhpi_get_value(handle, &val) != 0)
    {
        FAIL("cosim_common: vhpi_get_logic_vec: failed to get value for handle %s\n",
             vhpi_get_str(vhpiNameP, handle));
    }
    for (int i = 0; i < length; ++i)
    {
        // assumes DOWNTO vector
        value[i] = VHPI_LOGIC_TO_ENUM(val.value.enumvs[length - 1 - i]);
    }
}

/**
 * @brief Write STD_(U)LOGIC_VECTOR value of VHPI object.
 *
 * This sets the STD_(U)LOGIC_VECTOR value of the given VHPI object using the
 * mode vhpiDepositPropagate.
 *
 * @note Direction of vector is assumed to be DOWNTO.
 *
 * @param handle The VHPI handle of the object.
 * @param value  STD_(U)LOGIC_VECTOR value to write as array of HDL_0 and HDL_1.
 * @param length Size of `value` array.
 */
static void vhpi_put_logic_vec(const vhpiHandleT handle, const char *value,
                               int length)
{
    vhpiEnumT buf[sizeof(vhpiEnumT) * length];
    vhpiValueT val = {.value.enumvs = buf,
                      .bufSize = sizeof(vhpiEnumT) * length,
                      .format = vhpiLogicVecVal};
    for (int i = 0; i < length; ++i)
    {
        // assumes DOWNTO vector
        val.value.enumvs[length - 1 - i] = ENUM_TO_VHPI_LOGIC(value[i]);
    }
    if (vhpi_put_value(handle, &val, vhpiDepositPropagate) != 0)
    {
        FAIL("cosim_common: vhpi_put_logic_vec: failed to set value for handle %s\n",
             vhpi_get_str(vhpiNameP, handle));
    }
}

/**
 * @brief Get the STD_(U)LOGIC value from a VHPI object.
 *
 * This retrieves the STD_(U)LOGIC value from the given VHPI object.
 *
 * @param handle The VHPI handle of the object.
 * @return The STD_(U)LOGIC value as internal enum type.
 */
static char vhpi_get_logic(const vhpiHandleT handle)
{
    vhpiValueT val = {.format = vhpiLogicVal};
    if (vhpi_get_value(handle, &val) != 0)
    {
        FAIL("cosim_common: vhpi_get_logic: failed to get value for handle %s\n",
             vhpi_get_str(vhpiNameP, handle));
    }
    return VHPI_LOGIC_TO_ENUM(val.value.enumv);
}

/**
 * @brief Write STD_(U)LOGIC value of VHPI object.
 *
 * This sets the STD_(U)LOGIC value of the given VHPI object using the mode
 * vhpiDepositPropagate.
 *
 * @param handle The VHPI handle of the object.
 * @param value  Enum value to write.
 */
static void vhpi_put_logic(const vhpiHandleT handle, char value)
{
    vhpiValueT val = {.format = vhpiLogicVal,
                      .value.enumv = ENUM_TO_VHPI_LOGIC(value)};
    if (vhpi_put_value(handle, &val, vhpiDepositPropagate) != 0)
    {
        FAIL("cosim_common: vhpi_put_logic: failed to set value for handle %s\n",
             vhpi_get_str(vhpiNameP, handle));
    }
}

/**
 * @brief Get the INTEGER_VECTOR value from a VHPI object.
 *
 * This retrieves the INTEGER_VECTOR value from the given VHPI object.
 *
 * @note Direction of vector is assumed to be TO.
 *
 * @param handle     The VHPI handle of the object.
 * @param[out] value The INTEGER_VECTOR value in internal enum repr.
 * @param length     Size of `value` array.
 */
static void vhpi_get_int_vec(const vhpiHandleT handle, int *value, int length)
{
    vhpiIntT buf[length];
    vhpiValueT val = {.value.intgs = buf,
                      .bufSize = sizeof(vhpiIntT) * length,
                      .format = vhpiIntVecVal};
    if (vhpi_get_value(handle, &val) != 0)
    {
        FAIL("cosim_common: vhpi_get_int_vec: failed to get value for handle %s\n",
             vhpi_get_str(vhpiNameP, handle));
    }
    for (int i = 0; i < length; ++i)
    {
        value[i] = (int)buf[i];
    }
}

#endif // USE_VHPI
