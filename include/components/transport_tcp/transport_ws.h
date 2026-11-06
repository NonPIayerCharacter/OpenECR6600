/*
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE', which is part of this source code package.
 * Tuan PM <tuanpm at live dot com>
 */

#ifndef _TRANSPORT_WS_H_
#define _TRANSPORT_WS_H_

#include "transport.h"
#include <stdbool.h>
#include "common_define.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum ws_transport_opcodes {
    WS_TRANSPORT_OPCODES_CONT =  0x00,
    WS_TRANSPORT_OPCODES_TEXT =  0x01,
    WS_TRANSPORT_OPCODES_BINARY = 0x02,
    WS_TRANSPORT_OPCODES_CLOSE = 0x08,
    WS_TRANSPORT_OPCODES_PING = 0x09,
    WS_TRANSPORT_OPCODES_PONG = 0x0a,
    WS_TRANSPORT_OPCODES_FIN = 0x80,
    WS_TRANSPORT_OPCODES_NONE = 0x100,   /*!< not a valid opcode to indicate no message previously received
                                          * from the API transport_ws_get_read_opcode() */
} ws_transport_opcodes_t;

/**
 * WS transport configuration structure
 */
typedef struct {
    const char *ws_path;                    /*!< HTTP path to update protocol to websocket */
    const char *sub_protocol;               /*!< WS subprotocol */
    const char *user_agent;                 /*!< WS user agent */
    const char *headers;                    /*!< WS additional headers */
    bool        propagate_control_frames;   /*!< If true, control frames are passed to the reader
                                             *   If false, only user frames are propagated, control frames are handled
                                             *   automatically during read operations
                                             */
} transport_ws_config_t;

/**
 * @brief      Create web socket transport
 *
 * @return
 *  - transport
 *  - NULL
 */
transport_handle_t transport_ws_init(transport_handle_t parent_handle);

/**
 * @brief       Set HTTP path to update protocol to websocket
 *
 * @param t     websocket transport handle
 * @param path  The HTTP Path
 */
void transport_ws_set_path(transport_handle_t t, const char *path);

/**
 * @brief               Set websocket sub protocol header
 *
 * @param t             websocket transport handle
 * @param sub_protocol  Sub protocol string
 *
 * @return
 *      - TRANS_OK on success
 *      - One of the error codes
 */
ts_err_t transport_ws_set_subprotocol(transport_handle_t t, const char *sub_protocol);

/**
 * @brief               Set websocket user-agent header
 *
 * @param t             websocket transport handle
 * @param sub_protocol  user-agent string
 *
 * @return
 *      - TRANS_OK on success
 *      - One of the error codes
 */
ts_err_t transport_ws_set_user_agent(transport_handle_t t, const char *user_agent);

/**
 * @brief               Set websocket additional headers
 *
 * @param t             websocket transport handle
 * @param sub_protocol  additional header strings each terminated with \r\n
 *
 * @return
 *      - TRANS_OK on success
 *      - One of the error codes
 */
ts_err_t transport_ws_set_headers(transport_handle_t t, const char *headers);

/**
 * @brief               Set websocket transport parameters
 *
 * @param t             websocket transport handle
 * @param config        pointer to websocket config structure
 *
 * @return
 *      - TRANS_OK on success
 *      - One of the error codes
 */
ts_err_t transport_ws_set_config(transport_handle_t t, const transport_ws_config_t *config);

/**
 * @brief               Sends websocket raw message with custom opcode and payload
 *
 * Note that generic transport_write for ws handle sends
 * binary massages by default if size is > 0 and
 * ping message if message size is set to 0.
 * This API is provided to support explicit messages with arbitrary opcode,
 * should it be PING, PONG or TEXT message with arbitrary data.
 *
 * @param[in]  t           Websocket transport handle
 * @param[in]  opcode      ws operation code
 * @param[in]  buffer      The buffer
 * @param[in]  len         The length
 * @param[in]  timeout_ms  Total frame timeout in milliseconds (-1 blocks forever,
 *                        0 attempts immediate writes only). Header, payload and
 *                        retries share the same deadline.
 *
 * @return
 *  - len after the complete frame is written, including 0 for an empty frame.
 *  - (-1) on error or timeout. The caller must discard the connection because
 *    part of the frame may already have been sent. Masked payloads are restored
 *    before returning on both success and failure.
 */
int transport_ws_send_raw(transport_handle_t t, ws_transport_opcodes_t opcode, const char *b, int len, int timeout_ms);

/**
 * @brief               Returns websocket op-code for last received data
 *
 * @param t             websocket transport handle
 *
 * @return
 *      - Received op-code as enum
 */
ws_transport_opcodes_t transport_ws_get_read_opcode(transport_handle_t t);

/**
 * @brief               Returns payload length of the last received data
 *
 * @param t             websocket transport handle
 *
 * @return
 *      - Number of bytes in the payload
 */
int transport_ws_get_read_payload_len(transport_handle_t t);

/**
 * @brief               Polls the active connection for termination
 *
 * This API is typically used by the client to wait for clean connection closure
 * by websocket server
 *
 * @param t             Websocket transport handle
 * @param[in] timeout_ms The timeout milliseconds
 *
 * @return
 *      0 - no activity on read and error socket descriptor within timeout
 *      1 - Success: either connection terminated by FIN or the most common RST err codes
 *      -1 - Failure: Unexpected error code or socket is normally readable
 */
int transport_ws_poll_connection_closed(transport_handle_t t, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* _TRANSPORT_WS_H_ */
