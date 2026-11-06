#ifndef _WEBSOCKET_CLIENT_H_
#define _WEBSOCKET_CLIENT_H_


#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "system_queue.h"
#include "event_groups.h"
#include "common_define.h"
#include "transport_ws.h"

typedef struct {
    int                         task_stack;
    int                         task_prio;
    char                        *uri;
    char                        *host;
    char                        *path;
    char                        *scheme;
    char                        *username;
    char                        *password;
    int                         port;
    bool                        auto_reconnect;
    void                        *user_context;
    int                         network_timeout_ms;
    char                        *subprotocol;
    char                        *user_agent;
    char                        *headers;
    int                         pingpong_timeout_sec;
    size_t                      ping_interval_sec;
} websocket_config_storage_t;

typedef enum {
    WEBSOCKET_STATE_ERROR = -1,
    WEBSOCKET_STATE_UNKNOW = 0,
    WEBSOCKET_STATE_INIT,
    WEBSOCKET_STATE_CONNECTED,
    WEBSOCKET_STATE_WAIT_TIMEOUT,
    WEBSOCKET_STATE_CLOSING,
} websocket_client_state_t;

struct websocket_client {
    event_handler_t         event_handler;
    TaskHandle_t                task_handle;
    transport_list_handle_t transport_list;
    transport_handle_t      transport;
    websocket_config_storage_t *config;
    websocket_client_state_t    state;
    unsigned int active_tick;
    uint64_t                    reconnect_tick_ms;
    uint64_t                    ping_tick_ms;
    uint64_t                    pingpong_tick_ms;
    int                         wait_timeout_ms;
    int                         auto_reconnect;
    bool                        run;
    bool                        wait_for_pong_resp;
    EventGroupHandle_t          status_bits;
    SemaphoreHandle_t            lock;
    char                        *rx_buffer;
    char                        *tx_buffer;
    int                         buffer_size;
    ws_transport_opcodes_t      last_opcode;
    int                         payload_len;
    int                         payload_offset;
    transport_keep_alive_t  keep_alive_cfg;
};

typedef struct websocket_client *websocket_client_handle_t;

int websocket_log_level;
#define WS_LOGI(format, ...) {if(TRANS_LOG_INFO <= websocket_log_level)os_printf(LM_WIFI, LL_INFO,"[I][%s:%d]"format"\n", __FUNCTION__,__LINE__,##__VA_ARGS__);}
#define WS_LOGE(format, ...) {if(TRANS_LOG_ERROR <= websocket_log_level)os_printf(LM_WIFI, LL_INFO,"[E][%s:%d]"format"\n",__FUNCTION__,__LINE__, ##__VA_ARGS__);}
#define WS_LOGD(format, ...) {if(TRANS_LOG_DEBUG <= websocket_log_level)os_printf(LM_WIFI, LL_INFO,"[D][%s:%d]"format"\n",__FUNCTION__,__LINE__, ##__VA_ARGS__);}
#define WS_LOGW(format, ...) {if(TRANS_LOG_WARN <= websocket_log_level)os_printf(LM_WIFI, LL_INFO,"[W][%s:%d]"format"\n", __FUNCTION__,__LINE__,##__VA_ARGS__);}
#define WS_LOGV(format, ...) {if(TRANS_LOG_ALWAYS <= websocket_log_level)os_printf(LM_WIFI, LL_INFO,"[A][%s:%d]"format"\n",__FUNCTION__,__LINE__, ##__VA_ARGS__);}

EVENT_DECLARE_BASE(WEBSOCKET_EVENTS);         // declaration of the task events family

/**
 * @brief Websocket Client events id
 */
typedef enum {
    WEBSOCKET_EVENT_ANY = -1,
    WEBSOCKET_EVENT_ERROR = 0,      /*!< This event occurs when there are any errors during execution */
    WEBSOCKET_EVENT_CONNECTED,      /*!< Once the Websocket has been connected to the server, no data exchange has been performed */
    WEBSOCKET_EVENT_DISCONNECTED,   /*!< The connection has been disconnected */
    WEBSOCKET_EVENT_DATA,           /*!< When receiving data from the server, possibly multiple portions of the packet */
    WEBSOCKET_EVENT_CLOSED,         /*!< The connection has been closed cleanly */
    WEBSOCKET_EVENT_ACTIVE_TIMEOUT, /*!< The connection not transport packet in setting time */
    WEBSOCKET_EVENT_MAX
} websocket_event_id_t;

/**
 * @brief Websocket event data
 */
typedef struct {
    const char *data_ptr;                   /*!< Data pointer */
    int data_len;                           /*!< Data length */
    uint8_t op_code;                        /*!< Received opcode */
    websocket_client_handle_t client;   /*!< websocket_client_handle_t context */
    void *user_context;                     /*!< user_data context, from websocket_client_config_t user_data */
    int payload_len;                        /*!< Total payload length, payloads exceeding buffer will be posted through multiple events */
    int payload_offset;                     /*!< Actual offset for the data associated with this event */
} websocket_event_data_t;

