#include <stdio.h>
#include "errno.h"

#include "system_def.h"
#include "FreeRTOS.h"
#include "projdefs.h"
#include "portmacro.h"
#include "oshal.h"

#include "mqtt_client.h"
#include "mqtt_client_priv.h"

/* using uri parser */
#include "url_parser.h"

#define MQTT_QUEUE_LENGTH    12
#define MQTT_REV_MSG_TIMEOUT portMAX_DELAY
#define MQTT_REV_DATA_TIMEOUT 1000

#define MQTT_OVER_TCP_ORIGIN_SCHEME "tcp"
#define MQTT_OVER_SSL_ORIGIN_SCHEME "ssl"
#define MQTT_OVER_TLS_ORIGIN_SCHEME "tls"
#define MQTT_OVER_TCP_SCHEME "mqtt"
#define MQTT_OVER_SSL_SCHEME "mqtts"
#define MQTT_OVER_WS_SCHEME  "ws"
#define MQTT_OVER_WSS_SCHEME "wss"
#define MAX_ID_STRING (32)

uint ping_resp = 0;
uint ping_num = 0;

const static int RECONNECT_BIT = (1 << 0);
const static int DISCONNECT_BIT = (1 << 1);

static int trs_mqtt_dispatch_event(trs_mqtt_client_handle_t client);
static int trs_mqtt_dispatch_event_with_msgid(trs_mqtt_client_handle_t client);
static int trs_mqtt_connect(trs_mqtt_client_handle_t client, int timeout_ms);

static void trs_mqtt_abort_connection(trs_mqtt_client_handle_t client);
static int trs_mqtt_client_ping(trs_mqtt_client_handle_t client);
static char *create_string(const char *ptr, int len);
static int mqtt_message_receive(trs_mqtt_client_handle_t client, int read_poll_timeout_ms);
static void trs_mqtt_client_dispatch_transport_error(trs_mqtt_client_handle_t client);
static int send_disconnect_msg(trs_mqtt_client_handle_t client);

int vasprintf(char **ptr, const char *format, va_list ap)
{
    va_list ap_copy;
    /* Make sure it is determinate, despite manuals indicating otherwise */
    *ptr = 0;
    va_copy(ap_copy, ap);
    int count = vsnprintf(NULL, 0, format, ap);
    if (count >= 0) {
        char *buffer = mqtt_malloc(count + 1);
        if (buffer != NULL) {
            count = vsnprintf(buffer, count + 1, format, ap_copy);
            if (count < 0) {
                if (buffer) {
                    mqtt_free(buffer);
                    buffer = NULL;
                }
            } else {
                *ptr = buffer;
            }
        }
    }
    va_end(ap_copy);
    return count;
}

int mqtt_asprintf(char **strp, const char *fmt, ...)
{
    int result;

    va_list ap;
    va_start(ap, fmt);
    result = vasprintf(strp, fmt, ap);
    va_end(ap);

    return result;
}

static int trs_mqtt_handle_transport_read_error(int err, trs_mqtt_client_handle_t client)
{
    if (err == ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN) {
    	os_printf(LM_APP, LL_DBG, "%s: transport_read(): EOF\r\n", __func__);
        return -1;
    }

    if (err == ERR_TCP_TRANSPORT_CONNECTION_TIMEOUT) {
    	os_printf(LM_APP, LL_DBG, "%s: transport_read(): call timed out before data was ready!\r\n", __func__);
        return 0;
    }

    os_printf(LM_APP, LL_ERR, "%s: transport_read() error: errno=%d\r\n", __func__, errno);
    trs_mqtt_client_dispatch_transport_error(client);
    return -1;
}

#if CONFIG_MQTT_SSL
enum trs_mqtt_ssl_cert_key_api {
    MQTT_SSL_DATA_API_CA_CERT,
    MQTT_SSL_DATA_API_CLIENT_CERT,
    MQTT_SSL_DATA_API_CLIENT_KEY,
    MQTT_SSL_DATA_API_MAX,
};

static int trs_mqtt_set_cert_key_data(transport_handle_t ssl, enum trs_mqtt_ssl_cert_key_api what, const char *cert_key_data, int cert_key_len)
{
    char *data = (char *)cert_key_data;
    int ssl_transport_api_id = what;
    int len = cert_key_len;

    if (!data) {
        return 0;
    }

    if (len == 0) {
        // if length not specified, expect 0-terminated PEM string
        // and the original transport_api_id (by convention after the last api_id in the enum)
        ssl_transport_api_id += MQTT_SSL_DATA_API_MAX;
        len = strlen(data);
    }
#ifndef MQTT_SUPPORTED_FEATURE_DER_CERTIFICATES
    else {
    	os_printf(LM_APP, LL_ERR, "Explicit cert-/key-len is not available\r\n");
        return -1;
    }
#endif

    // option to force the cert/key config to null (i.e. skip validation) when existing config updates
    if (0 == strcmp(data, "NULL")) {
        data = NULL;
        len = 0;
    }

    switch (ssl_transport_api_id) {
#ifdef MQTT_SUPPORTED_FEATURE_DER_CERTIFICATES
    case MQTT_SSL_DATA_API_CA_CERT:
        transport_ssl_set_cert_data_der(ssl, data, len);
        break;
    case MQTT_SSL_DATA_API_CLIENT_CERT:
        transport_ssl_set_client_cert_data_der(ssl, data, len);
        break;
    case MQTT_SSL_DATA_API_CLIENT_KEY:
        transport_ssl_set_client_key_data_der(ssl, data, len);
        break;
#endif
    case MQTT_SSL_DATA_API_CA_CERT + MQTT_SSL_DATA_API_MAX:
        transport_ssl_set_cert_data(ssl, data, len);
        break;
    case MQTT_SSL_DATA_API_CLIENT_CERT + MQTT_SSL_DATA_API_MAX:
        transport_ssl_set_client_cert_data(ssl, data, len);
        break;
    case MQTT_SSL_DATA_API_CLIENT_KEY + MQTT_SSL_DATA_API_MAX:
        transport_ssl_set_client_key_data(ssl, data, len);
        break;
    default:
        return -1;
    }
    return 0;
}

static int trs_mqtt_set_ssl_transport_properties(transport_list_handle_t transport_list, mqtt_config_storage_t *cfg)
{
    transport_handle_t ssl = transport_list_get_transport(transport_list, MQTT_OVER_SSL_SCHEME);

    if (cfg->use_global_ca_store == true) {
        //transport_ssl_enable_global_ca_store(ssl);
    } else if (cfg->crt_bundle_attach != NULL) {
#ifdef MQTT_SUPPORTED_FEATURE_CERTIFICATE_BUNDLE
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        trs_transport_ssl_crt_bundle_attach(ssl, cfg->crt_bundle_attach);
#else
        os_printf(LM_APP, LL_ERR, "Certificate bundle is not enabled for mbedTLS in menuconfig\r\n");
        goto trs_mqtt_set_transport_failed;
#endif /* CONFIG_MBEDTLS_CERTIFICATE_BUNDLE */
#else
        os_printf(LM_APP, LL_ERR, "Certificate bundle feature is not available\r\n");
        goto trs_mqtt_set_transport_failed;
#endif /* MQTT_SUPPORTED_FEATURE_CERTIFICATE_BUNDLE */
    } else {
        OK_CHECK(trs_mqtt_set_cert_key_data(ssl, MQTT_SSL_DATA_API_CA_CERT, cfg->cacert_buf, cfg->cacert_bytes),
                     goto trs_mqtt_set_transport_failed);

    }
    if (cfg->psk_hint_key) {
#if defined(MQTT_SUPPORTED_FEATURE_PSK_AUTHENTICATION) && CONFIG_MQTT_SSL
#ifdef CONFIG_TRS_TLS_PSK_VERIFICATION
        transport_ssl_set_psk_key_hint(ssl, cfg->psk_hint_key);
#else
        os_printf(LM_APP, LL_ERR, "PSK authentication configured but not enabled in menuconfig: Please enable TRS_TLS_PSK_VERIFICATION option\r\n");
        goto trs_mqtt_set_transport_failed;
#endif
#else
        os_printf(LM_APP, LL_ERR, "PSK authentication is not available\r\n");
        goto trs_mqtt_set_transport_failed;
#endif
    }

    if (cfg->alpn_protos) {
#if defined(MQTT_SUPPORTED_FEATURE_ALPN) && CONFIG_MQTT_SSL
#if defined(CONFIG_MBEDTLS_SSL_ALPN) || defined(CONFIG_WOLFSSL_HAVE_ALPN)
        transport_ssl_set_alpn_protocol(ssl, (const char **)cfg->alpn_protos);
#else
        os_printf(LM_APP, LL_ERR, "APLN configured but not enabled in menuconfig: Please enable MBEDTLS_SSL_ALPN or WOLFSSL_HAVE_ALPN option\r\n");
        goto trs_mqtt_set_transport_failed;
#endif
#else
        os_printf(LM_APP, LL_ERR, "APLN is not available\r\n");
        goto trs_mqtt_set_transport_failed;
#endif
    }


    if (cfg->skip_cert_common_name_check) {
#if defined(MQTT_SUPPORTED_FEATURE_SKIP_CRT_CMN_NAME_CHECK) && CONFIG_MQTT_SSL
        transport_ssl_skip_common_name_check(ssl);
#else
        os_printf(LM_APP, LL_ERR, "Skip certificate common name check is not available\r\n");
        goto trs_mqtt_set_transport_failed;
#endif
    }

    if (cfg->common_name) {
#if defined(MQTT_SUPPORTED_FEATURE_CRT_CMN_NAME) && CONFIG_MQTT_SSL
        transport_ssl_set_common_name(ssl, cfg->common_name);
#else
        os_printf(LM_APP, LL_ERR, "Setting expected certificate common name is not available\r\n");
        goto trs_mqtt_set_transport_failed;
#endif
    }

    if (cfg->use_secure_element) {
#ifdef MQTT_SUPPORTED_FEATURE_SECURE_ELEMENT
#ifdef CONFIG_TRS_TLS_USE_SECURE_ELEMENT
        transport_ssl_use_secure_element(ssl);
#else
        os_printf(LM_APP, LL_ERR, "Secure element not enabled for trs-tls in menuconfig\r\n");
        goto trs_mqtt_set_transport_failed;
#endif /* CONFIG_TRS_TLS_USE_SECURE_ELEMENT */
#else
        os_printf(LM_APP, LL_ERR, "Secure element feature is not available\r\n");
        goto trs_mqtt_set_transport_failed;
#endif /* MQTT_SUPPORTED_FEATURE_SECURE_ELEMENT */
    }

    if (cfg->ds_data != NULL) {
#ifdef MQTT_SUPPORTED_FEATURE_DIGITAL_SIGNATURE
#ifdef CONFIG_TRS_TLS_USE_DS_PERIPHERAL
        transport_ssl_set_ds_data(ssl, cfg->ds_data);
#else
        os_printf(LM_APP, LL_ERR, "Digital Signature not enabled for trs-tls in menuconfig\r\n");
        goto trs_mqtt_set_transport_failed;
#endif /* CONFIG_TRS_TLS_USE_DS_PERIPHERAL */
#else
        os_printf(LM_APP, LL_ERR, "Digital Signature feature is not available\r\n");
        goto trs_mqtt_set_transport_failed;
#endif
    }
    OK_CHECK(trs_mqtt_set_cert_key_data(ssl, MQTT_SSL_DATA_API_CLIENT_CERT, cfg->clientcert_buf, cfg->clientcert_bytes),
                 goto trs_mqtt_set_transport_failed);
    OK_CHECK(trs_mqtt_set_cert_key_data(ssl, MQTT_SSL_DATA_API_CLIENT_KEY, cfg->clientkey_buf, cfg->clientkey_bytes),
                 goto trs_mqtt_set_transport_failed);

    if (cfg->clientkey_password && cfg->clientkey_password_len) {
#if defined(MQTT_SUPPORTED_FEATURE_CLIENT_KEY_PASSWORD) && CONFIG_MQTT_SSL
        transport_ssl_set_client_key_password(ssl,
                cfg->clientkey_password,
                cfg->clientkey_password_len);
#else
        os_printf(LM_APP, LL_ERR, "Password protected keys are not available\r\n");
        goto trs_mqtt_set_transport_failed;
#endif
    }


    return 0;

trs_mqtt_set_transport_failed:
    return -1;
}
#endif // CONFIG_MQTT_SSL

