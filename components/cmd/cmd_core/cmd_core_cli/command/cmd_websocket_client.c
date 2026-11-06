#include <stdio.h>
#include "cli.h"
#include "common_define.h"
#include "task.h"
#include "event_groups.h"
#include "websocket_client.h"

#define WEBSOCKET_CLIENT_NUMS 3

typedef enum {
    AT_WEBSOCKET_SEND_DATA = 0,
    AT_WEBSOCKET_SET_HEAD,
    AT_WEBSOCKET_OPEN,
    AT_WEBSOCKET_CLOSE,
    AT_WEBSOCKET_DESTROY,
}AT_WEBSOCKET_BUFF_TYPE;

typedef struct AT_WEBSOCKET_BUFF_HANDLE {
    uint16_t        opcode;
    uint16_t        length;
    int             id;
    int             timeout;
    uint16_t        offset;
    AT_WEBSOCKET_BUFF_TYPE  type;
    uint8_t  *buf;
    uint8_t  *headers;
} AT_WEBSOCKET_BUFF_HANDLE_T;

typedef struct WEBSOCKET_CFG
{
    bool status;
    os_sem_handle_t sem;
    websocket_client_handle_t client;
    websocket_client_config_t config;
}WEBSOCKET_CFG_T;

struct websocket_header_item
{
    char *buff_addr;
    int len;
    struct{struct websocket_header_item *stqe_next;}next;
};

typedef struct websocket_header_item* websocket_header_item_t;

static WEBSOCKET_CFG_T at_cfg[WEBSOCKET_CLIENT_NUMS] = {0};
static AT_WEBSOCKET_BUFF_HANDLE_T ws_at_buff = {0};
STAILQ_HEAD(header_list_t, websocket_header_item);
static  int ws_cmd_task_handle = 0;
static os_queue_handle_t ws_queue = 0;

static  int ws_header_task_handle = 0;
static os_queue_handle_t ws_header_queue = 0;

static struct header_list_t* header_list = NULL;

static void ws_at_command_send_header(AT_WEBSOCKET_BUFF_HANDLE_T msg);
static int ws_at_command_set_header_cb(uint8_t *buf,uint16_t len);
static void ws_at_command_send_data_cb(AT_WEBSOCKET_BUFF_HANDLE_T msg);
static void ws_cmd_progress_task();
static void ws_header_progress_task();

static void cmd_receive(int argc, char *argv[], char* buf) {
	int i = 0, j = 0, offset = 0;
    for (i = 0; i < argc ; ++i) {
        j = 0;
        if (i != 0) {
            buf[offset++] = ' ';
        }
        while(argv[i][j]) {
            buf[offset++] = argv[i][j++];
        }
    }
}

static int ws_get_active_client_cnt()
{
    int valid_clients = 0;
    for(int i = 0;i < WEBSOCKET_CLIENT_NUMS;i++)
    {
        if(at_cfg[i].client)
        {
            valid_clients ++;
        }
    }
    return valid_clients;
}

static int header_list_add(struct header_list_t *list,  const char *buff,int len)
{
    if (list == NULL || buff == NULL || len == 0) {
        return ERR_INVALID_ARG;
    }
    websocket_header_item_t item = os_calloc(1, sizeof(struct websocket_header_item));
    item->buff_addr = os_strdup(buff);
    item->len = len;
    STAILQ_INSERT_HEAD(list, item, next);

    return TRANS_OK;
}

static void ws_at_command_callback(char c)
{
    AT_WEBSOCKET_BUFF_HANDLE_T *buff = &ws_at_buff;
    if(buff->offset < buff->length)
    {
        buff->buf[buff->offset]= c;
    }
    if(buff->offset <= buff->length )
    {
        buff->offset++;
    }
    
    if(buff->offset == buff->length)
    {
        buff->buf[buff->offset] = '\0';
        switch (buff->type)
        {
        case AT_WEBSOCKET_SET_HEAD:
            /* code */
            ws_at_command_send_header(*buff);
            //ws_at_command_set_header_cb(buff->buf,buff->length);
            break;
        case AT_WEBSOCKET_SEND_DATA:
            ws_at_command_send_data_cb(*buff);
            break;
        default:
            break;
        }
    }
}

static int ws_at_command_set_header_cb(uint8_t *buf,uint16_t len)
{
    char *cut_str = NULL;
    cut_str = strchr((const char *)buf,':');
    if(cut_str == NULL)
    {
        return -1;
    }

    if(header_list == NULL)
    {
        header_list = os_zalloc(sizeof(struct header_list_t));
        STAILQ_INIT(header_list);
    }
    
    header_list_add(header_list, (char *)buf,len);
    return 0;
}

static void ws_set_header_progress(AT_WEBSOCKET_BUFF_HANDLE_T msg)
{   
    AT_WEBSOCKET_BUFF_HANDLE_T *buff = &msg;
    int res = 0;
    if(buff->type == AT_WEBSOCKET_SET_HEAD)
    {
        res = ws_at_command_set_header_cb(buff->buf, buff->length);
    }

    os_msleep(1);
    if(ws_at_buff.offset != ws_at_buff.length)
    {
        int old = ws_at_buff.offset;
        while (1)
        {
            if(old != ws_at_buff.offset){
                old = ws_at_buff.offset;
            }else{
                os_msleep(1);
                if(old != ws_at_buff.offset){
                    old = ws_at_buff.offset;			
                }else{
                    break;
                }
            }
        }
    }
    
    if(buff->buf)os_free(buff->buf);
    buff->buf = NULL;
    memset((void *)&buff,0,sizeof(buff));

    if(res == 0)
    {
        WS_LOGI("ws set header ok\n");
    }
    else 
    {
        WS_LOGI("ws set header fail\n");
    }
}

static void ws_at_command_send_data_cb(AT_WEBSOCKET_BUFF_HANDLE_T msg)
{
    int ret = 0;
    WS_LOGI("ws:%d\n",ws_queue);
    if(ws_queue)
    {
        ret = os_queue_send(ws_queue, (char *)&msg, sizeof(AT_WEBSOCKET_BUFF_HANDLE_T), 0);
        if (ret != 0) {
            os_printf(LM_APP, LL_ERR, "send cmd 0x%x failed\n", msg.type);
        }
    }
}

static void ws_at_command_send_header(AT_WEBSOCKET_BUFF_HANDLE_T msg)
{
    int ret = 0;
    WS_LOGI("ws:%d\n",ws_header_queue);
    if(ws_header_queue)
    {
        ret = os_queue_send(ws_header_queue, (char *)&msg, sizeof(AT_WEBSOCKET_BUFF_HANDLE_T), 0);
        if (ret != 0) {
            os_printf(LM_APP, LL_ERR, "send header 0x%x failed\n", msg.type);
        }
    }
}

static void websocket_event_handler(void *handler_args, event_base_t base, int32_t event_id, void *event_data)
{
    int index = 0;
    websocket_client_handle_t client= (websocket_client_handle_t)handler_args;
    websocket_event_data_t *data = (websocket_event_data_t *)event_data;
    AT_WEBSOCKET_BUFF_HANDLE_T msg = {0};
    for(int i = 0;i < WEBSOCKET_CLIENT_NUMS;i++)
    {
        if(at_cfg[i].client == client)
        {
            index = i;
            WS_LOGI("%s",at_cfg[i].config.uri);
        }
    }
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        WS_LOGI("WEBSOCKET_EVENT_CONNECTED\n");
        if(at_cfg[index].sem)
        {
            os_sem_post(at_cfg[index].sem);
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        WS_LOGI("WEBSOCKET_EVENT_DISCONNECTED\n");
        if(at_cfg[index].sem)
        {
            os_sem_post(at_cfg[index].sem);
        }
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
                if(data->op_code == WS_TRANSPORT_OPCODES_TEXT || data->op_code == WS_TRANSPORT_OPCODES_BINARY)
                {
                    WS_LOGI("+WS_DATA:%d,%d,%.*s\r\n",index,data->data_len,data->data_len,(char *)data->data_ptr);
                }
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
        if(at_cfg[index].sem)
        {
            os_sem_post(at_cfg[index].sem);
        }
        WS_LOGI("+WS_CLOSED:%d\r\n",index);
        msg.id = index;
        msg.type = AT_WEBSOCKET_CLOSE;
        if(ws_queue)
        {
            os_queue_send(ws_queue,(char *)&msg, sizeof(AT_WEBSOCKET_BUFF_HANDLE_T) ,0);
        }
        break;
    default:WS_LOGI("event id:%d",event_id)break;
    }
}

