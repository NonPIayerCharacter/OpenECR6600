#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "websocket_client.h"
#include "transport.h"
#include "transport_tcp.h"
#include "transport_ssl.h"
#include "transport_ws.h"
/* using uri parser */
#include "url_parser.h"
#include "common_define.h"


#define WEBSOCKET_TCP_DEFAULT_PORT      (80)
#define WEBSOCKET_SSL_DEFAULT_PORT      (443)
#define WEBSOCKET_BUFFER_SIZE_BYTE      (1024)
#define WEBSOCKET_RECONNECT_TIMEOUT_MS  (10*1000)
#define WEBSOCKET_TASK_PRIORITY         (5)
#define WEBSOCKET_TASK_STACK            (4*1024)
#define WEBSOCKET_NETWORK_TIMEOUT_MS    (10*1000)
#define WEBSOCKET_PING_INTERVAL_SEC     (10)
#define WEBSOCKET_EVENT_QUEUE_SIZE      (1)
#define WEBSOCKET_PINGPONG_TIMEOUT_SEC  (120)
#define WEBSOCKET_KEEP_ALIVE_IDLE       (5)
#define WEBSOCKET_KEEP_ALIVE_INTERVAL   (5)
#define WEBSOCKET_KEEP_ALIVE_COUNT      (3)

#define WS_CLIENT_MEM_CHECK(TAG, a, action) if (!(a)) {                                         \
        WS_LOGE("%s(%d): %s", __FUNCTION__, __LINE__, "Memory exhausted");                     \
        action;                                                                                     \
        }

#define WS_CLIENT_ERR_OK_CHECK(TAG, err, action)  { \
        ts_err_t _ws_err_to_check = err;           \
        if (_ws_err_to_check != TRANS_OK) {           \
            WS_LOGE("%s(%d): Expected TRANS_OK; reported: %d", __FUNCTION__, __LINE__, _ws_err_to_check); \
            action;                                     \
            }                                           \
        }

#define WS_CLIENT_STATE_CHECK(TAG, a, action) if ((a->state) < WEBSOCKET_STATE_INIT) {                                         \
        WS_LOGE("%s:%d (%s): %s", __FILE__, __LINE__, __FUNCTION__, "Websocket already stop");       \
        action;                                                                                     \
        }

const static int STOPPED_BIT = BIT0;
const static int CLOSE_FRAME_SENT_BIT = BIT1;   // Indicates that a close frame was sent by the client
                                        // and we are waiting for the server to continue with clean close
int websocket_log_level = TRANS_DEFAULT_LOG_LEVEL;
EVENT_DEFINE_BASE(WEBSOCKET_EVENTS);

int asprintf(char **buf, const char *fmt, ...)
{
    *buf = NULL;
    va_list ap;
    va_start(ap, fmt);
    int count = vsnprintf(NULL,  0, fmt, ap);
    va_end(ap);
    if (count < 0) {
        return count;
    }
 
    char *buffer = os_malloc(count + 1);
    if (buffer == NULL) {
        return -1;
    }
    buffer[count] = 0;
 
    va_start(ap, fmt);
    count = vsnprintf(buffer,  count + 1, fmt, ap);
    if (count < 0) {
        os_free(buffer);
    } else {
        *buf = buffer;
    }
    va_end(ap);
    return count;
}

static uint64_t _tick_get_ms(void)
{
    return os_time_get();
}

static ts_err_t websocket_client_dispatch_event(websocket_client_handle_t client,
        websocket_event_id_t event,
        const char *data,
        int data_len)
{
    ts_err_t err = TRANS_FAIL;
    websocket_event_data_t event_data;
    
    event_data.client = client;
    event_data.user_context = client->config->user_context;
    event_data.data_ptr = data;
    event_data.data_len = data_len;
    event_data.op_code = client->last_opcode;
    event_data.payload_len = client->payload_len;
    event_data.payload_offset = client->payload_offset;
    if(client && client->event_handler)
    {
        client->event_handler(client,WEBSOCKET_EVENTS,event,&event_data);
        err = TRANS_OK;
    }
    /* if ((err = event_post_to(client->event_handle,
                                 WEBSOCKET_EVENTS, event,
                                 &event_data,
                                 sizeof(websocket_event_data_t),
                                 portMAX_DELAY)) != TRANS_OK) {
        return err;
    }

    return event_loop_run(client->event_handle, 0);*/
    return err;
}

static ts_err_t websocket_client_abort_connection(websocket_client_handle_t client)
{
    WS_CLIENT_STATE_CHECK(TAG, client, return TRANS_FAIL);
    transport_close(client->transport);

    if (client->config->auto_reconnect) {
        client->wait_timeout_ms = WEBSOCKET_RECONNECT_TIMEOUT_MS;
        client->reconnect_tick_ms = _tick_get_ms();
        WS_LOGI("Reconnect after %d ms", client->wait_timeout_ms);
    }
    client->state = WEBSOCKET_STATE_WAIT_TIMEOUT;
    websocket_client_dispatch_event(client, WEBSOCKET_EVENT_DISCONNECTED, NULL, 0);
    return TRANS_OK;
}

static ts_err_t websocket_client_set_config(websocket_client_handle_t client, const websocket_client_config_t *config)
{
    websocket_config_storage_t *cfg = client->config;
    cfg->task_prio = config->task_prio;
    if (cfg->task_prio <= 0) {
        cfg->task_prio = WEBSOCKET_TASK_PRIORITY;
    }

    cfg->task_stack = config->task_stack;
    if (cfg->task_stack == 0) {
        cfg->task_stack = WEBSOCKET_TASK_STACK;
    }

    if (config->host) {
        cfg->host = os_strdup(config->host);
        WS_CLIENT_MEM_CHECK(TAG, cfg->host, return ERR_NO_MEM);
    }

    if (config->port) {
        cfg->port = config->port;
    }

    if (config->username) {
        os_free(cfg->username);
        cfg->username = os_strdup(config->username);
        WS_CLIENT_MEM_CHECK(TAG, cfg->username, return ERR_NO_MEM);
    }

    if (config->password) {
        os_free(cfg->password);
        cfg->password = os_strdup(config->password);
        WS_CLIENT_MEM_CHECK(TAG, cfg->password, return ERR_NO_MEM);
    }

    if (config->uri) {
        os_free(cfg->uri);
        cfg->uri = os_strdup(config->uri);
        WS_CLIENT_MEM_CHECK(TAG, cfg->uri, return ERR_NO_MEM);
    }
    if (config->path) {
        os_free(cfg->path);
        cfg->path = os_strdup(config->path);
        WS_CLIENT_MEM_CHECK(TAG, cfg->path, return ERR_NO_MEM);
    }
    if (config->subprotocol) {
        os_free(cfg->subprotocol);
        cfg->subprotocol = os_strdup(config->subprotocol);
        WS_CLIENT_MEM_CHECK(TAG, cfg->subprotocol, return ERR_NO_MEM);
    }
    if (config->user_agent) {
        os_free(cfg->user_agent);
        cfg->user_agent = os_strdup(config->user_agent);
        WS_CLIENT_MEM_CHECK(TAG, cfg->user_agent, return ERR_NO_MEM);
    }
    if (config->headers) {
        os_free(cfg->headers);
        cfg->headers = os_strdup(config->headers);
        WS_CLIENT_MEM_CHECK(TAG, cfg->headers, return ERR_NO_MEM);
    }

    cfg->network_timeout_ms = config->network_timeout_ms == 0 ?WEBSOCKET_NETWORK_TIMEOUT_MS:config->network_timeout_ms;
    cfg->user_context = config->user_context;
    cfg->auto_reconnect = true;
    if (config->disable_auto_reconnect) {
        cfg->auto_reconnect = false;
    }

    if (config->disable_pingpong_discon){
        cfg->pingpong_timeout_sec = 0;
    } else if (config->pingpong_timeout_sec) {
        cfg->pingpong_timeout_sec = config->pingpong_timeout_sec;
    } else {
        cfg->pingpong_timeout_sec = WEBSOCKET_PINGPONG_TIMEOUT_SEC;
    }

    if (config->ping_interval_sec == 0) {
        cfg->ping_interval_sec = WEBSOCKET_PING_INTERVAL_SEC;
    } else {
        cfg->ping_interval_sec = config->ping_interval_sec;
    }

    return TRANS_OK;
}

static ts_err_t websocket_client_destroy_config(websocket_client_handle_t client)
{
    if (client == NULL) {
        return ERR_INVALID_ARG;
    }
    websocket_config_storage_t *cfg = client->config;
    if (client->config == NULL) {
        return ERR_INVALID_ARG;
    }
    os_free(cfg->host);
    os_free(cfg->uri);
    os_free(cfg->path);
    os_free(cfg->scheme);
    os_free(cfg->username);
    os_free(cfg->password);
    os_free(cfg->subprotocol);
    os_free(cfg->user_agent);
    os_free(cfg->headers);
    memset(cfg, 0, sizeof(websocket_config_storage_t));
    os_free(client->config);
    client->config = NULL;
    return TRANS_OK;
}

static ts_err_t set_websocket_transport_optional_settings(websocket_client_handle_t client, const char *scheme)
{
    transport_handle_t trans = transport_list_get_transport(client->transport_list, scheme);
    if (trans) {
        const transport_ws_config_t config = {
                .ws_path = client->config->path,
                .sub_protocol = client->config->subprotocol,
                .user_agent = client->config->user_agent,
                .headers = client->config->headers,
                .propagate_control_frames = true
        };
        return transport_ws_set_config(trans, &config);
    }
    return ERR_INVALID_ARG;
}