/* Checks if the user supplied config values are internally consistent */
static int trs_mqtt_check_cfg_conflict(const mqtt_config_storage_t *cfg, const trs_mqtt_client_config_t *user_cfg)
{
    if(cfg == NULL || user_cfg == NULL) {
        os_printf(LM_APP, LL_ERR, "Invalid configuration\r\n");
        return -1;
    }
    int ret = 0;

    bool ssl_cfg_enabled = cfg->use_global_ca_store || cfg->cacert_buf || cfg->clientcert_buf || cfg->psk_hint_key || cfg->alpn_protos;
    bool is_ssl_scheme = false;
    if (cfg->scheme) {
        is_ssl_scheme = (strcasecmp(cfg->scheme, MQTT_OVER_SSL_SCHEME) == 0) || (strcasecmp(cfg->scheme, MQTT_OVER_WSS_SCHEME) == 0) \
                     || (strcasecmp(cfg->scheme, MQTT_OVER_SSL_ORIGIN_SCHEME) == 0) || (strcasecmp(cfg->scheme, MQTT_OVER_TLS_ORIGIN_SCHEME) == 0);
    }

    if (!is_ssl_scheme && ssl_cfg_enabled) {
        if (cfg->uri) {
        	os_printf(LM_APP, LL_DBG, "SSL related configs set, but the URI scheme specifies a non-SSL scheme, scheme = %s\r\n", cfg->scheme);
        } else {
        	os_printf(LM_APP, LL_DBG, "SSL related configs set, but the transport protocol is a non-SSL scheme, transport = %d\r\n", user_cfg->transport);
        }
        ret = -1;
    }

    if (cfg->uri && user_cfg->transport) {
    	os_printf(LM_APP, LL_DBG, "Transport config set, but overridden by scheme from URI: transport = %d, uri scheme = %s\r\n", user_cfg->transport, cfg->scheme);
        ret = -1;
    }

    return ret;
}

bool trs_mqtt_set_if_config(char const *const new_config, char **old_config)
{
    if (new_config) {
        if (*old_config != NULL) {
            mqtt_free(*old_config);
        }
        *old_config = os_strdup(new_config);
        if (*old_config == NULL) {
            return false;
        }
    }
    return true;
}

static int trs_mqtt_client_create_transport(trs_mqtt_client_handle_t client)
{
    int ret = 0;

    if (client->transport_list) {
        transport_list_destroy(client->transport_list);
        client->transport_list = NULL;
    }
    if (client->config->scheme) {
        client->transport_list = transport_list_init();
        MEM_CHECK(client->transport_list, return -1);
        
        if ((strcasecmp(client->config->scheme, MQTT_OVER_TCP_SCHEME) == 0) \
        || (strcasecmp(client->config->scheme, MQTT_OVER_WS_SCHEME) == 0) \
        || (strcasecmp(client->config->scheme, MQTT_OVER_TCP_ORIGIN_SCHEME) == 0) ) {
            transport_handle_t tcp = transport_tcp_init();
            MEM_CHECK(tcp, return -1);
            transport_set_default_port(tcp, MQTT_TCP_DEFAULT_PORT);
            transport_list_add(client->transport_list, tcp, MQTT_OVER_TCP_SCHEME);
            if (strcasecmp(client->config->scheme, MQTT_OVER_WS_SCHEME) == 0) {
#if MQTT_ENABLE_WS
                transport_handle_t ws = transport_ws_init(tcp);
                MEM_CHECK(ws, return -1);
                transport_set_default_port(ws, MQTT_WS_DEFAULT_PORT);
                if (client->config->path) {
                    transport_ws_set_path(ws, client->config->path);
                }
#ifdef MQTT_SUPPORTED_FEATURE_WS_SUBPROTOCOL
                transport_ws_set_subprotocol(ws, MQTT_OVER_TCP_SCHEME);
#endif
                transport_list_add(client->transport_list, ws, MQTT_OVER_WS_SCHEME);
#else
                os_printf(LM_APP, LL_ERR, "Please enable MQTT_ENABLE_WS to use %s\r\n", client->config->scheme);
                ret = -1;
#endif
            } else if(strcasecmp(client->config->scheme, MQTT_OVER_TCP_ORIGIN_SCHEME) == 0) {
                mqtt_free(client->config->scheme);
                client->config->scheme = strdup(MQTT_OVER_TCP_SCHEME);
            }
        } else if ((strcasecmp(client->config->scheme, MQTT_OVER_SSL_SCHEME) == 0) || (strcasecmp(client->config->scheme, MQTT_OVER_WSS_SCHEME) == 0) \
                || (strcasecmp(client->config->scheme, MQTT_OVER_SSL_ORIGIN_SCHEME) == 0) || (strcasecmp(client->config->scheme, MQTT_OVER_TLS_ORIGIN_SCHEME) == 0)) {
#if CONFIG_MQTT_SSL
            transport_handle_t ssl = transport_ssl_init();
            MEM_CHECK(ssl, return -1);

            transport_set_default_port(ssl, MQTT_SSL_DEFAULT_PORT);
            transport_list_add(client->transport_list, ssl, MQTT_OVER_SSL_SCHEME);
            if (strcasecmp(client->config->scheme, MQTT_OVER_WSS_SCHEME) == 0) {
#if MQTT_ENABLE_WS
                transport_handle_t wss = transport_ws_init(ssl);
                MEM_CHECK(wss, return -1);
                transport_set_default_port(wss, MQTT_WSS_DEFAULT_PORT);
                if (client->config->path) {
                    transport_ws_set_path(wss, client->config->path);
                }
#ifdef MQTT_SUPPORTED_FEATURE_WS_SUBPROTOCOL
                transport_ws_set_subprotocol(wss, MQTT_OVER_TCP_SCHEME);
#endif
                transport_list_add(client->transport_list, wss, MQTT_OVER_WSS_SCHEME);
#else
                os_printf(LM_APP, LL_ERR, "Please enable MQTT_ENABLE_WS to use %s\r\n", client->config->scheme);
                ret = -1;
#endif
            } else if ((strcasecmp(client->config->scheme, MQTT_OVER_SSL_ORIGIN_SCHEME) == 0) || (strcasecmp(client->config->scheme, MQTT_OVER_TLS_ORIGIN_SCHEME) == 0)) {
                mqtt_free(client->config->scheme);
                client->config->scheme = strdup(MQTT_OVER_SSL_SCHEME);
            }
#else
            os_printf(LM_APP, LL_ERR, "Please enable CONFIG_MQTT_SSL to use %s\r\n", client->config->scheme);
            ret = -1;
#endif
        } else {
        	os_printf(LM_APP, LL_ERR, "Not support this mqtt scheme %s\r\n", client->config->scheme);
            ret = -1;
        }
    } else {
    	os_printf(LM_APP, LL_ERR, "No scheme found\r\n");
        ret = -1;
    }
    return ret;
}

static int trs_mqtt_set_config(trs_mqtt_client_handle_t client, const trs_mqtt_client_config_t *config)
{
    if (!client) {
    	os_printf(LM_APP, LL_ERR, "Client was not initialized\r\n");
        return -1;
    }
    MQTT_API_LOCK(client);
    //Copy user configurations to client context
    int err = 0;

    if(!client->config) {
        client->config = mqtt_calloc(1, sizeof(mqtt_config_storage_t));
        MEM_CHECK(client->config, {
            MQTT_API_UNLOCK(client);
            return -1;
        });
    }
    
    client->config->message_retransmit_timeout = config->message_retransmit_timeout;
    if (config->message_retransmit_timeout <= 0) {
        client->config->message_retransmit_timeout = 1000;
    }

    client->config->task_prio = config->task_prio;
    if (client->config->task_prio <= 0) {
        client->config->task_prio = MQTT_TASK_PRIORITY;
    }

    client->config->task_stack = config->task_stack;
    if (client->config->task_stack <= 0) {
        client->config->task_stack = MQTT_TASK_STACK;
    }
    
    if (config->port) {
        client->config->port = config->port;
    }

    err = -1;
    MEM_CHECK(trs_mqtt_set_if_config(config->host, &client->config->host), goto _mqtt_set_config_failed);
    MEM_CHECK(trs_mqtt_set_if_config(config->path, &client->config->path), goto _mqtt_set_config_failed);
    MEM_CHECK(trs_mqtt_set_if_config(config->username, &client->connect_info.username), goto _mqtt_set_config_failed);
	MEM_CHECK(trs_mqtt_set_if_config(config->password, &client->connect_info.password), goto _mqtt_set_config_failed);

    if (!config->set_null_client_id) {
        if (config->client_id) {
            MEM_CHECK(trs_mqtt_set_if_config(config->client_id, &client->connect_info.client_id), goto _mqtt_set_config_failed);
        } else if (client->connect_info.client_id == NULL) {
            client->connect_info.client_id = platform_create_id_string();
        }
        MEM_CHECK(client->connect_info.client_id, goto _mqtt_set_config_failed);
        os_printf(LM_APP, LL_INFO, "MQTT:client_id=%s\r\n", client->connect_info.client_id);
    }

	MEM_CHECK(trs_mqtt_set_if_config(config->uri, &client->config->uri), goto _mqtt_set_config_failed);
    MEM_CHECK(trs_mqtt_set_if_config(config->lwt_topic, &client->connect_info.will_topic), goto _mqtt_set_config_failed);

    if (config->lwt_msg_len && config->lwt_msg) {
        mqtt_free(client->connect_info.will_message);
        client->connect_info.will_message = mqtt_malloc(config->lwt_msg_len);

        MEM_CHECK(client->connect_info.will_message, goto _mqtt_set_config_failed);
        memcpy(client->connect_info.will_message, config->lwt_msg, config->lwt_msg_len);
        client->connect_info.will_length = config->lwt_msg_len;
    } else if (config->lwt_msg) {
        mqtt_free(client->connect_info.will_message);
        client->connect_info.will_message = os_strdup(config->lwt_msg);

        MEM_CHECK(client->connect_info.will_message, goto _mqtt_set_config_failed);
        client->connect_info.will_length = strlen(config->lwt_msg);
    }
    if (config->lwt_qos) {
        client->connect_info.will_qos = config->lwt_qos;
    }
    if (config->lwt_retain) {
        client->connect_info.will_retain = config->lwt_retain;
    }
    client->connect_info.clean_session = 1;
    if (config->clean_session == 0) {
        client->connect_info.clean_session = 0;
        if (config->set_null_client_id) {
        	os_printf(LM_APP, LL_ERR, "Clean Session flag must be true if client has a null id\r\n");
        }
    }
    if (config->keepalive) {
        client->connect_info.keepalive = config->keepalive;
    }
    if (client->connect_info.keepalive == 0) {
        client->connect_info.keepalive = MQTT_KEEPALIVE_TICK;
    }
    if (config->disable_keepalive) {
        // internal `keepalive` value (in connect_info) is in line with 3.1.2.10 Keep Alive from mqtt spec:
        //      * keepalive=0: Keep alive mechanism disabled (server not to disconnect the client on its inactivity)
        //      * period in seconds to send a Control packet if inactive
        client->connect_info.keepalive = 0;
    }

    if (config->protocol_ver) {
        client->connect_info.protocol_ver = config->protocol_ver;
    }
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_UNDEFINED) {
#ifdef MQTT_PROTOCOL_311
        client->connect_info.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
#else
        client->connect_info.protocol_ver = MQTT_PROTOCOL_V_3_1;
#endif
    } else if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifndef CONFIG_MQTT_PROTOCOL_5
        os_printf(LM_APP, LL_ERR, "Please first enable CONFIG_MQTT_PROTOCOL_5 feature in menuconfig\r\n");
        goto _mqtt_set_config_failed;
