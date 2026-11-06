#ifndef _MQTT5_CLIENT_PRIV_H_
#define _MQTT5_CLIENT_PRIV_H_

#include "mqtt5_client.h"
#include "mqtt_client_priv.h"
#include "mqtt5_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mqtt5_topic_alias {
    char *topic;
    uint16_t topic_len;
    uint16_t topic_alias;
    STAILQ_ENTRY(mqtt5_topic_alias) next;
} mqtt5_topic_alias_t;
STAILQ_HEAD(mqtt5_topic_alias_list_t, mqtt5_topic_alias);
typedef struct mqtt5_topic_alias_list_t *mqtt5_topic_alias_handle_t;
typedef struct mqtt5_topic_alias *mqtt5_topic_alias_item_t;

typedef struct {
    trs_mqtt5_connection_property_storage_t connect_property_info;
    trs_mqtt5_connection_will_property_storage_t will_property_info;
    trs_mqtt5_connection_server_resp_property_t server_resp_property_info;
    trs_mqtt5_disconnect_property_config_t disconnect_property_info;
    const trs_mqtt5_publish_property_config_t *publish_property_info;
    const trs_mqtt5_subscribe_property_config_t *subscribe_property_info;
    const trs_mqtt5_unsubscribe_property_config_t *unsubscribe_property_info;
    mqtt5_topic_alias_handle_t peer_topic_alias;
} mqtt5_config_storage_t;

void trs_mqtt5_flow_control(trs_mqtt5_client_handle_t client);
void trs_mqtt5_parse_pubcomp(trs_mqtt5_client_handle_t client);
void trs_mqtt5_parse_puback(trs_mqtt5_client_handle_t client);
void trs_mqtt5_parse_unsuback(trs_mqtt5_client_handle_t client);
void trs_mqtt5_parse_suback(trs_mqtt5_client_handle_t client);
int trs_mqtt5_parse_connack(trs_mqtt5_client_handle_t client, int *connect_rsp_code);
void trs_mqtt5_client_destory(trs_mqtt5_client_handle_t client);
int trs_mqtt5_client_publish_check(trs_mqtt5_client_handle_t client, int qos, int retain);
int trs_mqtt5_client_subscribe_check(trs_mqtt5_client_handle_t client, int qos);
int trs_mqtt5_create_default_config(trs_mqtt5_client_handle_t client);
int trs_mqtt5_get_publish_data(trs_mqtt5_client_handle_t client, uint8_t *msg_buf, size_t msg_read_len, char **msg_topic, size_t *msg_topic_len, char **msg_data, size_t *msg_data_len);
#ifdef __cplusplus
}
#endif //__cplusplus

#endif