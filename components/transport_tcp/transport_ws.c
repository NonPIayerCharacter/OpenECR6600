#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "lwip/err.h"

#include "transport.h"
#include "transport_tcp.h"
#include "transport_ws.h"
#include "errno.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"


#define WS_BUFFER_SIZE              1024
#define WS_FIN                      0x80
#define WS_OPCODE_CONT              0x00
#define WS_OPCODE_TEXT              0x01
#define WS_OPCODE_BINARY            0x02
#define WS_OPCODE_CLOSE             0x08
#define WS_OPCODE_PING              0x09
#define WS_OPCODE_PONG              0x0a
#define WS_OPCODE_CONTROL_FRAME     0x08

// Second byte
#define WS_MASK                     0x80
#define WS_SIZE16                   126
#define WS_SIZE64                   127
#define MAX_WEBSOCKET_HEADER_SIZE   16
#define WS_RESPONSE_OK              101
#define WS_TRANSPORT_MAX_CONTROL_FRAME_BUFFER_LEN 125


typedef struct {
    uint8_t opcode;
    char mask_key[4];                   /*!< Mask key for this payload */
    int payload_len;                    /*!< Total length of the payload */
    int bytes_remaining;                /*!< Bytes left to read of the payload  */
    bool header_received;               /*!< Flag to indicate that a new message header was received */
} ws_transport_frame_state_t;

typedef struct {
    char *path;
    char *buffer;
    char *sub_protocol;
    char *user_agent;
    char *headers;
    char *auth;
    bool propagate_control_frames;
    ws_transport_frame_state_t frame_state;
    transport_handle_t parent;
} transport_ws_t;

/**
 * @brief               Handles control frames
 *
 * This API is used internally to handle control frames at the transport layer.
 * The API could be possibly promoted to a public API if needed by some clients
 *
 * @param t             Websocket transport handle
 * @param buffer        Buffer with the actual payload of the control packet to be processed
 * @param len           Length of the buffer (typically the same as the payload buffer)
 * @param timeout_ms    The timeout milliseconds
 * @param client_closed To indicate that the connection has been closed by the client
*                       (to prevent echoing the CLOSE packet if true, as this is the actual echo from the server)
 *
 * @return
 *      0 - no activity, or successfully responded to PING
 *      -1 - Failure: Error on read or the actual payload longer then buffer
 *      1 - Close handshake success
 *      2 - Got PONG message
 */

static int transport_ws_handle_control_frames(transport_handle_t t, char *buffer, int len, int timeout_ms, bool client_closed);

static inline uint8_t ws_get_bin_opcode(ws_transport_opcodes_t opcode)
{
    return (uint8_t)opcode;
}

static transport_handle_t ws_get_payload_transport_handle(transport_handle_t t)
{
    transport_ws_t *ws = transport_get_context_data(t);

    /* Reading parts of a frame directly will disrupt the WS internal frame state,
        reset bytes_remaining to prepare for reading a new frame */
    ws->frame_state.bytes_remaining = 0;

    return ws->parent;
}

static char *get_http_header(char *buffer, const char *key)
{
    size_t key_len;
    char *line;

    if (buffer == NULL || key == NULL) {
        return NULL;
    }

    key_len = strlen(key);
    line = strstr(buffer, "\r\n");
    if (line == NULL) {
        return NULL;
    }
    line += 2;

    while (*line != '\0') {
        char *line_end = strstr(line, "\r\n");
        char *value;

        if (line_end == NULL || line_end == line) {
            return NULL;
        }
        if ((size_t)(line_end - line) >= key_len &&
            strncasecmp(line, key, key_len) == 0) {
            value = line + key_len;
            while (value < line_end && isspace((unsigned char)*value)) {
                ++value;
            }
            while (line_end > value &&
                   isspace((unsigned char)line_end[-1])) {
                --line_end;
            }
            *line_end = '\0';
            return value;
        }
        line = line_end + 2;
    }
    return NULL;
}

