#include "system_wifi.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "mqtt_queue.h"
#include "cli.h"
#include "mqtt_client.h"

/****************************************************************************
* 	                                           Local Macros
****************************************************************************/
#define IS_ASCII(c) (c > 0x1F && c < 0x7F)

//#ifndef MIN
//#define MIN(x,y) (x < y ? x : y)
//#endif

#define CLIENT_ID_STRING_MAX_LEN 256//defined in Section 1.5.3
#define PRINT_STRING_MAX_LEN (CLI_PRINT_BUFFER_SIZE - 32) 

#define ALREADY_SUB "ALREADY SUBSCRIBE"
#define NO_UNSUB "NO UNSUBSCRIBE"
#define QOS_PROMPT "qos error! please use qos xx(0 or 1 or 2)\r\n"
#define CLEAN_SESSION_PROMPT "clean_session err! please use clean_session xx(0 or 1)\r\n"
#define URL_PROMPT "url err! please use mqtt_start url xx\r\n"
#define RETAIN_PROMPT "retain error! please use retain xx(0 or 1)\r\n"
#define CLIENT_ID_TOO_LONG_PROMPT "The length of ClientID must be between 1 and 23 bytes!\r\n"

/****************************************************************************
* 	                                           Local Types
****************************************************************************/

/****************************************************************************
* 	                                           Local Constants
****************************************************************************/

/****************************************************************************
* 	                                           Local Function Prototypes
****************************************************************************/

/****************************************************************************
* 	                                          Global Constants
****************************************************************************/

/****************************************************************************
* 	                                          Global Variables
****************************************************************************/

/****************************************************************************
* 	                                          Global Function Prototypes
****************************************************************************/

/****************************************************************************
* 	                                          Function Definitions
****************************************************************************/

#include "mqtt_client.h"
#include "platform_tr6600.h"
#include "os.h"

trs_mqtt_client_handle_t mqtt_client = NULL;
trs_mqtt_client_config_t mqtt_cfg = {0};

int g_MQTT_EVENT_CONNECTED = 0;
int g_MQTT_EVENT_DISCONNECTED = 0;
int g_MQTT_EVENT_SUBSCRIBED = 0;
int g_MQTT_EVENT_UNSUBSCRIBED = 0;
int g_MQTT_EVENT_PUBLISHED = 0;
int g_MQTT_EVENT_DATA = 0;
int g_MQTT_EVENT_ERROR = 0;

int g_MQTT_PING_SEND_OK = 0;
int g_MQTT_PING_SEND_NO = 0;
int g_MQTT_PING_OK = 0;
int g_MQTT_PING_NO = 0;
int g_mqtt = 0;

typedef enum
{
    AT_MQTT_RET_OK = 0,
    AT_MQTT_RET_ERR,
}AT_MQTT_RET_E;

typedef struct SUB_TOPIC_ITEM {
    char *topic;
    int qos;
    STAILQ_ENTRY(SUB_TOPIC_ITEM) next;
} SUB_TOPIC_ITEM_T;

STAILQ_HEAD(SUB_TOPIC_ITEM_LIST_T, SUB_TOPIC_ITEM);
typedef struct SUB_TOPIC_ITEM_LIST_T * sub_topic_list_handle_t;
typedef struct SUB_TOPIC_ITEM * sub_topic_item_handle_t;
static sub_topic_list_handle_t at_mqtt_sub_list = NULL;

static sub_topic_list_handle_t at_mqtt_sub_list_init()
{
    sub_topic_list_handle_t list = mqtt_calloc(1, sizeof(struct SUB_TOPIC_ITEM_LIST_T));
    MEM_CHECK(list, return NULL);
    STAILQ_INIT(list);
    return list;
}
static sub_topic_item_handle_t at_mqtt_sub_topic_dequeue(sub_topic_list_handle_t head)
{
    sub_topic_item_handle_t item = NULL;
    sub_topic_item_handle_t tmp = NULL;
    STAILQ_FOREACH_SAFE(item, head, next, tmp) {
        os_printf(LM_APP, LL_DBG, "at_mqtt_sub_topic_dequeue(), item =%p\r\n", item);
        if (!STAILQ_EMPTY(head))
        {
            return item;
        }
        else
        {
            break;
        }
    }
    return NULL;
}

static void at_mqtt_sub_list_destroy(sub_topic_list_handle_t head)
{
    sub_topic_item_handle_t item = NULL;
    do{
        item = at_mqtt_sub_topic_dequeue(head);
        if (item == NULL) {
            break;
        }
        STAILQ_REMOVE(head, item, SUB_TOPIC_ITEM, next);
        mqtt_free(item->topic);
        mqtt_free(item);
    }while(item != NULL); 
    // don't delete head
}
static AT_MQTT_RET_E at_mqtt_sub_topic_query(sub_topic_list_handle_t head, const char * topic, int *qos)
{
    sub_topic_item_handle_t item = NULL;
    sub_topic_item_handle_t tmp = NULL;
    STAILQ_FOREACH_SAFE(item, head, next, tmp) {
        os_printf(LM_APP, LL_DBG, "at_mqtt_sub_topic_query() =%p\r\n", item);
        if (strcmp(item->topic, topic) == 0) {
            *qos = item->qos;
            os_printf(LM_APP, LL_DBG, "AT MQTT:query topic =%s. qos = %d\r\n", item->topic, item->qos);
            return AT_MQTT_RET_OK;
        }
    }
    return AT_MQTT_RET_ERR;
}
static sub_topic_item_handle_t  at_mqtt_sub_topic_enqueue(sub_topic_list_handle_t head, const char *topic, int qos)
{
    if ((head == NULL) || (topic == NULL))
    {
        return NULL;
    }

    int topic_len = strlen((char *)topic);
    os_printf(LM_APP, LL_DBG, "AT MQTT:ENQUEUE sizeof(SUB_TOPIC_ITEM_T)s=%d.\r\n", sizeof(SUB_TOPIC_ITEM_T));
    sub_topic_item_handle_t item = mqtt_malloc(sizeof(SUB_TOPIC_ITEM_T));
    os_printf(LM_APP, LL_DBG, "AT MQTT:ENQUEUE sizeof(SUB_TOPIC_ITEM_T)s=%d. item:%p\r\n", sizeof(SUB_TOPIC_ITEM_T), item);
    memset(item, 0, sizeof(SUB_TOPIC_ITEM_T));
    MEM_CHECK(item, return NULL);
    item->qos = qos;
    item->topic = mqtt_malloc(topic_len + 1);
    MEM_CHECK(item->topic, {
        mqtt_free(item);
        return NULL;
    });
    memset(item->topic, 0, topic_len + 1);
    memcpy(item->topic, topic, topic_len);
    STAILQ_INSERT_TAIL(head, item, next);
    os_printf(LM_APP, LL_DBG, "AT MQTT:ENQUEUE topic =%s, qos=%d.\r\n", item->topic, item->qos);
    return item;
}
static AT_MQTT_RET_E at_mqtt_sub_topic_delete(sub_topic_list_handle_t head, const char * topic, int topic_len)
{
    sub_topic_item_handle_t item = NULL;
    sub_topic_item_handle_t tmp = NULL;
    if (topic_len == 0)
    {
        topic_len = strlen(topic);
    }
    
    STAILQ_FOREACH_SAFE(item, head, next, tmp) {
        os_printf(LM_APP, LL_INFO, "at_mqtt_sub_topic_delete() =%p\r\n", item);
        if (memcmp(item->topic, topic, topic_len) == 0) {
            STAILQ_REMOVE(head, item, SUB_TOPIC_ITEM, next);
            mqtt_free(item->topic);
            mqtt_free(item);
            os_printf(LM_APP, LL_INFO, "AT MQTT:DELETED topic =%s.\r\n", topic);
            return AT_MQTT_RET_OK;
        }
    }
    
    return AT_MQTT_RET_ERR;
}

struct os_time g_MQTT_disconnected_time = 
{
	.sec = 0,
	.usec = 0
};

void liuyongTime(void *env)
{
	os_printf(LM_APP, LL_INFO, "MQTT PING\r\n\tSEND_OK:%d\r\n\tSEND_NO:%d\r\n\tOK:%d\r\n\tNO:%d\r\n", g_MQTT_PING_SEND_OK, g_MQTT_PING_SEND_NO, g_MQTT_PING_OK, g_MQTT_PING_NO);
	os_printf(LM_APP, LL_INFO, "MQTT EVENT\r\n\tCON:%d\r\n\tDISCON:%d\r\n\tSUBSCRIBED:%d\r\n\tUNSUBSCRIBED:%d\r\n\tPUBLISHED:%d\r\n\tD:%d\r\n\tE:%d\r\n", 
		g_MQTT_EVENT_CONNECTED, g_MQTT_EVENT_DISCONNECTED, g_MQTT_EVENT_SUBSCRIBED, g_MQTT_EVENT_UNSUBSCRIBED, 
		g_MQTT_EVENT_PUBLISHED, g_MQTT_EVENT_DATA, g_MQTT_EVENT_ERROR);
}

os_timer_handle_t liuyong_timer_handle = NULL;

static int default_mqtt_handle(trs_mqtt_event_handle_t event)
{
    //trs_mqtt_client_handle_t client = event->client;
    //int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
			if(g_mqtt == 0)
			{
				g_MQTT_EVENT_CONNECTED += 1;
				g_mqtt = 1;
			}
            os_printf(LM_APP, LL_INFO, "MQTT_EVENT_CONNECTED:%d\n", g_MQTT_EVENT_CONNECTED);
			if(g_MQTT_disconnected_time.sec!=0 || g_MQTT_disconnected_time.usec!=0)
			{
				struct os_time now_time;
				os_get_time(&now_time);
				long s = now_time.sec - g_MQTT_disconnected_time.sec;
				long us = now_time.usec - g_MQTT_disconnected_time.usec;
				os_printf(LM_APP, LL_INFO, "* - * - * - * - * - mqtt test - * - * - * - * - * - *\n");
				os_printf(LM_APP, LL_INFO, "reconnect time= %d s %d ms %d us\n", s, (int)(us/1000), us%1000);
				g_MQTT_disconnected_time.sec=0;
				g_MQTT_disconnected_time.usec=0;
			}
            break;
			
        case MQTT_EVENT_DISCONNECTED:
			if(g_mqtt == 1)
			{
				g_MQTT_EVENT_DISCONNECTED += 1;
				g_mqtt = 0;
			}
            os_printf(LM_APP, LL_INFO, "MQTT_EVENT_DISCONNECTED:%d\n", g_MQTT_EVENT_DISCONNECTED);
			if(g_MQTT_disconnected_time.sec==0 && g_MQTT_disconnected_time.usec==0)
			{
				os_get_time(&g_MQTT_disconnected_time);
			}
            break;

        case MQTT_EVENT_SUBSCRIBED:
			g_MQTT_EVENT_SUBSCRIBED++;
            os_printf(LM_APP, LL_INFO, "MQTT_EVENT_SUBSCRIBED:%d, msg_id=%d\n", g_MQTT_EVENT_SUBSCRIBED, event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
			g_MQTT_EVENT_UNSUBSCRIBED++;
            os_printf(LM_APP, LL_INFO, "MQTT_EVENT_UNSUBSCRIBED:%d, msg_id=%d\n", g_MQTT_EVENT_UNSUBSCRIBED, event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
			g_MQTT_EVENT_PUBLISHED++;
            os_printf(LM_APP, LL_INFO, "MQTT_EVENT_PUBLISHED:%d, msg_id=%d\n", g_MQTT_EVENT_PUBLISHED, event->msg_id);
            break;
        case MQTT_EVENT_DATA:
			g_MQTT_EVENT_DATA++;
            os_printf(LM_APP, LL_INFO, "MQTT_EVENT_DATA:%d\n", g_MQTT_EVENT_DATA);
            os_printf(LM_APP, LL_INFO, "TOPIC_len=%d, DATA_len= %d\r\n", event->topic_len, event->data_len);
            if (event->topic_len < PRINT_STRING_MAX_LEN)
            {
                os_printf(LM_APP, LL_INFO, "TOPIC=%.*s\r\n", event->topic_len, event->topic);
            }
            else
            {
                os_printf(LM_APP, LL_INFO, "The length of TOPIC exceeds the length of the print buffer, so do't printf!\r\n");
            }

            if (event->data_len < PRINT_STRING_MAX_LEN)
            {
                if (event->data_len == 0)
                {
                    os_printf(LM_APP, LL_INFO, "DATA is null!\r\n");
                }
                else
                {
                    os_printf(LM_APP, LL_INFO, "DATA=%.*s\r\n", event->data_len, event->data);
                }
            }
            else
            {
                os_printf(LM_APP, LL_INFO, "The length of DATA exceeds the length of the print buffer, so do't printf!\r\n");
            }
            
            break;
        case MQTT_EVENT_ERROR:
			g_MQTT_EVENT_ERROR++;
            os_printf(LM_APP, LL_INFO, "MQTT_EVENT_ERROR:%d\n", g_MQTT_EVENT_ERROR);
            break;

        case MQTT_EVENT_ANY:
            os_printf(LM_CMD, LL_DBG, "MQTT_EVENT_ANY\r\n");
            break;
        case MQTT_EVENT_BEFORE_CONNECT:
            os_printf(LM_CMD, LL_DBG, "MQTT_EVENT_BEFORE_CONNECT\r\n");
            break;
        case MQTT_EVENT_DELETED:
            os_printf(LM_CMD, LL_DBG, "MQTT_EVENT_DELETED\r\n");
            break;
        case MQTT_USER_EVENT:
            os_printf(LM_CMD, LL_DBG, "MQTT_USER_EVENT\r\n");
            break;
        default:
            os_printf(LM_CMD, LL_ERR, "mqtt_event_id is err(%d)!\r\n", event->event_id);
            break;
    }
    return 0;
}


extern const uint8_t server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");

uint16_t g_mqtt_cli_port = 0;
int mqtt_client_port(   int argc, char *argv[])
{
    int i = 1;
    if ((argc == 3) && (strcmp(argv[i], "local_port") == 0))
    {
        if ((atoi(argv[i+1]) >= 49152) && (atoi(argv[i+1]) < 65535))
        {
            g_mqtt_cli_port = atoi(argv[i+1]);
            os_printf(LM_APP, LL_INFO, "mqtt client local port parameters g_mqtt_client_port is %d\r\n", g_mqtt_cli_port);
        }
        else
        {
            os_printf(LM_CMD, LL_ERR, "mqtt client local port number is wrong! \r\n");
            return -1;
        }
    }
    else
    {
        os_printf(LM_CMD, LL_ERR, "mqtt set client local port parameters is wrong! \r\n");
        return -1;
    }
    return 0;
}

CLI_CMD(mqtt_client_port, mqtt_client_port, "", "");

int mqtt_client_get_port(   int argc, char *argv[])
{
    if(argc != 1)
	{
		os_printf(LM_CMD, LL_ERR, "mqtt get client local port parameters is wrong! \r\n");
		return -1;
	}
	else
	{
		os_printf(LM_APP, LL_INFO, "cur_mqtt_client_port: %d\r\n", g_mqtt_cli_port);
		return 0;
	}

}
CLI_CMD(mqtt_client_get_port, mqtt_client_get_port, "", "");


int mqtt_start(   int argc, char *argv[])
{
    uint8_t i;
    void * p_tmp = NULL;
    char *topic = NULL;
    char *data = NULL;
    int qos = 0;
    int retain = 0;
    memset(&mqtt_cfg,0,sizeof(trs_mqtt_client_config_t));
    mqtt_cfg.event_handle = default_mqtt_handle;

    at_mqtt_sub_list = at_mqtt_sub_list_init();
    MEM_CHECK(at_mqtt_sub_list, {
        return CMD_RET_FAILURE;
    });

#if 0
    mqtt_cfg.cert_pem = (const char *)server_root_cert_pem_start;
#endif
    if ((7 <= argc ) && ( argc <= 23))
    {
        for(i=1; i<argc; i+=2)
        {
            if(strcmp(argv[i], "uri")==0)
            {
                mqtt_cfg.uri = argv[i+1];
                if((strncasecmp("mqtts://", argv[i+1], 8) != 0)
                   &&(strncasecmp("mqtt://", argv[i+1], 7) != 0)
                   &&(strncasecmp("tcp://", argv[i+1], 6) != 0)
                   &&(strncasecmp("ssl://", argv[i+1], 6) != 0)
                   &&(strncasecmp("tls://", argv[i+1], 6) != 0))
                {
                    mqtt_cfg.transport = MQTT_TRANSPORT_UNKNOWN;
                    os_printf(LM_APP, LL_ERR, "ERROR MQTT_TRANSPORT_UNKNOWN\r\n");
                    return CMD_RET_FAILURE;
                }
            }
            else if(strcmp(argv[i], "port")==0)
            {
                if((atoi(argv[i+1]) >= 0) && (atoi(argv[i+1]) < 65535))
                {
                    mqtt_cfg.port = atoi(argv[i+1]);
                }
                else
                {
                    os_printf(LM_APP, LL_ERR,"mqtt_start parameters port is wrong! \r\n");
                    return CMD_RET_FAILURE;
                }

            }
            else if(strcmp(argv[i], "client_id")==0)
            {
                mqtt_cfg.client_id = (const char*)argv[i+1];
            }
            else if(strcmp(argv[i], "username")==0)
            {
                mqtt_cfg.username = (const char*)argv[i+1];
            }
            else if(strcmp(argv[i], "password")==0)
            {
                mqtt_cfg.password = (const char*)argv[i+1];
            }
            else if(strcmp(argv[i], "clean_session")==0)
            {
                mqtt_cfg.clean_session = atoi(argv[i+1]);
                if ((mqtt_cfg.clean_session < 0) || (mqtt_cfg.clean_session > 1))
                {
                    os_printf(LM_CMD, LL_INFO, CLEAN_SESSION_PROMPT);
                    return CMD_RET_FAILURE;
                }
            }
            else if(strcmp(argv[i], "keepalive")==0)
            {
                if((atoi(argv[i+1]) >= 0) && (atoi(argv[i+1]) <= 7200))
                {
                    mqtt_cfg.keepalive = atoi(argv[i+1]);
                }
                else
                {
                    os_printf(LM_APP, LL_ERR,"mqtt_start parameters keepalive is wrong! \r\n");
                    return CMD_RET_FAILURE;
                }
            }
            else if(strcmp(argv[i], "topic")==0)
            {
                topic = argv[i+1];
                int topic_len = strlen(topic);
                if ((topic_len <= 0) || (topic_len >= PRINT_STRING_MAX_LEN))
                {
                    os_printf(LM_APP, LL_INFO, "topic long is error\r\n");
                    return CMD_RET_FAILURE;
                }
                p_tmp = mqtt_malloc(topic_len + 1);
                MEM_CHECK(p_tmp, return -1);
                memset(p_tmp, 0 , topic_len + 1);
                memcpy(p_tmp, topic, topic_len);
                mqtt_cfg.lwt_topic = p_tmp;
            }
            else if(strcmp(argv[i], "data")==0)
            {
                if(strcmp(argv[i+1], "qos")==0)
                {
                    data = "";
                    i -=1;
                }
                else
                {
                    data = argv[i+1];
                    int msg_len = strlen(data);
                    if (msg_len < 0 || msg_len >= PRINT_STRING_MAX_LEN)
                    {
                        os_printf(LM_APP, LL_INFO, "data long is error\r\n");
                        return CMD_RET_FAILURE;
                    }
                    p_tmp = mqtt_malloc(msg_len + 1);
                    MEM_CHECK(p_tmp, return -1);
                    memset(p_tmp, 0 , msg_len + 1);
                    memcpy(p_tmp, data, msg_len);
                    mqtt_cfg.lwt_msg = p_tmp;
                    mqtt_cfg.lwt_msg_len = msg_len;
                }
            }
            else if(strcmp(argv[i], "qos")==0)
            {
                qos = atoi(argv[i+1]);
                if ((qos < MQTT_QOS0) || (qos > MQTT_QOS2))
                {
                    os_printf(LM_CMD, LL_INFO, QOS_PROMPT);
                    return CMD_RET_FAILURE;
                }
                mqtt_cfg.lwt_qos = qos;
            }
            else if(strcmp(argv[i], "retain")==0)
            {
                retain = atoi(argv[i+1]);
                if ((retain != 0) && (retain != 1))
                {
                    os_printf(LM_CMD, LL_INFO, RETAIN_PROMPT);
                    return CMD_RET_FAILURE;
                }
                mqtt_cfg.lwt_retain = retain;
            }
            else
            {
                os_printf(LM_CMD, LL_INFO, "mqtt_start parameters is wrong! \r\n");
                return CMD_RET_FAILURE;
            }
        }
    }
    else
    {
        os_printf(LM_CMD, LL_INFO, "mqtt_start parameters number is wrong! \r\n");
        return CMD_RET_FAILURE;
    }

    if (mqtt_client != NULL)
    {
        os_printf(LM_CMD, LL_INFO, "mqtt has started!\r\n");
        return CMD_RET_SUCCESS;
    }

    if (mqtt_cfg.uri == NULL)  // must set url
    {
        os_printf(LM_CMD, LL_INFO, URL_PROMPT);
        return CMD_RET_FAILURE;
    }

    int client_id_len = strlen(mqtt_cfg.client_id);
    if (client_id_len > CLIENT_ID_STRING_MAX_LEN)
    {
        os_printf(LM_CMD, LL_INFO, CLIENT_ID_TOO_LONG_PROMPT);
        return CMD_RET_FAILURE;
    }

    os_printf(LM_APP, LL_INFO, "In mqtt_start(),mqtt_cfg.url = %s\r\n",mqtt_cfg.uri);
    mqtt_client = trs_mqtt_client_init(&mqtt_cfg);
    if(mqtt_client == NULL){
        os_printf(LM_APP, LL_INFO, "mqtt init failed\n");
        return CMD_RET_FAILURE;
    }
    if (0 != trs_mqtt_client_start(mqtt_client)){
        os_printf(LM_APP, LL_INFO, "mqtt start failed\r\n");
        mqtt_client = NULL;
        return CMD_RET_FAILURE;
    }
	os_printf(LM_APP, LL_INFO, "mqtt start done\n");

	if(liuyong_timer_handle == NULL)
	{
		os_printf(LM_CMD, LL_INFO, "* os_timer_create mqtt timer *\n");
		extern void liuyongTime(void *env);
		liuyong_timer_handle = os_timer_create("liuyongTime", 60000, 1, liuyongTime, NULL);
		os_timer_start(liuyong_timer_handle);
	}
    return CMD_RET_SUCCESS;
}

//CLI_SUBCMD(test,mqtt_start,mqtt_start, "mqtt_test", "mqtt_test");
CLI_CMD(mqtt_start,mqtt_start, "mqtt_test", "mqtt_test");

int mqtt_stop(   int argc, char *argv[])
{
    if (at_mqtt_sub_list != NULL)
    {
        at_mqtt_sub_list_destroy(at_mqtt_sub_list);
    }

    if (mqtt_client == NULL)
    {
        os_printf(LM_APP, LL_INFO, "not start mqtt\n");
        return CMD_RET_FAILURE;
    }
    os_queue_handle_t cfg_queue_handle = trs_mqtt_client_get_queue(mqtt_client);
    if (cfg_queue_handle != NULL)
    {
    MQTT_CFG_MSG_ST msg_send = {0};
    msg_send.cfg_type = CFG_MSG_TYPE_STOP;
        if(os_queue_send(cfg_queue_handle, (char *)&msg_send, sizeof(msg_send), 0) == 0)
        {
    mqtt_client = NULL;
        }
    }

    if(liuyong_timer_handle != NULL)
    {
        int ret = os_timer_delete(liuyong_timer_handle);
        if (ret == 0)
        {
            os_printf(LM_CMD, LL_INFO, "* delete mqtt timer success *\n");
        }
        else
        {
            os_printf(LM_CMD, LL_INFO, "* delete mqtt timer failed *\n");
        }
        liuyong_timer_handle = NULL;
    }

    return CMD_RET_SUCCESS;
}

CLI_CMD(mqtt_stop, mqtt_stop, "", "");

int mqtt_sub(   int argc, char *argv[])
{
    int i = 0;
    char * topic = NULL;
    int qos = 0;
    if (mqtt_client == NULL)
    {
        os_printf(LM_APP, LL_INFO, "not start mqtt\n");
        return CMD_RET_FAILURE;
    }

    if(argc == 5)
    {
        for(i=1; i<argc; i+=2)
        {
            if(strcmp(argv[i], "topic")==0)
            {
                topic = argv[i+1];
            }
            else if(strcmp(argv[i], "qos")==0)
            {
                qos = atoi(argv[i+1]);
            }
            else
            {
                os_printf(LM_CMD, LL_INFO, "mqtt_sub  parameters is wrong! \r\n");
                return CMD_RET_FAILURE;
            }
        }
    }
    else
    {
        os_printf(LM_CMD, LL_INFO, "mqtt_sub parameters number is wrong! \r\n");
        return CMD_RET_FAILURE;
    }
    int topic_len = strlen(topic);
    if ((topic_len <= 0) || (topic_len >= PRINT_STRING_MAX_LEN))
    {
        os_printf(LM_APP, LL_INFO, "topic error\n");
        return CMD_RET_FAILURE;
    }

    if ((qos < MQTT_QOS0) || (qos > MQTT_QOS2))
    {
        os_printf(LM_CMD, LL_INFO, QOS_PROMPT);
        return CMD_RET_FAILURE;
    }

    int qos_old = 0;
    int topic_if_exit = 0;
    int ret = 0;
    if (at_mqtt_sub_list != NULL)
    {
        topic_if_exit = 0;
        if (at_mqtt_sub_topic_query(at_mqtt_sub_list, topic, &qos_old) == AT_MQTT_RET_OK)
        {
            if (qos_old == qos)
            {
                // at_output_data(ALREADY_SUB, strlen(ALREADY_SUB));
                os_printf(LM_CMD, LL_INFO, "%s\r\n", ALREADY_SUB);
                topic_if_exit = 1;
                //return 0;
            }
            else
            {
                at_mqtt_sub_topic_delete(at_mqtt_sub_list, topic, topic_len);//no unsub from server
            }
        }

        os_queue_handle_t cfg_queue_handle = trs_mqtt_client_get_queue(mqtt_client);
        MQTT_CFG_MSG_ST msg_send = {0};
        set_mqtt_msg(SET_TOPIC, &msg_send, topic, topic_len);
        msg_send.cfg_type = CFG_MSG_TYPE_SUB;
        msg_send.qos = qos;
        if (cfg_queue_handle != NULL)
        {
        os_queue_send(cfg_queue_handle, (char *)&msg_send, sizeof(msg_send), 0);
        }
        else
        {
            os_printf(LM_APP, LL_INFO, "mqtt_client->cfg_queue_handle is null！ \r\n");
            return CMD_RET_FAILURE;
        }

        if (ret  == AT_MQTT_RET_OK)
        {
            if(topic_if_exit == 0)
            {
               at_mqtt_sub_topic_enqueue(at_mqtt_sub_list, topic, qos);
            }
        }
        else
        {
            os_printf(LM_CMD, LL_ERR, "%s:%d: at_mqtt_send_sub_or_unsub_msg() return err!\r\n", __func__, __LINE__);
            return CMD_RET_FAILURE;
        }
    }
    else
    {
        os_printf(LM_CMD, LL_ERR, "%s:%d: at_mqtt_sub_list is null\r\n", __func__, __LINE__);
         return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}

CLI_CMD(mqtt_sub, mqtt_sub, "", "");

int mqtt_unsub(   int argc, char *argv[])
{
    int i = 0;
    char * topic = NULL;

    if (mqtt_client == NULL)
    {
        os_printf(LM_APP, LL_INFO, "not start mqtt\n");
        return CMD_RET_FAILURE;
    }
    if(argc == 3)
    {
        for(i=1; i<argc; i+=2)
        {
            if(strcmp(argv[i], "topic")==0)
            {
                topic = argv[i+1];
            }
            else
            {
                os_printf(LM_CMD, LL_INFO, "mqtt_unsub  parameters is wrong! \r\n");
                return CMD_RET_FAILURE;
            }
        }
    }
    else
    {
        os_printf(LM_CMD, LL_INFO, "mqtt_unsub parameters number is wrong! \r\n");
        return CMD_RET_FAILURE;
    }

    int topic_len = strlen(topic);
    if ((topic_len <= 0) || (topic_len >= PRINT_STRING_MAX_LEN))
    {
        os_printf(LM_APP, LL_INFO, "topic error\n");
        return CMD_RET_FAILURE;
    }

    if (at_mqtt_sub_list != NULL)
    {
        int qos = 0;
        int ret = 0;
        if (at_mqtt_sub_topic_query(at_mqtt_sub_list, topic, &qos) == AT_MQTT_RET_OK)
        {
            os_queue_handle_t cfg_queue_handle = trs_mqtt_client_get_queue(mqtt_client);
            MQTT_CFG_MSG_ST msg_send = {0};
            set_mqtt_msg(SET_TOPIC, &msg_send, topic, topic_len);
            msg_send.cfg_type = CFG_MSG_TYPE_UNSUB;
            if (cfg_queue_handle != NULL)
            {
                os_queue_send(cfg_queue_handle, (char *)&msg_send, sizeof(msg_send), 0);
            }
            else
            {
                os_printf(LM_APP, LL_INFO, "mqtt_client->cfg_queue_handle is null！ \r\n");
                return CMD_RET_FAILURE;
            }

            if (ret == AT_MQTT_RET_OK)
            {
                at_mqtt_sub_topic_delete(at_mqtt_sub_list, topic, 0);
                os_printf(LM_CMD, LL_INFO, "topic removed\r\n");
            }
            else
            {
                os_printf(LM_CMD, LL_DBG, "%s:%d: at_mqtt_send_sub_or_unsub_msg() return err!\r\n", __func__, __LINE__);
                return CMD_RET_FAILURE;
            }
        }else
        {
            os_printf(LM_CMD, LL_INFO, "%s\r\n", NO_UNSUB);
        }
    }else{
        os_printf(LM_CMD, LL_DBG, "%s:%d: at_mqtt_sub_list is null\r\n", __func__, __LINE__);
    }
    return CMD_RET_SUCCESS;
}

CLI_CMD(mqtt_unsub, mqtt_unsub, "", "");

int mqtt_pub(   int argc, char *argv[])
{
    char *topic = NULL;
    char *data = NULL;
    int qos = 0;
    int retain = 0;
    int i =0;
    if (mqtt_client == NULL)
    {
        os_printf(LM_APP, LL_INFO, "not start mqtt\n");
        return CMD_RET_FAILURE;
    }
    if(argc == 9)
    {
        for(i=1; i<argc; i+=2)
        {
            if(strcmp(argv[i], "topic")==0)
            {
                topic = argv[i+1];
            }
            else if(strcmp(argv[i], "data")==0)
            {
                data = argv[i+1];
            }
            else if(strcmp(argv[i], "qos")==0)
            {
                qos = atoi(argv[i+1]);
            }
            else if(strcmp(argv[i], "retain")==0)
            {
                retain = atoi(argv[i+1]);
            }
            else
            {
                os_printf(LM_CMD, LL_INFO, "mqtt_pub parameters is wrong! \r\n");
                return CMD_RET_FAILURE;
            }
        }
    }
    else if(argc == 8)
    {
        for(i=1; i<argc; i+=2)
        {
            if(strcmp(argv[i], "topic")==0)
            {
                topic = argv[i+1];
            }
            else if(strcmp(argv[i], "data")==0)
            {
                if(strcmp(argv[i+1], "qos")==0)
                {
                    data = "";
                    i -=1;
                }
                else
                {
                    data = argv[i+1];
                }
            }
            else if(strcmp(argv[i], "qos")==0)
            {
                qos = atoi(argv[i+1]);
            }
            else if(strcmp(argv[i], "retain")==0)
            {
                retain = atoi(argv[i+1]);
            }
            else
            {
                os_printf(LM_CMD, LL_INFO, "mqtt_pub parameters is wrong! \r\n");
                return CMD_RET_FAILURE;
            }
        }
    }

    else
    {
        os_printf(LM_CMD, LL_INFO, "mqtt_pub parameters number is wrong! \r\n");
        return CMD_RET_FAILURE;
    }

    int topic_len = strlen(topic);
    if ((topic_len <= 0) || (topic_len >= PRINT_STRING_MAX_LEN))
    {
        os_printf(LM_APP, LL_INFO, "topic long is error\r\n");
        return CMD_RET_FAILURE;
    }

    int data_len = strlen(data);
    if (data_len < 0 || data_len >= PRINT_STRING_MAX_LEN)
    {
        os_printf(LM_APP, LL_INFO, "data long is error\r\n");
        return CMD_RET_FAILURE;
    }

    if ((qos < MQTT_QOS0) || (qos > MQTT_QOS2))
    {
        os_printf(LM_CMD, LL_INFO, QOS_PROMPT);
        return CMD_RET_FAILURE;
    }

    if ((retain != 0) && (retain != 1))
    {
        os_printf(LM_CMD, LL_INFO, RETAIN_PROMPT);
        return CMD_RET_FAILURE;
    }

    MQTT_CFG_MSG_ST msg_send = {0};
    set_mqtt_msg(SET_TOPIC, &msg_send, topic, topic_len);

    if (data_len != 0)
    {
        set_mqtt_msg(SET_DATA, &msg_send, data, data_len);
    }

    os_queue_handle_t cfg_queue_handle = trs_mqtt_client_get_queue(mqtt_client);
    msg_send.qos = qos;
    msg_send.retain = retain;
    msg_send.cfg_type = CFG_MSG_TYPE_PUB;
    if (cfg_queue_handle != NULL)
    {
        os_queue_send(cfg_queue_handle, (char *)&msg_send, sizeof(msg_send), 0);
    }
    else
    {
        os_printf(LM_CMD, LL_INFO, "mqtt_client->cfg_queue_handle is null！ \r\n");
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}

CLI_CMD(mqtt_pub, mqtt_pub, "", "");

#if 0

typedef struct {
	unsigned int ipaddr;
	unsigned int mask;
	unsigned int gateway;
	unsigned int dns1;
	unsigned int dns2;
	unsigned char ssid[32];
	unsigned char psk[32];
	unsigned char ssid_len;
	unsigned char psk_len;
	unsigned char sec_type;
	unsigned char channel;
	unsigned char bssid[6];
	unsigned char reserved[2];
}wifi_network_t;


int wlan_sta_start(wifi_network_t *network)
{
    int ret;
    //ip_info_t ipInfoTmp;
    wifi_config_u config;

    memset(&config, 0, sizeof(config));
    memcpy(config.sta.ssid, network->ssid, MIN(network->ssid_len, sizeof(config.sta.ssid) - 1));
    
    if(network->psk_len > 0 && network->psk[0])
    {
        memcpy(config.sta.password, network->psk, MIN(sizeof(config.sta.password), network->psk_len));
    }
    
    ret = wifi_start_station(&config);
    if (SYS_ERR_WIFI_MODE == ret)
    {   
        return -1;
    }
    else if (SYS_ERR_WIFI_BUSY == ret)
    {
        return -1;
    }
    else
    {
        return 0;
    }
}


static int test_mqtt_create_sta(   int argc, char *argv[])
{
	wifi_network_t network;
	uint8_t i;

	if(argc < 3) return CMD_RET_FAILURE;

	//uint8_t channel = 0;
	memset((char*)&network, 0, sizeof(network));

	for(i=1; i<argc; i+=2)
	{
		if(strcmp(argv[i], "type")==0)
		{
			network.sec_type = atoi(argv[i+1]);
			if(network.sec_type>5)
				return CMD_RET_FAILURE;
		}
		else if(strcmp(argv[i], "ch")==0)
		{
			network.channel = atoi(argv[i+1]);
			if(network.channel < 1 || network.channel > 14)
				return CMD_RET_FAILURE;
			//channel = network.channel;
		}
		else if(strcmp(argv[i], "ssid")==0)
		{
			network.ssid_len = strlen(argv[i+1]);
			if(network.ssid_len < 33)
				memcpy(network.ssid, argv[i+1], network.ssid_len);
			else
				return CMD_RET_FAILURE;
		}
		else if(strcmp(argv[i], "psk")==0)
		{
			network.psk_len = strlen(argv[i+1]);
			if(network.psk_len < 33)
				memcpy(network.psk, argv[i+1], network.psk_len);
			else
				return CMD_RET_FAILURE;
		}
		else return CMD_RET_FAILURE;
	}

	if(network.ipaddr != 0)
	{
		if(network.gateway==0)
			network.gateway = (network.ipaddr&0x00FFFFFF)|0x01000000;
		if(network.mask==0)
			network.mask = 0x00FFFFFF; // 255.255.255.0
		if(network.dns1==0)
			network.dns1 = 0x72727272; // 114.114.114.114
		if(network.dns2==0)
			network.dns2 = 0x08080808; // 8.8.8.8
	}

	wlan_sta_start(&network);

	return CMD_RET_SUCCESS;
}
CLI_SUBCMD(test, create_sta, test_mqtt_create_sta, "", "");
#endif

