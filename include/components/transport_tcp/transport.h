// Copyright 2015-2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include "common_define.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
*  @brief Keep alive parameters structure
*/
typedef struct transport_keepalive {
    bool            keep_alive_enable;      /*!< Enable keep-alive timeout */
    int             keep_alive_idle;        /*!< Keep-alive idle time (second) */
    int             keep_alive_interval;    /*!< Keep-alive interval time (second) */
    int             keep_alive_count;       /*!< Keep-alive packet retry send count */
} transport_keep_alive_t;

typedef struct tls_last_error {
    ts_err_t last_error;               /*!< error code (based on ERR_tLS_BASE) of the last occurred error */
    int       tls_error_code;       /*!< tls error code from last tls failed api */
    int       tls_flags;            /*!< last certification verification flags */
} tls_last_error_t;

typedef struct transport_internal* transport_list_handle_t;
typedef struct transport_item_t* transport_handle_t;

typedef int (*connect_func)(transport_handle_t t, const char *host, int port, int timeout_ms);
typedef int (*io_func)(transport_handle_t t, const char *buffer, int len, int timeout_ms);
typedef int (*io_read_func)(transport_handle_t t, char *buffer, int len, int timeout_ms);
typedef int (*trans_func)(transport_handle_t t);
typedef int (*poll_func)(transport_handle_t t, int timeout_ms);
typedef int (*connect_async_func)(transport_handle_t t, const char *host, int port, int timeout_ms);
typedef int (*get_socket_func)(transport_handle_t t);
typedef transport_handle_t (*payload_transfer_func)(transport_handle_t);

typedef struct tls_last_error* tls_error_handle_t;

/**
 * @brief Error types for TCP connection issues not covered in socket's errno
 */
enum tcp_transport_err_t {
    ERR_TCP_TRANSPORT_NO_MEM = -3,
    ERR_TCP_TRANSPORT_CONNECTION_FAILED = -2,
    ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN = -1,
    ERR_TCP_TRANSPORT_CONNECTION_TIMEOUT = 0,
};

/**
 * @brief      Sets error to common transport handle
 *
 *             Note: This function copies the supplied error handle object to tcp_transport's internal
 *             error handle object
 *
 * @param[in]  A transport handle
 *
 */
void transport_set_errors(transport_handle_t t, const tls_error_handle_t error_handle);

/**
 * Transport layer structure, which will provide functions, basic properties for transport types
 */
struct transport_item_t {
    int             port;
    char            *scheme;        /*!< Tag name */
    void            *data;          /*!< Additional transport data */
    connect_func    _connect;       /*!< Connect function of this transport */
    io_read_func    _read;          /*!< Read */
    io_func         _write;         /*!< Write */
    trans_func      _close;         /*!< Close */
    poll_func       _poll_read;     /*!< Poll and read */
    poll_func       _poll_write;    /*!< Poll and write */
    trans_func      _destroy;       /*!< Destroy and free transport */
    connect_async_func _connect_async;      /*!< non-blocking connect function of this transport */
    payload_transfer_func  _parent_transfer;        /*!< Function returning underlying transport layer */
    get_socket_func        _get_socket;             /*!< Function returning the transport's socket */
    struct transport_error_s*    error_handle;  /*!< Error handle (based on esp-tls error handle)
                                                     * extended with transport's specific errors */
    transport_keep_alive_t *keep_alive_cfg;     /*!< TCP keep-alive config */

    //STAILQ_ENTRY(transport_item_t) next;
    struct{struct transport_item_t *stqe_next;}next;
    
};

/**
 * @brief Returns underlying socket for the supplied transport handle
 *
 * @param t Transport handle
 *
 * @return Socket file descriptor in case of success
 *         -1 in case of error
 */
int transport_get_socket(transport_handle_t t);

/**
 * @brief      Captures the current errno
 *
 * @param[in] t The transport handle
 * @param[in] sock_errno Socket errno to store in internal transport structures
 *
 */
void transport_capture_errno(transport_handle_t t, int sock_errno);

/**
 * @brief      Create transport list
 *
 * @return     A handle can hold all transports
 */
transport_list_handle_t transport_list_init(void);