static int ws_cfg ( int argc, char *argv[])
{
    int id = 0, ping_intv_sec = 10,ping_timeout_sec = 120,buf_size = 1024;
    WS_LOGI("argc %d\n",argc);

    id = atoi(argv[1]);
    if(id < 0 || id > WEBSOCKET_CLIENT_NUMS-1)
    {
        WS_LOGI("id = %d number error\n", id);
        return -1;
    }
    ping_intv_sec = atoi(argv[2]);
    if(ping_intv_sec < 1 || ping_intv_sec > 7200)
    {
        WS_LOGI("ping_intv_sec = %d error\n", ping_intv_sec);
        return -1;
    }

    ping_timeout_sec = atoi(argv[3]);
    if(ping_timeout_sec < 1 || ping_timeout_sec > 7200)
    {
        WS_LOGI("ping_timeout_sec = %d error\n", ping_timeout_sec);
        return -1;
    }
    if(argc == 5)
    {
        buf_size = atoi(argv[4]);
        if(buf_size < 1 || buf_size > 8192)
        {
            WS_LOGI("buf_size = %d error\n", buf_size);
            return -1;
        }
    }

    for(int i = 0;i < WEBSOCKET_CLIENT_NUMS;i++)
    {
        if(at_cfg[i].client && i == id)
        {
            WS_LOGI("ws = %d alredy init\n", id); 
            return 0;
        }
    }
    at_cfg[id].config.ping_interval_sec = ping_intv_sec;
    at_cfg[id].config.pingpong_timeout_sec = ping_timeout_sec;
    at_cfg[id].config.buffer_size = buf_size;   
    WS_LOGI("id:%d sec:%d timeout:%d buff:%d\n",id,at_cfg[id].config.ping_interval_sec,at_cfg[id].config.pingpong_timeout_sec,buf_size);
    return 0;
}

static int ws_header ( int argc, char *argv[])
{
    int length = 0;
    websocket_header_item_t item = NULL;

    if(argc == 1)
    {
        int index = 0;
        if(header_list ==NULL || STAILQ_EMPTY(header_list))
        {
            return 0;
        }

        STAILQ_FOREACH(item, header_list, next){
            if(item != NULL)
            {
                WS_LOGI("%d : %s\n",index,item->buff_addr);
                index++;
            }
        }     
    }
    else
    {
        char data[100];
        length = atoi(argv[1]);
        if(length < 0)
        {
            WS_LOGI("length = %d error\n", length);
            return -1;
        }
        if(length == 0)
        {
            if(header_list == NULL || STAILQ_EMPTY(header_list))
            {
                return 0;
            }
            
            STAILQ_FOREACH(item, header_list, next){
                if((!STAILQ_EMPTY(header_list)) && (item != NULL))
                {
                    STAILQ_REMOVE(header_list, item, websocket_header_item, next);
                    os_free(item->buff_addr);
                    os_free(item);
                }
            }

            if(header_list)os_free(header_list);
            header_list = NULL;
            return 0;
        }

        memset((void *)&ws_at_buff,0,sizeof(ws_at_buff));
        ws_at_buff.length = length;
        ws_at_buff.type = AT_WEBSOCKET_SET_HEAD;
        ws_at_buff.buf = (uint8_t *)os_zalloc(ws_at_buff.length + 1);
        cmd_receive(argc-2, &argv[2], data);/*todo: uart receive data */
        ws_header_queue = os_queue_create("ws_header_queue",10,  sizeof(AT_WEBSOCKET_BUFF_HANDLE_T),0);
        ws_header_task_handle = os_task_create("ws header set task", 6 ,4096, ws_header_progress_task, 0);

        for(int i = 0;i < length;i++)
        {
            ws_at_command_callback(data[i]);
        }
    }
    return 0;
}

