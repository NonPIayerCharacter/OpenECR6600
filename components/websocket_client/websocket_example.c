#include <stdio.h>
#include "cli.h"
#include "common_define.h"
#include "task.h"
#include "event_groups.h"

#include "websocket_client.h"

#define WEBSOCKET_CLIENT_NUMS 3
#define NO_DATA_TIMEOUT_SEC 20
#define CONFIG_WEBSOCKET_URI "wss://demo.piesocket.com/v3/channel_123?api_key=VCXCEuvhGcBDP7XhiJJUDvR1e1D3eiVjgZ9VRiaV&notify_self"
#define CONFIG_HEADER "AUTHORIZATION: Basic QTIzMzIyMDE5OTk6MTIzNDU2Nzg="

typedef struct WEBSOCKET_QUEUE
{
    char client_number;
    uint8_t op_code;
    char *buff;
    int buff_len;
}WEBSOCKET_QUEUE_T;

typedef struct WEBSOCKET_CFG
{
    bool status;
    websocket_client_handle_t client;
    websocket_client_config_t config;
}WEBSOCKET_CFG_T;

WEBSOCKET_CFG_T cfg[WEBSOCKET_CLIENT_NUMS] = {0};

static void websocket_event_handler(void *handler_args, event_base_t base, int32_t event_id, void *event_data)
{
    websocket_client_handle_t client= (websocket_client_handle_t)handler_args;
    websocket_event_data_t *data = (websocket_event_data_t *)event_data;
    for(int i = 0;i < WEBSOCKET_CLIENT_NUMS;i++)
    {
        if(cfg[i].client == client)
        {
            WS_LOGI("%s",cfg[i].config.uri);
        }
    }
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        WS_LOGI("WEBSOCKET_EVENT_CONNECTED");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        WS_LOGI("WEBSOCKET_EVENT_DISCONNECTED");
        break;
    case WEBSOCKET_EVENT_DATA:
        WS_LOGI("WEBSOCKET_EVENT_DATA");
        WS_LOGI("Received opcode=%d", data->op_code);
        if (data->op_code == 0x08 && data->data_len == 2) {
            WS_LOGW("Received closed message with code=%d", 256*data->data_ptr[0] + data->data_ptr[1]);
        } else {
            if(data->data_len)
            {
                WS_LOGW("Received=%.*s", data->data_len, (char *)data->data_ptr);
            }
            else
            {
                WS_LOGW("Received None data");
            }
        }
        WS_LOGW("Total payload length=%d, data_len=%d, current payload offset=%d\r\n", data->payload_len, data->data_len, data->payload_offset);
        break;
    case WEBSOCKET_EVENT_ERROR:
        WS_LOGI("WEBSOCKET_EVENT_ERROR");
        break;
    case WEBSOCKET_EVENT_CLOSED:
        WS_LOGI("WEBSOCKET_EVENT CLOSE\n");
        break;
    default:WS_LOGI("event id:%d",event_id)break;
    }
}


int get_heap( int argc, char *argv[])
{
    WS_LOGI("[APP] GET Free memory: %d bytes", xPortGetFreeHeapSize());
    return 0;
}
CLI_CMD(get_heap, get_heap, "web", "web");


int ws_cfg( int argc, char *argv[])
{
    int16_t id = 0, ping_intv_sec = 10,ping_timeout_sec = 120,buf_size = 1024;
    id = atoi(argv[1]);
    if(id > 3 || !cfg[id].status || cfg[id].client != NULL || argc < 3 || argc > 4)return -1;

    ping_intv_sec = atoi(argv[2]);
    ping_timeout_sec = atoi(argv[3]);
    if(argc == 4)
    {
        buf_size = atoi(argv[4]);
    }

    if(ping_intv_sec < 1 || ping_intv_sec > 7200\
    ||ping_timeout_sec < 1 || ping_timeout_sec >7200 \
    ||buf_size < 1 || buf_size > 8192 )
    {
        return -1;
    }

    cfg[id].config.ping_interval_sec = ping_intv_sec;
    cfg[id].config.pingpong_timeout_sec = ping_timeout_sec;
    cfg[id].config.buffer_size = buf_size;
    return 0;
}
CLI_CMD(WSCFG, ws_cfg, "ws_cfg", "ws_cfg");

int ws_header(int argc, char *argv[])
{
    int16_t id = atoi(argv[1]);
    if(cfg[id].status || cfg[id].client != NULL || argc <= 1)return -1;
    cfg[id].config.headers = CONFIG_HEADER;
    WS_LOGI("SET header: %s", CONFIG_HEADER);
    return 0;
}
CLI_CMD(WSHEAD, ws_header, "ws_header", "ws_header");

int ws_open(int argc, char *argv[])
{
    int state = 0;
    int id = 0,res = 0;
    websocket_client_handle_t client;
    WS_LOGI("argc %d\n",argc);
    if(argc <= 1)
    {
        for(int i = 0;i < WEBSOCKET_CLIENT_NUMS;i++)
        {
            if(cfg[i].client)
            {
                client = (websocket_client_handle_t)cfg[i].client;
                //websocket_client_get_uri(client,uri);

                websocket_client_get_state(client,&state);
                WS_LOGI("+WSOPEN:%d,%d,%s\n",i,state,cfg[i].config.uri);
            }
        }
    }else
    {
        id = atoi(argv[1]);
        if(id >= WEBSOCKET_CLIENT_NUMS || argc < 3  || argc > 6 || cfg[id].status == true)
        {
            return -1;
        }
        cfg[id].config.uri = os_strdup(argv[2]);
        if(argc > 3 && argc < 6)
        {
            cfg[id].config.subprotocol = argv[3];
            cfg[id].config.network_timeout_ms = atoi(argv[4]);
            if(cfg[id].config.network_timeout_ms < 0 || cfg[id].config.network_timeout_ms > 180000)
            {
                cfg[id].config.subprotocol = NULL;
                cfg[id].config.network_timeout_ms = 15000;
                if(cfg[id].config.uri)os_free((void *)cfg[id].config.uri);
                cfg[id].config.uri = NULL;
                return -1;
            }
        }
        client = websocket_client_init(&cfg[id].config);
        
        if(client == NULL)
        {
            goto error_init;
        }
        res = websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
        if(res != TRANS_OK)
        {
            goto error_init;
        }
        res = websocket_client_start(client);
        if(res != TRANS_OK)
        {
            goto error_start;
        }
        WS_LOGI("client start ok\n");
        cfg[id].client = client;
        cfg[id].status = true;
    }
    return 0;
error_start:
    websocket_client_close(client, 100/ portTICK_PERIOD_MS);
error_init:
    websocket_client_destroy(client);
    if(cfg[id].config.uri)os_free((void *)cfg[id].config.uri);
    cfg[id].config.uri = NULL;
    WS_LOGE("client %d stop ok\n",id);
    return 0;
}
CLI_CMD(WSOPEN, ws_open, "ws_open", "ws_open");

int ws_close( int argc, char *argv[])
{
    int id = 0;
    websocket_client_handle_t client;
    id = atoi(argv[1]);
    if(argc < 1 || id >= WEBSOCKET_CLIENT_NUMS || cfg[id].client == NULL || cfg[id].status == false)
    {
        return -1;
    }
    client = cfg[id].client;
    websocket_client_close(client, 0);
    websocket_client_destroy(client);
    cfg[id].client = NULL;
    if(cfg[id].config.uri)os_free((void *)cfg[id].config.uri);
    cfg[id].status = false; 
    WS_LOGI("client %d close ok\n",id);
    return 0;
}
CLI_CMD(WSCLOSE, ws_close, "ws_close", "ws_close");

int ws_send( int argc, char *argv[])
{
    int id = 0,length = 0,opcode = 1,timeout_ms = 10000;
    uint8_t *data = NULL;
    int res = 0;
    websocket_client_handle_t client;
    id = atoi(argv[1]);
    if(argc < 3 || argc > 6 || id >= WEBSOCKET_CLIENT_NUMS || cfg[id].client == NULL || cfg[id].status == false)
    {
        return -1;
    }
    client = cfg[id].client;
    length = atoi(argv[2]); 
    data = (uint8_t *)argv[3]; /*todo: uart receive data */
    if(argc > 3 && argc <= 6)
    {
        opcode = atoi(argv[4]);
        timeout_ms = atoi(argv[5]);
        if(((opcode != WS_TRANSPORT_OPCODES_CONT)  \
        && (opcode != WS_TRANSPORT_OPCODES_TEXT)  \
        && (opcode != WS_TRANSPORT_OPCODES_BINARY)    \
        && (opcode != WS_TRANSPORT_OPCODES_CLOSE)\
        && (opcode != WS_TRANSPORT_OPCODES_PING)\
        && (opcode != WS_TRANSPORT_OPCODES_PONG))
        || (timeout_ms > 60000))
        {
            return -1;
        }
    }
    res = websocket_client_send_with_opcode(client,opcode,data,length,timeout_ms);
    if(res > 0)
    {
        WS_LOGI("client %d send ok");
    }
    return 0;
}
CLI_CMD(WSSEND, ws_send, "ws_send", "ws_send");