/**
 * @brief      Cleanup and free all transports, include itself,
 *             this function will invoke transport_destroy of every transport have added this the list
 *
 * @param[in]  list  The list
 *
 * @return
 *     - TRANS_OK
 *     - TRANS_FAIL
 */
ts_err_t transport_list_destroy(transport_list_handle_t list);

/**
 * @brief      Add a transport to the list, and define a scheme to indentify this transport in the list
 *
 * @param[in]  list    The list
 * @param[in]  t       The Transport
 * @param[in]  scheme  The scheme
 *
 * @return
 *     - TRANS_OK
 */
ts_err_t transport_list_add(transport_list_handle_t list, transport_handle_t t, const char *scheme);

/**
 * @brief      This function will remove all transport from the list,
 *             invoke transport_destroy of every transport have added this the list
 *
 * @param[in]  list  The list
 *
 * @return
 *     - TRANS_OK
 *     - ERR_INVALID_ARG
 */
ts_err_t transport_list_clean(transport_list_handle_t list);

/**
 * @brief      Get the transport by scheme, which has been defined when calling function `transport_list_add`
 *
 * @param[in]  list  The list
 * @param[in]  tag   The tag
 *
 * @return     The transport handle
 */
transport_handle_t transport_list_get_transport(transport_list_handle_t list, const char *scheme);

/**
 * @brief      Initialize a transport handle object
 *
 * @return     The transport handle
 */
transport_handle_t transport_init(void);

/**
 * @brief      Cleanup and free memory the transport
 *
 * @param[in]  t     The transport handle
 *
 * @return
 *     - TRANS_OK
 *     - TRANS_FAIL
 */
ts_err_t transport_destroy(transport_handle_t t);

/**
 * @brief      Get default port number used by this transport
 *
 * @param[in]  t     The transport handle
 *
 * @return     the port number
 */
int transport_get_default_port(transport_handle_t t);

/**
 * @brief      Set default port number that can be used by this transport
 *
 * @param[in]  t     The transport handle
 * @param[in]  port  The port number
 *
 * @return
 *     - TRANS_OK
 *     - TRANS_FAIL
 */
ts_err_t transport_set_default_port(transport_handle_t t, int port);

/**
 * @brief      Transport connection function, to make a connection to server
 *
 * @param      t           The transport handle
 * @param[in]  host        Hostname
 * @param[in]  port        Port
 * @param[in]  timeout_ms  The timeout milliseconds (-1 indicates wait forever)
 *
 * @return
 * - socket for will use by this transport
 * - (-1) if there are any errors, should check errno
 */
int transport_connect(transport_handle_t t, const char *host, int port, int timeout_ms);

/**
 * @brief      Non-blocking transport connection function, to make a connection to server
 *
 * @param      t           The transport handle
 * @param[in]  host        Hostname
 * @param[in]  port        Port
 * @param[in]  timeout_ms  The timeout milliseconds (-1 indicates wait forever)
 *
 * @return
 * - socket for will use by this transport
 * - (-1) if there are any errors, should check errno
 */
int transport_connect_async(transport_handle_t t, const char *host, int port, int timeout_ms);

/**
 * @brief      Transport read function
 *
 * @param      t           The transport handle
 * @param      buffer      The buffer
 * @param[in]  len         The length
 * @param[in]  timeout_ms  The timeout milliseconds (-1 indicates wait forever)
 *
 * @return
 *  - Number of bytes was read
 *  - (-1) if there are any errors, should check errno
 */
int transport_read(transport_handle_t t, char *buffer, int len, int timeout_ms);

/**
 * @brief      Poll the transport until readable or timeout
 *
 * @param[in]  t           The transport handle
 * @param[in]  timeout_ms  The timeout milliseconds (-1 indicates wait forever)
 *
 * @return
 *     - 0      Timeout
 *     - (-1)   If there are any errors, should check errno
 *     - other  The transport can read
 */
int transport_poll_read(transport_handle_t t, int timeout_ms);

/**
 * @brief      Transport write function
 *
 * @param      t           The transport handle
 * @param      buffer      The buffer
 * @param[in]  len         The length
 * @param[in]  timeout_ms  The timeout milliseconds (-1 indicates wait forever)
 *
 * @return
 *  - Number of bytes was written
 *  - (-1) if there are any errors, should check errno
 */