websocket_client_handle_t websocket_client_init(const websocket_client_config_t *config)
{
    websocket_client_handle_t client = calloc(1, sizeof(struct websocket_client));
    WS_CLIENT_MEM_CHECK(TAG, client, return NULL);

    if (config->keep_alive_enable == true) {
        client->keep_alive_cfg.keep_alive_enable = true;
        client->keep_alive_cfg.keep_alive_idle = (config->keep_alive_idle == 0) ? WEBSOCKET_KEEP_ALIVE_IDLE : config->keep_alive_idle;
        client->keep_alive_cfg.keep_alive_interval = (config->keep_alive_interval == 0) ? WEBSOCKET_KEEP_ALIVE_INTERVAL : config->keep_alive_interval;
        client->keep_alive_cfg.keep_alive_count =  (config->keep_alive_count == 0) ? WEBSOCKET_KEEP_ALIVE_COUNT : config->keep_alive_count;
    }

    client->lock = xSemaphoreCreateRecursiveMutex();
    WS_CLIENT_MEM_CHECK(TAG, client->lock, goto _websocket_init_fail);

    client->config = calloc(1, sizeof(websocket_config_storage_t));
    WS_CLIENT_MEM_CHECK(TAG, client->config, goto _websocket_init_fail);

    client->transport_list = transport_list_init();
    WS_CLIENT_MEM_CHECK(TAG, client->transport_list, goto _websocket_init_fail);

    transport_handle_t tcp = transport_tcp_init();
    WS_CLIENT_MEM_CHECK(TAG, tcp, goto _websocket_init_fail);

    transport_set_default_port(tcp, WEBSOCKET_TCP_DEFAULT_PORT);
    transport_tcp_set_keep_alive(tcp, &client->keep_alive_cfg);
    transport_list_add(client->transport_list, tcp, "_tcp"); // need to save to transport list, for cleanup


    transport_handle_t ws = transport_ws_init(tcp);
    WS_CLIENT_MEM_CHECK(TAG, ws, goto _websocket_init_fail);

    transport_set_default_port(ws, WEBSOCKET_TCP_DEFAULT_PORT);
    transport_list_add(client->transport_list, ws, "ws");
    if (config->transport == WEBSOCKET_TRANSPORT_OVER_TCP) {
        asprintf(&client->config->scheme, "ws");
        WS_CLIENT_MEM_CHECK(TAG, client->config->scheme, goto _websocket_init_fail);
    }

    transport_handle_t ssl = transport_ssl_init();
    WS_CLIENT_MEM_CHECK(TAG, ssl, goto _websocket_init_fail);

    transport_set_default_port(ssl, WEBSOCKET_SSL_DEFAULT_PORT);
    if (config->use_global_ca_store == true) {
        transport_ssl_enable_global_ca_store(ssl);
    } else if (config->cert_pem) {
        if (!config->cert_len) {
            transport_ssl_set_cert_data(ssl, config->cert_pem, strlen(config->cert_pem));
        } else {
            transport_ssl_set_cert_data_der(ssl, config->cert_pem, config->cert_len);
        }
    }
    if (config->client_cert) {
        if (!config->client_cert_len) {
            transport_ssl_set_client_cert_data(ssl, config->client_cert, strlen(config->client_cert));
        } else {
            transport_ssl_set_client_cert_data_der(ssl, config->client_cert, config->client_cert_len);
        }
    }
    if (config->client_key) {
        if (!config->client_key_len) {
            transport_ssl_set_client_key_data(ssl, config->client_key, strlen(config->client_key));
        } else {
            transport_ssl_set_client_key_data_der(ssl, config->client_key, config->client_key_len);
        }
    }
    if (config->skip_cert_common_name_check) {
        transport_ssl_skip_common_name_check(ssl);
    }
    transport_ssl_set_keep_alive(ssl, &client->keep_alive_cfg);
    transport_list_add(client->transport_list, ssl, "_ssl"); // need to save to transport list, for cleanup

    transport_handle_t wss = transport_ws_init(ssl);
    WS_CLIENT_MEM_CHECK(TAG, wss, goto _websocket_init_fail);

    transport_set_default_port(wss, WEBSOCKET_SSL_DEFAULT_PORT);

    transport_list_add(client->transport_list, wss, "wss");
    if (config->transport == WEBSOCKET_TRANSPORT_OVER_SSL) {
        asprintf(&client->config->scheme, "wss");
        WS_CLIENT_MEM_CHECK(TAG, client->config->scheme, goto _websocket_init_fail);
    }

    if (config->uri) {
        if (websocket_client_set_uri(client, config->uri) != TRANS_OK) {
            WS_LOGE("Invalid uri");
            goto _websocket_init_fail;
        }
    }

    if (websocket_client_set_config(client, config) != TRANS_OK) {
        WS_LOGE("Failed to set the configuration");
        goto _websocket_init_fail;
    }

    if (client->config->scheme == NULL) {
        asprintf(&client->config->scheme, "ws");
        WS_CLIENT_MEM_CHECK(TAG, client->config->scheme, goto _websocket_init_fail);
    }

    WS_CLIENT_ERR_OK_CHECK(TAG, set_websocket_transport_optional_settings(client, "ws"), goto _websocket_init_fail;)
    WS_CLIENT_ERR_OK_CHECK(TAG, set_websocket_transport_optional_settings(client, "wss"), goto _websocket_init_fail;)

    client->reconnect_tick_ms = _tick_get_ms();
    client->ping_tick_ms = _tick_get_ms();
    client->wait_for_pong_resp = false;

    int buffer_size = config->buffer_size;
    if (buffer_size <= 0) {
        buffer_size = WEBSOCKET_BUFFER_SIZE_BYTE;
    }
    //buffer_size = 1024;
    client->rx_buffer = os_malloc(buffer_size);
    WS_CLIENT_MEM_CHECK(TAG, client->rx_buffer, {
        goto _websocket_init_fail;
    });
    client->tx_buffer = os_malloc(buffer_size);
    WS_CLIENT_MEM_CHECK(TAG, client->tx_buffer, {
        goto _websocket_init_fail;
    });
    client->status_bits = xEventGroupCreate();
    WS_CLIENT_MEM_CHECK(TAG, client->status_bits, {
        goto _websocket_init_fail;
    });

    client->buffer_size = buffer_size;
    client->active_tick = xTaskGetTickCount();
    return client;

_websocket_init_fail:
    websocket_client_destroy(client);
    return NULL;
}

ts_err_t websocket_client_destroy(websocket_client_handle_t client)
{
    if (client == NULL) {
        return ERR_INVALID_ARG;
    }
    if (client->run) {
        websocket_client_stop(client);
    }
    if (client->event_handler) {
        client->event_handler = NULL;
    }
    websocket_client_destroy_config(client);
    transport_list_destroy(client->transport_list);
    vQueueDelete(client->lock);
    os_free(client->tx_buffer);
    os_free(client->rx_buffer);
    if (client->status_bits) {
        vEventGroupDelete(client->status_bits);
    }
    os_free(client);
    client = NULL;
    return TRANS_OK;
}

ts_err_t websocket_client_set_uri(websocket_client_handle_t client, const char *uri)
{
    if (client == NULL || uri == NULL) {
        return ERR_INVALID_ARG;
    }
    struct http_parser_url puri;
    //http_parser_url_init(&puri);
    memset(&puri,0,sizeof(struct http_parser_url));
    int parser_status = http_parser_parse_url(uri, strlen(uri), 0, &puri);
    if (parser_status != 0) {
        WS_LOGE("Error parse uri = %s", uri);
        return TRANS_FAIL;
    }
    if (puri.field_data[UF_SCHEMA].len) {
        os_free(client->config->scheme);
        asprintf(&client->config->scheme, "%.*s", puri.field_data[UF_SCHEMA].len, uri + puri.field_data[UF_SCHEMA].off);
        WS_CLIENT_MEM_CHECK(TAG, client->config->scheme, return ERR_NO_MEM);
    }

    if (puri.field_data[UF_HOST].len) {
        os_free(client->config->host);
        asprintf(&client->config->host, "%.*s", puri.field_data[UF_HOST].len, uri + puri.field_data[UF_HOST].off);
        WS_CLIENT_MEM_CHECK(TAG, client->config->host, return ERR_NO_MEM);
    }


    if (puri.field_data[UF_PATH].len || puri.field_data[UF_QUERY].len) {
        os_free(client->config->path);
        if (puri.field_data[UF_QUERY].len == 0) {
            asprintf(&client->config->path, "%.*s", puri.field_data[UF_PATH].len, uri + puri.field_data[UF_PATH].off);
        } else if (puri.field_data[UF_PATH].len == 0)  {
            asprintf(&client->config->path, "/?%.*s", puri.field_data[UF_QUERY].len, uri + puri.field_data[UF_QUERY].off);
        } else {
            asprintf(&client->config->path, "%.*s?%.*s", puri.field_data[UF_PATH].len, uri + puri.field_data[UF_PATH].off,
                    puri.field_data[UF_QUERY].len, uri + puri.field_data[UF_QUERY].off);
        }
        WS_CLIENT_MEM_CHECK(TAG, client->config->path, return ERR_NO_MEM);
    }
    if (puri.field_data[UF_PORT].off) {
        client->config->port = strtol((const char*)(uri + puri.field_data[UF_PORT].off), NULL, 10);
    }

    if (puri.field_data[UF_USERINFO].len) {
        char *user_info = NULL;
        asprintf(&user_info, "%.*s", puri.field_data[UF_USERINFO].len, uri + puri.field_data[UF_USERINFO].off);
        if (user_info) {
            char *pass = strchr(user_info, ':');
            if (pass) {
                pass[0] = 0; //terminal username
                pass ++;
                os_free(client->config->password);
                client->config->password = os_strdup(pass);
                WS_CLIENT_MEM_CHECK(TAG, client->config->password, return ERR_NO_MEM);
            }
            os_free(client->config->username);
            client->config->username = os_strdup(user_info);
            WS_CLIENT_MEM_CHECK(TAG, client->config->username, return ERR_NO_MEM);
            os_free(user_info);
        } else {
            return ERR_NO_MEM;
        }
    }
    return TRANS_OK;
}

ts_err_t websocket_client_get_uri(websocket_client_handle_t client,char *uri)
{
    uri = client->config->uri;
    return TRANS_OK;
}

