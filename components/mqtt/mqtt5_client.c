#include <string.h>
#include "oshal.h"
#include "mqtt5_client.h"
#include "mqtt_client_priv.h"
#include "mqtt5_client_priv.h"

#ifdef CONFIG_MQTT_PROTOCOL_5

static void trs_mqtt5_print_error_code(trs_mqtt5_client_handle_t client, int code);
static int trs_mqtt5_client_update_topic_alias(mqtt5_topic_alias_handle_t topic_alias_handle, uint16_t topic_alias, char *topic, size_t topic_len);
static char *trs_mqtt5_client_get_topic_alias(mqtt5_topic_alias_handle_t topic_alias_handle, uint16_t topic_alias, size_t *topic_length);
static void trs_mqtt5_client_delete_topic_alias(mqtt5_topic_alias_handle_t topic_alias_handle);
static int trs_mqtt5_user_property_copy(mqtt5_user_property_handle_t user_property_new, const mqtt5_user_property_handle_t user_property_old);

void trs_mqtt5_flow_control(trs_mqtt5_client_handle_t client)
{
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
        int msg_type = mqtt5_get_type(client->mqtt_state.outbound_message->data);
        if (msg_type == MQTT_MSG_TYPE_PUBLISH) {
            int msg_qos = mqtt5_get_qos(client->mqtt_state.outbound_message->data);
            if (msg_qos > 0) {
                client->send_publish_packet_count ++;
                os_printf(LM_APP, LL_DBG, "Sent (%d) qos > 0 publish packet without ack\r\n", client->send_publish_packet_count);
            }
        }
    }
}

void trs_mqtt5_parse_pubcomp(trs_mqtt5_client_handle_t client)
{
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
        os_printf(LM_APP, LL_INFO, "MQTT_MSG_TYPE_PUBCOMP return code is %d\r\n", mqtt5_msg_get_reason_code(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len));
        size_t msg_data_len = client->mqtt_state.in_buffer_read_len;
        client->event.data = mqtt5_get_pubcomp_data(client->mqtt_state.in_buffer, &msg_data_len, &client->event.property->user_property);
        client->event.data_len = msg_data_len;
        client->event.total_data_len = msg_data_len;
        client->event.current_data_offset = 0;
    }
}

void trs_mqtt5_parse_puback(trs_mqtt5_client_handle_t client)
{
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
        os_printf(LM_APP, LL_INFO, "MQTT_MSG_TYPE_PUBACK return code is %d\r\n", mqtt5_msg_get_reason_code(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len));
        size_t msg_data_len = client->mqtt_state.in_buffer_read_len;
        client->event.data = mqtt5_get_puback_data(client->mqtt_state.in_buffer, &msg_data_len, &client->event.property->user_property);
        client->event.data_len = msg_data_len;
        client->event.total_data_len = msg_data_len;
        client->event.current_data_offset = 0;
        client->send_publish_packet_count --;
    }
}

void trs_mqtt5_parse_unsuback(trs_mqtt5_client_handle_t client)
{
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
        os_printf(LM_APP, LL_INFO, "MQTT_MSG_TYPE_UNSUBACK return code is %d\r\n", mqtt5_msg_get_reason_code(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len));
        size_t msg_data_len = client->mqtt_state.in_buffer_read_len;
        client->event.data = mqtt5_get_unsuback_data(client->mqtt_state.in_buffer, &msg_data_len, &client->event.property->user_property);
        client->event.data_len = msg_data_len;
        client->event.total_data_len = msg_data_len;
        client->event.current_data_offset = 0;
    }
}

void trs_mqtt5_parse_suback(trs_mqtt5_client_handle_t client)
{
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
        os_printf(LM_APP, LL_INFO, "MQTT_MSG_TYPE_SUBACK return code is %d\r\n", mqtt5_msg_get_reason_code(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len));
    }
}

int trs_mqtt5_parse_connack(trs_mqtt5_client_handle_t client, int *connect_rsp_code)
{
    size_t len = client->mqtt_state.in_buffer_read_len;
    client->mqtt_state.in_buffer_read_len = 0;
    uint8_t ack_flag = 0;
    if (mqtt5_msg_parse_connack_property(client->mqtt_state.in_buffer, len, &client->connect_info, &client->mqtt5_config->connect_property_info, &client->mqtt5_config->server_resp_property_info, connect_rsp_code, &ack_flag, &client->event.property->user_property) != 0) {
        os_printf(LM_APP, LL_ERR, "Failed to parse CONNACK packet\r\n");
        return -1;
    }
    if (*connect_rsp_code == MQTT_CONNECTION_ACCEPTED) {
        os_printf(LM_APP, LL_DBG, "Connected\r\n");
        client->event.session_present = ack_flag & 0x01; 
        return 0;
    }
    trs_mqtt5_print_error_code(client, *connect_rsp_code);
    return -1;
}

int trs_mqtt5_get_publish_data(trs_mqtt5_client_handle_t client, uint8_t *msg_buf, size_t msg_read_len, char **msg_topic, size_t *msg_topic_len, char **msg_data, size_t *msg_data_len)
{
    // get property
    uint16_t property_len = 0;
    trs_mqtt5_publish_resp_property_t property = {0};
    *msg_data = mqtt5_get_publish_property_payload(msg_buf, msg_read_len, msg_topic, msg_topic_len, &property, &property_len, msg_data_len, &client->event.property->user_property);
     if (*msg_data == NULL) {
    	os_printf(LM_APP, LL_ERR, "%s: mqtt5_get_publish_property_payload() failed\r\n", __func__);
        return -1;
    }

    if (property.topic_alias > client->mqtt5_config->connect_property_info.topic_alias_maximum) {
    	os_printf(LM_APP, LL_ERR, "%s: Broker response topic alias %d is over the max topic alias %d\r\n", __func__, property.topic_alias, client->mqtt5_config->connect_property_info.topic_alias_maximum);
        return -1;
    }

    if (property.topic_alias) {
        if (*msg_topic_len == 0) {
        	os_printf(LM_APP, LL_INFO, "Publish topic is empty, use topic alias\r\n");
            *msg_topic = trs_mqtt5_client_get_topic_alias(client->mqtt5_config->peer_topic_alias, property.topic_alias, msg_topic_len);
            if (!*msg_topic) {
            	os_printf(LM_APP, LL_ERR, "%s: trs_mqtt5_client_get_topic_alias() failed\r\n", __func__);
                return -1;
            }
        } else {
            if (trs_mqtt5_client_update_topic_alias(client->mqtt5_config->peer_topic_alias, property.topic_alias, *msg_topic, *msg_topic_len) != 0) {
            	os_printf(LM_APP, LL_ERR, "%s: trs_mqtt5_client_update_topic_alias() failed\r\n", __func__);
                return -1;
            }
        }
    }

    client->event.property->payload_format_indicator = property.payload_format_indicator;
    client->event.property->response_topic = property.response_topic;
    client->event.property->response_topic_len = property.response_topic_len;
    client->event.property->correlation_data = property.correlation_data;
    client->event.property->correlation_data_len = property.correlation_data_len;
    client->event.property->content_type = property.content_type;
    client->event.property->content_type_len = property.content_type_len;
    return 0;
}

int trs_mqtt5_create_default_config(trs_mqtt5_client_handle_t client)
{
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
        client->event.property = mqtt_calloc(1, sizeof(trs_mqtt5_event_property_t));
        MEM_CHECK(client->event.property, return -1);
        client->mqtt5_config = mqtt_calloc(1, sizeof(mqtt5_config_storage_t));
        MEM_CHECK(client->mqtt5_config, return -1);
        client->mqtt5_config->server_resp_property_info.max_qos = 2;
        client->mqtt5_config->server_resp_property_info.retain_available = true;
        client->mqtt5_config->server_resp_property_info.wildcard_subscribe_available = true;
        client->mqtt5_config->server_resp_property_info.subscribe_identifiers_available = true;
        client->mqtt5_config->server_resp_property_info.shared_subscribe_available = true;
        client->mqtt5_config->server_resp_property_info.receive_maximum = 65535;
    }
    return 0;
}

static void trs_mqtt5_print_error_code(trs_mqtt5_client_handle_t client, int code)
{
    switch (code) {
    case MQTT5_UNSPECIFIED_ERROR:
    	os_printf(LM_APP, LL_DBG, "Unspecified error\r\n");
        break;
    case MQTT5_MALFORMED_PACKET:
    	os_printf(LM_APP, LL_DBG, "Malformed Packet\r\n");
        break;
    case MQTT5_PROTOCOL_ERROR:
    	os_printf(LM_APP, LL_DBG, "Protocol Error\r\n");
        break;
    case MQTT5_IMPLEMENT_SPECIFIC_ERROR:
    	os_printf(LM_APP, LL_DBG, "Implementation specific error\r\n");
        break;
    case MQTT5_UNSUPPORTED_PROTOCOL_VER:
    	os_printf(LM_APP, LL_DBG, "Unsupported Protocol Version\r\n");
        break;
    case MQTT5_INVALID_CLIENT_ID:
    	os_printf(LM_APP, LL_DBG, "Client Identifier not valid\r\n");
        break;
    case MQTT5_BAD_USERNAME_OR_PWD:
    	os_printf(LM_APP, LL_DBG, "Bad User Name or Password\r\n");
        break;
    case MQTT5_NOT_AUTHORIZED:
    	os_printf(LM_APP, LL_DBG, "Not authorized\r\n");
        break;
    case MQTT5_SERVER_UNAVAILABLE:
    	os_printf(LM_APP, LL_DBG, "Server unavailable\r\n");
        break;
    case MQTT5_SERVER_BUSY:
    	os_printf(LM_APP, LL_DBG, "Server busy\r\n");
        break;
    case MQTT5_BANNED:
    	os_printf(LM_APP, LL_DBG, "Banned\r\n");
        break;
    case MQTT5_SERVER_SHUTTING_DOWN:
    	os_printf(LM_APP, LL_DBG, "Server shutting down\r\n");
        break;
    case MQTT5_BAD_AUTH_METHOD:
    	os_printf(LM_APP, LL_DBG, "Bad authentication method\r\n");
        break;
    case MQTT5_KEEP_ALIVE_TIMEOUT:
    	os_printf(LM_APP, LL_DBG, "Keep Alive timeout\r\n");
        break;
    case MQTT5_SESSION_TAKEN_OVER:
    	os_printf(LM_APP, LL_DBG, "Session taken over\r\n");
        break;
    case MQTT5_TOPIC_FILTER_INVALID:
    	os_printf(LM_APP, LL_DBG, "Topic Filter invalid\r\n");
        break;
    case MQTT5_TOPIC_NAME_INVALID:
    	os_printf(LM_APP, LL_DBG, "Topic Name invalid\r\n");
        break;
    case MQTT5_PACKET_IDENTIFIER_IN_USE:
    	os_printf(LM_APP, LL_DBG, "Packet Identifier in use\r\n");
        break;
    case MQTT5_PACKET_IDENTIFIER_NOT_FOUND:
    	os_printf(LM_APP, LL_DBG, "Packet Identifier not found\r\n");
        break;
    case MQTT5_RECEIVE_MAXIMUM_EXCEEDED:
    	os_printf(LM_APP, LL_DBG, "Receive Maximum exceeded\r\n");
        break;
    case MQTT5_TOPIC_ALIAS_INVALID:
    	os_printf(LM_APP, LL_DBG, "Topic Alias invalid\r\n");
        break;
    case MQTT5_PACKET_TOO_LARGE:
    	os_printf(LM_APP, LL_DBG, "Packet too large\r\n");
        break;
    case MQTT5_MESSAGE_RATE_TOO_HIGH:
    	os_printf(LM_APP, LL_DBG, "Message rate too high\r\n");
        break;
    case MQTT5_QUOTA_EXCEEDED:
    	os_printf(LM_APP, LL_DBG, "Quota exceeded\r\n");
        break;
    case MQTT5_ADMINISTRATIVE_ACTION:
    	os_printf(LM_APP, LL_DBG, "Administrative action\r\n");
        break;
    case MQTT5_PAYLOAD_FORMAT_INVALID:
    	os_printf(LM_APP, LL_DBG, "Payload format invalid\r\n");
        break;
    case MQTT5_RETAIN_NOT_SUPPORT:
    	os_printf(LM_APP, LL_DBG, "Retain not supported\r\n");
        break;
    case MQTT5_QOS_NOT_SUPPORT:
    	os_printf(LM_APP, LL_DBG, "QoS not supported\r\n");
        break;
    case MQTT5_USE_ANOTHER_SERVER:
    	os_printf(LM_APP, LL_DBG, "Use another server\r\n");
        break;
    case MQTT5_SERVER_MOVED:
    	os_printf(LM_APP, LL_DBG, "Server moved\r\n");
        break;
    case MQTT5_SHARED_SUBSCR_NOT_SUPPORTED:
    	os_printf(LM_APP, LL_DBG, "Shared Subscriptions not supported\r\n");
        break;
    case MQTT5_CONNECTION_RATE_EXCEEDED:
    	os_printf(LM_APP, LL_DBG, "Connection rate exceeded\r\n");
        break;
    case MQTT5_MAXIMUM_CONNECT_TIME:
    	os_printf(LM_APP, LL_DBG, "Maximum connect time\r\n");
        break;
    case MQTT5_SUBSCRIBE_IDENTIFIER_NOT_SUPPORT:
    	os_printf(LM_APP, LL_DBG, "Subscription Identifiers not supported\r\n");
        break;
    case MQTT5_WILDCARD_SUBSCRIBE_NOT_SUPPORT:
    	os_printf(LM_APP, LL_DBG, "Wildcard Subscriptions not supported\r\n");
        break;
    default:
    	os_printf(LM_APP, LL_DBG, "Connection refused, Unknow reason\r\n");
        break;
    }
}

int trs_mqtt5_client_subscribe_check(trs_mqtt5_client_handle_t client, int qos)
{
    /* Check Server support QoS level */
    if (client->mqtt5_config->server_resp_property_info.max_qos < qos) {
    	os_printf(LM_APP, LL_ERR, "Server only support max QoS level %d\r\n", client->mqtt5_config->server_resp_property_info.max_qos);
        return -1;
    }

    return 0;
}

int trs_mqtt5_client_publish_check(trs_mqtt5_client_handle_t client, int qos, int retain)
{
    /* Check Server support QoS level */
    if (client->mqtt5_config->server_resp_property_info.max_qos < qos) {
    	os_printf(LM_APP, LL_ERR, "Server only support max QoS level %d\r\n", client->mqtt5_config->server_resp_property_info.max_qos);
        return -1;
    }

    /* Check Server support RETAIN */
    if (!client->mqtt5_config->server_resp_property_info.retain_available && retain) {
    	os_printf(LM_APP, LL_ERR, "Server not support retain\r\n");
        return -1;
    }

    /* Flow control to check PUBLISH(No PUBACK or PUBCOMP received) packet sent count(Only record QoS1 and QoS2)*/
    if (client->send_publish_packet_count >= client->mqtt5_config->server_resp_property_info.receive_maximum) {
    	os_printf(LM_APP, LL_ERR, "Client send more than %d QoS1 and QoS2 PUBLISH packet without no ack\r\n", client->mqtt5_config->server_resp_property_info.receive_maximum);
        return -1;
    }

    return 0;
}

void trs_mqtt5_client_destory(trs_mqtt5_client_handle_t client)
{
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
        if (client->mqtt5_config) {
            mqtt_free(client->mqtt5_config->will_property_info.content_type);
            mqtt_free(client->mqtt5_config->will_property_info.response_topic);
            mqtt_free(client->mqtt5_config->will_property_info.correlation_data);
            mqtt_free(client->mqtt5_config->server_resp_property_info.response_info);
            trs_mqtt5_client_delete_topic_alias(client->mqtt5_config->peer_topic_alias);
            trs_mqtt5_client_delete_user_property(client->mqtt5_config->connect_property_info.user_property);
            trs_mqtt5_client_delete_user_property(client->mqtt5_config->will_property_info.user_property);
            trs_mqtt5_client_delete_user_property(client->mqtt5_config->disconnect_property_info.user_property);
            mqtt_free(client->mqtt5_config);
        }
        mqtt_free(client->event.property);
    }
}

static void trs_mqtt5_client_delete_topic_alias(mqtt5_topic_alias_handle_t topic_alias_handle)
{
    if (topic_alias_handle) {
        mqtt5_topic_alias_item_t item, tmp;
        STAILQ_FOREACH_SAFE(item, topic_alias_handle, next, tmp) {
            STAILQ_REMOVE(topic_alias_handle, item, mqtt5_topic_alias, next);
            mqtt_free(item->topic);
            mqtt_free(item);
        }
        mqtt_free(topic_alias_handle);
    }
}

static int trs_mqtt5_client_update_topic_alias(mqtt5_topic_alias_handle_t topic_alias_handle, uint16_t topic_alias, char *topic, size_t topic_len)
{
    mqtt5_topic_alias_item_t item;
    bool found = false;
    STAILQ_FOREACH(item, topic_alias_handle, next) {
        if (item->topic_alias == topic_alias) {
            found = true;
            break;
        }
    }
    if (found) {
        if ((item->topic_len != topic_len) || strncmp(topic, item->topic, topic_len)) {
            mqtt_free(item->topic);
            item->topic = mqtt_calloc(1, topic_len);
            MEM_CHECK(item->topic, return -1);
            memcpy(item->topic, topic, topic_len);
            item->topic_len = topic_len;
        }
    } else {
        item = mqtt_calloc(1, sizeof(mqtt5_topic_alias_t));
        MEM_CHECK(item, return -1);
        item->topic_alias = topic_alias;
        item->topic_len = topic_len;
        item->topic = mqtt_calloc(1, topic_len);
        MEM_CHECK(item->topic, {
            mqtt_free(item);
            return -1;
        });
        memcpy(item->topic, topic, topic_len);
        STAILQ_INSERT_TAIL(topic_alias_handle, item, next);
    }
    return 0;
}