int transport_write(transport_handle_t t, const char *buffer, int len, int timeout_ms);

/**
 * @brief      Poll the transport until writeable or timeout
 *
 * @param[in]  t           The transport handle
 * @param[in]  timeout_ms  The timeout milliseconds (-1 indicates wait forever)
 *
 * @return
 *     - 0      Timeout
 *     - (-1)   If there are any errors, should check errno
 *     - other  The transport can write
 */
int transport_poll_write(transport_handle_t t, int timeout_ms);

/**
 * @brief      Transport close
 *
 * @param      t     The transport handle
 *
 * @return
 * - 0 if ok
 * - (-1) if there are any errors, should check errno
 */
int transport_close(transport_handle_t t);

/**
 * @brief      Get user data context of this transport
 *
 * @param[in]  t        The transport handle
 *
 * @return     The user data context
 */
void *transport_get_context_data(transport_handle_t t);

/**
 * @brief      Get transport handle of underlying protocol
 *             which can access this protocol payload directly
 *             (used for receiving longer msg multiple times)
 *
 * @param[in]  t        The transport handle
 *
 * @return     Payload transport handle
 */
transport_handle_t transport_get_payload_transport_handle(transport_handle_t t);

/**
 * @brief      Set the user context data for this transport
 *
 * @param[in]  t        The transport handle
 * @param      data     The user data context
 *
 * @return
 *     - TRANS_OK
 */
ts_err_t transport_set_context_data(transport_handle_t t, void *data);

/**
 * @brief      Set transport functions for the transport handle
 *
 * @param[in]  t            The transport handle
 * @param[in]  _connect     The connect function pointer
 * @param[in]  _read        The read function pointer
 * @param[in]  _write       The write function pointer
 * @param[in]  _close       The close function pointer
 * @param[in]  _poll_read   The poll read function pointer
 * @param[in]  _poll_write  The poll write function pointer
 * @param[in]  _destroy     The destroy function pointer
 *
 * @return
 *     - TRANS_OK
 */
ts_err_t transport_set_func(transport_handle_t t,
                             connect_func _connect,
                             io_read_func _read,
                             io_func _write,
                             trans_func _close,
                             poll_func _poll_read,
                             poll_func _poll_write,
                             trans_func _destroy);


/**
 * @brief      Set transport functions for the transport handle
 *
 * @param[in]  t                    The transport handle
 * @param[in]  _connect_async_func  The connect_async function pointer
 *
 * @return
 *     - TRANS_OK
 *     - TRANS_FAIL
 */
ts_err_t transport_set_async_connect_func(transport_handle_t t, connect_async_func _connect_async_func);

/**
 * @brief      Set parent transport function to the handle
 *
 * @param[in]  t                    The transport handle
 * @param[in]  _parent_transport    The underlying transport getter pointer
 *
 * @return
 *     - TRANS_OK
 *     - TRANS_FAIL
 */
ts_err_t transport_set_parent_transport_func(transport_handle_t t, payload_transfer_func _parent_transport);

/**
 * @brief      Returns tls error handle.
 *             Warning: The returned pointer is valid only as long as transport_handle_t exists. Once transport
 *             handle gets destroyed, this value (tls_error_handle_t) is freed automatically.
 *
 * @param[in]  A transport handle
 *
 * @return
 *            - valid pointer of error_handle_t
 *            - NULL if invalid transport handle
 */
tls_error_handle_t transport_get_error_handle(transport_handle_t t);

/**
 * @brief      Get and clear last captured socket errno
 *
 * Socket errno is internally stored whenever any of public facing API
 * for reading, writing, polling or connection fails returning negative return code.
 * The error code corresponds to the `SO_ERROR` value retrieved from the underlying
 * transport socket using `getsockopt()` API. After reading the captured errno,
 * the internal value is cleared to 0.
 *
 * @param[in] t The transport handle
 *
 * @return
 *   - >=0 Last captured socket errno
 *   - -1  Invalid transport handle or invalid transport's internal error storage
 */
int transport_get_errno(transport_handle_t t);

#ifdef __cplusplus
}
#endif
#endif /* _TRANSPORT_ */