/**
 * @brief Websocket Client transport
 */
typedef enum {
    WEBSOCKET_TRANSPORT_UNKNOWN = 0x0,  /*!< Transport unknown */
    WEBSOCKET_TRANSPORT_OVER_TCP,       /*!< Transport over tcp */
    WEBSOCKET_TRANSPORT_OVER_SSL,       /*!< Transport over ssl */
} websocket_transport_t;

/**
 * @brief Websocket client setup configuration
 */
typedef struct {
    const char                  *uri;                       /*!< Websocket URI, the information on the URI can be overrides the other fields below, if any */
    const char                  *host;                      /*!< Domain or IP as string */
    int                         port;                       /*!< Port to connect, default depend on websocket_transport_t (80 or 443) */
    const char                  *username;                  /*!< Using for Http authentication - Not supported for now */
    const char                  *password;                  /*!< Using for Http authentication - Not supported for now */
    const char                  *path;                      /*!< HTTP Path, if not set, default is `/` */
    bool                        disable_auto_reconnect;     /*!< Disable the automatic reconnect function when disconnected */
    void                        *user_context;              /*!< HTTP user data context */
    int                         task_prio;                  /*!< Websocket task priority */
    int                         task_stack;                 /*!< Websocket task stack */
    int                         buffer_size;                /*!< Websocket buffer size */
    const char                  *cert_pem;                  /*!< Pointer to certificate data in PEM or DER format for server verify (with SSL), default is NULL, not required to verify the server. PEM-format must have a terminating NULL-character. DER-format requires the length to be passed in cert_len. */
    size_t                      cert_len;                   /*!< Length of the buffer pointed to by cert_pem. May be 0 for null-terminated pem */
    const char                  *client_cert;               /*!< Pointer to certificate data in PEM or DER format for SSL mutual authentication, default is NULL, not required if mutual authentication is not needed. If it is not NULL, also `client_key` has to be provided. PEM-format must have a terminating NULL-character. DER-format requires the length to be passed in client_cert_len. */
    size_t                      client_cert_len;            /*!< Length of the buffer pointed to by client_cert. May be 0 for null-terminated pem */
    const char                  *client_key;                /*!< Pointer to private key data in PEM or DER format for SSL mutual authentication, default is NULL, not required if mutual authentication is not needed. If it is not NULL, also `client_cert` has to be provided. PEM-format must have a terminating NULL-character. DER-format requires the length to be passed in client_key_len */
    size_t                      client_key_len;             /*!< Length of the buffer pointed to by client_key_pem. May be 0 for null-terminated pem */
    websocket_transport_t   transport;                  /*!< Websocket transport type, see `websocket_transport_t */
    char                        *subprotocol;               /*!< Websocket subprotocol */
    char                        *user_agent;                /*!< Websocket user-agent */
    char                        *headers;                   /*!< Websocket additional headers */
    char                        *auth;
    int                         network_timeout_ms;         /*!< Websocket connect timeout*/
    int                         pingpong_timeout_sec;       /*!< Period before connection is aborted due to no PONGs received */
    bool                        disable_pingpong_discon;    /*!< Disable auto-disconnect due to no PONG received within pingpong_timeout_sec */
    bool                        use_global_ca_store;        /*!< Use a global ca_store for all the connections in which this bool is set. */
    bool                        skip_cert_common_name_check;/*!< Skip any validation of server certificate CN field */
    bool                        keep_alive_enable;          /*!< Enable keep-alive timeout */
    int                         keep_alive_idle;            /*!< Keep-alive idle time. Default is 5 (second) */
    int                         keep_alive_interval;        /*!< Keep-alive interval time. Default is 5 (second) */
    int                         keep_alive_count;           /*!< Keep-alive packet retry send count. Default is 3 counts */
    size_t                      ping_interval_sec;          /*!< Websocket ping interval, defaults to 10 seconds if not set */
} websocket_client_config_t;

/**
 * @brief      Start a Websocket session
 *             This function must be the first function to call,
 *             and it returns a websocket_client_handle_t that you must use as input to other functions in the interface.
 *             This call MUST have a corresponding call to websocket_client_destroy when the operation is complete.
 *
 * @param[in]  config  The configuration
 *
 * @return
 *     - `websocket_client_handle_t`
 *     - NULL if any errors
 */
websocket_client_handle_t websocket_client_init(const websocket_client_config_t *config);

/**
 * @brief      Set URL for client, when performing this behavior, the options in the URL will replace the old ones
 *             Must stop the WebSocket client before set URI if the client has been connected
 *
 * @param[in]  client  The client
 * @param[in]  uri     The uri
 *
 * @return     ts_err_t
 */
ts_err_t websocket_client_set_uri(websocket_client_handle_t client, const char *uri);

/**
 * @brief      Open the WebSocket connection
 *
 * @param[in]  client  The client
 *
 * @return     ts_err_t
 */
ts_err_t websocket_client_start(websocket_client_handle_t client);

/**
 * @brief      Stops the WebSocket connection without websocket closing handshake
 *
 * This API stops ws client and closes TCP connection directly without sending
 * close frames. It is a good practice to close the connection in a clean way
 * using websocket_client_close().
 *
 *  Notes:
 *  - Cannot be called from the websocket event handler
 *
 * @param[in]  client  The client
 *
 * @return     ts_err_t
 */
ts_err_t websocket_client_stop(websocket_client_handle_t client);

/**
 * @brief      Destroy the WebSocket connection and free all resources.
 *             This function must be the last function to call for an session.
 *             It is the opposite of the websocket_client_init function and must be called with the same handle as input that a websocket_client_init call returned.
 *             This might close all connections this handle has used.
 *
 *  Notes:
 *  - Cannot be called from the websocket event handler
 *
 * @param[in]  client  The client
 *
 * @return     ts_err_t
 */
ts_err_t websocket_client_destroy(websocket_client_handle_t client);

/**
 * @brief      Generic write data to the WebSocket connection; defaults to binary send
 *
 * @param[in]  client  The client
 * @param[in]  data    The data
 * @param[in]  len     The length
 * @param[in]  timeout Write data timeout in RTOS ticks
 *
 * @return
 *     - Number of data was sent
 *     - (-1) if any errors
 */
int websocket_client_send(websocket_client_handle_t client, const char *data, int len, TickType_t timeout);

/**
 * @brief      Write binary data to the WebSocket connection (data send with WS OPCODE=02, i.e. binary)
 *
 * @param[in]  client  The client
 * @param[in]  data    The data
 * @param[in]  len     The length
 * @param[in]  timeout Write data timeout in RTOS ticks
 *
 * @return
 *     - Number of data was sent
 *     - (-1) if any errors
 */
int websocket_client_send_bin(websocket_client_handle_t client, const char *data, int len, TickType_t timeout);

/**
 * @brief      Write textual data to the WebSocket connection (data send with WS OPCODE=01, i.e. text)
 *
 * @param[in]  client  The client
 * @param[in]  data    The data
 * @param[in]  len     The length
 * @param[in]  timeout Write data timeout in RTOS ticks
 *
 * @return
 *     - Number of data was sent
 *     - (-1) if any errors
 */
int websocket_client_send_text(websocket_client_handle_t client, const char *data, int len, TickType_t timeout);

int websocket_client_send_with_opcode(websocket_client_handle_t client, ws_transport_opcodes_t opcode, const uint8_t *data, int len, TickType_t timeout);
/**
 * @brief      Close the WebSocket connection in a clean way
 *
 * Sequence of clean close initiated by client:
 * * Client sends CLOSE frame
 * * Client waits until server echos the CLOSE frame
 * * Client waits until server closes the connection
 * * Client is stopped the same way as by the `websocket_client_stop()`
 *
 *  Notes:
 *  - Cannot be called from the websocket event handler
 *
 * @param[in]  client  The client
 * @param[in]  timeout Timeout in RTOS ticks for waiting
 *
 * @return     ts_err_t
 */
ts_err_t websocket_client_close(websocket_client_handle_t client, TickType_t timeout);

/**
 * @brief      Close the WebSocket connection in a clean way with custom code/data
 *             Closing sequence is the same as for websocket_client_close()
 *
 *  Notes:
 *  - Cannot be called from the websocket event handler
 *
 * @param[in]  client  The client
 * @param[in]  code    Close status code as defined in RFC6455 section-7.4
 * @param[in]  data    Additional data to closing message
 * @param[in]  len     The length of the additional data
 * @param[in]  timeout Timeout in RTOS ticks for waiting
 *
 * @return     ts_err_t
 */
ts_err_t websocket_client_close_with_code(websocket_client_handle_t client, int code, const char *data, int len, TickType_t timeout);

/**
 * @brief      Check the WebSocket client connection state
 *
 * @param[in]  client  The client handle
 *
 * @return
 *     - true
 *     - false
 */
bool websocket_client_is_connected(websocket_client_handle_t client);

/**
 * @brief Register the Websocket Events
 *
 * @param client            The client handle
 * @param event             The event id
 * @param event_handler     The callback function
 * @param event_handler_arg User context
 * @return ts_err_t
 */
ts_err_t websocket_register_events(websocket_client_handle_t client,
                                        websocket_event_id_t event,
                                        event_handler_t event_handler,
                                        void *event_handler_arg);

ts_err_t websocket_client_get_uri(websocket_client_handle_t client,char *uri);
ts_err_t websocket_client_get_state(websocket_client_handle_t client,int *state);
#ifdef __cplusplus
}
#endif

#endif