static int ws_connect(transport_handle_t t, const char *host, int port, int timeout_ms)
{
    transport_ws_t *ws = transport_get_context_data(t);
    if (transport_connect(ws->parent, host, port, timeout_ms) < 0) {
        TRANS_LOGE("Error connecting to host %s:%d", host, port);
        return -1;
    }
    const char *user_agent_ptr = (ws->user_agent)?(ws->user_agent):"ESWIN Websocket Client";
    unsigned char random_key[16] = { 0 };
    unsigned char client_key[28] = {0};
    int i;
    for (i = 0; i < sizeof(random_key); i++) {
        random_key[i] = rand() & 0xFF;
    }
    size_t outlen = 0;
    mbedtls_base64_encode(client_key, sizeof(client_key),  &outlen, random_key, sizeof(random_key));
    int len = snprintf(ws->buffer, WS_BUFFER_SIZE,
                         "GET %s HTTP/1.1\r\n"
                         "Connection: Upgrade\r\n"
                         "Host: %s:%d\r\n"
                         "User-Agent: %s\r\n"
                         "Upgrade: websocket\r\n"
                         "Sec-WebSocket-Version: 13\r\n"
                         "Sec-WebSocket-Key: %s\r\n",
                         ws->path,
                         host, port, user_agent_ptr,
                         client_key);
    if (len <= 0 || len >= WS_BUFFER_SIZE) {
        TRANS_LOGE("Error in request generation, desired request len: %d, buffer size: %d", len, WS_BUFFER_SIZE);
        return -1;
    }
    if (ws->sub_protocol) {
        TRANS_LOGD("sub_protocol: %s", ws->sub_protocol);
        int r = snprintf(ws->buffer + len, WS_BUFFER_SIZE - len, "Sec-WebSocket-Protocol: %s\r\n", ws->sub_protocol);
        len += r;
        if (r <= 0 || len >= WS_BUFFER_SIZE) {
            TRANS_LOGE("Error in request generation"
                          "(snprintf of subprotocol returned %d, desired request len: %d, buffer size: %d", r, len, WS_BUFFER_SIZE);
            return -1;
        }
    }
    if (ws->auth) {
        TRANS_LOGD("Authorization: %s", ws->auth);
        int r = snprintf(ws->buffer + len, WS_BUFFER_SIZE - len, "Authorization: %s\r\n", ws->auth);
        len += r;
        if (r <= 0 || len >= WS_BUFFER_SIZE) {
            TRANS_LOGD("Error in request generation"
                     "(snprintf of authorization returned %d, desired request len: %d, buffer size: %d", r, len, WS_BUFFER_SIZE);
            return -1;
        }
    }
    if (ws->headers) {
        TRANS_LOGD("headers: %s", ws->headers);
        int r = snprintf(ws->buffer + len, WS_BUFFER_SIZE - len, "%s", ws->headers);
        len += r;
        if (r <= 0 || len >= WS_BUFFER_SIZE) {
            TRANS_LOGE("Error in request generation"
                          "(strncpy of headers returned %d, desired request len: %d, buffer size: %d", r, len, WS_BUFFER_SIZE);
            return -1;
        }
    }
    int r = snprintf(ws->buffer + len, WS_BUFFER_SIZE - len, "\r\n");
    len += r;
    if (r <= 0 || len >= WS_BUFFER_SIZE) {
        TRANS_LOGE("Error in request generation"
                       "(snprintf of header terminal returned %d, desired request len: %d, buffer size: %d", r, len, WS_BUFFER_SIZE);
        return -1;
    }
    os_printf(LM_APP, LL_INFO, "Write upgrate request\r\n%s", ws->buffer);
    if (transport_write(ws->parent, ws->buffer, len, timeout_ms) <= 0) {
        os_printf(LM_APP, LL_INFO, "Error write Upgrade header %s", ws->buffer);
        return -1;
    }
    int header_len = 0;
    do {
        if ((len = transport_read(ws->parent, ws->buffer + header_len, WS_BUFFER_SIZE - header_len, timeout_ms)) <= 0) {
            TRANS_LOGE("Error read response for Upgrade header %s", ws->buffer);
            return -1;
        }
        header_len += len;
        ws->buffer[header_len] = '\0';
        TRANS_LOGD("Read header chunk %d, current header size: %d", len, header_len);
    } while (NULL == strstr(ws->buffer, "\r\n\r\n") && header_len < WS_BUFFER_SIZE);

    char *server_key = get_http_header(ws->buffer, "Sec-WebSocket-Accept:");
    if (server_key == NULL) {
        os_printf(LM_APP, LL_ERR, "[WS] Sec-WebSocket-Accept not found\n");
        return -1;
    }

    unsigned char client_key_b64[128] = {0};
    unsigned char valid_client_key[20] = {0};
    unsigned char accept_key[33] = {0};
    int key_len = sprintf((char*)client_key_b64, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", (char*)client_key);
    mbedtls_sha1(client_key_b64, (size_t)key_len, valid_client_key);
    mbedtls_base64_encode(accept_key, 32,  &outlen, valid_client_key, 20);
    accept_key[outlen] = 0;
    os_printf(LM_APP, LL_INFO, "server key=%s, send_key=%s, accept_key=%s\n", (char *)server_key, (char*)client_key, accept_key);
    if (strcmp((char*)accept_key, (char*)server_key) != 0) {
        os_printf(LM_APP, LL_INFO, "Invalid websocket key");
        return -1;
    }
    return 0;
}

/* Header and payload share one deadline. Retry the same bytes without adding a new WS header. */
static int ws_write_all(transport_handle_t parent, const char *data, int len,
                        uint32_t started_ms, int timeout_ms)
{
    int written = 0;

    while (written < len) {
        int remaining_ms = timeout_ms;
        int ret;

        if (timeout_ms > 0) {
            uint32_t elapsed_ms = (uint32_t)os_time_get() - started_ms;
            if (elapsed_ms >= (uint32_t)timeout_ms) {
                TRANS_LOGE("WS write timed out: sent=%d total=%d", written, len);
                return -1;
            }
            remaining_ms -= (int)elapsed_ms;
        }

        /* The parent transport waits for socket writability within the remaining budget. */
        errno = 0;
        ret = transport_write(parent, data + written, len - written, remaining_ms);
        if (ret > 0) {
            if (ret > len - written) {
                return -1;
            }
            written += ret;
            continue;
        }
        if (ret < 0 && ret != ERR_WOULDBLOCK &&
            !(ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))) {
            TRANS_LOGE("WS write failed: ret=%d errno=%d sent=%d total=%d",
                       ret, errno, written, len);
            return -1;
        }
        if (timeout_ms == 0) {
            return -1;
        }
        /* A writable socket can still reject a write; yield instead of spinning. */
        os_msleep(1);
    }
    return written;
}

static int _ws_write(transport_handle_t t, int opcode, int mask_flag, const char *b, int len, int timeout_ms)
{
    transport_ws_t *ws = transport_get_context_data(t);
    char *buffer = (char *)b;
    char ws_header[MAX_WEBSOCKET_HEADER_SIZE];
    char *mask;
    int header_len = 0, i;
    int ret = -1;
    uint32_t started_ms = (uint32_t)os_time_get();

    if (len < 0 || (len > 0 && b == NULL)) {
        return -1;
    }
    ws_header[header_len++] = opcode;

    if (len <= 125) {
        ws_header[header_len++] = (uint8_t)(len | mask_flag);
    } else if (len < 65536) {
        ws_header[header_len++] = WS_SIZE16 | mask_flag;
        ws_header[header_len++] = (uint8_t)(len >> 8);
        ws_header[header_len++] = (uint8_t)(len & 0xFF);
    } else {
        ws_header[header_len++] = WS_SIZE64 | mask_flag;
        /* Support maximum 4 bytes length */
        ws_header[header_len++] = 0; //(uint8_t)((len >> 56) & 0xFF);
        ws_header[header_len++] = 0; //(uint8_t)((len >> 48) & 0xFF);
        ws_header[header_len++] = 0; //(uint8_t)((len >> 40) & 0xFF);
        ws_header[header_len++] = 0; //(uint8_t)((len >> 32) & 0xFF);
        ws_header[header_len++] = (uint8_t)((len >> 24) & 0xFF);
        ws_header[header_len++] = (uint8_t)((len >> 16) & 0xFF);
        ws_header[header_len++] = (uint8_t)((len >> 8) & 0xFF);
        ws_header[header_len++] = (uint8_t)((len >> 0) & 0xFF);
    }

    if (mask_flag) {
        mask = &ws_header[header_len];
        ws_header[header_len++] = rand() & 0xFF;
        ws_header[header_len++] = rand() & 0xFF;
        ws_header[header_len++] = rand() & 0xFF;
        ws_header[header_len++] = rand() & 0xFF;

        for (i = 0; i < len; ++i) {
            buffer[i] = (buffer[i] ^ mask[i % 4]);
        }
    }

    if (ws_write_all(ws->parent, ws_header, header_len, started_ms, timeout_ms) != header_len) {
        TRANS_LOGE("Error write header");
        goto restore_payload;
    }

    ret = ws_write_all(ws->parent, buffer, len, started_ms, timeout_ms);
restore_payload:
    // in case of masked transport we have to revert back to the original data, as ws layer
    // does not create its own copy of data to be sent
    if (mask_flag) {
        mask = &ws_header[header_len-4];
        for (i = 0; i < len; ++i) {
            buffer[i] = (buffer[i] ^ mask[i % 4]);
        }
    }
    return ret;
}

int transport_ws_send_raw(transport_handle_t t, ws_transport_opcodes_t opcode, const char *b, int len, int timeout_ms)
{
    uint8_t op_code = ws_get_bin_opcode(opcode);
    if (t == NULL) {
        TRANS_LOGE("Transport must be a valid ws handle");
        return ERR_INVALID_ARG;
    }
    TRANS_LOGD("Sending raw ws message with opcode %d", op_code);
    return _ws_write(t, op_code, WS_MASK, b, len, timeout_ms);
}

static int ws_write(transport_handle_t t, const char *b, int len, int timeout_ms)
{
    if (len == 0) {
        // Default transport write of zero length in ws layer sends out a ping message.
        // This behaviour could however be altered in IDF 5.0, since a separate API for sending
        // messages with user defined opcodes has been introduced.
        TRANS_LOGD("Write PING message");
        return _ws_write(t, WS_OPCODE_PING | WS_FIN, WS_MASK, NULL, 0, timeout_ms);
    }
    return _ws_write(t, WS_OPCODE_BINARY | WS_FIN, WS_MASK, b, len, timeout_ms);
}


static int ws_read_payload(transport_handle_t t, char *buffer, int len, int timeout_ms)
{
    transport_ws_t *ws = transport_get_context_data(t);

    int bytes_to_read;
    int rlen = 0;

    if (ws->frame_state.bytes_remaining > len) {
        TRANS_LOGD("Actual data to receive (%d) are longer than ws buffer (%d)", ws->frame_state.bytes_remaining, len);
        bytes_to_read = len;

    } else {
        bytes_to_read = ws->frame_state.bytes_remaining;
    }

    // Receive and process payload
    if (bytes_to_read != 0 && (rlen = transport_read(ws->parent, buffer, bytes_to_read, timeout_ms)) <= 0) {
        TRANS_LOGE("Error read data");
        return rlen;
    }
    ws->frame_state.bytes_remaining -= rlen;

    if (ws->frame_state.mask_key) {
        for (int i = 0; i < bytes_to_read; i++) {
            buffer[i] = (buffer[i] ^ ws->frame_state.mask_key[i % 4]);
        }
    }
    return rlen;
}


