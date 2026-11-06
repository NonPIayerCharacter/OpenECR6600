#ifndef _MQTT_CLIENT_H_
#define _MQTT_CLIENT_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "oshal.h"
#include "mqtt_config.h"

#ifdef CONFIG_MQTT_PROTOCOL_5
#include "mqtt5_client.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_CONN_STRING "MQTT_EVENT_CONNECTED!\r\n"
#define MQTT_DISCONN_STRING "MQTT_EVENT_DISCONNECTED!\r\n"
#define MQTT_SUB_STRING "MQTT_EVENT_SUBSCRIBED, msg_id=%d;\r\n"
#define MQTT_UNSUB_STRING "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d;\r\n"
#define MQTT_PUB_STRING "MQTT_EVENT_PUBLISHED, msg_id=%d;\r\n"
#define MQTT_DATA_STRING "MQTT_EVENT_DATA!\r\n"
#define MQTT_ERROR_STRING "MQTT_EVENT_ERROR!\r\n"
#define MQTT_TOPIC_TOO_LONG_HINT "The length of TOPIC exceeds the length of the print buffer, so do't printf!\r\n"
#define MQTT_DATA_TOO_LONG_HINT "The length of DATA exceeds the length of the print buffer, so do't printf!\r\n"

#define PRINT_STRING_MAX_LEN (CLI_PRINT_BUFFER_SIZE - 32) 

#define MQTT_CLIENT_CHECK_RETURN(condition, ret, logstr) \
    do { \
        if ((condition) != 0) { \
            if ((logstr) != NULL) { \
                os_printf(LM_APP, LL_ERR, "%s[%d]%s, ret %d\n", __FUNCTION__, __LINE__, logstr, ret); \
            } \
            return ret; \
        } \
    } while (0)

extern uint16_t g_mqtt_cli_port;

typedef enum{
    MQTT_PUB_SUCCESS = 0,
    MQTT_PUB_FAILED   
}MQTT_PUB_STATUS_E;

typedef void (* mqtt_pub_callback_t)(MQTT_PUB_STATUS_E pub_status);
typedef struct trs_mqtt_client* trs_mqtt_client_handle_t;

/**
 * @brief MQTT event types.
 *
 * User event handler receives context data in `trs_mqtt_event_t` structure with
 *  - `user_context` - user data from `trs_mqtt_client_config_t`
 *  - `client` - mqtt client handle
 *  - various other data depending on event type
 *
 */
typedef enum trs_mqtt_event_id_t {
    MQTT_EVENT_ANY = -1,
    MQTT_EVENT_ERROR = 0,
    MQTT_EVENT_CONNECTED,          /*!< connected event, additional context: session_present flag */
    MQTT_EVENT_DISCONNECTED,       /*!< disconnected event */
    MQTT_EVENT_SUBSCRIBED,         /*!< subscribed event, additional context: msg_id */
    MQTT_EVENT_UNSUBSCRIBED,       /*!< unsubscribed event */
    MQTT_EVENT_PUBLISHED,          /*!< published event, additional context:  msg_id */
    MQTT_EVENT_DATA,               /*!< data event, additional context:
                                        - msg_id               message id
                                        - topic                pointer to the received topic
                                        - topic_len            length of the topic
                                        - data                 pointer to the received data
                                        - data_len             length of the data for this event
                                        - current_data_offset  offset of the current data for this event
                                        - total_data_len       total length of the data received
                                         */
    MQTT_EVENT_BEFORE_CONNECT, /*!< The event occurs before connecting */
    MQTT_EVENT_DELETED,        /*!< Notification on delete of one message from the
                                internal outbox,        if the message couldn't have been sent
                                and acknowledged before expiring        defined in
                                OUTBOX_EXPIRED_TIMEOUT_MS.        (events are not posted upon
                                deletion of successfully acknowledged messages)
                                  - This event id is posted only if
                                MQTT_REPORT_DELETED_MESSAGES==1
                                  - Additional context: msg_id (id of the deleted
                                message).
                                  */
    MQTT_USER_EVENT,            /*!< Custom event used to queue tasks into mqtt event handler
                                 All fields from the trs_mqtt_event_t type could be used to pass
                                 an additional context data to the handler.
                                 */
} trs_mqtt_event_id_t;

/**
 * *MQTT* connection error codes propagated via ERROR event
 */
typedef enum trs_mqtt_connect_return_code_t {
    MQTT_CONNECTION_ACCEPTED = 0,    /*!< Connection accepted  */
    MQTT_CONNECTION_REFUSE_PROTOCOL, /*!< *MQTT* connection refused reason: Wrong
                                      protocol */
    MQTT_CONNECTION_REFUSE_ID_REJECTED, /*!< *MQTT* connection refused reason: ID
                                         rejected */
    MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE, /*!< *MQTT* connection refused
                                                reason: Server unavailable */
    MQTT_CONNECTION_REFUSE_BAD_USERNAME,  /*!< *MQTT* connection refused reason:
                                           Wrong user */
    MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED /*!< *MQTT* connection refused reason:
                                           Wrong username or password */
} trs_mqtt_connect_return_code_t;

/**
 * *MQTT* connection error codes propagated via ERROR event
 */
typedef enum trs_mqtt_error_type_t {
    MQTT_ERROR_TYPE_NONE = 0,
    MQTT_ERROR_TYPE_TCP_TRANSPORT,
    MQTT_ERROR_TYPE_CONNECTION_REFUSED,
    MQTT_ERROR_TYPE_SUBSCRIBE_FAILED
} trs_mqtt_error_type_t;


typedef enum {
    MQTT_TRANSPORT_UNKNOWN = 0x0,
    MQTT_TRANSPORT_OVER_TCP,      /*!< MQTT over TCP, using scheme: ``mqtt`` */
    MQTT_TRANSPORT_OVER_SSL,      /*!< MQTT over SSL, using scheme: ``mqtts`` */
    MQTT_TRANSPORT_OVER_WS,       /*!< MQTT over Websocket, using scheme:: ``ws`` */
    MQTT_TRANSPORT_OVER_WSS       /*!< MQTT over Websocket Secure, using scheme: ``wss`` */
} trs_mqtt_transport_t;

/**
 *  *MQTT* protocol version used for connection
 */
typedef enum trs_mqtt_protocol_ver_t {
    MQTT_PROTOCOL_UNDEFINED = 0,
    MQTT_PROTOCOL_V_3_1,
    MQTT_PROTOCOL_V_3_1_1,
    MQTT_PROTOCOL_V_5,
} trs_mqtt_protocol_ver_t;

/**
 * @brief *MQTT* error code structure to be passed as a contextual information
 * into ERROR event
 *
 * Important: This structure extends `trs_tls_last_error` error structure and is
 * backward compatible with it (so might be down-casted and treated as
 * `trs_tls_last_error` error, but recommended to update applications if used
 * this way previously)
 *
 * Use this structure directly checking error_type first and then appropriate
 * error code depending on the source of the error:
 *
 * | error_type | related member variables | note |
 * | MQTT_ERROR_TYPE_TCP_TRANSPORT | trs_tls_last_trs_err, trs_tls_stack_err,
 * trs_tls_cert_verify_flags, sock_errno | Error reported from
 * tcp_transport/trs-tls | | MQTT_ERROR_TYPE_CONNECTION_REFUSED |
 * connect_return_code | Internal error reported from *MQTT* broker on
 * connection |
 */
typedef struct trs_mqtt_error_codes {
    /* compatible portion of the struct corresponding to struct trs_tls_last_error
     */
    int trs_tls_last_trs_err; /*!< last trs_err code reported from trs-tls
                                     component */
    int trs_tls_stack_err; /*!< tls specific error code reported from underlying
                            tls stack */
    int trs_tls_cert_verify_flags; /*!< tls flags reported from underlying tls
                                    stack during certificate verification */
    /* trs-mqtt specific structure extension */
    trs_mqtt_error_type_t
    error_type; /*!< error type referring to the source of the error */
    trs_mqtt_connect_return_code_t
    connect_return_code; /*!< connection refused error code reported from
                              *MQTT* broker on connection */
    /* tcp_transport extension */
    int trs_transport_sock_errno; /*!< errno from the underlying socket */

} trs_mqtt_error_codes_t;

/**
 * MQTT event configuration structure
 */
typedef struct trs_mqtt_event_t {
    trs_mqtt_event_id_t event_id;       /*!< MQTT event type */
    trs_mqtt_client_handle_t client;    /*!< MQTT client handle for this event */
    //void *user_context;                 /*!< User context passed from MQTT client config */
    char *data;                         /*!< Data asociated with this event */
    int data_len;                       /*!< Lenght of the data for this event */
    int total_data_len;                 /*!< Total length of the data (longer data are supplied with multiple events) */
    int current_data_offset;            /*!< Actual offset for the data asociated with this event */
    char *topic;                        /*!< Topic asociated with this event */
    int topic_len;                      /*!< Length of the topic for this event asociated with this event */
    int msg_id;                         /*!< MQTT messaged id of message */
    int session_present;                /*!< MQTT session_present flag for connection event */
    trs_mqtt_error_codes_t
    *error_handle; /*!< trs-mqtt error handle including trs-tls errors as well
                        as internal *MQTT* errors */
    bool retain; /*!< Retained flag of the message associated with this event */
    int qos;     /*!< QoS of the messages associated with this event */
    bool dup;    /*!< dup flag of the message associated with this event */
    trs_mqtt_protocol_ver_t protocol_ver;   /*!< MQTT protocol version used for connection, defaults to value from menuconfig*/
#ifdef CONFIG_MQTT_PROTOCOL_5
    trs_mqtt5_event_property_t *property; /*!< MQTT 5 property associated with this event */
#endif

} trs_mqtt_event_t;

typedef trs_mqtt_event_t* trs_mqtt_event_handle_t;

typedef int (* mqtt_event_callback_t)(trs_mqtt_event_handle_t event);

/**
 * *MQTT* client configuration structure
 *
 *  - Default values can be set via menuconfig
 *  - All certificates and key data could be passed in PEM or DER format. PEM format must have a terminating NULL
 *    character and the related len field set to 0. DER format requires a related len field set to the correct length.
 */
typedef struct trs_mqtt_client_config_t {
    /**
     *   Callback
     */
	mqtt_event_callback_t event_handle; /*!< handle for MQTT events */
    mqtt_pub_callback_t pub_handle;     /*!< handle for pub success or failed */

    /**
     *   Broker address
     */
    const char *uri;                /*!< Complete *MQTT* broker URI */
    const char *host;               /*!< Hostname, to set ipv4 pass it as string) */
    trs_mqtt_transport_t transport; /*!< Selects transport*/
    const char *path;               /*!< Path in the URI*/
    uint32_t port;                  /*!< *MQTT* server port */

    /**
     * Broker identity verification
     *
     * If fields are not set broker's identity isn't verified. it's recommended
     * to set the options in this struct for security reasons.
     */
    bool use_global_ca_store;                   /*!< Use a global ca_store, look trs-tls documentation for details. */
    int (*crt_bundle_attach)(void *conf); /*!< Pointer to ESP x509 Certificate Bundle attach function for
                                                   the usage of certificate bundles. */
    const char *cert_pem;                    /*!< Certificate data, default is NULL, not required to verify the server. */
    size_t cert_pem_len;                     /*!< Length of the buffer pointed to by certificate. */
    const struct psk_key_hint *psk_hint_key;    /*!< Pointer to PSK struct defined in trs_tls.h to enable PSK
                                                   authentication (as alternative to certificate verification).
                                                   PSK is enabled only if there are no other ways to
                                                   verify broker.*/
    bool skip_cert_common_name_check;           /*!< Skip any validation of server certificate CN field, this reduces the
                                                   security of TLS and makes the *MQTT* client susceptible to MITM attacks  */
    const char *common_name;
    const char **alpn_protos;                   /*!< NULL-terminated list of supported application protocols to be used for ALPN */

    /**
     * Client related credentials for authentication.
     */
    const char *username;    /*!< *MQTT* username */
    const char *client_id;   /*!< Set *MQTT* client identifier. Ignored if set_null_client_id == true If NULL set
                                the default client id. Default client id is ``TRS32_%CHIPID%`` where `%CHIPID%` are
                                last 3 bytes of MAC address in hex format */
    bool set_null_client_id; /*!< Selects a NULL client id */

    /**
     * Client authentication
     *
     * Fields related to client authentication by broker
     *
     * For mutual authentication using TLS, user could select certificate and key,
     * secure element or digital signature peripheral if available.
     *
     */
    const char *password;            /*!< *MQTT* password */
    const char *client_cert_pem;     /*!< Certificate for ssl mutual authentication, not required if mutual
                                     authentication is not needed. Must be provided with `key`.*/
    size_t client_cert_pem_len;      /*!< Length of the buffer pointed to by certificate.*/
    const char *client_key_pem;      /*!< Private key for SSL mutual authentication, not required if mutual authentication
                                     is not needed. If it is not NULL, also `certificate` has to be provided.*/
    size_t client_key_pem_len;       /*!< Length of the buffer pointed to by key.*/
    const char *client_key_password; /*!< Client key decryption password, not PEM nor DER, if provided
                                     `key_password_len` must be correctly set. */
    int client_key_password_len;     /*!< Length of the password pointed to by `key_password` */
    bool use_secure_element;         /*!< Enable secure element, available in ESP32-ROOM-32SE, for SSL connection */
    void *ds_data;                   /*!< Carrier of handle for digital signature parameters, digital signature peripheral is
                                     available in some Espressif devices. */

    /**
     * *MQTT* Session related configuration
     */
    int clean_session;         /*!< *MQTT* clean session, default clean_session is true */
    int keepalive;              /*!< *MQTT* keepalive, default is 120 seconds */
    bool disable_keepalive;     /*!< Set `disable_keepalive=true` to turn off keep-alive mechanism, keepalive is active
                                 by default. Note: setting the config value `keepalive` to `0` doesn't disable
                                 keepalive feature, but uses a default keepalive period */
    trs_mqtt_protocol_ver_t protocol_ver; /*!< *MQTT* protocol version used for connection.*/
    int message_retransmit_timeout;       /*!< timeout for retransmitting of failed packet */

    /**
     * Last Will and Testament message configuration.
     */
    const char *lwt_topic; /*!< LWT (Last Will and Testament) message topic */
    const char *lwt_msg;   /*!< LWT message, may be NULL terminated*/
    int lwt_msg_len;       /*!< LWT message length, if msg isn't NULL terminated must have the correct length */
    int lwt_qos;           /*!< LWT message QoS */
    int lwt_retain;        /*!< LWT retained message flag */

    /**
     * Network related configuration
     */
    int reconnect_timeout_ms;        /*!< Reconnect to the broker after this value in miliseconds if auto reconnect is not
                                     disabled (defaults to 10s) */
    int timeout_ms;                  /*!< Abort network operation if it is not completed after this value, in milliseconds
                                     (defaults to 10s). */
    int refresh_connection_after_ms; /*!< Refresh connection after this value (in milliseconds) */
    int disable_auto_reconnect;     /*!< Client will reconnect to server (when errors/disconnect). Set
                                      `disable_auto_reconnect=true` to disable */

    /**
     * Client task configuration
     */
    int task_prio;   /*!< *MQTT* task priority*/
    int task_stack;  /*!< *MQTT* task stack size*/

    /**
     * Client buffer size configuration
     *
     * Client have two buffers for input and output respectivelly.
     */
    int buffer_size;     /*!< size of *MQTT* send/receive buffer*/
    int buffer_out_size; /*!< size of *MQTT* output buffer. If not defined, defaults to the size defined by ``buffer_size`` */
} trs_mqtt_client_config_t;