ts_err_t websocket_client_get_state(websocket_client_handle_t client,int *state)
{
    *state = client->state;
    return TRANS_OK;
}

static ts_err_t websocket_client_recv(websocket_client_handle_t client)
{
    int rlen;
    client->payload_offset = 0;
    do {
        rlen = transport_read(client->transport, client->rx_buffer, client->buffer_size, client->config->network_timeout_ms);
        if (rlen < 0) {
            WS_LOGE("Error read data");
            return TRANS_FAIL;
        }
        client->payload_len = transport_ws_get_read_payload_len(client->transport);
        client->last_opcode = transport_ws_get_read_opcode(client->transport);

        if (rlen == 0 && client->last_opcode == WS_TRANSPORT_OPCODES_NONE ) {
            WS_LOGV("transport_read timeouts");
            return TRANS_FAIL;
        }

		#if defined(CONFIG_AI_COZE) || defined(CONFIG_AI_QZT)
		if (client->state == WEBSOCKET_STATE_CONNECTED && client->wait_for_pong_resp) {
			client->pingpong_tick_ms = _tick_get_ms();
    	}
		#endif

        if (client->last_opcode != WS_TRANSPORT_OPCODES_PONG) {
            client->active_tick = xTaskGetTickCount();
        }
        websocket_client_dispatch_event(client, WEBSOCKET_EVENT_DATA, client->rx_buffer, rlen);

        client->payload_offset += rlen;
    } while (client->payload_offset < client->payload_len);

    // if a PING message received -> send out the PONG, this will not work for PING messages with payload longer than buffer len
    if (client->last_opcode == WS_TRANSPORT_OPCODES_PING) {
        const char *data = (client->payload_len == 0) ? NULL : client->rx_buffer;
        WS_LOGD("Sending PONG with payload len=%d", client->payload_len);
        if (transport_ws_send_raw(client->transport, WS_TRANSPORT_OPCODES_PONG | WS_TRANSPORT_OPCODES_FIN,
                                  data, client->payload_len, client->config->network_timeout_ms) != client->payload_len) {
            WS_LOGE("Failed to send PONG");
            return TRANS_FAIL;
        }
    } else if (client->last_opcode == WS_TRANSPORT_OPCODES_PONG) {
        client->wait_for_pong_resp = false;
    } else if (client->last_opcode == WS_TRANSPORT_OPCODES_CLOSE) {
        WS_LOGD("Received close frame");
        client->state = WEBSOCKET_STATE_CLOSING;
    }

    return TRANS_OK;
}

int websocket_client_send_with_opcode(websocket_client_handle_t client, ws_transport_opcodes_t opcode, const uint8_t *data, int len, TickType_t timeout);

static int websocket_client_send_close(websocket_client_handle_t client, int code, const char *additional_data, int total_len, TickType_t timeout);

static void websocket_check_active_timeout(websocket_client_handle_t client)
{
    unsigned int passtime;
    unsigned int curr_tick = xTaskGetTickCount();
    unsigned int activetime = pdMS_TO_TICKS(600*1000);

    if (curr_tick < client->active_tick) {
        passtime = (portMAX_DELAY - client->active_tick) + curr_tick;
    } else {
        passtime = curr_tick - client->active_tick;
    }

    if (passtime > activetime) {
        websocket_client_dispatch_event(client, WEBSOCKET_EVENT_ACTIVE_TIMEOUT, NULL, 0);
    }
}

static void websocket_client_task(void *pv)
{
    const int lock_timeout = portMAX_DELAY;
    websocket_client_handle_t client = (websocket_client_handle_t) pv;
    client->run = true;

    WS_LOGI("websocket_client_task start\n");
    //get transport by scheme
    client->transport = transport_list_get_transport(client->transport_list, client->config->scheme);

    if (client->transport == NULL) {
        WS_LOGE("There are no transports valid, stop websocket client");
        client->run = false;
    }
    //default port
    if (client->config->port == 0) {
        client->config->port = transport_get_default_port(client->transport);
    }

    client->state = WEBSOCKET_STATE_INIT;
    xEventGroupClearBits(client->status_bits, STOPPED_BIT | CLOSE_FRAME_SENT_BIT);
    int read_select = 0;
    while (client->run) {
        if (xSemaphoreTakeRecursive(client->lock, lock_timeout) != pdPASS) {
            WS_LOGE("Failed to lock ws-client tasks, exiting the task...");
            break;
        }
        switch ((int)client->state) {
            case WEBSOCKET_STATE_INIT:
                if (client->transport == NULL) {
                    WS_LOGE("There are no transport");
                    client->run = false;
                    break;
                }
                if (transport_connect(client->transport,
                                          client->config->host,
                                          client->config->port,
                                          client->config->network_timeout_ms) < 0) {
                    WS_LOGE("Error transport connect");
                    websocket_client_abort_connection(client);
                    break;
                }
                WS_LOGD("Transport connected to %s://%s:%d", client->config->scheme, client->config->host, client->config->port);

                client->state = WEBSOCKET_STATE_CONNECTED;
                client->wait_for_pong_resp = false;
                websocket_client_dispatch_event(client, WEBSOCKET_EVENT_CONNECTED, NULL, 0);

                break;
            case WEBSOCKET_STATE_CONNECTED:
                if ((CLOSE_FRAME_SENT_BIT & xEventGroupGetBits(client->status_bits)) == 0) { // only send and check for PING
                                                                                                          // if closing hasn't been initiated
                    if (_tick_get_ms() - client->ping_tick_ms > client->config->ping_interval_sec*1000) {
                        client->ping_tick_ms = _tick_get_ms();
                        WS_LOGD("Sending PING...");
                        if (transport_ws_send_raw(client->transport, WS_TRANSPORT_OPCODES_PING | WS_TRANSPORT_OPCODES_FIN,
                                                  NULL, 0, client->config->network_timeout_ms) != 0) {
                            WS_LOGE("Failed to send PING");
                            websocket_client_abort_connection(client);
                            break;
                        }

                        if (!client->wait_for_pong_resp && client->config->pingpong_timeout_sec) {
                            client->pingpong_tick_ms = _tick_get_ms();
                            client->wait_for_pong_resp = true;
                        }
                    }

                    if ( _tick_get_ms() - client->pingpong_tick_ms > client->config->pingpong_timeout_sec*1000 ) {
                        if (client->wait_for_pong_resp) {
                            WS_LOGE("Error, no PONG received for more than %d seconds after PING", client->config->pingpong_timeout_sec);
                            websocket_client_abort_connection(client);
                            break;
                        }
                    }
                }

                websocket_check_active_timeout(client);

                if (read_select == 0) {
                    WS_LOGD("Read poll timeout: skipping transport_read()...");
                    break;
                }
                client->ping_tick_ms = _tick_get_ms();

                if (websocket_client_recv(client) == TRANS_FAIL) {
                    WS_LOGE("Error receive data");
                    websocket_client_abort_connection(client);
                    break;
                }
                break;
            case WEBSOCKET_STATE_WAIT_TIMEOUT:

                if (!client->config->auto_reconnect) {
                    client->run = false;
                    break;
                }
                if (_tick_get_ms() - client->reconnect_tick_ms > client->wait_timeout_ms) {
                    client->state = WEBSOCKET_STATE_INIT;
                    client->reconnect_tick_ms = _tick_get_ms();
                    WS_LOGD("Reconnecting...");
                    WS_LOGI("[APP] Free memory: %d bytes", xPortGetFreeHeapSize());
                }
                break;
            case WEBSOCKET_STATE_CLOSING:
                // if closing not initiated by the client echo the close message back
                if ((CLOSE_FRAME_SENT_BIT & xEventGroupGetBits(client->status_bits)) == 0) {
                    WS_LOGD("Closing initiated by the server, sending close frame");
                    if (transport_ws_send_raw(client->transport, WS_TRANSPORT_OPCODES_CLOSE | WS_TRANSPORT_OPCODES_FIN,
                                              NULL, 0, client->config->network_timeout_ms) != 0) {
                        WS_LOGE("Failed to send CLOSE");
                        websocket_client_abort_connection(client);
                        break;
                    }
                    xEventGroupSetBits(client->status_bits, CLOSE_FRAME_SENT_BIT);
                }
                break;
            default:
                WS_LOGD("Client run iteration in a default state: %d", client->state);
                break;
        }
        xSemaphoreGiveRecursive(client->lock);
        if (WEBSOCKET_STATE_CONNECTED == client->state) {
            read_select = transport_poll_read(client->transport, 1000); //Poll every 1000ms
            if (read_select < 0) {
                WS_LOGE("Network error: transport_poll_read() returned %d\n", read_select);
                //websocket_client_abort_connection(client);
                client->state = WEBSOCKET_STATE_CLOSING;
            }
        } else if (WEBSOCKET_STATE_WAIT_TIMEOUT == client->state) {
            if (!client->config->auto_reconnect) {
                client->run = false;
                break;
            }
            // waiting for reconnecting...
            vTaskDelay(client->wait_timeout_ms / 2 / portTICK_RATE_MS);
        } else if (WEBSOCKET_STATE_CLOSING == client->state &&
                  (CLOSE_FRAME_SENT_BIT & xEventGroupGetBits(client->status_bits))) {
            WS_LOGD(" Waiting for TCP connection to be closed by the server");
            int ret = transport_ws_poll_connection_closed(client->transport, 1000);
            if (ret == 0) {
                // still waiting
                break;
            }
            if (ret < 0) {
                WS_LOGW("Connection terminated while waiting for clean TCP close");
            }
            client->run = false;
            client->state = WEBSOCKET_STATE_UNKNOW;
            if (xSemaphoreTakeRecursive(client->lock, lock_timeout) != pdPASS) {            
                WS_LOGE("Failed to lock ws-client task, exiting the task..");            
                break;       
            }
            websocket_client_dispatch_event(client, WEBSOCKET_EVENT_CLOSED, NULL, 0);
            xSemaphoreGiveRecursive(client->lock);
            
            break;
        }
        vTaskDelay(10 / portTICK_RATE_MS);
    }

    transport_close(client->transport);
    xEventGroupSetBits(client->status_bits, STOPPED_BIT);
    client->state = WEBSOCKET_STATE_UNKNOW;
    WS_LOGE("The client has closed");
    vTaskDelete(NULL);
}