/* Read and parse the WS header, determine length of payload */
static int ws_read_header(transport_handle_t t, char *buffer, int len, int timeout_ms)
{
    transport_ws_t *ws = transport_get_context_data(t);
    int payload_len;

    char ws_header[MAX_WEBSOCKET_HEADER_SIZE];
    char *data_ptr = ws_header, mask;
    int rlen;
    int poll_read;
    ws->frame_state.header_received = false;
    if ((poll_read = transport_poll_read(ws->parent, timeout_ms)) <= 0) {
        return poll_read;
    }

    // Receive and process header first (based on header size)
    int header = 2;
    int mask_len = 4;
    if ((rlen = transport_read(ws->parent, data_ptr, header, timeout_ms)) <= 0) {
        TRANS_LOGE("Error read data");
        return rlen;
    }
    ws->frame_state.header_received = true;
    ws->frame_state.opcode = (*data_ptr & 0x0F);
    data_ptr ++;
    mask = ((*data_ptr >> 7) & 0x01);
    payload_len = (*data_ptr & 0x7F);
    data_ptr++;
    TRANS_LOGD("Opcode: %d, mask: %d, len: %d\r\n", ws->frame_state.opcode, mask, payload_len);
    if (payload_len == 126) {
        // headerLen += 2;
        if ((rlen = transport_read(ws->parent, data_ptr, header, timeout_ms)) <= 0) {
            TRANS_LOGE("Error read data");
            return rlen;
        }
        payload_len = ((unsigned char)data_ptr[0]) << 8 | ((unsigned char)data_ptr[1]);
    } else if (payload_len == 127) {
        // headerLen += 8;
        header = 8;
        if ((rlen = transport_read(ws->parent, data_ptr, header, timeout_ms)) <= 0) {
            TRANS_LOGE("Error read data");
            return rlen;
        }

        if (data_ptr[0] != 0 || data_ptr[1] != 0 || data_ptr[2] != 0 || data_ptr[3] != 0) {
            // really too big!
            payload_len = 0xFFFFFFFF;
        } else {
            payload_len=((unsigned char)data_ptr[4]) << 24 | \
                        ((unsigned char)data_ptr[5]) << 16 | \
                        ((unsigned char)data_ptr[6]) << 8  | \
                        ((unsigned char)data_ptr[7]);
        }
    }

    if (mask) {
        // Read and store mask
        if (payload_len != 0 && (rlen = transport_read(ws->parent, buffer, mask_len, timeout_ms)) <= 0) {
            TRANS_LOGE("Error read data");
            return rlen;
        }
        memcpy(ws->frame_state.mask_key, buffer, mask_len);
    } else {
        memset(ws->frame_state.mask_key, 0, mask_len);
    }

    ws->frame_state.payload_len = payload_len;
    ws->frame_state.bytes_remaining = payload_len;

    return payload_len;
}

static int ws_handle_control_frame_internal(transport_handle_t t, int timeout_ms)
{
    transport_ws_t *ws = transport_get_context_data(t);
    char *control_frame_buffer = NULL;
    int control_frame_buffer_len = 0;
    int payload_len = ws->frame_state.payload_len;
    int ret = 0;

    // If no new header reception in progress, or not a control frame
    // just pass 0 -> no need to handle control frames
    if (ws->frame_state.header_received == false ||
        !(ws->frame_state.opcode & WS_OPCODE_CONTROL_FRAME)) {
        return 0;
    }

    if (payload_len > WS_TRANSPORT_MAX_CONTROL_FRAME_BUFFER_LEN) {
        TRANS_LOGE("Not enough room for reading control frames (need=%d, max_allowed=%d)",
                 ws->frame_state.payload_len, WS_TRANSPORT_MAX_CONTROL_FRAME_BUFFER_LEN);
        return -1;
    }

    // Now we can handle the control frame correctly (either zero payload, or a short one for which we allocate mem)
    control_frame_buffer_len = payload_len;
    if (control_frame_buffer_len > 0) {
        control_frame_buffer = malloc(control_frame_buffer_len);
        if (control_frame_buffer == NULL) {
            TRANS_LOGE("Cannot allocate buffer for control frames, need-%d", control_frame_buffer_len);
            return -1;
        }
    } else {
        control_frame_buffer_len = 0;
    }

    // read the payload of the control frame
    int actual_len = ws_read_payload(t, control_frame_buffer, control_frame_buffer_len, timeout_ms);
    if (actual_len != payload_len) {
        TRANS_LOGE("Control frame (opcode=%d) payload read failed (payload_len=%d, read_len=%d)",
                 ws->frame_state.opcode, payload_len, actual_len);
        ret = -1;
        goto free_payload_buffer;
    }

    ret = transport_ws_handle_control_frames(t, control_frame_buffer, control_frame_buffer_len, timeout_ms, false);

free_payload_buffer:
    os_free(control_frame_buffer);
    return ret > 0 ? 0 : ret; // We don't propagate control frames, pass 0 to upper layers

}

static int ws_read(transport_handle_t t, char *buffer, int len, int timeout_ms)
{
    int rlen = 0;
    transport_ws_t *ws = transport_get_context_data(t);

    // If message exceeds buffer len then subsequent reads will skip reading header and read whatever is left of the payload
    if (ws->frame_state.bytes_remaining <= 0) {

        if ( (rlen = ws_read_header(t, buffer, len, timeout_ms)) < 0) {
            // If something when wrong then we prepare for reading a new header
            ws->frame_state.bytes_remaining = 0;
            return rlen;
        }

        // If the new opcode is a control frame and we don't pass it to the app
        //  - try to handle it internally using the application buffer
        if (ws->frame_state.header_received && (ws->frame_state.opcode & WS_OPCODE_CONTROL_FRAME) &&
            ws->propagate_control_frames == false) {
            // automatically handle only 0 payload frames and make the transport read to return 0 on success
            // which might be interpreted as timeouts
            return ws_handle_control_frame_internal(t, timeout_ms);
        }

        if (rlen == 0) {
            ws->frame_state.bytes_remaining = 0;
            return 0; // timeout
        }
    }

    if (ws->frame_state.payload_len) {
        if ( (rlen = ws_read_payload(t, buffer, len, timeout_ms)) <= 0) {
            TRANS_LOGE("Error reading payload data");
            ws->frame_state.bytes_remaining = 0;
            return rlen;
        }
    }

    return rlen;
}


static int ws_poll_read(transport_handle_t t, int timeout_ms)
{
    transport_ws_t *ws = transport_get_context_data(t);
    return transport_poll_read(ws->parent, timeout_ms);
}

static int ws_poll_write(transport_handle_t t, int timeout_ms)
{
    transport_ws_t *ws = transport_get_context_data(t);
    return transport_poll_write(ws->parent, timeout_ms);;
}

static int ws_close(transport_handle_t t)
{
    transport_ws_t *ws = transport_get_context_data(t);
    return transport_close(ws->parent);
}

static ts_err_t ws_destroy(transport_handle_t t)
{
    transport_ws_t *ws = transport_get_context_data(t);
    os_free(ws->buffer);
    os_free(ws->path);
    os_free(ws->sub_protocol);
    os_free(ws->user_agent);
    os_free(ws->headers);
    os_free(ws);
    return 0;
}
static ts_err_t internal_transport_ws_set_path(transport_handle_t t, const char *path)
{
    if (t == NULL) {
        return ERR_INVALID_ARG;
    }
    transport_ws_t *ws = transport_get_context_data(t);
    if (ws->path) {
        os_free(ws->path);
    }
    if (path == NULL) {
        ws->path = NULL;
        return TRANS_OK;
    }
    ws->path = os_strdup(path);
    if (ws->path == NULL) {
        return ERR_NO_MEM;
    }
    return TRANS_OK;
}

void transport_ws_set_path(transport_handle_t t, const char *path)
{
    ts_err_t err = internal_transport_ws_set_path(t, path);
    if (err != TRANS_OK) {
        TRANS_LOGE("transport_ws_set_path has internally failed with err=%d", err);
    }
}

static int ws_get_socket(transport_handle_t t)
{
    if (t) {
        transport_ws_t *ws = t->data;
        if (ws && ws->parent && ws->parent->_get_socket) {
            return ws->parent->_get_socket(ws->parent);
        }
    }
    return -1;
}

transport_handle_t transport_ws_init(transport_handle_t parent_handle)
{
    transport_handle_t t = transport_init();
    transport_ws_t *ws = calloc(1, sizeof(transport_ws_t));
    TRANSPORT_MEM_CHECK(ws, return NULL);
    ws->parent = parent_handle;

    ws->path = os_strdup("/");
    TRANSPORT_MEM_CHECK(ws->path, {
        os_free(ws);
        transport_destroy(t);
        return NULL;
    });
    ws->buffer = os_malloc(WS_BUFFER_SIZE);
    TRANSPORT_MEM_CHECK(ws->buffer, {
        os_free(ws->path);
        os_free(ws);
        transport_destroy(t);
        return NULL;
    });

    transport_set_func(t, ws_connect, ws_read, ws_write, ws_close, ws_poll_read, ws_poll_write, ws_destroy);
    // webocket underlying transfer is the payload transfer handle
    transport_set_parent_transport_func(t, ws_get_payload_transport_handle);

    transport_set_context_data(t, ws);
    t->_get_socket = ws_get_socket;
    return t;
}

ts_err_t transport_ws_set_subprotocol(transport_handle_t t, const char *sub_protocol)
{
    if (t == NULL) {
        return ERR_INVALID_ARG;
    }
    transport_ws_t *ws = transport_get_context_data(t);
    if (ws->sub_protocol) {
        os_free(ws->sub_protocol);
    }
    if (sub_protocol == NULL) {
        ws->sub_protocol = NULL;
        return TRANS_OK;
    }
    ws->sub_protocol = os_strdup(sub_protocol);
    if (ws->sub_protocol == NULL) {
        return ERR_NO_MEM;
    }
    return TRANS_OK;
}

ts_err_t transport_ws_set_user_agent(transport_handle_t t, const char *user_agent)
{
    if (t == NULL) {
        return ERR_INVALID_ARG;
    }
    transport_ws_t *ws = transport_get_context_data(t);
    if (ws->user_agent) {
        os_free(ws->user_agent);
    }
    if (user_agent == NULL) {
        ws->user_agent = NULL;
        return TRANS_OK;
    }
    ws->user_agent = os_strdup(user_agent);
    if (ws->user_agent == NULL) {
        return ERR_NO_MEM;
    }
    return TRANS_OK;
}

ts_err_t transport_ws_set_headers(transport_handle_t t, const char *headers)
{
    if (t == NULL) {
        return ERR_INVALID_ARG;
    }
    transport_ws_t *ws = transport_get_context_data(t);
    if (ws->headers) {
        os_free(ws->headers);
    }
    if (headers == NULL) {
        ws->headers = NULL;
        return TRANS_OK;
    }
    ws->headers = os_strdup(headers);
    if (ws->headers == NULL) {
        return ERR_NO_MEM;
    }
    return TRANS_OK;
}

ts_err_t transport_ws_set_config(transport_handle_t t, const transport_ws_config_t *config)
{
    if (t == NULL) {
        return ERR_INVALID_ARG;
    }
    ts_err_t err = TRANS_OK;
    transport_ws_t *ws = transport_get_context_data(t);
    if (config->ws_path) {
        err = internal_transport_ws_set_path(t, config->ws_path);
        TRANSPORT_ERR_OK_CHECK(TAG, err, return err;)
    }
    if (config->sub_protocol) {
        err = transport_ws_set_subprotocol(t, config->sub_protocol);
        TRANSPORT_ERR_OK_CHECK(TAG, err, return err;)
    }
    if (config->user_agent) {
        err = transport_ws_set_user_agent(t, config->user_agent);
        TRANSPORT_ERR_OK_CHECK(TAG, err, return err;)
    }
    if (config->headers) {
        err = transport_ws_set_headers(t, config->headers);
        TRANSPORT_ERR_OK_CHECK(TAG, err, return err;)
    }
    ws->propagate_control_frames = config->propagate_control_frames;

    return err;
}

ws_transport_opcodes_t transport_ws_get_read_opcode(transport_handle_t t)
{
    transport_ws_t *ws = transport_get_context_data(t);
    if (ws->frame_state.header_received) {
        // convert the header byte to enum if correctly received
        return (ws_transport_opcodes_t)ws->frame_state.opcode;
    }
    return WS_TRANSPORT_OPCODES_NONE;
}

int transport_ws_get_read_payload_len(transport_handle_t t)
{
    transport_ws_t *ws = transport_get_context_data(t);
    return ws->frame_state.payload_len;
}

static int transport_ws_handle_control_frames(transport_handle_t t, char *buffer, int len, int timeout_ms, bool client_closed)
{
    transport_ws_t *ws = transport_get_context_data(t);

    // If no new header reception in progress, or not a control frame
    // just pass 0 -> no need to handle control frames
    if (ws->frame_state.header_received == false ||
        !(ws->frame_state.opcode & WS_OPCODE_CONTROL_FRAME)) {
        return 0;
    }
    int actual_len;
    int payload_len = ws->frame_state.payload_len;

    TRANS_LOGD("Handling control frame with %d bytes payload", payload_len);
    if (payload_len > len) {
        TRANS_LOGE("Not enough room for processing the payload (need=%d, available=%d)", payload_len, len);
        ws->frame_state.bytes_remaining = payload_len - len;
        return -1;
    }

    if (ws->frame_state.opcode == WS_OPCODE_PING) {
        // handle PING frames internally: just send a PONG with the same payload
        actual_len = _ws_write(t, WS_OPCODE_PONG | WS_FIN, WS_MASK, buffer,
                               payload_len, timeout_ms);
        if (actual_len != payload_len) {
            TRANS_LOGE("PONG send failed (payload_len=%d, written_len=%d)", payload_len, actual_len);
            return -1;
        }
        TRANS_LOGD("PONG sent correctly (payload_len=%d)", payload_len);

        // control frame handled correctly, reset the flag indicating new header received
        ws->frame_state.header_received = false;
        return 0;

    } else if (ws->frame_state.opcode == WS_OPCODE_CLOSE) {
        // handle CLOSE by the server: send a zero payload frame
        if (buffer && payload_len > 0) {     // if some payload, print out the status code
            //uint16_t *code_network_order = (uint16_t *) buffer;
            //TRANS_LOGI("Got CLOSE frame with status code=%u", ntohs(*code_network_order));
        }

        if (client_closed == false) {
            // Only echo the closing frame if not initiated by the client
            if (_ws_write(t, WS_OPCODE_CLOSE | WS_FIN, WS_MASK, NULL,0, timeout_ms) < 0) {
                TRANS_LOGE("Sending CLOSE frame with 0 payload failed");
                return -1;
            }
            TRANS_LOGD("CLOSE frame with no payload sent correctly");
        }

        // control frame handled correctly, reset the flag indicating new header received
        ws->frame_state.header_received = false;
        int ret = transport_ws_poll_connection_closed(t, timeout_ms);
        if (ret == 0) {
            TRANS_LOGW("Connection cannot be terminated gracefully within timeout=%d", timeout_ms);
            return -1;
        }
        if (ret < 0) {
            TRANS_LOGW("Connection terminated while waiting for clean TCP close");
            return -1;
        }
        TRANS_LOGI("Connection terminated gracefully");
        return 1;
    } else if (ws->frame_state.opcode == WS_OPCODE_PONG) {
        // handle PONG: just indicate return code
        TRANS_LOGD("Received PONG frame with payload=%d", payload_len);
        // control frame handled correctly, reset the flag indicating new header received
        ws->frame_state.header_received = false;
        return 2;
    }
    return 0;
}

int transport_ws_poll_connection_closed(transport_handle_t t, int timeout_ms)
{
    struct timeval timeout;
    int sock = transport_get_socket(t);
    fd_set readset;
    fd_set errset;
    FD_ZERO(&readset);
    FD_ZERO(&errset);
    FD_SET(sock, &readset);
    FD_SET(sock, &errset);
    ms_to_timeval(timeout_ms, &timeout);
    int ret = select(sock + 1, &readset, NULL, &errset, &timeout);
    if (ret > 0) {
        if (FD_ISSET(sock, &readset)) {
            uint8_t buffer;
            if (recv(sock, &buffer, 1, MSG_PEEK) <= 0) {
                // socket is readable, but reads zero bytes -- connection cleanly closed by FIN flag
                return 1;
            }
            TRANS_LOGW("transport_ws_poll_connection_closed: unexpected data readable on socket=%d", sock);
        } else if (FD_ISSET(sock, &errset)) {
            int sock_errno = 0;
            uint32_t optlen = sizeof(sock_errno);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &sock_errno, &optlen);
            TRANS_LOGD("transport_ws_poll_connection_closed select error %d, errno = %s, fd = %d", sock_errno, strerror(sock_errno), sock);
            if (sock_errno == ENOTCONN || sock_errno == ECONNRESET || sock_errno == ECONNABORTED) {
                // the three err codes above might be caused by connection termination by RTS flag
                // which we still assume as expected closing sequence of ws-transport connection
                return 1;
            }
            TRANS_LOGE("transport_ws_poll_connection_closed: unexpected errno=%d on socket=%d", sock_errno, sock);
        }
        return -1; // indicates error: socket unexpectedly reads an actual data, or unexpected errno code
    }
    return ret;

}