static int ws_open ( int argc, char *argv[])
{
    int state = 0;
    int id = 0,ret = -1;
    char *url = NULL;
    char *subprotol = NULL;
    char *auth = NULL;
    int timeout_ms = 15000;
    AT_WEBSOCKET_BUFF_HANDLE_T msg = {0};
    websocket_client_handle_t client;
    int valid_clients = 0;
    if(argc == 1)
    {
        for(int i = 0;i < WEBSOCKET_CLIENT_NUMS;i++)
        {
            if(at_cfg[i].client)
            {
                client = (websocket_client_handle_t)at_cfg[i].client;
                websocket_client_get_state(client,&state);
                WS_LOGI("id = %d, state = %d, uri = %s\n",i,state,at_cfg[i].config.uri);
            }
        }
        return 0;
    }
    else if(argc >= 3)
    {
        id = atoi(argv[1]);
        if(id < 0 || id > WEBSOCKET_CLIENT_NUMS-1)
        {
            WS_LOGI("id = %d number error\n", id);
            return -1;
        }
        url = argv[2];
        if(strlen(argv[2]) < 0 || strlen(argv[2]) > 2048)
        {
            WS_LOGI("url too leng\n");
            return -1;
        }

        for(int i = 0;i < WEBSOCKET_CLIENT_NUMS;i++)
        {
            if(at_cfg[i].status && i == id)
            {
                WS_LOGI("ws id = %d already open\n",id);
                return -1;
            }
        }

        if(argc > 3)
        {
            subprotol = argv[3];
            if(strlen(argv[3]) < 1 || strlen(argv[3]) > 2048)
            {
                WS_LOGI("subprotol too leng\n");
                return -1;
            }

            timeout_ms = atoi(argv[4]);
            if(timeout_ms < 0 || timeout_ms > 180000)
            {
                WS_LOGI("timeout_ms = %d error\n", timeout_ms);
                return -1;
            }

            auth = argv[5];
            if(strlen(argv[5]) < 1 || strlen(argv[5]) > 100)
            {
                WS_LOGI("auth too leng\n");
                return -1;
            }

            if(subprotol)
            {
                at_cfg[id].config.subprotocol = os_strdup(subprotol);
            }

            if(auth)
            {
                at_cfg[id].config.auth = os_strdup(auth);
            }
        }

        at_cfg[id].config.uri = os_strdup(url);
        at_cfg[id].config.network_timeout_ms = timeout_ms;

        if(at_cfg[id].config.buffer_size <=0)
        {
            at_cfg[id].config.buffer_size = 1024;
        }

        if(header_list!= NULL && !STAILQ_EMPTY(header_list))
        {
            websocket_header_item_t item = NULL;
            char *headers = NULL;
            int len = 0;
            STAILQ_FOREACH(item, header_list, next){
                if(item)
                {
                    len += item->len + sizeof("\r\n");
                }
            }
            headers = os_zalloc(len + 1);
            if(headers == NULL)
            {
                ret =  -1;
                goto error;
            }
            STAILQ_FOREACH(item, header_list, next){
                if(item)
                {
                    headers = strcat(headers,item->buff_addr);
                    headers = strcat(headers,"\r\n");
                }
            }
            at_cfg[id].config.headers = headers;
        }
        
        valid_clients = ws_get_active_client_cnt();
        os_printf(LM_OS, LL_ERR, "##task creat.%d %d %d\n",valid_clients,ws_queue,ws_cmd_task_handle);
        if(valid_clients == 0 && ws_queue == 0)
        {
            ws_queue = os_queue_create("ws_queue",10,  sizeof(AT_WEBSOCKET_BUFF_HANDLE_T),0);
            ws_cmd_task_handle = os_task_create("ws cmd task", 6 ,4096, ws_cmd_progress_task, 0);
        }
        msg.id = id;
        msg.type = AT_WEBSOCKET_OPEN;
        if(ws_queue)
        {
            at_cfg[id].status = true;
            os_queue_send(ws_queue,(char *)&msg, sizeof(AT_WEBSOCKET_BUFF_HANDLE_T),0);
        }
        ret = 0;
    }
    return ret;
error:
    if(at_cfg[id].config.subprotocol)os_free(at_cfg[id].config.subprotocol);
    if(at_cfg[id].config.auth)os_free(at_cfg[id].config.auth);
    if(at_cfg[id].config.headers)os_free(at_cfg[id].config.headers);
    if(url)os_free(url);
    at_cfg[id].config.subprotocol = NULL;
    at_cfg[id].config.auth = NULL;
    at_cfg[id].config.headers = NULL;
    at_cfg[id].config.uri = NULL;
    return ret;
}

static int ws_close( int argc, char *argv[])
{
    int id = 0;
    int ret = -1;
    AT_WEBSOCKET_BUFF_HANDLE_T msg = {0};

    id = atoi(argv[1]);
    if(id < 0 || id > WEBSOCKET_CLIENT_NUMS - 1)
    {
        WS_LOGI("id = %d number error\n", id);
        return -1;
    }

    msg.id = id;
    msg.type = AT_WEBSOCKET_CLOSE;
    if(ws_queue)
    {
        os_queue_send(ws_queue,(char *)&msg, sizeof(AT_WEBSOCKET_BUFF_HANDLE_T) ,0);
        at_cfg[id].status = true;
    }
    ret = 0;

    WS_LOGI("client %d close ok\n",id);
    return ret;
}

static void ws_send_data_progress(AT_WEBSOCKET_BUFF_HANDLE_T msg)
{   
    int ret = -1;
    int res = 0, is_busy = 0;
    AT_WEBSOCKET_BUFF_HANDLE_T *buff = &msg;

    if(buff->type == AT_WEBSOCKET_SEND_DATA \
        &&  buff->id >= 0 \
        && buff->id < WEBSOCKET_CLIENT_NUMS)
    {
        res = websocket_client_send_with_opcode(at_cfg[buff->id].client,buff->opcode,buff->buf,buff->length,buff->timeout);
        if(res >= 0)
        {
            WS_LOGI("index:%d send len:%d,opcode:%d ok\n",buff->id,buff->length,buff->opcode);
            ret= 0;
        }
    }

    os_msleep(1);
    res = 0;
    if(ws_at_buff.offset != ws_at_buff.length)
    {
        int old = ws_at_buff.offset;
        while (1)
        {
            if(old != ws_at_buff.offset){
                old = ws_at_buff.offset;
                is_busy = 1;
            }else{
                os_msleep(1);
                if(old != ws_at_buff.offset){
                    old = ws_at_buff.offset;
                    is_busy = 1;				
                }else{
                    break;
                }
            }
            if(is_busy == 1 && res == 0)
            {
                WS_LOGI("\r\nbusy p...\r\n");
                res = 1;
            }
        }
    }

    if(ret != 0)
    {
       WS_LOGI("\r\nSEND FAIL\r\n");
    }
    else
    {
        WS_LOGI("\r\nSEND OK\r\n");
    }  
    if(buff->buf)os_free(buff->buf);
    buff->buf = NULL;
    memset((void *)&buff,0,sizeof(buff));
}

static void ws_open_progress(int index)
{
    int id = index;
    int ret = 0;
    websocket_client_handle_t client;
    at_cfg[id].sem = os_sem_create(1, 0);
    if(id < 0 || id >= WEBSOCKET_CLIENT_NUMS)
    {
        goto error_index;
    }

    client = websocket_client_init(&at_cfg[id].config);
    if(client == NULL)
    {
        goto error_init;
    }
    ret = websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
    if(ret != TRANS_OK)
    {
        goto error_init;
    }
    ret = websocket_client_start(client);
    if(ret != TRANS_OK)
    {
        goto error_start;
    }
    at_cfg[id].client = client;
    WS_LOGI("id:%d url:%s\n",id,at_cfg[id].config.uri);

    os_sem_wait(at_cfg[id].sem,at_cfg[id].config.network_timeout_ms);

    if(websocket_client_is_connected(client))
    {
        os_msleep(300);
        WS_LOGI("\nOK\n");
        goto connect_ok;
    }
error_start:
    WS_LOGI("client stop\n");
    websocket_client_close(client, 0);
error_init:
    websocket_client_destroy(client);
error_index:
    at_cfg[id].client = NULL;
    if(at_cfg[id].config.uri)os_free((void *)at_cfg[id].config.uri);
    if(at_cfg[id].config.subprotocol)os_free((void *)at_cfg[id].config.subprotocol);
    if(at_cfg[id].config.auth)os_free((void *)at_cfg[id].config.auth);
    at_cfg[id].config.uri = NULL;
    at_cfg[id].config.subprotocol = NULL;
    at_cfg[id].config.headers = NULL;
    at_cfg[id].config.auth = NULL;
    WS_LOGI("\nERROR\n");
connect_ok:
    if(at_cfg[id].config.headers)os_free((void *)at_cfg[id].config.headers);
    at_cfg[id].config.headers = NULL;
    at_cfg[id].status = false;
    if(at_cfg[id].sem)
    {
        os_sem_destroy(at_cfg[id].sem);
        at_cfg[id].sem = 0;
    }
}

static void ws_close_progress(int index)
{
    int id = index;
    websocket_client_handle_t client;
    if(id < 0 || id >= WEBSOCKET_CLIENT_NUMS)
    {
        return;
    }

    client = at_cfg[id].client;
    websocket_client_close(client, 0);
    websocket_client_destroy(client);
    at_cfg[id].client = NULL;
    if(at_cfg[id].config.uri)os_free((void *)at_cfg[id].config.uri);
    if(at_cfg[id].config.headers)os_free((void *)at_cfg[id].config.headers);
    if(at_cfg[id].config.subprotocol)os_free((void *)at_cfg[id].config.subprotocol);
    if(at_cfg[id].config.auth)os_free((void *)at_cfg[id].config.auth);
    if(at_cfg[id].sem)os_sem_destroy(at_cfg[id].sem);
    at_cfg[id].sem = 0;
    at_cfg[id].config.uri = NULL;
    at_cfg[id].config.subprotocol = NULL;
    at_cfg[id].config.headers = NULL;
    at_cfg[id].config.auth = NULL;
    at_cfg[id].status = false;

}