static char *trs_mqtt5_client_get_topic_alias(mqtt5_topic_alias_handle_t topic_alias_handle, uint16_t topic_alias, size_t *topic_length)
{
    mqtt5_topic_alias_item_t item;
    STAILQ_FOREACH(item, topic_alias_handle, next) {
        if (item->topic_alias == topic_alias) {
            *topic_length = item->topic_len;
            return item->topic;
        }
    }
    *topic_length = 0;
    return NULL;
}

static int trs_mqtt5_user_property_copy(mqtt5_user_property_handle_t user_property_new, const mqtt5_user_property_handle_t user_property_old)
{
    if (!user_property_new || !user_property_old) {
    	os_printf(LM_APP, LL_ERR, "Input is NULL\r\n");
        return -1;
    }

    mqtt5_user_property_item_t old_item, new_item;
    STAILQ_FOREACH(old_item, user_property_old, next) {
        new_item = mqtt_calloc(1, sizeof(mqtt5_user_property_t));
        MEM_CHECK(new_item, return -1);
        new_item->key = os_strdup(old_item->key);
        MEM_CHECK(new_item->key, {
            mqtt_free(new_item);
            return -1;
        });
        new_item->value = os_strdup(old_item->value);
        MEM_CHECK(new_item->value, {
            mqtt_free(new_item->key);
            mqtt_free(new_item);
            return -1;
        });
        STAILQ_INSERT_TAIL(user_property_new, new_item, next);
    }
    return 0;
}

int trs_mqtt5_client_set_publish_property(trs_mqtt5_client_handle_t client, const trs_mqtt5_publish_property_config_t *property)
{
    if (!client) {
    	os_printf(LM_APP, LL_ERR, "Client was not initialized\r\n");
        return -1;
    }
    MQTT_API_LOCK(client);

    /* Check protocol version */
    if(client->connect_info.protocol_ver != MQTT_PROTOCOL_V_5) {
    	os_printf(LM_APP, LL_ERR, "MQTT protocol version is not v5\r\n");
        MQTT_API_UNLOCK(client);
        return -1;
    }
    /* Check topic alias less than server maximum topic alias */
    if (property->topic_alias > client->mqtt5_config->server_resp_property_info.topic_alias_maximum) {
    	os_printf(LM_APP, LL_ERR, "Topic alias %d is bigger than server support %d\r\n", property->topic_alias, client->mqtt5_config->server_resp_property_info.topic_alias_maximum);
        MQTT_API_UNLOCK(client);
        return -1;
    }
    client->mqtt5_config->publish_property_info = property;
    MQTT_API_UNLOCK(client);
    return 0;
}

int trs_mqtt5_client_set_subscribe_property(trs_mqtt5_client_handle_t client, const trs_mqtt5_subscribe_property_config_t *property)
{
    if (!client) {
    	os_printf(LM_APP, LL_ERR, "Client was not initialized\r\n");
        return -1;
    }
    if (property->retain_handle > 2) {
    	os_printf(LM_APP, LL_ERR, "retain_handle only support 0, 1, 2\r\n");
        return -1;
    }
    MQTT_API_LOCK(client);

    /* Check protocol version */
    if (client->connect_info.protocol_ver != MQTT_PROTOCOL_V_5) {
    	os_printf(LM_APP, LL_ERR, "MQTT protocol version is not v5\r\n");
        MQTT_API_UNLOCK(client);
        return -1;
    }
    if (property->is_share_subscribe) {
        if (property->no_local_flag) {
            // MQTT-3.8.3-4 not allow that No Local bit to 1 on a Shared Subscription
        	os_printf(LM_APP, LL_ERR, "Protocol error that no local flag set on shared subscription\r\n");
            MQTT_API_UNLOCK(client);
            return -1;
        }
        if (!client->mqtt5_config->server_resp_property_info.shared_subscribe_available) {
        	os_printf(LM_APP, LL_ERR, "MQTT broker not support shared subscribe\r\n");
            MQTT_API_UNLOCK(client);
            return -1;
        }
        if (!property->share_name || !strlen(property->share_name)) {
        	os_printf(LM_APP, LL_ERR, "Share name can't be empty for shared subscribe\r\n");
            MQTT_API_UNLOCK(client);
            return -1;
        }
    }
    client->mqtt5_config->subscribe_property_info = property;
    MQTT_API_UNLOCK(client);
    return 0;
}

int trs_mqtt5_client_set_unsubscribe_property(trs_mqtt5_client_handle_t client, const trs_mqtt5_unsubscribe_property_config_t *property)
{
    if (!client) {
    	os_printf(LM_APP, LL_ERR, "Client was not initialized\r\n");
        return -1;
    }
    MQTT_API_LOCK(client);

    /* Check protocol version */
    if (client->connect_info.protocol_ver != MQTT_PROTOCOL_V_5) {
    	os_printf(LM_APP, LL_ERR, "MQTT protocol version is not v5\r\n");
        MQTT_API_UNLOCK(client);
        return -1;
    }
    if (property->is_share_subscribe) {
        if (!client->mqtt5_config->server_resp_property_info.shared_subscribe_available) {
        	os_printf(LM_APP, LL_ERR, "MQTT broker not support shared subscribe\r\n");
            MQTT_API_UNLOCK(client);
            return -1;
        }
        if (!property->share_name || !strlen(property->share_name)) {
        	os_printf(LM_APP, LL_ERR, "Share name can't be empty for shared subscribe\r\n");
            MQTT_API_UNLOCK(client);
            return -1;
        }
    }
    client->mqtt5_config->unsubscribe_property_info = property;
    MQTT_API_UNLOCK(client);
    return 0;
}

int trs_mqtt5_client_set_disconnect_property(trs_mqtt5_client_handle_t client, const trs_mqtt5_disconnect_property_config_t *property)
{
    if (!client) {
    	os_printf(LM_APP, LL_ERR, "Client was not initialized\r\n");
        return -1;
    }
    MQTT_API_LOCK(client);

    /* Check protocol version */
    if (client->connect_info.protocol_ver != MQTT_PROTOCOL_V_5) {
    	os_printf(LM_APP, LL_ERR, "MQTT protocol version is not v5\r\n");
        MQTT_API_UNLOCK(client);
        return -1;
    }
    if (property) {
        if (property->session_expiry_interval) {
            client->mqtt5_config->disconnect_property_info.session_expiry_interval = property->session_expiry_interval;
        }
        if (property->disconnect_reason) {
            client->mqtt5_config->disconnect_property_info.disconnect_reason = property->disconnect_reason;
        }
        if (property->user_property) {
            trs_mqtt5_client_delete_user_property(client->mqtt5_config->disconnect_property_info.user_property);
            client->mqtt5_config->disconnect_property_info.user_property = mqtt_calloc(1, sizeof(struct mqtt5_user_property_list_t));
            MEM_CHECK(client->mqtt5_config->disconnect_property_info.user_property, {
                MQTT_API_UNLOCK(client);
                return -1;
            });
            STAILQ_INIT(client->mqtt5_config->disconnect_property_info.user_property);
            if (trs_mqtt5_user_property_copy(client->mqtt5_config->disconnect_property_info.user_property, property->user_property) != 0) {
            	os_printf(LM_APP, LL_ERR, "trs_mqtt5_user_property_copy fail\r\n");
                mqtt_free(client->mqtt5_config->disconnect_property_info.user_property);
                client->mqtt5_config->disconnect_property_info.user_property = NULL;
                MQTT_API_UNLOCK(client);
                return -1;
            }
        }
    }

    MQTT_API_UNLOCK(client);
    return 0;
}

int trs_mqtt5_client_set_connect_property(trs_mqtt5_client_handle_t client, const trs_mqtt5_connection_property_config_t *connect_property)
{
    if (!client) {
    	os_printf(LM_APP, LL_ERR, "Client was not initialized\r\n");
        return -1;
    }
    MQTT_API_LOCK(client);

    /* Check protocol version */
    if (client->connect_info.protocol_ver != MQTT_PROTOCOL_V_5) {
    	os_printf(LM_APP, LL_ERR, "MQTT protocol version is not v5\r\n");
        MQTT_API_UNLOCK(client);
        return -1;
    }
    if (connect_property) {
        if (connect_property->session_expiry_interval) {
            client->mqtt5_config->connect_property_info.session_expiry_interval = connect_property->session_expiry_interval;
        }
        if (connect_property->maximum_packet_size) {
            if (connect_property->maximum_packet_size > client->mqtt_state.in_buffer_length) {
            	os_printf(LM_APP, LL_DBG, "Connect maximum_packet_size property is over buffer_size(%d), Please first change it\r\n", client->mqtt_state.in_buffer_length);
                MQTT_API_UNLOCK(client);
                return -1;
            } else {
                client->mqtt5_config->connect_property_info.maximum_packet_size = connect_property->maximum_packet_size;
            }  
        } else {
            client->mqtt5_config->connect_property_info.maximum_packet_size = client->mqtt_state.in_buffer_length;
        }
        if (connect_property->receive_maximum) {
            client->mqtt5_config->connect_property_info.receive_maximum = connect_property->receive_maximum;
        }
        if (connect_property->topic_alias_maximum) {
            client->mqtt5_config->connect_property_info.topic_alias_maximum = connect_property->topic_alias_maximum;
            if (!client->mqtt5_config->peer_topic_alias) {
                client->mqtt5_config->peer_topic_alias = mqtt_calloc(1, sizeof(struct mqtt5_topic_alias_list_t));
                MEM_CHECK(client->mqtt5_config->peer_topic_alias, goto _mqtt_set_config_failed);
                STAILQ_INIT(client->mqtt5_config->peer_topic_alias);
            }
        }
        if (connect_property->request_resp_info) {
            client->mqtt5_config->connect_property_info.request_resp_info = connect_property->request_resp_info;
        }
        if (connect_property->request_problem_info) {
            client->mqtt5_config->connect_property_info.request_problem_info = connect_property->request_problem_info;
        }
        if (connect_property->user_property) {
            trs_mqtt5_client_delete_user_property(client->mqtt5_config->connect_property_info.user_property);
            client->mqtt5_config->connect_property_info.user_property = mqtt_calloc(1, sizeof(struct mqtt5_user_property_list_t));
            MEM_CHECK(client->mqtt5_config->connect_property_info.user_property, goto _mqtt_set_config_failed);
            STAILQ_INIT(client->mqtt5_config->connect_property_info.user_property);
            if (trs_mqtt5_user_property_copy(client->mqtt5_config->connect_property_info.user_property, connect_property->user_property) != 0) {
            	os_printf(LM_APP, LL_ERR, "trs_mqtt5_user_property_copy fail\r\n");
                goto _mqtt_set_config_failed;
            }
        }
        if (connect_property->payload_format_indicator) {
            client->mqtt5_config->will_property_info.payload_format_indicator = connect_property->payload_format_indicator;
        }
        if (connect_property->will_delay_interval) {
            client->mqtt5_config->will_property_info.will_delay_interval = connect_property->will_delay_interval;
        }
        if (connect_property->message_expiry_interval) {
            client->mqtt5_config->will_property_info.message_expiry_interval = connect_property->message_expiry_interval;
        }
        MEM_CHECK(trs_mqtt_set_if_config(connect_property->content_type, &client->mqtt5_config->will_property_info.content_type), goto _mqtt_set_config_failed);
        MEM_CHECK(trs_mqtt_set_if_config(connect_property->response_topic, &client->mqtt5_config->will_property_info.response_topic), goto _mqtt_set_config_failed);
        if (connect_property->correlation_data && connect_property->correlation_data_len) {
            mqtt_free(client->mqtt5_config->will_property_info.correlation_data);
            client->mqtt5_config->will_property_info.correlation_data = mqtt_malloc(connect_property->correlation_data_len);
            MEM_CHECK(client->mqtt5_config->will_property_info.correlation_data, goto _mqtt_set_config_failed);
            memcpy(client->mqtt5_config->will_property_info.correlation_data, connect_property->correlation_data, connect_property->correlation_data_len);
            client->mqtt5_config->will_property_info.correlation_data_len = connect_property->correlation_data_len;
        }
        if (connect_property->will_user_property) {
            trs_mqtt5_client_delete_user_property(client->mqtt5_config->will_property_info.user_property);
            client->mqtt5_config->will_property_info.user_property = mqtt_calloc(1, sizeof(struct mqtt5_user_property_list_t));
            MEM_CHECK(client->mqtt5_config->will_property_info.user_property, goto _mqtt_set_config_failed);
            STAILQ_INIT(client->mqtt5_config->will_property_info.user_property);
            if (trs_mqtt5_user_property_copy(client->mqtt5_config->will_property_info.user_property, connect_property->will_user_property) != 0) {
            	os_printf(LM_APP, LL_ERR, "trs_mqtt5_user_property_copy fail\r\n");
                goto _mqtt_set_config_failed;
            }
        }
    }
    MQTT_API_UNLOCK(client);
    return 0;
_mqtt_set_config_failed:
    trs_mqtt_destroy_config(client);
    MQTT_API_UNLOCK(client);
    return -1;
}

int trs_mqtt5_client_set_user_property(mqtt5_user_property_handle_t *user_property, trs_mqtt5_user_property_item_t item[], uint8_t item_num)
{
    if (!item_num || !item) {
    	os_printf(LM_APP, LL_ERR, "Input value is NULL\r\n");
        return -1;
    }

    if (!*user_property) {
        *user_property = mqtt_calloc(1, sizeof(struct mqtt5_user_property_list_t));
        MEM_CHECK(*user_property, return -1);
        STAILQ_INIT(*user_property);
    }

    for (int i = 0; i < item_num; i ++) {
        if (item[i].key && item[i].value) {
            mqtt5_user_property_item_t user_property_item = mqtt_calloc(1, sizeof(mqtt5_user_property_t));
            MEM_CHECK(user_property_item, goto err);
            size_t key_len = strlen(item[i].key);
            size_t value_len = strlen(item[i].value);

            user_property_item->key = mqtt_calloc(1, key_len + 1);
            MEM_CHECK(user_property_item->key, {
                mqtt_free(user_property_item);
                goto err;
            });
            memcpy(user_property_item->key, item[i].key, key_len);
            user_property_item->key[key_len] = '\0';

            user_property_item->value = mqtt_calloc(1, value_len + 1);
            MEM_CHECK(user_property_item->value, {
                mqtt_free(user_property_item->key);
                mqtt_free(user_property_item);
                goto err;
            });
            memcpy(user_property_item->value, item[i].value, value_len);
            user_property_item->value[value_len] = '\0';

            STAILQ_INSERT_TAIL(*user_property, user_property_item, next);
        }
    }
    return 0;
err:
    trs_mqtt5_client_delete_user_property(*user_property);
    *user_property = NULL;
    return -1;
}

int trs_mqtt5_client_get_user_property(mqtt5_user_property_handle_t user_property, trs_mqtt5_user_property_item_t *item, uint8_t *item_num)
{
    int i = 0, j = 0;
    if (user_property && item && *item_num) {
        mqtt5_user_property_item_t user_property_item;
        uint8_t num = *item_num;
        STAILQ_FOREACH(user_property_item, user_property, next) {
            if (i < num) {
                size_t item_key_len = strlen(user_property_item->key);
                size_t item_value_len = strlen(user_property_item->value);
                char *key = mqtt_calloc(1, item_key_len + 1);
                MEM_CHECK(key, goto err);
                memcpy(key, user_property_item->key, item_key_len);
                key[item_key_len] = '\0';
                char *value = mqtt_calloc(1, item_value_len + 1);
                MEM_CHECK(value, {
                    mqtt_free(key);
                    goto err;
                });
                memcpy(value, user_property_item->value, item_value_len);
                value[item_value_len] = '\0';
                item[i].key = key;
                item[i].value = value;
                i ++;
            } else {
                break;
            }
        }
        *item_num = i;
        return 0;
    } else {
    	os_printf(LM_APP, LL_ERR, "Input value is NULL or item_num is 0\r\n");
        return -1;
    }
err:
    for (j = 0; j < i; j ++) {
        if (item[j].key) {
            mqtt_free((char *)item[j].key);
        }
        if (item[j].value) {
            mqtt_free((char *)item[j].value);
        }
    }
    return -1;
}

uint8_t trs_mqtt5_client_get_user_property_count(mqtt5_user_property_handle_t user_property)
{
    uint8_t count = 0;
    if (user_property) {
        mqtt5_user_property_item_t item;
        STAILQ_FOREACH(item, user_property, next) {
            count ++;
        }
    }
    return count;
}

void trs_mqtt5_client_delete_user_property(mqtt5_user_property_handle_t user_property)
{
    if (user_property) {
        mqtt5_user_property_item_t item, tmp;
        STAILQ_FOREACH_SAFE(item, user_property, next, tmp) {
            STAILQ_REMOVE(user_property, item, mqtt5_user_property, next);
            mqtt_free(item->key);
            mqtt_free(item->value);
            mqtt_free(item);
        }
    }
    mqtt_free(user_property);
}
#endif