ts_err_t websocket_client_start(websocket_client_handle_t client)
{
    if (client == NULL) {
        return ERR_INVALID_ARG;
    }
    if (client->state >= WEBSOCKET_STATE_INIT) {
        WS_LOGE("The client has started");
        return TRANS_FAIL;
    }
    if (xTaskCreate(websocket_client_task, "websocket_task", client->config->task_stack / sizeof(StackType_t), client, client->config->task_prio, &client->task_handle) != pdTRUE) {
        WS_LOGE("Error create websocket task");
        return TRANS_FAIL;
    }
    xEventGroupClearBits(client->status_bits, STOPPED_BIT | CLOSE_FRAME_SENT_BIT);
    return TRANS_OK;
}

ts_err_t websocket_client_stop(websocket_client_handle_t client)
{
    if (client == NULL) {
        return ERR_INVALID_ARG;
    }
    if (!client->run) {
        WS_LOGW("Client was not started");
        return TRANS_FAIL;
    }

    /* A running client cannot be stopped from the websocket task/event handler */
    TaskHandle_t running_task = xTaskGetCurrentTaskHandle();
    if (running_task == client->task_handle) {
        WS_LOGE("Client cannot be stopped from websocket task");
        return TRANS_FAIL;
    }


    client->run = false;
    xEventGroupWaitBits(client->status_bits, STOPPED_BIT, false, true, portMAX_DELAY);
    client->state = WEBSOCKET_STATE_UNKNOW;
    return TRANS_OK;
}
#ifndef os_bswap_16
#define os_bswap_16(x)   ((uint16_t)                \
    ((((x) & 0xff00) >> 8) |                        \
     (((x) & 0x00ff) << 8)))
#endif
#ifndef htons
#define htons(x)    os_bswap_16(x)
#endif
static int websocket_client_send_close(websocket_client_handle_t client, int code, const char *additional_data, int total_len, TickType_t timeout)
{
    uint8_t *close_status_data = NULL;
    // RFC6455#section-5.5.1: The Close frame MAY contain a body (indicated by total_len >= 2)
    if (total_len >= 2) {
        close_status_data = calloc(1, total_len);
        WS_CLIENT_MEM_CHECK(TAG, close_status_data, return -1);
        // RFC6455#section-5.5.1: The first two bytes of the body MUST be a 2-byte representing a status
        uint16_t *code_network_order = (uint16_t *) close_status_data;
        *code_network_order = htons(code);
        memcpy(close_status_data + 2, additional_data, total_len - 2);
    }
    int ret = websocket_client_send_with_opcode(client, WS_TRANSPORT_OPCODES_CLOSE, close_status_data, total_len, timeout);
    os_free(close_status_data);
    return ret;
}


static ts_err_t websocket_client_close_with_optional_body(websocket_client_handle_t client, bool send_body, int code, const char *data, int len, TickType_t timeout)
{
    if (client == NULL) {
        return ERR_INVALID_ARG;
    }
    if (!client->run) {
        WS_LOGW("Client was not started");
        return TRANS_FAIL;
    }

    /* A running client cannot be stopped from the websocket task/event handler */
    TaskHandle_t running_task = xTaskGetCurrentTaskHandle();
    if (running_task == client->task_handle) {
        WS_LOGE("Client cannot be stopped from websocket task");
        return TRANS_FAIL;
    }

    if (send_body) {
        websocket_client_send_close(client, code, data, len + 2, portMAX_DELAY); // len + 2 -> always sending the code
    } else {
        websocket_client_send_close(client, 0, NULL, 0, portMAX_DELAY); // only opcode frame
    }

    // Set closing bit to prevent from sending PING frames while connected
    xEventGroupSetBits(client->status_bits, CLOSE_FRAME_SENT_BIT);

    if (STOPPED_BIT & xEventGroupWaitBits(client->status_bits, STOPPED_BIT, false, true, timeout)) {
        return TRANS_OK;
    }

    // If could not close gracefully within timeout, stop the client and disconnect
    client->run = false;
    xEventGroupWaitBits(client->status_bits, STOPPED_BIT, false, true, portMAX_DELAY);
    client->state = WEBSOCKET_STATE_UNKNOW;
    return TRANS_OK;
}

ts_err_t websocket_client_close_with_code(websocket_client_handle_t client, int code, const char *data, int len, TickType_t timeout)
{
    return websocket_client_close_with_optional_body(client, true, code, data, len, timeout);
}

ts_err_t websocket_client_close(websocket_client_handle_t client, TickType_t timeout)
{
    return websocket_client_close_with_optional_body(client, false, 0, NULL, 0, timeout);
}

int websocket_client_send_text(websocket_client_handle_t client, const char *data, int len, TickType_t timeout)
{
    return websocket_client_send_with_opcode(client, WS_TRANSPORT_OPCODES_TEXT, (const uint8_t *)data, len, timeout);
}

int websocket_client_send(websocket_client_handle_t client, const char *data, int len, TickType_t timeout)
{
    return websocket_client_send_with_opcode(client, WS_TRANSPORT_OPCODES_BINARY, (const uint8_t *)data, len, timeout);
}

int websocket_client_send_bin(websocket_client_handle_t client, const char *data, int len, TickType_t timeout)
{
    return websocket_client_send_with_opcode(client, WS_TRANSPORT_OPCODES_BINARY, (const uint8_t *)data, len, timeout);
}

int websocket_client_send_with_opcode(websocket_client_handle_t client, ws_transport_opcodes_t opcode, const uint8_t *data, int len, TickType_t timeout)
{
    int need_write = len;
    int wlen = 0, widx = 0;
    int ret = TRANS_FAIL;
    TickType_t started = xTaskGetTickCount();

    if (client == NULL || len < 0 || (len > 0 && data == NULL) ||
        (len == 0 && opcode != WS_TRANSPORT_OPCODES_CLOSE &&
         opcode != WS_TRANSPORT_OPCODES_PING && opcode != WS_TRANSPORT_OPCODES_PONG)) {
        WS_LOGE("opcode %d len %d %s%s", opcode, len, (client==NULL)?"client is null":"", (data==NULL)?"client is null":"");
        return TRANS_FAIL;
    }

    if (xSemaphoreTakeRecursive(client->lock, timeout) != pdPASS) {
        WS_LOGE("Could not lock ws-client within %d timeout", timeout);
        return TRANS_FAIL;
    }

    if (!websocket_client_is_connected(client)) {
        WS_LOGE("Websocket client is not connected");
        goto unlock_and_return;
    }

    if (client->transport == NULL || client->buffer_size <= 0) {
        WS_LOGE("Invalid transport");
        goto unlock_and_return;
    }
	
	#if defined(CONFIG_AI_COZE) || defined(CONFIG_AI_QZT)
	client->ping_tick_ms = _tick_get_ms();
	#endif

    uint32_t current_opcode = opcode;
    while (widx < len || current_opcode) {  // allow for sending "current_opcode" only message with len==0
        int send_timeout_ms = -1;

        if (timeout != portMAX_DELAY) {
            TickType_t elapsed = xTaskGetTickCount() - started;
            uint64_t remaining_ms;

            if (timeout != 0 && elapsed >= timeout) {
                WS_LOGE("Websocket send timed out: sent=%d total=%d", widx, len);
                websocket_client_abort_connection(client);
                goto unlock_and_return;
            }
            remaining_ms = (uint64_t)(timeout > elapsed ? timeout - elapsed : 0) * portTICK_PERIOD_MS;
            send_timeout_ms = remaining_ms > INT_MAX ? INT_MAX : (int)remaining_ms;
        }
        if (need_write > client->buffer_size) {
            need_write = client->buffer_size;
        } else {
            current_opcode |= WS_TRANSPORT_OPCODES_FIN;
        }
        if (need_write > 0) {
            memcpy(client->tx_buffer, data + widx, need_write);
        }
        // send with ws specific way and specific opcode

        if (opcode != WS_TRANSPORT_OPCODES_PING) {
            client->active_tick = xTaskGetTickCount();
        }
        wlen = transport_ws_send_raw(client->transport, current_opcode, (char *)client->tx_buffer, need_write,
                                      send_timeout_ms);
        if (wlen != need_write) {
            ret = wlen < 0 ? wlen : TRANS_FAIL;
            WS_LOGE("Incomplete WebSocket frame: sent=%d expected=%d", wlen, need_write);
            /* A partial frame cannot be followed by a new header on the same connection. */
            websocket_client_abort_connection(client);
            goto unlock_and_return;
        }
        current_opcode = 0;
        widx += wlen;
        need_write = len - widx;

    }
    ret = widx;
unlock_and_return:
    xSemaphoreGiveRecursive(client->lock);
    return ret;
}

bool websocket_client_is_connected(websocket_client_handle_t client)
{
    if (client == NULL) {
        return false;
    }
    return client->state == WEBSOCKET_STATE_CONNECTED;
}

ts_err_t websocket_register_events(websocket_client_handle_t client,
                                        websocket_event_id_t event,
                                        event_handler_t event_handler,
                                        void *event_handler_arg)
{
    if (client == NULL || event_handler == NULL) {
        return ERR_INVALID_ARG;
    }
    client->event_handler = event_handler;
    return TRANS_OK;
    //return event_handler_register_with(client->event_handle, WEBSOCKET_EVENTS, event, event_handler, event_handler_arg);
}

ts_err_t websocket_client_set_header(websocket_client_handle_t client, const websocket_client_config_t *config)
{
    websocket_config_storage_t *cfg = client->config;
    if (config->headers) {
        os_free(cfg->headers);
        cfg->headers = os_strdup(config->headers);
        WS_CLIENT_MEM_CHECK(TAG, cfg->headers, return ERR_NO_MEM);
    }
    return TRANS_OK;
}