#endif
    }

    client->config->network_timeout_ms = config->timeout_ms;
    if (client->config->network_timeout_ms <= 0) {
        client->config->network_timeout_ms = MQTT_NETWORK_TIMEOUT_MS;
    }

    client->config->event_handle = config->event_handle;
    client->config->pub_handle = config->pub_handle;
	if (config->refresh_connection_after_ms) {
        client->config->refresh_connection_after_ms = config->refresh_connection_after_ms;
    }

    //client->config->auto_reconnect = true;
    //if (config->disable_auto_reconnect == client->config->auto_reconnect) {
        //client->config->auto_reconnect = !config->disable_auto_reconnect;
    //}

    client->config->auto_reconnect = true;
    if (config->disable_auto_reconnect == 1) {
        client->config->auto_reconnect = false;
    }

    if (config->reconnect_timeout_ms) {
        client->config->reconnect_timeout_ms = config->reconnect_timeout_ms;
    } else {
        client->config->reconnect_timeout_ms = MQTT_RECON_DEFAULT_MS;
    }

	if (config->alpn_protos) {
        for (int i = 0; i < client->config->num_alpn_protos; i++) {
            mqtt_free(client->config->alpn_protos[i]);
        }

        if (client->config->alpn_protos != NULL) {
            mqtt_free(client->config->alpn_protos);
        }
        client->config->num_alpn_protos = 0;
        
        const char **p;

        for (p = config->alpn_protos; *p != NULL; p++ ) {
            client->config->num_alpn_protos++;
        }
        // mbedTLS expects the list to be null-terminated
        client->config->alpn_protos = mqtt_calloc(client->config->num_alpn_protos + 1, sizeof(config->alpn_protos));
        MEM_CHECK(client->config->alpn_protos, goto _mqtt_set_config_failed);

        for (int i = 0; i < client->config->num_alpn_protos; i++) {
            client->config->alpn_protos[i] = os_strdup(config->alpn_protos[i]);
            MEM_CHECK(client->config->alpn_protos[i], goto _mqtt_set_config_failed);
        }
    }

    // configure ssl related parameters
    client->config->use_global_ca_store = config->use_global_ca_store;
    client->config->cacert_buf = config->cert_pem;
    client->config->cacert_bytes = config->cert_pem_len;
    client->config->psk_hint_key = config->psk_hint_key;
    client->config->crt_bundle_attach = config->crt_bundle_attach;
    client->config->clientcert_buf = config->client_cert_pem;
    client->config->clientcert_bytes = config->client_cert_pem_len;
    client->config->clientkey_buf = config->client_key_pem;
    client->config->clientkey_bytes = config->client_key_pem_len;
    client->config->skip_cert_common_name_check = config->skip_cert_common_name_check;
    client->config->common_name = config->common_name;
    client->config->use_secure_element = config->use_secure_element;
    client->config->ds_data = config->ds_data;
//    client->config->event_handle       = config->event_handle;

    if (config->client_key_password && config->client_key_password_len) {
        client->config->clientkey_password_len = config->client_key_password_len;
        client->config->clientkey_password = mqtt_malloc(client->config->clientkey_password_len);
        MEM_CHECK(client->config->clientkey_password, goto _mqtt_set_config_failed);
        memcpy(client->config->clientkey_password, config->client_key_password, client->config->clientkey_password_len);
    }

    if (config->transport) {
        mqtt_free(client->config->scheme);
        client->config->scheme = NULL;
        if (config->transport == MQTT_TRANSPORT_OVER_TCP) {
            client->config->scheme = create_string(MQTT_OVER_TCP_SCHEME, strlen(MQTT_OVER_TCP_SCHEME));
            MEM_CHECK(client->config->scheme, goto _mqtt_set_config_failed);
        }
#if MQTT_ENABLE_WS
        else if (config->transport == MQTT_TRANSPORT_OVER_WS) {
            client->config->scheme = create_string(MQTT_OVER_WS_SCHEME, strlen(MQTT_OVER_WS_SCHEME));
            MEM_CHECK(client->config->scheme, goto _mqtt_set_config_failed);
        }
#endif
#if CONFIG_MQTT_SSL
        else if (config->transport == MQTT_TRANSPORT_OVER_SSL) {
            client->config->scheme = create_string(MQTT_OVER_SSL_SCHEME, strlen(MQTT_OVER_SSL_SCHEME));
            MEM_CHECK(client->config->scheme, goto _mqtt_set_config_failed);
        }
#endif
#if MQTT_ENABLE_WSS
        else if (config->transport == MQTT_TRANSPORT_OVER_WSS) {
            client->config->scheme = create_string(MQTT_OVER_WSS_SCHEME, strlen(MQTT_OVER_WSS_SCHEME));
            MEM_CHECK(client->config->scheme, goto _mqtt_set_config_failed);
        }
#endif
    }

    // Set uri at the end of config to override separately configured uri elements
    if (config->uri) {
        if (trs_mqtt_client_set_uri(client, client->config->uri) != 0) {
            err = -1;
            goto _mqtt_set_config_failed;
        }
    }
    int config_has_conflict = trs_mqtt_check_cfg_conflict(client->config, config);

    MQTT_API_UNLOCK(client);

    return config_has_conflict;
_mqtt_set_config_failed:
    trs_mqtt_destroy_config(client);
    MQTT_API_UNLOCK(client);
    return err;
}

void trs_mqtt_destroy_config(trs_mqtt_client_handle_t client)
{
    mqtt_config_storage_t *cfg = client->config;
    if (cfg == NULL) {
        return;
    }
    if (cfg->host != NULL) {
        mqtt_free(cfg->host);
        cfg->host = NULL;
    }
    if (cfg->uri != NULL) {
        mqtt_free(cfg->uri);
        cfg->uri = NULL;
    }
    if (cfg->path != NULL) {
        mqtt_free(cfg->path);
        cfg->path = NULL;
    }
	if (cfg->scheme != NULL) {
        mqtt_free(cfg->scheme);
        cfg->scheme = NULL;
    }
	
	for (int i = 0; i < cfg->num_alpn_protos; i++) {
        mqtt_free(cfg->alpn_protos[i]);
    }

    if (client->config->alpn_protos != NULL) {
	    mqtt_free(client->config->alpn_protos);
        client->config->alpn_protos = NULL;
    }

    if (client->config->clientkey_password != NULL) {
	    mqtt_free(client->config->clientkey_password);
        client->config->clientkey_password = NULL;
    }
    if (client->connect_info.will_topic != NULL) {
        mqtt_free(client->connect_info.will_topic);
        client->connect_info.will_topic = NULL;
    }
    if (client->connect_info.will_message != NULL) {
        mqtt_free(client->connect_info.will_message);
        client->connect_info.will_message = NULL;
    }
    if (client->connect_info.client_id != NULL) {
        mqtt_free(client->connect_info.client_id);
        client->connect_info.client_id = NULL;
    }
    if (client->connect_info.username != NULL) {
        mqtt_free(client->connect_info.username);
        client->connect_info.username = NULL;
    }
    if (client->connect_info.password != NULL) {
        mqtt_free(client->connect_info.password);
        client->connect_info.password = NULL;
    }
#ifdef CONFIG_MQTT_PROTOCOL_5
    trs_mqtt5_client_destory(client);
#endif
    memset(&client->connect_info, 0, sizeof(mqtt_connect_info_t));
    memset(cfg, 0, sizeof(mqtt_config_storage_t));
    if (client->config != NULL) {
        mqtt_free(client->config);
        client->config = NULL;
    }
}

static inline bool has_timed_out(uint64_t last_tick, uint64_t timeout)
{
    uint64_t next = last_tick + timeout;
    return (int64_t)(next - platform_tick_get_ms()) <= 0;
}

static int process_keepalive(trs_mqtt_client_handle_t client)
{
    if (client->connect_info.keepalive > 0) {
        const uint64_t keepalive_ms = client->connect_info.keepalive * 1000;

        if (client->wait_for_ping_resp == true ) {
            if (has_timed_out(client->keepalive_tick, keepalive_ms)) {
            	os_printf(LM_APP, LL_ERR, "No PING_RESP, disconnected\r\n");
                trs_mqtt_abort_connection(client);
                client->wait_for_ping_resp = false;
                return -1;
            }
            return 0;
        }

        if (has_timed_out(client->keepalive_tick, keepalive_ms / 2)) {
            if (trs_mqtt_client_ping(client) == -1) {
            	os_printf(LM_APP, LL_ERR, "Can't send ping, disconnected\r\n");
                trs_mqtt_abort_connection(client);
                return -1;
            }
            //os_printf(LM_APP, LL_ERR, "send ping, succeed %d\r\n", ping_num++);
            client->wait_for_ping_resp = true;
            return 0;
        }
    }
    return 0;
}

static inline int trs_mqtt_write(trs_mqtt_client_handle_t client)
{
    int wlen = 0, widx = 0, len = client->mqtt_state.outbound_message->length;
    while (len > 0) {
        wlen = transport_write(client->transport,
                                   (char *)client->mqtt_state.outbound_message->data + widx,
                                   len,
                                   client->config->network_timeout_ms);
        if (wlen < 0) {
        	os_printf(LM_APP, LL_ERR, "Writing failed: errno=%d\r\n", errno);
            return -1;
        } else if (wlen == 0) {
        	os_printf(LM_APP, LL_ERR, "Writing didn't complete in specified timeout: errno=%d\r\n", errno);
            return -1;
        }
        widx += wlen;
        len -= wlen;
    }
    return 0;
}

static int trs_mqtt_connect(trs_mqtt_client_handle_t client, int timeout_ms)
{
    int read_len, connect_rsp_code = 0;
    client->wait_for_ping_resp = false;
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
    	os_printf(LM_APP, LL_INFO, "Connect MQTT_PROTOCOL_V_5\r\n");
        client->mqtt_state.outbound_message = mqtt5_msg_connect(&client->mqtt_state.mqtt_connection,
                                              &client->connect_info, &client->mqtt5_config->connect_property_info, &client->mqtt5_config->will_property_info);
#endif
    } else {
        client->mqtt_state.outbound_message = mqtt_msg_connect(&client->mqtt_state.mqtt_connection,
                                              &client->connect_info);
    }
    if (client->mqtt_state.outbound_message->length == 0) {
    	os_printf(LM_APP, LL_ERR, "Connect message cannot be created\r\n");
        return -1;
    }

    client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
        client->mqtt_state.pending_msg_id = mqtt5_get_id(client->mqtt_state.outbound_message->data,
                                            client->mqtt_state.outbound_message->length);
#endif
    } else {
        client->mqtt_state.pending_msg_id = mqtt_get_id(client->mqtt_state.outbound_message->data,
                                            client->mqtt_state.outbound_message->length);
    }
    os_printf(LM_APP, LL_DBG, "MQTT:Sending MQTT CONNECT message, type: %d, id: %04X\r\n",
             client->mqtt_state.pending_msg_type,
             client->mqtt_state.pending_msg_id);

    if (trs_mqtt_write(client) != 0) {
        return -1;
    }

    client->mqtt_state.in_buffer_read_len = 0;
    client->mqtt_state.message_length = 0;

    /* wait configured network timeout for broker connection response */
    uint64_t connack_recv_started = platform_tick_get_ms();
    do {
        read_len = mqtt_message_receive(client, client->config->network_timeout_ms);
    } while (read_len == 0 && platform_tick_get_ms() - connack_recv_started < client->config->network_timeout_ms);

    if (read_len <= 0) {
        os_printf(LM_APP, LL_ERR, "%s: mqtt_message_receive() returned %d\r\n", __func__, read_len);
        return -1;
    }

    if (mqtt_get_type(client->mqtt_state.in_buffer) != MQTT_MSG_TYPE_CONNACK) {
        os_printf(LM_APP, LL_DBG, "MQTT:Invalid MSG_TYPE response: %d, read_len: %d\r\n", mqtt_get_type(client->mqtt_state.in_buffer), read_len);
        return -1;
    }
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
        if (trs_mqtt5_parse_connack(client, &connect_rsp_code) == 0) {
            return 0;
        }
#endif
    } else {
        client->mqtt_state.in_buffer_read_len = 0;
        connect_rsp_code = mqtt_get_connect_return_code(client->mqtt_state.in_buffer);
        if (connect_rsp_code == MQTT_CONNECTION_ACCEPTED) {
        	os_printf(LM_APP, LL_DBG, "Connected\r\n");
            return 0;
        }
        switch (connect_rsp_code) {
        case MQTT_CONNECTION_REFUSE_PROTOCOL:
            os_printf(LM_APP, LL_INFO, "MQTT:Connection refused, bad protocol\r\n");
            break;
        case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE:
            os_printf(LM_APP, LL_INFO, "MQTT:Connection refused, server unavailable\r\n");
            break;
        case MQTT_CONNECTION_REFUSE_BAD_USERNAME:
            os_printf(LM_APP, LL_INFO, "MQTT:Connection refused, bad username or password\r\n");
            break;
        case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED:
            os_printf(LM_APP, LL_INFO, "MQTT:Connection refused, not authorized\r\n");
            break;
        default:
            os_printf(LM_APP, LL_INFO, "MQTT:Connection refused, Unknow reason\r\n");
            break;
        }
    }
    /* propagate event with connection refused error */
    client->event.event_id = MQTT_EVENT_ERROR;
    client->event.error_handle->error_type = MQTT_ERROR_TYPE_CONNECTION_REFUSED;
    client->event.error_handle->connect_return_code = connect_rsp_code;
    client->event.error_handle->trs_tls_stack_err = 0;
    client->event.error_handle->trs_tls_last_trs_err = 0;
    client->event.error_handle->trs_tls_cert_verify_flags = 0;
    trs_mqtt_dispatch_event_with_msgid(client);

    return -1;
}

static void trs_mqtt_abort_connection(trs_mqtt_client_handle_t client)
{
    MQTT_API_LOCK(client);
    transport_close(client->transport);
    client->wait_timeout_ms = client->config->reconnect_timeout_ms;
    client->reconnect_tick = platform_tick_get_ms();
    client->state = MQTT_STATE_WAIT_RECONNECT;
    os_printf(LM_APP, LL_INFO, "MQTT:Reconnect after %d ms\r\n", client->wait_timeout_ms);
    client->event.event_id = MQTT_EVENT_DISCONNECTED;
    client->wait_for_ping_resp = false;
    trs_mqtt_dispatch_event_with_msgid(client);
    MQTT_API_UNLOCK(client);
}

static bool create_client_data(trs_mqtt_client_handle_t client)
{
    client->event.error_handle = mqtt_calloc(1, sizeof(trs_mqtt_error_codes_t));
    MEM_CHECK(client->event.error_handle, return false);

    client->api_lock = xSemaphoreCreateRecursiveMutex();
    MEM_CHECK(client->api_lock, return false);

    return true;
}

static int trs_mqtt_client_destroy(trs_mqtt_client_handle_t client)
{
    if (client == NULL) {
        return -1;
    }

    if (client->api_lock) {
        trs_mqtt_client_stop(client);
    }

    trs_mqtt_destroy_config(client);
    if (client->transport_list) {
        transport_list_destroy(client->transport_list);
    }
    if (client->cfg_queue_handle) {
        os_queue_destroy(client->cfg_queue_handle);
        client->cfg_queue_handle = NULL;
    }

    if (client->outbox) {
        outbox_destroy(client->outbox);
    }

    if (client->mqtt_state.in_buffer) {
        mqtt_free(client->mqtt_state.in_buffer);
        client->mqtt_state.in_buffer = NULL;
    }

    if (client->mqtt_state.out_buffer) {
        mqtt_free(client->mqtt_state.out_buffer);
        client->mqtt_state.out_buffer = NULL;
    }

    if (client->status_bits) {
        vEventGroupDelete(client->status_bits);
    }

    if (client->api_lock) {
        vSemaphoreDelete(client->api_lock);
    }

    if (client->event.error_handle) {
        mqtt_free(client->event.error_handle);
        client->event.error_handle = NULL;
    }

    if (client) {
        mqtt_free(client);
        client = NULL;
    }
    return 0;
}

trs_mqtt_client_handle_t trs_mqtt_client_init(const trs_mqtt_client_config_t *config)
{
    trs_mqtt_client_handle_t client = mqtt_calloc(1, sizeof(struct trs_mqtt_client));
    MEM_CHECK(client, return NULL);
    if (!create_client_data(client)) {
        goto _mqtt_init_failed;
    }

    if (trs_mqtt_set_config(client, config) != 0) {
        goto _mqtt_init_failed;
    }
    client->keepalive_tick = platform_tick_get_ms();
    client->reconnect_tick = platform_tick_get_ms();
    client->refresh_connection_tick = platform_tick_get_ms();
    client->wait_for_ping_resp = false;
    int buffer_size = config->buffer_size;
    if (buffer_size <= 0) {
        buffer_size = MQTT_BUFFER_SIZE_BYTE;
    }
    // use separate value for output buffer size if configured
    int out_buffer_size = config->buffer_out_size > 0 ? config->buffer_out_size : buffer_size;
    client->mqtt_state.in_buffer = (uint8_t *)mqtt_malloc(buffer_size);
    MEM_CHECK(client->mqtt_state.in_buffer, goto _mqtt_init_failed);
    client->mqtt_state.in_buffer_length = buffer_size;
    client->mqtt_state.out_buffer = (uint8_t *)mqtt_malloc(out_buffer_size);
    MEM_CHECK(client->mqtt_state.out_buffer, goto _mqtt_init_failed);

    client->mqtt_state.out_buffer_length = out_buffer_size;
    client->outbox = outbox_init();
    MEM_CHECK(client->outbox, goto _mqtt_init_failed);
    client->status_bits = xEventGroupCreate();
    MEM_CHECK(client->status_bits, goto _mqtt_init_failed);
    mqtt_msg_init(&client->mqtt_state.mqtt_connection, client->mqtt_state.out_buffer,
                  client->mqtt_state.out_buffer_length);
#ifdef CONFIG_MQTT_PROTOCOL_5
    if (trs_mqtt5_create_default_config(client) != 0) {
        goto _mqtt_init_failed;
    }
#endif
    return client;
_mqtt_init_failed:
    trs_mqtt_client_destroy(client);
    return NULL;
}

static char *create_string(const char *ptr, int len)
{
    char *ret;
    if (len <= 0) {
        return NULL;
    }
    ret = mqtt_calloc(1, len + 1);
    MEM_CHECK(ret, return NULL);
    memcpy(ret, ptr, len);
    return ret;
}

int trs_mqtt_client_set_uri(trs_mqtt_client_handle_t client, const char *uri)
{
    struct http_parser_url puri;
    memset(&puri, 0, sizeof(struct http_parser_url));
    int parser_status = http_parser_parse_url(uri, strlen(uri), 0, &puri);
    if (parser_status != 0) {
        os_printf(LM_APP, LL_ERR, "MQTT:Error parse uri = %s\r\n", uri);
        return -1;
    }
	
    // This API could be also executed when client is active (need to protect config fields)
    MQTT_API_LOCK(client);
    // set uri overrides actual scheme, host, path if configured previously
    if(client->config->scheme){
        mqtt_free(client->config->scheme);
        client->config->scheme = NULL;
    }
    if(client->config->host){
        mqtt_free(client->config->host);
        client->config->host = NULL;
    }
    if(client->config->path){
        mqtt_free(client->config->path);
        client->config->path = NULL;
    }

    client->config->scheme = create_string(uri + puri.field_data[UF_SCHEMA].off, puri.field_data[UF_SCHEMA].len);
    client->config->host = create_string(uri + puri.field_data[UF_HOST].off, puri.field_data[UF_HOST].len);
    client->config->path = NULL;

    if (puri.field_data[UF_PATH].len || puri.field_data[UF_QUERY].len) {
        if (puri.field_data[UF_QUERY].len == 0) {
            mqtt_asprintf(&client->config->path, "%.*s", puri.field_data[UF_PATH].len, uri + puri.field_data[UF_PATH].off);
        } else if (puri.field_data[UF_PATH].len == 0)  {
            mqtt_asprintf(&client->config->path, "/?%.*s", puri.field_data[UF_QUERY].len, uri + puri.field_data[UF_QUERY].off);
        } else {
            mqtt_asprintf(&client->config->path, "%.*s?%.*s", puri.field_data[UF_PATH].len, uri + puri.field_data[UF_PATH].off,
                     puri.field_data[UF_QUERY].len, uri + puri.field_data[UF_QUERY].off);
        }
        MEM_CHECK(client->config->path, {
            MQTT_API_UNLOCK(client);
            return -1;
        });
    }

    if (puri.field_data[UF_PORT].len) {
        client->config->port = strtol((const char*)(uri + puri.field_data[UF_PORT].off), NULL, 10);
    }

    char *user_info = create_string(uri + puri.field_data[UF_USERINFO].off, puri.field_data[UF_USERINFO].len);
    if (user_info) {
        char *pass = strchr(user_info, ':');
        if (pass) {
            pass[0] = 0; //terminal username
            pass ++;
            client->connect_info.password = os_strdup(pass);
        }
        client->connect_info.username = os_strdup(user_info);

        mqtt_free(user_info);
    }
	
    MQTT_API_UNLOCK(client);
    return 0;
}

static int mqtt_write_data(trs_mqtt_client_handle_t client)
{
    if (trs_mqtt_write(client) != 0) {
        trs_mqtt_client_dispatch_transport_error(client);
        return -1;
    }
#ifdef CONFIG_MQTT_PROTOCOL_5
    trs_mqtt5_flow_control(client);
#endif
    return 0;
}

static int trs_mqtt_dispatch_event_with_msgid(trs_mqtt_client_handle_t client)
{
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
        client->event.msg_id = mqtt5_get_id(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_length);
#endif
    } else {
        client->event.msg_id = mqtt_get_id(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_length);
    }
    return trs_mqtt_dispatch_event(client);
}

static int trs_mqtt_dispatch_event(trs_mqtt_client_handle_t client)
{
    client->event.client = client;
    client->event.protocol_ver = client->connect_info.protocol_ver;
    int ret = -1;
    if (client->config->event_handle) {
        ret = client->config->event_handle(&client->event);
    } else {
        return -1;
    }
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
        trs_mqtt5_client_delete_user_property(client->event.property->user_property);
        client->event.property->user_property = NULL;
#endif
    }
    return ret;
}

static int deliver_publish(trs_mqtt_client_handle_t client)
{
    uint8_t *msg_buf = client->mqtt_state.in_buffer;
    size_t msg_read_len = client->mqtt_state.in_buffer_read_len;
    size_t msg_total_len = client->mqtt_state.message_length;
    size_t msg_topic_len = msg_read_len, msg_data_len = msg_read_len;
    size_t msg_data_offset = 0;
    char *msg_topic = NULL, *msg_data = NULL;

    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
        if (trs_mqtt5_get_publish_data(client, msg_buf, msg_read_len, &msg_topic, &msg_topic_len, &msg_data, &msg_data_len) != 0) {
        	os_printf(LM_APP, LL_ERR, "%s: trs_mqtt5_get_publish_data() failed\r\n", __func__);
            return -1;
        }
#endif
    } else {
        // get topic
        msg_topic = mqtt_get_publish_topic(msg_buf, &msg_topic_len);
        if (msg_topic == NULL) {
        	os_printf(LM_APP, LL_ERR, "%s: mqtt_get_publish_topic() failed\r\n", __func__);
            return -1;
        }
        os_printf(LM_APP, LL_DBG, "%s: msg_topic_len=%zu\r\n", __func__, msg_topic_len);

        // get payload
        msg_data = mqtt_get_publish_data(msg_buf, &msg_data_len);
        if (msg_data_len > 0 && msg_data == NULL) {
        	os_printf(LM_APP, LL_ERR, "%s: mqtt_get_publish_data() failed\r\n", __func__);
            return -1;
        }
    }
    // post data event
    client->event.retain = mqtt_get_retain(msg_buf);
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
        client->event.msg_id = mqtt5_get_id(msg_buf, msg_read_len);
#endif
    } else {
        client->event.msg_id = mqtt_get_id(msg_buf, msg_read_len);
    }
    client->event.qos = mqtt_get_qos(msg_buf);
    client->event.dup = mqtt_get_dup(msg_buf);
    client->event.total_data_len = msg_data_len + msg_total_len - msg_read_len;
post_data_event:
    os_printf(LM_APP, LL_DBG, "Get data len= %zu, topic len=%zu, total_data: %d offset: %zu\r\n", msg_data_len, msg_topic_len,
             client->event.total_data_len, msg_data_offset);
    client->event.event_id = MQTT_EVENT_DATA;
    client->event.data = msg_data_len > 0 ? msg_data : NULL;
    client->event.data_len = msg_data_len;
    client->event.current_data_offset = msg_data_offset;
    client->event.topic = msg_topic;
    client->event.topic_len = msg_topic_len;
    trs_mqtt_dispatch_event(client);

    if (msg_read_len < msg_total_len) {
        size_t buf_len = client->mqtt_state.in_buffer_length;

        msg_data = (char *)client->mqtt_state.in_buffer;
        msg_topic = NULL;
        msg_topic_len = 0;
        msg_data_offset += msg_data_len;
        int ret = transport_read(client->transport, (char *)client->mqtt_state.in_buffer,
                                     msg_total_len - msg_read_len > buf_len ? buf_len : msg_total_len - msg_read_len,
                                     client->config->network_timeout_ms);
        if (ret <= 0) {
            return trs_mqtt_handle_transport_read_error(ret, client) == 0 ? 0 : -1;
        }

        msg_data_len = ret;
        msg_read_len += msg_data_len;
        goto post_data_event;
    }
    return 0;
}

static int deliver_suback(trs_mqtt_client_handle_t client)
{
    uint8_t *msg_buf = client->mqtt_state.in_buffer;
    size_t msg_data_len = client->mqtt_state.in_buffer_read_len;
    char *msg_data = NULL;

    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
        msg_data = mqtt5_get_suback_data(msg_buf, &msg_data_len, &client->event.property->user_property);
#endif
    } else {
        msg_data = mqtt_get_suback_data(msg_buf, &msg_data_len);
    }
    if (msg_data_len <= 0) {
    	os_printf(LM_APP, LL_ERR, "Failed to acquire suback data\r\n");
        return -1;
    }
    client->event.error_handle->trs_tls_stack_err = 0;
    client->event.error_handle->trs_tls_last_trs_err = 0;
    client->event.error_handle->trs_tls_cert_verify_flags = 0;
    client->event.error_handle->error_type = MQTT_ERROR_TYPE_NONE;
    client->event.error_handle->connect_return_code = MQTT_CONNECTION_ACCEPTED;
    // post data event
    if ((uint8_t)*msg_data == 0x80) {
        client->event.error_handle->error_type = MQTT_ERROR_TYPE_SUBSCRIBE_FAILED;
    }
    client->event.data_len = msg_data_len;
    client->event.total_data_len = msg_data_len;
    client->event.event_id = MQTT_EVENT_SUBSCRIBED;
    client->event.data = msg_data;
    client->event.current_data_offset = 0;
    trs_mqtt_dispatch_event_with_msgid(client);

    return 0;
}

static bool is_valid_mqtt_msg(trs_mqtt_client_handle_t client, int msg_type, int msg_id)
{
    os_printf(LM_APP, LL_DBG, "MQTT:pending_id=%d, pending_msg_count = %d\r\n", client->mqtt_state.pending_msg_id, client->mqtt_state.pending_msg_count);
    if (client->mqtt_state.pending_msg_count == 0) {
        return false;
    }
    if (outbox_delete(client->outbox, msg_id, msg_type) == 0) {
        client->mqtt_state.pending_msg_count --;
        return true;
    }

    return false;
}

static outbox_item_handle_t mqtt_enqueue_oversized(trs_mqtt_client_handle_t client, uint8_t *remaining_data, int remaining_len)
{
	os_printf(LM_APP, LL_DBG, "mqtt_enqueue_oversized id: %d, type=%d successful\r\n",
             client->mqtt_state.pending_msg_id, client->mqtt_state.pending_msg_type);
    //lock mutex
    outbox_message_t msg = { 0 };
    msg.data = client->mqtt_state.outbound_message->data;
    msg.len =  client->mqtt_state.outbound_message->length;
    msg.msg_id = client->mqtt_state.pending_msg_id;
    msg.msg_type = client->mqtt_state.pending_msg_type;
    msg.msg_qos = client->mqtt_state.pending_publish_qos;
    msg.remaining_data = remaining_data;
    msg.remaining_len = remaining_len;
    //Copy to queue buffer
    return outbox_enqueue(client->outbox, &msg, platform_tick_get_ms());
    //unlock
}

static outbox_item_handle_t mqtt_enqueue(trs_mqtt_client_handle_t client)
{
    //os_printf(LM_APP, LL_ERR, "MQTT:mqtt_enqueue id: %d, type=%d successful\r\n", client->mqtt_state.pending_msg_id, client->mqtt_state.pending_msg_type);
    if (client->mqtt_state.pending_msg_count > 0) {
        outbox_message_t msg = { 0 };
        msg.data = client->mqtt_state.outbound_message->data;
        msg.len =  client->mqtt_state.outbound_message->length;
        msg.msg_id = client->mqtt_state.pending_msg_id;
        msg.msg_type = client->mqtt_state.pending_msg_type;
        msg.msg_qos = client->mqtt_state.pending_publish_qos;
        //Copy to queue buffer
        return outbox_enqueue(client->outbox, &msg, platform_tick_get_ms());
    }
    return NULL;
}

/*
 * Returns:
 *     -1 in case of failure
 *      0 if no message has been received
 *      1 if a message has been received and placed to client->mqtt_state:
 *           message length:  client->mqtt_state.message_length
 *           message content: client->mqtt_state.in_buffer
 */
static int mqtt_message_receive(trs_mqtt_client_handle_t client, int read_poll_timeout_ms)
{
    int read_len, total_len, fixed_header_len;
    uint8_t *buf = client->mqtt_state.in_buffer + client->mqtt_state.in_buffer_read_len;
    transport_handle_t t = client->transport;

    client->mqtt_state.message_length = 0;
    if (client->mqtt_state.in_buffer_read_len == 0) {
        /*
         * Read first byte of the mqtt packet fixed header, it contains packet
         * type and flags.
         */
        read_len = transport_read(t, (char *)buf, 1, read_poll_timeout_ms);
        if (read_len <= 0) {
            return trs_mqtt_handle_transport_read_error(read_len, client);
        }
        //os_printf(LM_APP, LL_ERR, "%s: first byte: 0x%x\r\n", __func__, *buf);
        /*
         * Verify the flags and act according to MQTT protocol: close connection
         * if the flags are set incorrectly.
         */
        if (!mqtt_has_valid_msg_hdr(buf, read_len)) {
        	os_printf(LM_APP, LL_ERR, "%s: received a message with an invalid header=0x%x\r\n", __func__, *buf);
            goto err;
        }
        buf++;
        client->mqtt_state.in_buffer_read_len++;
    }
    if ((client->mqtt_state.in_buffer_read_len == 1) ||
            ((client->mqtt_state.in_buffer_read_len < 6) && (*(buf - 1) & 0x80))) {
        do {
            /*
             * Read the "remaining length" part of mqtt packet fixed header.  It
             * starts at second byte and spans up to 4 bytes, but we accept here
             * only up to 2 bytes of remaining length, i.e. messages with
             * maximal remaining length value = 16383 (maximal total message
             * size of 16386 bytes).
             */
            read_len = transport_read(t, (char *)buf, 1, read_poll_timeout_ms);
            if (read_len <= 0) {
                return trs_mqtt_handle_transport_read_error(read_len, client);
            }
            os_printf(LM_APP, LL_DBG, "%s: read \"remaining length\" byte: 0x%x\r\n", __func__, *buf);
            buf++;
            client->mqtt_state.in_buffer_read_len++;
        } while ((client->mqtt_state.in_buffer_read_len < 6) && (*(buf - 1) & 0x80));
    }
    total_len = mqtt_get_total_length(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len, &fixed_header_len);
    os_printf(LM_APP, LL_DBG, "%s: total message length: %d (already read: %zu)\r\n", __func__, total_len, client->mqtt_state.in_buffer_read_len);
    client->mqtt_state.message_length = total_len;
    if (client->mqtt_state.in_buffer_length < total_len) {
        if (mqtt_get_type(client->mqtt_state.in_buffer) == MQTT_MSG_TYPE_PUBLISH) {
            /*
             * In case larger publish messages, we only need to read full topic, data can be split to multiple data event.
             * Evaluate and correct total_len to read only publish message header, so data can be read separately
             */
            if (client->mqtt_state.in_buffer_read_len < fixed_header_len + 2) {
                /* read next 2 bytes - topic length to get minimum portion of publish packet */
                read_len = transport_read(t, (char *)buf, client->mqtt_state.in_buffer_read_len - fixed_header_len + 2, read_poll_timeout_ms);
                os_printf(LM_APP, LL_DBG, "%s: read_len=%d\r\n", __func__, read_len);
                if (read_len <= 0) {
                    return trs_mqtt_handle_transport_read_error(read_len, client);
                }
                client->mqtt_state.in_buffer_read_len += read_len;
                buf += read_len;
                if (client->mqtt_state.in_buffer_read_len < fixed_header_len + 2) {
                	os_printf(LM_APP, LL_DBG, "%s: transport_read(): message reading left in progress :: total message length: %d (already read: %zu)\r\n",
                             __func__, total_len, client->mqtt_state.in_buffer_read_len);
                    return 0;
                }
            }
            int topic_len = client->mqtt_state.in_buffer[fixed_header_len] << 8;
            topic_len |= client->mqtt_state.in_buffer[fixed_header_len + 1];
            total_len = fixed_header_len + topic_len + (mqtt_get_qos(client->mqtt_state.in_buffer) > 0 ? 2 : 0);
            os_printf(LM_APP, LL_DBG, "%s: total len modified to %d as message longer than input buffer\r\n", __func__, total_len);
            if (client->mqtt_state.in_buffer_length < total_len) {
            	os_printf(LM_APP, LL_ERR, "%s: message is too big, insufficient buffer size\r\n", __func__);
                goto err;
            } else {
                total_len = client->mqtt_state.in_buffer_length;
            }
            /* free to continue with reading */
        } else {
        	os_printf(LM_APP, LL_ERR, "%s: message is too big, insufficient buffer size\r\n", __func__);
            goto err;
        }
    }
    if (client->mqtt_state.in_buffer_read_len < total_len) {
        /* read the rest of the mqtt message */
        read_len = transport_read(t, (char *)buf, total_len - client->mqtt_state.in_buffer_read_len, read_poll_timeout_ms);
        os_printf(LM_APP, LL_DBG, "%s: read_len=%d\r\n", __func__, read_len);
        if (read_len <= 0) {
            return trs_mqtt_handle_transport_read_error(read_len, client);
        }
        client->mqtt_state.in_buffer_read_len += read_len;
        if (client->mqtt_state.in_buffer_read_len < total_len) {
        	os_printf(LM_APP, LL_DBG, "%s: transport_read(): message reading left in progress :: total message length: %d (already read: %zu)\r\n",
                     __func__, total_len, client->mqtt_state.in_buffer_read_len);
            return 0;
        }
    }
    os_printf(LM_APP, LL_DBG, "%s: transport_read():%zu %zu\r\n", __func__, client->mqtt_state.in_buffer_read_len, client->mqtt_state.message_length);
    return 1;
err:
    trs_mqtt_client_dispatch_transport_error(client);
    return -1;
}

static int mqtt_process_receive(trs_mqtt_client_handle_t client)
{
    uint8_t msg_type = 0, msg_qos = 0;
    uint16_t msg_id = 0;

    int recv = mqtt_message_receive(client, MQTT_REV_DATA_TIMEOUT);
    if (recv < 0) {
        os_printf(LM_APP, LL_ERR, "%s: mqtt_message_receive() returned %d\r\n", __func__, recv);
        return -1;
    }
    if (recv == 0) {
        return 0;
    }
    int read_len = client->mqtt_state.message_length;

    // If the message was valid, get the type, quality of service and id of the message
    msg_type = mqtt_get_type(client->mqtt_state.in_buffer);
    msg_qos = mqtt_get_qos(client->mqtt_state.in_buffer);
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
        msg_id = mqtt5_get_id(client->mqtt_state.in_buffer, read_len);
#endif
    } else {
        msg_id = mqtt_get_id(client->mqtt_state.in_buffer, read_len);
    }

    os_printf(LM_APP, LL_DBG, "MQTT:msg_type=%d, msg_id=%d\r\n", msg_type, msg_id);

    switch (msg_type) {
    case MQTT_MSG_TYPE_SUBACK:
        if (is_valid_mqtt_msg(client, MQTT_MSG_TYPE_SUBSCRIBE, msg_id)) {
#ifdef CONFIG_MQTT_PROTOCOL_5
            trs_mqtt5_parse_suback(client);
#endif
	    os_printf(LM_APP, LL_INFO, "MQTT:Subscribe successful\r\n");
            //os_printf(LM_APP, LL_INFO, "MQTT:deliver_suback, message_length_read\r\n");
            if (deliver_suback(client) != 0) {
                os_printf(LM_APP, LL_ERR, "Failed to deliver suback message id=%d\r\n", msg_id);
                return -1;
            }
        }
        break;
    case MQTT_MSG_TYPE_UNSUBACK:
        if (is_valid_mqtt_msg(client, MQTT_MSG_TYPE_UNSUBSCRIBE, msg_id)) {
#ifdef CONFIG_MQTT_PROTOCOL_5
            trs_mqtt5_parse_unsuback(client);
#endif
            os_printf(LM_APP, LL_INFO, "MQTT:UnSubscribe successful\r\n");
            client->event.event_id = MQTT_EVENT_UNSUBSCRIBED;
            trs_mqtt_dispatch_event_with_msgid(client);
        }
        break;
    case MQTT_MSG_TYPE_PUBLISH:
        os_printf(LM_APP, LL_DBG, "deliver_publish, message_length_read=%zu, message_length=%zu\r\n", client->mqtt_state.in_buffer_read_len, client->mqtt_state.message_length);
        if (deliver_publish(client) != 0) {
            os_printf(LM_APP, LL_ERR, "Failed to deliver publish message id=%d\r\n", msg_id);
            return -1;
        }
        if (msg_qos == 1) {
            if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
                client->mqtt_state.outbound_message = mqtt5_msg_puback(&client->mqtt_state.mqtt_connection, msg_id);
#endif
            } else {
                client->mqtt_state.outbound_message = mqtt_msg_puback(&client->mqtt_state.mqtt_connection, msg_id);
            }
        } else if (msg_qos == 2) {
            if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
                client->mqtt_state.outbound_message = mqtt5_msg_pubrec(&client->mqtt_state.mqtt_connection, msg_id);
#endif
            } else {
                client->mqtt_state.outbound_message = mqtt_msg_pubrec(&client->mqtt_state.mqtt_connection, msg_id);
            }
        }
        if (client->mqtt_state.outbound_message->length == 0) {
        	os_printf(LM_APP, LL_ERR, "Publish response message PUBACK or PUBREC cannot be created\r\n");
            return -1;
        }

        if (msg_qos == 1 || msg_qos == 2) {
            os_printf(LM_APP, LL_INFO, "MQTT:Queue response QoS: %d\r\n", msg_qos);

            if (mqtt_write_data(client) != 0) {
                os_printf(LM_APP, LL_ERR, "MQTT:Error write qos msg repsonse, qos = %d\r\n", msg_qos);
                return -1;
            }
        }
        break;
    case MQTT_MSG_TYPE_PUBACK:
        if (is_valid_mqtt_msg(client, MQTT_MSG_TYPE_PUBLISH, msg_id)) {
            os_printf(LM_APP, LL_INFO, "MQTT:received MQTT_MSG_TYPE_PUBACK, finish QoS1 publish\r\n");
#ifdef CONFIG_MQTT_PROTOCOL_5
            trs_mqtt5_parse_puback(client);
#endif
            client->event.event_id = MQTT_EVENT_PUBLISHED;
            trs_mqtt_dispatch_event_with_msgid(client);
        }
        break;
    case MQTT_MSG_TYPE_PUBREC:
        os_printf(LM_APP, LL_INFO, "MQTT:received MQTT_MSG_TYPE_PUBREC\r\n");
        if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
        	os_printf(LM_APP, LL_INFO, "MQTT_MSG_TYPE_PUBREC return code is %d\r\n", mqtt5_msg_get_reason_code(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len));
            client->mqtt_state.outbound_message = mqtt5_msg_pubrel(&client->mqtt_state.mqtt_connection, msg_id);
#endif
        } else {
            client->mqtt_state.outbound_message = mqtt_msg_pubrel(&client->mqtt_state.mqtt_connection, msg_id);
        }
        if (client->mqtt_state.outbound_message->length == 0) {
        	os_printf(LM_APP, LL_ERR, "Publish response message PUBREL cannot be created\r\n");
            return -1;
        }

        outbox_set_pending(client->outbox, msg_id, ACKNOWLEDGED);
        mqtt_write_data(client);
        break;
    case MQTT_MSG_TYPE_PUBREL:
    	os_printf(LM_APP, LL_DBG, "received MQTT_MSG_TYPE_PUBREL\r\n");
        if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
        	os_printf(LM_APP, LL_INFO, "MQTT_MSG_TYPE_PUBREL return code is %d\r\n", mqtt5_msg_get_reason_code(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len));
            client->mqtt_state.outbound_message = mqtt5_msg_pubcomp(&client->mqtt_state.mqtt_connection, msg_id);
#endif
        } else {
            client->mqtt_state.outbound_message = mqtt_msg_pubcomp(&client->mqtt_state.mqtt_connection, msg_id);
        }
        if (client->mqtt_state.outbound_message->length == 0) {
        	os_printf(LM_APP, LL_ERR, "Publish response message PUBCOMP cannot be created\r\n");
            return -1;
        }

        mqtt_write_data(client);
        break;
    case MQTT_MSG_TYPE_PUBCOMP:
        os_printf(LM_APP, LL_INFO, "MQTT:received MQTT_MSG_TYPE_PUBCOMP\r\n");
        if (is_valid_mqtt_msg(client, MQTT_MSG_TYPE_PUBLISH, msg_id)) {
            os_printf(LM_APP, LL_INFO, "MQTT:Receive MQTT_MSG_TYPE_PUBCOMP, finish QoS2 publish\r\n");
#ifdef CONFIG_MQTT_PROTOCOL_5
            trs_mqtt5_parse_pubcomp(client);
#endif
            client->event.event_id = MQTT_EVENT_PUBLISHED;
            trs_mqtt_dispatch_event_with_msgid(client);
        }
        break;
    case MQTT_MSG_TYPE_PINGRESP:
        //os_printf(LM_APP, LL_ERR, "MQTT:PINGRESP %d\r\n", ping_resp++);
        client->wait_for_ping_resp = false;
        /* It is the responsibility of the Client to ensure that the interval between Control Packets
         * being sent does not exceed the Keep Alive value. In the absence of sending any other Control
         * Packets, the Client MUST send a PINGREQ Packet [MQTT-3.1.2-23].
         * [MQTT-3.1.2-23]
         */
        client->keepalive_tick = platform_tick_get_ms();
        break;
    }

    client->mqtt_state.in_buffer_read_len = 0;
    return 0;
}

static int mqtt_resend_queued(trs_mqtt_client_handle_t client, outbox_item_handle_t item)
{
    // decode queued data
    client->mqtt_state.outbound_message->data = outbox_item_get_data(item, &client->mqtt_state.outbound_message->length, &client->mqtt_state.pending_msg_id,
            &client->mqtt_state.pending_msg_type, &client->mqtt_state.pending_publish_qos);
    // set duplicate flag for QoS-1 and QoS-2 messages
    if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_publish_qos > 0 && (outbox_item_get_pending(item) == TRANSMITTED)) {
        mqtt_set_dup(client->mqtt_state.outbound_message->data);
        os_printf(LM_APP, LL_DBG, "Sending Duplicated QoS%d message with id=%d\r\n", client->mqtt_state.pending_publish_qos, client->mqtt_state.pending_msg_id);
    }

    // try to resend the data
    if (mqtt_write_data(client) != 0) {
    	os_printf(LM_APP, LL_ERR, "Error to resend data\r\n");
        trs_mqtt_abort_connection(client);
        return -1;
    }

    // check if it was QoS-0 publish message
    if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_publish_qos == 0) {
        // delete all qos0 publish messages once we process them
        if (outbox_delete_item(client->outbox, item) != 0) {
        	os_printf(LM_APP, LL_ERR, "Failed to remove queued qos0 message from the outbox\r\n");
        }
    }
    return 0;
}
static void mqtt_delete_expired_messages(trs_mqtt_client_handle_t client)
{
    // Delete message after OUTBOX_EXPIRED_TIMEOUT_MS milliseconds
#if MQTT_REPORT_DELETED_MESSAGES
    // also report the deleted items as MQTT_EVENT_DELETED events if enabled
    int deleted_items = 0;
    int msg_id = 0;
    while ((msg_id = outbox_delete_single_expired(client->outbox, platform_tick_get_ms(), OUTBOX_EXPIRED_TIMEOUT_MS)) > 0) {
        client->event.event_id = MQTT_EVENT_DELETED;
        client->event.msg_id = msg_id;
        if (trs_mqtt_dispatch_event(client) != 0) {
        	os_printf(LM_APP, LL_ERR, "Failed to post event on deleting message id=%d\r\n", msg_id);
        }
        deleted_items ++;
    }
#else
    int deleted_items = outbox_delete_expired(client->outbox, platform_tick_get_ms(), OUTBOX_EXPIRED_TIMEOUT_MS);
#endif
    client->mqtt_state.pending_msg_count -= deleted_items;

    if (client->mqtt_state.pending_msg_count < 0) {
        client->mqtt_state.pending_msg_count = 0;
    }
}

static void mqtt_message_task(void *pv)
{
    trs_mqtt_client_handle_t client = (trs_mqtt_client_handle_t)pv;
    MQTT_CFG_MSG_ST cfg_msg = {0};

    do {
        if (os_queue_receive(client->cfg_queue_handle, (char *)&cfg_msg, sizeof(cfg_msg), MQTT_REV_MSG_TIMEOUT) == 0) {
            os_printf(LM_APP, LL_DBG, "MQTT:mqtt receives msg, type:%d;  topic:%p; topic_len:%d; data:%p; data_len: %d; qos:%d; retain:%d \r\n", 
                cfg_msg.cfg_type, cfg_msg.topic, cfg_msg.topic_len, cfg_msg.data, cfg_msg.data_len, cfg_msg.qos, cfg_msg.retain);

            if (cfg_msg.cfg_type == CFG_MSG_TYPE_PUB) {
                trs_mqtt_client_publish(client, cfg_msg.topic, cfg_msg.data, cfg_msg.data_len, cfg_msg.qos, cfg_msg.retain); 
            } else if (cfg_msg.cfg_type == CFG_MSG_TYPE_PUB_RAW) {
                MQTT_PUB_STATUS_E pub_status = MQTT_PUB_SUCCESS;

                if (trs_mqtt_client_publish(client, cfg_msg.topic, cfg_msg.data, cfg_msg.data_len, cfg_msg.qos, cfg_msg.retain) < 0) {
                    pub_status = MQTT_PUB_FAILED;
                }
                if (client->config->pub_handle != NULL) {
                    client->config->pub_handle(pub_status);
                }
            } else if (cfg_msg.cfg_type == CFG_MSG_TYPE_SUB) {
                trs_mqtt_client_subscribe(client, cfg_msg.topic, cfg_msg.qos);
            } else if (cfg_msg.cfg_type == CFG_MSG_TYPE_UNSUB) {
                trs_mqtt_client_unsubscribe(client, cfg_msg.topic);
            } else if (cfg_msg.cfg_type == CFG_MSG_TYPE_RECON) {
                trs_mqtt_client_reconnect(client);
            } else if (cfg_msg.cfg_type == CFG_MSG_TYPE_DISC) {
                trs_mqtt_client_disconnect(client);
            } else if (cfg_msg.cfg_type == CFG_MSG_TYPE_STOP) {
                trs_mqtt_client_stop(client);
            } else {
                os_printf(LM_APP, LL_ERR, "MQTT:mqtt receives cfg_msg.cfg_type err(%d).\r\n", cfg_msg.cfg_type);
            }
            
            if (cfg_msg.topic != NULL) {
                mqtt_free((void *)cfg_msg.topic);
            }

            if (cfg_msg.data != NULL) {
                mqtt_free((void *)cfg_msg.data);
            }

            if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
                if (!(cfg_msg.cfg_type == CFG_MSG_TYPE_PUB_RAW || cfg_msg.cfg_type == CFG_MSG_TYPE_RECON)) {
                    if (*cfg_msg.user_property != NULL)
                    {
                        trs_mqtt5_client_delete_user_property(*cfg_msg.user_property);
                        *cfg_msg.user_property = NULL;
                    }
                }
#endif
            }
        }
    } while (client->run);

    do {
        os_msleep(50); /* wait mqtt net task exit first */
    } while(client->state != MQTT_STATE_WAIT_CLOSE);
    transport_close(client->transport);
    outbox_delete_all_items(client->outbox);
    trs_mqtt_client_destroy(client);
    os_printf(LM_APP, LL_ERR, "MQTT:mqtt message task exit\r\n");
}

static void trs_mqtt_task(void *pv)
{
    trs_mqtt_client_handle_t client = (trs_mqtt_client_handle_t) pv;
    uint64_t last_retransmit = 0;
    outbox_tick_t msg_tick = 0;

    //get transport by scheme
    client->transport = transport_list_get_transport(client->transport_list, client->config->scheme);
    if (client->transport == NULL) {
        os_printf(LM_APP, LL_ERR, "MQTT:There are no transports valid, stop mqtt client, config scheme = %s\r\n", client->config->scheme);
        client->run = false;
    }
    //default port
    if (client->config->port == 0) {
        client->config->port = transport_get_default_port(client->transport);
    }

    client->state = MQTT_STATE_INIT;
    while (client->run) {
        MQTT_API_LOCK(client);
        switch ((int)client->state) {
            case MQTT_STATE_DISCONNECTED:
            break;
            case MQTT_STATE_INIT:
				xEventGroupClearBits(client->status_bits, RECONNECT_BIT | DISCONNECT_BIT);
                client->event.event_id = MQTT_EVENT_BEFORE_CONNECT;
                trs_mqtt_dispatch_event_with_msgid(client);

                if (client->transport == NULL) {
                    os_printf(LM_APP, LL_ERR, "MQTT:There are no transport\r\n");
                    client->run = false;
                }
#if CONFIG_MQTT_SSL
                trs_mqtt_set_ssl_transport_properties(client->transport_list, client->config);
#endif
                os_printf(LM_APP, LL_INFO, "MQTT:client->config->host = %s; client->config->port = %d;\r\n", client->config->host, client->config->port);
                if (transport_connect(client->transport,
                                      client->config->host,
                                      client->config->port,
                                      client->config->network_timeout_ms) < 0) {
                    os_printf(LM_APP, LL_ERR, "MQTT:Error transport connect\r\n");
                    trs_mqtt_client_dispatch_transport_error(client);
                    trs_mqtt_abort_connection(client);
                    break;
                }
    
                os_printf(LM_APP, LL_INFO, "MQTT:Transport connected to %s://%s:%d\r\n", client->config->scheme, client->config->host, client->config->port);
                if (trs_mqtt_connect(client, client->config->network_timeout_ms) != 0) {
                    os_printf(LM_APP, LL_ERR, "MQTT:Error MQTT Connected\r\n");
                    trs_mqtt_abort_connection(client);
                    break;
                }

                client->event.event_id = MQTT_EVENT_CONNECTED;
                if (client->connect_info.protocol_ver != MQTT_PROTOCOL_V_5) {
                client->event.session_present = mqtt_get_connect_session_present(client->mqtt_state.in_buffer);
                }
                client->state = MQTT_STATE_CONNECTED;
                trs_mqtt_dispatch_event_with_msgid(client);
                client->refresh_connection_tick = platform_tick_get_ms();
                client->keepalive_tick = platform_tick_get_ms();

                break;
            case MQTT_STATE_CONNECTED:
                client->everconn = 1; // connect to mqttserver once
                // check for disconnection request
                if (xEventGroupWaitBits(client->status_bits, DISCONNECT_BIT, true, true, 0) & DISCONNECT_BIT) {
                    send_disconnect_msg(client);    // ignore error, if clean disconnect fails, just abort the connection
                    trs_mqtt_abort_connection(client);
                    break;
                }
                // receive and process data
                if (mqtt_process_receive(client) == -1) {
                    trs_mqtt_abort_connection(client);
                    break;
                }
                
				// delete long pending messages
                mqtt_delete_expired_messages(client);

                // resend all non-transmitted messages first
                outbox_item_handle_t item = outbox_dequeue(client->outbox, QUEUED, NULL);
                if (item) {
                    if (mqtt_resend_queued(client, item) == 0) {
                        outbox_set_pending(client->outbox, client->mqtt_state.pending_msg_id, TRANSMITTED);
                    }
                // resend other "transmitted" messages after 1s
                } else if (has_timed_out(last_retransmit, client->config->message_retransmit_timeout)) {
                    last_retransmit = platform_tick_get_ms();
                    item = outbox_dequeue(client->outbox, TRANSMITTED, &msg_tick);
                    if (item && (last_retransmit - msg_tick > client->config->message_retransmit_timeout))  {
                        mqtt_resend_queued(client, item);
                    }
                }

                if (process_keepalive(client) != 0) {
                    break;
                }

                if (client->config->refresh_connection_after_ms &&
                    has_timed_out(client->refresh_connection_tick, client->config->refresh_connection_after_ms)) {
            	    os_printf(LM_APP, LL_DBG, "Refreshing the connection...\r\n");
                    trs_mqtt_abort_connection(client);
                    client->state = MQTT_STATE_INIT;
                }

                break;
            case MQTT_STATE_WAIT_RECONNECT:

                if (!client->config->auto_reconnect && xEventGroupGetBits(client->status_bits)&RECONNECT_BIT) {
                    xEventGroupClearBits(client->status_bits, RECONNECT_BIT);
                    client->state = MQTT_STATE_INIT;
                    client->wait_timeout_ms = MQTT_RECON_DEFAULT_MS;
                    os_printf(LM_APP, LL_DBG, "Reconnecting per user request...\r\n");
                    break;
                }else if (client->config->auto_reconnect &&
                       platform_tick_get_ms() - client->reconnect_tick > client->wait_timeout_ms) {
                    client->state = MQTT_STATE_INIT;
                    client->reconnect_tick = platform_tick_get_ms();
                    os_printf(LM_APP, LL_DBG, "MQTT:Reconnecting...\r\n");
					break;
                }
                MQTT_API_UNLOCK(client);
                os_printf(LM_APP, LL_INFO, "MQTT:waiting for connnecting\r\n");
                xEventGroupWaitBits(client->status_bits, RECONNECT_BIT, false, true,
                                client->wait_timeout_ms / 2 / portTICK_PERIOD_MS);
                //vTaskDelay(client->wait_timeout_ms / 2 / portTICK_RATE_MS);
                continue;
            default:
                os_printf(LM_APP, LL_ERR, "MQTT client error, client is in an unrecoverable state, client->state:%d\r\n", client->state);
                break;
        }
        MQTT_API_UNLOCK(client);
        if (MQTT_STATE_CONNECTED == client->state) {
        	if (transport_poll_read(client->transport, MQTT_POLL_READ_TIMEOUT_MS) < 0) {
            	os_printf(LM_APP, LL_ERR, "Poll read error: %d, aborting connection\r\n", errno);
                trs_mqtt_abort_connection(client);
            }
        }
    }

    client->state = MQTT_STATE_WAIT_CLOSE;
    os_printf(LM_APP, LL_ERR, "MQTT:mqtt nettask exit\r\n");
}

int trs_mqtt_client_start(trs_mqtt_client_handle_t client)
{
    int ret;
    MQTT_CLIENT_CHECK_RETURN(client == NULL, -1, "client should be init");
    MQTT_CLIENT_CHECK_RETURN(client->cfg_queue_handle != NULL, -1, "client has started");

    MQTT_API_LOCK(client);
    ret = trs_mqtt_client_create_transport(client);
    if (ret != 0) {
        trs_mqtt_client_destroy(client);
        MQTT_API_UNLOCK(client);
        MQTT_CLIENT_CHECK_RETURN(1, -1, "Failed to create transport list");
    }

    client->cfg_queue_handle = os_queue_create(NULL, MQTT_QUEUE_LENGTH, sizeof(MQTT_CFG_MSG_ST), 0);
    if (client->cfg_queue_handle == NULL) {
        trs_mqtt_client_destroy(client);
        MQTT_API_UNLOCK(client);
        MQTT_CLIENT_CHECK_RETURN(1, -1, "Failed to create mqtt queue");
    }

    client->run = true;
    ret = os_task_create("mqtt_mtask", client->config->task_prio+1, client->config->task_stack, mqtt_message_task, client);
    if (ret < 0) {
        trs_mqtt_client_destroy(client);
        MQTT_API_UNLOCK(client);
        MQTT_CLIENT_CHECK_RETURN(1, -1, "MQTT:Error create mqtt message task");
    }
    MQTT_API_UNLOCK(client);

    ret = os_task_create("mqtt_ntask", client->config->task_prio, client->config->task_stack, trs_mqtt_task, client);
    if (ret < 0) {
        MQTT_CFG_MSG_ST msg_send = {0};
        os_printf(LM_APP, LL_ERR, "MQTT:Error create mqtt nettask\r\n");
        client->state = MQTT_STATE_WAIT_CLOSE;
        msg_send.cfg_type = CFG_MSG_TYPE_STOP;
        ret = os_queue_send(client->cfg_queue_handle, (char *)&msg_send, sizeof(msg_send), 0);
        MQTT_CLIENT_CHECK_RETURN(ret != 0, -1, "MQTT:send stop message failed");

        return -1;
    }

    return 0;
}

int trs_mqtt_client_disconnect(trs_mqtt_client_handle_t client)
{
    if (!client) {
    	os_printf(LM_APP, LL_ERR, "Client was not initialized\r\n");
        return -1;
    }
    os_printf(LM_APP, LL_INFO, "Client asked to disconnect\r\n");
    xEventGroupSetBits(client->status_bits, DISCONNECT_BIT);
    return 0;
}

int trs_mqtt_client_reconnect(trs_mqtt_client_handle_t client)
{
    if (!client) {
    	os_printf(LM_APP, LL_ERR, "Client was not initialized\r\n");
        return -1;
    }
    os_printf(LM_APP, LL_INFO, "Client force reconnect requested\r\n");
    if (client->state != MQTT_STATE_WAIT_RECONNECT) {
    	os_printf(LM_APP, LL_DBG, "The client is not waiting for reconnection. Ignore the request\r\n");
        return -1;
    }
    client->wait_timeout_ms = 0;
    xEventGroupSetBits(client->status_bits, RECONNECT_BIT);
    return 0;
}

static int send_disconnect_msg(trs_mqtt_client_handle_t client)
{
    // Notify the broker we are disconnecting
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
        client->mqtt_state.outbound_message = mqtt5_msg_disconnect(&client->mqtt_state.mqtt_connection, &client->mqtt5_config->disconnect_property_info);
        if (client->mqtt_state.outbound_message->length) {
            trs_mqtt5_client_delete_user_property(client->mqtt5_config->disconnect_property_info.user_property);
            client->mqtt5_config->disconnect_property_info.user_property = NULL;
            memset(&client->mqtt5_config->disconnect_property_info, 0, sizeof(trs_mqtt5_disconnect_property_config_t));
        }
#endif
    } else {
        client->mqtt_state.outbound_message = mqtt_msg_disconnect(&client->mqtt_state.mqtt_connection);
    }
    if (client->mqtt_state.outbound_message->length == 0) {
    	os_printf(LM_APP, LL_ERR, "Disconnect message cannot be created\r\n");
        return -1;
    }
    if (mqtt_write_data(client) != 0) {
    	os_printf(LM_APP, LL_ERR, "Error sending disconnect message\r\n");
    }
    return 0;
}

int trs_mqtt_client_stop(trs_mqtt_client_handle_t client)
{
    if (!client) {
    	os_printf(LM_APP, LL_ERR, "Client was not initialized\r\n");
        return -1;
    }
    MQTT_API_LOCK(client);
	if (client->run) {
        // Only send the disconnect message if the client is connected
        if (client->state == MQTT_STATE_CONNECTED) {
            if (send_disconnect_msg(client) != 0) {
                MQTT_API_UNLOCK(client);
                return -1;
            }
        }
        client->run = false;
        client->state = MQTT_STATE_DISCONNECTED;
        MQTT_API_UNLOCK(client);
        return 0;
    } else {
    	os_printf(LM_APP, LL_DBG, "Client asked to stop, but was not started\r\n");
        MQTT_API_UNLOCK(client);
        return -1;
    }
    
}

static int trs_mqtt_client_ping(trs_mqtt_client_handle_t client)
{
    client->mqtt_state.outbound_message = mqtt_msg_pingreq(&client->mqtt_state.mqtt_connection);
    if (client->mqtt_state.outbound_message->length == 0) {
    	os_printf(LM_APP, LL_ERR, "Ping message cannot be created\r\n");
        return -1;
    }

    if (mqtt_write_data(client) != 0) {
        os_printf(LM_APP, LL_ERR, "MQTT:Error sending PING\r\n");
        return -1;
    }
    //os_printf(LM_APP, LL_ERR, "MQTT:PING send\r\n");
    return 0;
}

int trs_mqtt_client_subscribe(trs_mqtt_client_handle_t client, const char *topic, int qos)
{
    if (!client) {
        os_printf(LM_APP, LL_ERR, "Client was not initialized\r\n");
        return -1;
    }
    MQTT_API_LOCK(client);
    if (client->state != MQTT_STATE_CONNECTED) {
        os_printf(LM_APP, LL_ERR, "MQTT:Client has not connected\r\n");
        MQTT_API_UNLOCK(client);
        return -1;
    }

    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
        if (trs_mqtt5_client_subscribe_check(client, qos) != 0) {
        	os_printf(LM_APP, LL_INFO, "MQTT5 subscribe check fail\r\n");
            MQTT_API_UNLOCK(client);
            return -1;
        }
        client->mqtt_state.outbound_message = mqtt5_msg_subscribe(&client->mqtt_state.mqtt_connection,
                                              topic, qos,
                                              &client->mqtt_state.pending_msg_id, client->mqtt5_config->subscribe_property_info);
        if (client->mqtt_state.outbound_message->length) {
            client->mqtt5_config->subscribe_property_info = NULL;
        }
#endif
    } else {
        client->mqtt_state.outbound_message = mqtt_msg_subscribe(&client->mqtt_state.mqtt_connection,
                                              topic, qos,
                                              &client->mqtt_state.pending_msg_id);
    }
    if (client->mqtt_state.outbound_message->length == 0) {
        os_printf(LM_APP, LL_ERR, "Subscribe message cannot be created\r\n");
        MQTT_API_UNLOCK(client);
        return -1;
    }

    client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
    client->mqtt_state.pending_msg_count ++;
    //move pending msg to outbox (if have)
    if (!mqtt_enqueue(client)) {
        MQTT_API_UNLOCK(client);
        return -1;
    }
    outbox_set_pending(client->outbox, client->mqtt_state.pending_msg_id, TRANSMITTED);// handle error

    if (mqtt_write_data(client) != 0) {
        os_printf(LM_APP, LL_ERR, "MQTT:Error to subscribe topic=%s, qos=%d\r\n", topic, qos);
        MQTT_API_UNLOCK(client);
        return -1;
    }

    os_printf(LM_APP, LL_DBG, "MQTT:Sent subscribe topic=%s\r\n", topic);
    os_printf(LM_APP, LL_DBG, ", id: %d, type: %d successful\r\n", client->mqtt_state.pending_msg_id, client->mqtt_state.pending_msg_type);
    MQTT_API_UNLOCK(client);
    return client->mqtt_state.pending_msg_id;
}

int trs_mqtt_client_unsubscribe(trs_mqtt_client_handle_t client, const char *topic)
{
    if (!client) {
    	os_printf(LM_APP, LL_ERR, "Client was not initialized\r\n");
        return -1;
    }
    MQTT_API_LOCK(client);
    if (client->state != MQTT_STATE_CONNECTED) {
        os_printf(LM_APP, LL_ERR, "MQTT:Client has not connected\r\n");
        MQTT_API_UNLOCK(client);
        return -1;
    }
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
        client->mqtt_state.outbound_message = mqtt5_msg_unsubscribe(&client->mqtt_state.mqtt_connection,
                                              topic,
                                              &client->mqtt_state.pending_msg_id, client->mqtt5_config->unsubscribe_property_info);
        if (client->mqtt_state.outbound_message->length) {
            client->mqtt5_config->unsubscribe_property_info = NULL;
        }
#endif
    } else {
        client->mqtt_state.outbound_message = mqtt_msg_unsubscribe(&client->mqtt_state.mqtt_connection,
                                              topic,
                                              &client->mqtt_state.pending_msg_id);
    }
    if (client->mqtt_state.outbound_message->length == 0) {
        os_printf(LM_APP, LL_ERR, "Unubscribe message cannot be created\r\n");
        MQTT_API_UNLOCK(client);
        return -1;
    }
    os_printf(LM_APP, LL_DBG, "MQTT:unsubscribe, topic\"%s\", id: %d\r\n", topic, client->mqtt_state.pending_msg_id);

    client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
    client->mqtt_state.pending_msg_count ++;
    if (!mqtt_enqueue(client)) {
        MQTT_API_UNLOCK(client);
        return -1;
    }
    outbox_set_pending(client->outbox, client->mqtt_state.pending_msg_id, TRANSMITTED); //handle error

    if (mqtt_write_data(client) != 0) {
        os_printf(LM_APP, LL_ERR, "MQTT:Error to unsubscribe topic=%s\r\n", topic);
        MQTT_API_UNLOCK(client);
        return -1;
    }

    os_printf(LM_APP, LL_INFO, "MQTT:Sent Unsubscribe topic=%s, id: %d, successful\r\n", topic, client->mqtt_state.pending_msg_id);
    MQTT_API_UNLOCK(client);
    return client->mqtt_state.pending_msg_id;
}

static inline int mqtt_client_enqueue_priv(trs_mqtt_client_handle_t client, const char *topic, const char *data,
        int len, int qos, int retain, bool store)
{
    uint16_t pending_msg_id = 0;
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef CONFIG_MQTT_PROTOCOL_5
        client->mqtt_state.outbound_message = mqtt5_msg_publish(&client->mqtt_state.mqtt_connection,
                                              topic, data, len,
                                              qos, retain,
                                              &pending_msg_id, client->mqtt5_config->publish_property_info, client->mqtt5_config->server_resp_property_info.response_info);
        if (client->mqtt_state.outbound_message->length) {
            client->mqtt5_config->publish_property_info = NULL;
        }
#endif
    } else {
        client->mqtt_state.outbound_message = mqtt_msg_publish(&client->mqtt_state.mqtt_connection,
                                              topic, data, len,
                                              qos, retain,
                                              &pending_msg_id);
    }

    if (client->mqtt_state.outbound_message->length == 0) {
        os_printf(LM_APP, LL_ERR, "Publish message cannot be created\r\n");
        return -1;
    }
    /* We have to set as pending all the qos>0 messages */
    //TODO: client->mqtt_state.outbound_message = publish_msg;
    if (qos > 0 || store) {
        client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
        client->mqtt_state.pending_msg_id = pending_msg_id;
        client->mqtt_state.pending_publish_qos = qos;
        client->mqtt_state.pending_msg_count ++;
        // by default store as QUEUED (not transmitted yet) only for messages which would fit outbound buffer
        if (client->mqtt_state.mqtt_connection.message.fragmented_msg_total_length == 0) {
            if (!mqtt_enqueue(client)) {
                return -1;
            }
        } else {
            int first_fragment = client->mqtt_state.outbound_message->length - client->mqtt_state.outbound_message->fragmented_msg_data_offset;
            if (!mqtt_enqueue_oversized(client, ((uint8_t *)data) + first_fragment, len - first_fragment)) {
                return -1;
            }
            client->mqtt_state.outbound_message->fragmented_msg_total_length = 0;
        }
    }
    return pending_msg_id;
}

int trs_mqtt_client_publish(trs_mqtt_client_handle_t client, const char *topic, const char *data, int len, int qos, int retain)
{
    if (!client) {
        os_printf(LM_APP, LL_ERR, "Client was not initialized\r\n");
        return -1;
    }
    MQTT_API_LOCK(client);
#if MQTT_SKIP_PUBLISH_IF_DISCONNECTED
    if (client->state != MQTT_STATE_CONNECTED) {
        os_printf(LM_APP, LL_ERR, "Publishing skipped: client is not connected\r\n");
        MQTT_API_UNLOCK(client);
        return -1;
    }
#endif

#ifdef CONFIG_MQTT_PROTOCOL_5
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
        if (trs_mqtt5_client_publish_check(client, qos, retain) != 0) {
        	os_printf(LM_APP, LL_INFO, "MQTT5 publish check fail\r\n");
            MQTT_API_UNLOCK(client);
            return -1;
        }
    }
#endif

    /* Acceptable publish messages:
        data == NULL, len == 0: publish null message
        data valid,   len == 0: publish all data, payload len is determined from string length
        data valid,   len >  0: publish data with defined length
     */
    if (len <= 0 && data != NULL) {
        len = strlen(data);
    }

    int pending_msg_id = mqtt_client_enqueue_priv(client, topic, data, len, qos, retain, false);
    if (pending_msg_id < 0) {
        MQTT_API_UNLOCK(client);
        return -1;
    }
    int ret = 0;

    /* Skip sending if not connected (rely on resending) */
    if (client->state != MQTT_STATE_CONNECTED) {
    	os_printf(LM_APP, LL_DBG, "Publish: client is not connected\r\n");
        if (qos > 0) {
            ret = pending_msg_id;
        }

        // delete long pending messages
        mqtt_delete_expired_messages(client);

        goto cannot_publish;
    }

    /* Provide support for sending fragmented message if it doesn't fit buffer */
    int remaining_len = len;
    const char *current_data = data;
    bool sending = true;

    while (sending)  {

        if (mqtt_write_data(client) != 0) {
            trs_mqtt_abort_connection(client);
            ret = -1;
            goto cannot_publish;
        }

        int data_sent = client->mqtt_state.outbound_message->length - client->mqtt_state.outbound_message->fragmented_msg_data_offset;
        client->mqtt_state.outbound_message->fragmented_msg_data_offset = 0;
        client->mqtt_state.outbound_message->fragmented_msg_total_length = 0;
        remaining_len -= data_sent;
        current_data +=  data_sent;

        if (remaining_len > 0) {
            mqtt_connection_t *connection = &client->mqtt_state.mqtt_connection;
            os_printf(LM_APP, LL_DBG, "Sending fragmented message, remains to send %d bytes of %d\r\n", remaining_len, len);
            if (remaining_len > connection->buffer_length) {
                // Continue with sending
                memcpy(connection->buffer, current_data, connection->buffer_length);
                connection->message.length = connection->buffer_length;
                sending = true;
            } else {
                memcpy(connection->buffer, current_data, remaining_len);
                connection->message.length = remaining_len;
                sending = true;
            }
            connection->message.data = connection->buffer;
            client->mqtt_state.outbound_message = &connection->message;
        } else {
            // Message was sent correctly
            sending = false;
        }
    }

    if (qos > 0) {
        //Tick is set after transmit to avoid retransmitting too early due slow network speed / big messages
        outbox_set_tick(client->outbox, pending_msg_id, platform_tick_get_ms());
        outbox_set_pending(client->outbox, pending_msg_id, TRANSMITTED);
    }
    MQTT_API_UNLOCK(client);
    return pending_msg_id;

cannot_publish:
    // clear out possible fragmented publish if failed or skipped
    client->mqtt_state.outbound_message->fragmented_msg_total_length = 0;
    if (qos == 0) {
    	os_printf(LM_APP, LL_DBG, "Publish: Losing qos0 data when client not connected\r\n");
    }
    MQTT_API_UNLOCK(client);

    return ret;
}

int trs_mqtt_client_enqueue(trs_mqtt_client_handle_t client, const char *topic, const char *data, int len, int qos, int retain, bool store)
{
    if (!client) {
    	os_printf(LM_APP, LL_ERR, "Client was not initialized\r\n");
        return -1;
    }

    /* Acceptable publish messages:
        data == NULL, len == 0: publish null message
        data valid,   len == 0: publish all data, payload len is determined from string length
        data valid,   len >  0: publish data with defined length
     */
    if (len <= 0 && data != NULL) {
        len = strlen(data);
    }

    MQTT_API_LOCK(client);
#ifdef CONFIG_MQTT_PROTOCOL_5
    if (client->connect_info.protocol_ver == MQTT_PROTOCOL_V_5) {
        if (trs_mqtt5_client_publish_check(client, qos, retain) != 0) {
            os_printf(LM_APP, LL_INFO, "trs_mqtt_client_enqueue check fail\r\n");
			MQTT_API_UNLOCK(client);
            return -1;
        }
    }
#endif
    int ret = mqtt_client_enqueue_priv(client, topic, data, len, qos, retain, store);
    MQTT_API_UNLOCK(client);
    if (ret == 0 && store == false) {
        // messages with qos=0 are not enqueued if not overridden by store_in_outobx -> indicate as error
        return -1;
    }
    return ret;
}

static void trs_mqtt_client_dispatch_transport_error(trs_mqtt_client_handle_t client)
{
    client->event.event_id = MQTT_EVENT_ERROR;
    client->event.error_handle->error_type = MQTT_ERROR_TYPE_TCP_TRANSPORT;
    client->event.error_handle->connect_return_code = 0;
#ifdef MQTT_SUPPORTED_FEATURE_TRANSPORT_ERR_REPORTING
    client->event.error_handle->trs_tls_last_trs_err = trs_tls_get_and_clear_last_error(trs_transport_get_error_handle(client->transport),
            &client->event.error_handle->trs_tls_stack_err,
            &client->event.error_handle->trs_tls_cert_verify_flags);
#ifdef MQTT_SUPPORTED_FEATURE_TRANSPORT_SOCK_ERRNO_REPORTING
    client->event.error_handle->trs_transport_sock_errno = trs_transport_get_errno(client->transport);
#endif
#endif
    trs_mqtt_dispatch_event_with_msgid(client);
}

int trs_mqtt_client_get_outbox_size(trs_mqtt_client_handle_t client)
{
    int outbox_size = 0;

    if (client == NULL) {
        return 0;
    }

    MQTT_API_LOCK(client);

    if (client->outbox) {
        outbox_size = outbox_get_size(client->outbox);
    }

    MQTT_API_UNLOCK(client);

    return outbox_size;
}

int trs_mqtt_client_get_states(trs_mqtt_client_handle_t client)
{
    if(client){
        return (int)client->state;
    } else {
        return -1;
    }
}

char* trs_mqtt_client_get_url(trs_mqtt_client_handle_t client)
{
    if(client){
        return (char* )client->config->uri;
    } else {
        return NULL;
    }
}

os_queue_handle_t trs_mqtt_client_get_queue(trs_mqtt_client_handle_t client)
{
    if (client) {
        return client->cfg_queue_handle;
    } else {
        return NULL;
    }
}

trs_mqtt_protocol_ver_t trs_mqtt_client_get_version(trs_mqtt_client_handle_t client)
{
    if (client) {
        return client->connect_info.protocol_ver;
    } else {
        return -1;
    }
}

int set_mqtt_msg(SET_TYPE_EN type, MQTT_CFG_MSG_ST *msg, const char *data, int data_len)
{
    if ((msg == NULL) || (data == NULL)) {
        system_printf("In set_mqtt_msg(), para err\r\n");
        return -1;
    }

    void *p_tmp = mqtt_malloc(data_len + 1);
    MEM_CHECK(p_tmp, return -1);
    memset(p_tmp, 0, data_len + 1);
    memcpy(p_tmp, data, data_len);
    if (type == SET_TOPIC) {
        msg->topic     = p_tmp;
        msg->topic_len = data_len;
    } else if (type == SET_DATA) {
        msg->data     = p_tmp;
        msg->data_len = data_len;
    } else {
        system_printf("In set_mqtt_msg(), set_type err(%d)\r\n", type);
    }
    
    return 0;
}