static int ws_send ( int argc, char *argv[])
{
    int id = 0,length = 0,opcode = 1,timeout_ms = 10000;
    int ret = -1;
    char data[] = "1234567890qwertyuiopasdfghjklzxcvbnmQwertyuiopasdfghjklzxcvbnm@#$/&*~^/?,<>|";

    if(argc < 3 || argc > 5)
    {
        WS_LOGI("argc %d\n",argc);
        return -1;
    }

    id = atoi(argv[1]);
    if(id < 0 || id > WEBSOCKET_CLIENT_NUMS-1)
    {
        WS_LOGI("id = %d number error\n", id);
        return -1;
    }

    length = atoi(argv[2]);
    if(length < 1 || id > at_cfg[id].config.buffer_size)
    {
        WS_LOGI("length = %d error\n", length);
        return -1;
    }

    if(argc > 4)
    {
        opcode = atoi(argv[3]);
        if(ret != CMD_RET_SUCCESS || 
        ((opcode != WS_TRANSPORT_OPCODES_CONT)      \
        && (opcode != WS_TRANSPORT_OPCODES_TEXT)    \
        && (opcode != WS_TRANSPORT_OPCODES_BINARY)  \
        && (opcode != WS_TRANSPORT_OPCODES_CLOSE)   \
        && (opcode != WS_TRANSPORT_OPCODES_PING)    \
        && (opcode != WS_TRANSPORT_OPCODES_PONG)))
        {
            WS_LOGI("opcode = %d error\n", opcode);
            return -1;
        }

        timeout_ms = atoi(argv[4]);
        if(timeout_ms < 0 || timeout_ms > 60000)
        {
            WS_LOGI("timeout_ms = %d error\n", timeout_ms);
            return -1;
        }
    }
    memset((void *)&ws_at_buff,0,sizeof(ws_at_buff));
    ws_at_buff.length = length;
    ws_at_buff.id = id;
    ws_at_buff.timeout = timeout_ms;
    ws_at_buff.opcode = opcode;
    ws_at_buff.type = AT_WEBSOCKET_SEND_DATA;
    ws_at_buff.buf = (uint8_t *)os_zalloc(ws_at_buff.length + 1);
    if(ws_at_buff.buf == NULL)
    {
        WS_LOGE("buff malloc failed\n");
        return -1;
    }
    
    for(int i = 0;i < length;i++)
    {
        ws_at_command_callback(data[i%strlen(data)]);
    }
    WS_LOGE("send ok\n");  
    return 0;
}

static void ws_header_progress_task()
{
    AT_WEBSOCKET_BUFF_HANDLE_T msg = {0};
    int ret = os_queue_receive(ws_header_queue,(void *)&msg,sizeof(AT_WEBSOCKET_BUFF_HANDLE_T),portMAX_DELAY);
    if (ret < 0) {
        WS_LOGE("queue recv error.\n");
        os_task_delete(ws_header_task_handle);
        return;
    }
    if(msg.type == AT_WEBSOCKET_SET_HEAD){
        ws_set_header_progress(msg);
    }

    os_queue_destroy(ws_header_queue);
    ws_header_queue = 0;
    os_task_delete(ws_header_task_handle);
    ws_header_task_handle = 0;
}

static void ws_cmd_progress_task()
{
    AT_WEBSOCKET_BUFF_HANDLE_T msg = {0};
    int ret = 0;
    int run = 1;
    int client_cnt = 0;

    while(run)
    {
        ret = os_queue_receive(ws_queue,(void *)&msg,sizeof(AT_WEBSOCKET_BUFF_HANDLE_T),portMAX_DELAY);
        if (ret < 0) {
            WS_LOGE("queue recv error.\n");
            continue;
        }
        switch (msg.type)
        {
            case AT_WEBSOCKET_OPEN:
                ws_open_progress(msg.id);
                client_cnt = ws_get_active_client_cnt();
                if(client_cnt == 0)
                {
                    os_queue_destroy(ws_queue);
                    ws_queue = 0;
                    run = 0;
                }
                break;
            case AT_WEBSOCKET_CLOSE:
                ws_close_progress(msg.id);
                client_cnt = ws_get_active_client_cnt();
                if(client_cnt == 0)
                {
                    os_queue_destroy(ws_queue);
                    ws_queue = 0;
                    run = 0;
                }
                break;
            case AT_WEBSOCKET_SEND_DATA:
                ws_send_data_progress(msg);
                
                break;
            default:
                break;
        }
    }
    os_task_delete(ws_cmd_task_handle);
    ws_cmd_task_handle = 0;
}

CLI_CMD(wscfg, ws_cfg, "ws_cfg", "ws_cfg");
CLI_CMD(wshead, ws_header, "ws_header", "ws_header");
CLI_CMD(wsopen, ws_open, "ws_open", "ws_open");
CLI_CMD(wssend, ws_send, "ws_send", "ws_send");
CLI_CMD(wsclose, ws_close, "ws_close", "ws_close");