typedef enum
{
   CFG_MSG_TYPE_SUB = 1,
   CFG_MSG_TYPE_UNSUB,   
   CFG_MSG_TYPE_PUB, 
   CFG_MSG_TYPE_PUB_RAW, 
   CFG_MSG_TYPE_RECON,
   CFG_MSG_TYPE_DISC,
   CFG_MSG_TYPE_STOP, 
}MQTT_CFG_MSG_TYPE;

typedef struct 
{
    MQTT_CFG_MSG_TYPE cfg_type;
    const char *topic;
    int topic_len;
    const char *data;
    int data_len;
    int qos;
    int retain;
#ifdef CONFIG_MQTT_PROTOCOL_5
	mqtt5_user_property_handle_t *user_property;
#endif
} MQTT_CFG_MSG_ST;


typedef enum
{
    SET_TOPIC = 1,
    SET_DATA,
}SET_TYPE_EN;

/**
 * @brief Enum for the MQTT QOS type.
 */
typedef enum mqtt_qos_type
{
    MQTT_QOS0 = 0, /*!< MQTT QOS 0 */
    MQTT_QOS1, /*!< MQTT QOS 1 */
    MQTT_QOS2, /*!< MQTT QOS 2 */
} ECM_MQTT_QOS_TYPE_EN;

trs_mqtt_client_handle_t trs_mqtt_client_init(const trs_mqtt_client_config_t *config);
int trs_mqtt_client_set_uri(trs_mqtt_client_handle_t client, const char *uri);
int trs_mqtt_client_start(trs_mqtt_client_handle_t client);
int trs_mqtt_client_stop(trs_mqtt_client_handle_t client);
int trs_mqtt_client_subscribe(trs_mqtt_client_handle_t client, const char *topic, int qos);
int trs_mqtt_client_unsubscribe(trs_mqtt_client_handle_t client, const char *topic);
int trs_mqtt_client_publish(trs_mqtt_client_handle_t client, const char *topic, const char *data, int len, int qos, int retain);
/* Bytes still awaiting QoS acknowledgement in the client's outbox. */
int trs_mqtt_client_get_outbox_size(trs_mqtt_client_handle_t client);
int trs_mqtt_client_get_states(trs_mqtt_client_handle_t client);
char* trs_mqtt_client_get_url(trs_mqtt_client_handle_t client);
int set_mqtt_msg(SET_TYPE_EN type, MQTT_CFG_MSG_ST *msg, const char *data, int data_len);
os_queue_handle_t trs_mqtt_client_get_queue(trs_mqtt_client_handle_t client);
trs_mqtt_protocol_ver_t trs_mqtt_client_get_version(trs_mqtt_client_handle_t client);
int trs_mqtt_client_reconnect(trs_mqtt_client_handle_t client);
int trs_mqtt_client_disconnect(trs_mqtt_client_handle_t client);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif
