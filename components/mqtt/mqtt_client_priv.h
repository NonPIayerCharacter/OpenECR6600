#ifndef _MQTT_CLIENT_PRIV_H_
#define _MQTT_CLIENT_PRIV_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <errno.h>
#include <string.h>

#include "event_groups.h"
#include "mqtt_client.h"
#include "mqtt_msg.h"
#ifdef CONFIG_MQTT_PROTOCOL_5
#include "mqtt5_client_priv.h"
#endif
#include "transport.h"
#include "transport_tcp.h"
#include "transport_ssl.h"
#include "transport_ws.h"
#include "mqtt_outbox.h"

/* using uri parser */
#include "url_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MQTT_DISABLE_API_LOCKS
# define MQTT_API_LOCK(c)
# define MQTT_API_UNLOCK(c)
#else
# define MQTT_API_LOCK(c)          xSemaphoreTakeRecursive(c->api_lock, portMAX_DELAY)
# define MQTT_API_UNLOCK(c)        xSemaphoreGiveRecursive(c->api_lock)
#endif /* MQTT_USE_API_LOCKS */

typedef struct mqtt_state {
    uint8_t *in_buffer;
    uint8_t *out_buffer;
    int in_buffer_length;
    int out_buffer_length;
    size_t message_length;
    size_t in_buffer_read_len;
    mqtt_message_t *outbound_message;
    mqtt_connection_t mqtt_connection;
    uint16_t pending_msg_id;
    int pending_msg_type;
    int pending_publish_qos;
    int pending_msg_count;
} mqtt_state_t;

typedef struct {
    mqtt_event_callback_t event_handle;
    mqtt_pub_callback_t pub_handle;
    int task_stack;
    int task_prio;
    char *uri;
    char *host;
    char *path;
    char *scheme;
    int port;
    bool auto_reconnect;
    int network_timeout_ms;
    int refresh_connection_after_ms;
    int reconnect_timeout_ms;
    char **alpn_protos;
    int num_alpn_protos;
    char *clientkey_password;
    int clientkey_password_len;
    bool use_global_ca_store;
    int ((*crt_bundle_attach)(void *conf));
    const char *cacert_buf;
    size_t cacert_bytes;
    const char *clientcert_buf;
    size_t clientcert_bytes;
    const char *clientkey_buf;
    size_t clientkey_bytes;
    const struct psk_key_hint *psk_hint_key;
    bool skip_cert_common_name_check;
    const char *common_name;
    bool use_secure_element;
    void *ds_data;
    int message_retransmit_timeout;
} mqtt_config_storage_t;

typedef enum {
    MQTT_STATE_INIT = 0,
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_WAIT_RECONNECT,
    MQTT_STATE_WAIT_CLOSE,
} mqtt_client_state_t;

struct trs_mqtt_client {
    transport_list_handle_t transport_list;
    transport_handle_t transport;
    os_queue_handle_t cfg_queue_handle;
    mqtt_config_storage_t *config;
    mqtt_state_t  mqtt_state;
    mqtt_connect_info_t connect_info;
    mqtt_client_state_t state;
    uint64_t refresh_connection_tick;
    int64_t keepalive_tick;
    uint64_t reconnect_tick;
#ifdef CONFIG_MQTT_PROTOCOL_5
    mqtt5_config_storage_t *mqtt5_config;
    uint16_t send_publish_packet_count; // This is for MQTT v5.0 flow control
#endif
    int wait_timeout_ms;
    int auto_reconnect;
    trs_mqtt_event_t event;
    bool run;
    bool wait_for_ping_resp;
    char heartbeat_count;
    outbox_handle_t outbox;
    EventGroupHandle_t status_bits;
    SemaphoreHandle_t  api_lock;
    bool everconn;
};

bool trs_mqtt_set_if_config(char const *const new_config, char **old_config);
void trs_mqtt_destroy_config(trs_mqtt_client_handle_t client);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif
