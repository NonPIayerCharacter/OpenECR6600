#include <stdint.h>
#include "system_queue.h"
#include "easyflash.h"
#include "hal_uart.h"
#include "at_common.h"
#include "dce_commands.h"
#include "dce.h"
#include "at_def.h"

#include "oshal.h"
#include "string.h"
#include "platform_tr6600.h"

#include "http_command.h"
#include "httpclient.h"

// AT+HTTPCLIENT=<opt>,<"url">,[,<"data">][,<"http_req_header">][,<"http_req_header">][...]
typedef enum
{
    HTTP_CLIENT_PARA_METHOD = 0,
    HTTP_CLIENT_PARA_URL,
    HTTP_CLIENT_PARA_DATA,
    HTTP_CLIENT_PARA_REQ_HEADER
} http_client_para_e;

typedef enum
{
    HTTP_CMD_METHOD_HEAD = 1,
    HTTP_CMD_METHOD_GET = 2,
    HTTP_CMD_METHOD_POST,
    // HTTP_CMD_METHOD_PUT = 4,
    // HTTP_CMD_METHOD_DELETE,
    HTTP_CMD_METHOD_MAX
} http_cmd_method_e;

typedef enum
{
    HTTP_RET_OK = 0,
    HTTP_RET_ERR,
} http_ret_e;

//#define CONFIG_HTTP_RANGE -1
typedef struct {
#ifdef CONFIG_HTTP_RANGE
    unsigned int dlofft;
#endif
    unsigned int contentlen;
} http_client_head_t;

typedef struct {
    os_sem_handle_t httpsem;
    unsigned char *param;
    unsigned int pos;
    unsigned int len;
} http_client_dy_t;

typedef struct {
    unsigned char method;
    unsigned char contentype;
    unsigned char status;
    const char *url;
    const char *pdata;
    http_client_dy_t args;
    http_client_head_t head;
    dce_t *dce;
} http_client_local_t;

#define HTTP_HEADER_KEY_VALUE_DELIMITER ": "
static http_client_local_t g_http_priv = {0};

static int http_client_parse_url(arg_t argv)
{
    if (argv.type != ARG_TYPE_STRING) {
        os_printf(LM_CMD, LL_ERR, "http client url type error\n");
        return HTTP_RET_ERR;
    }

    g_http_priv.url = argv.value.string;

    return HTTP_RET_OK;
}

static int http_client_parse_data(arg_t argv)
{
    if (argv.type != ARG_TYPE_STRING) {
        os_printf(LM_CMD, LL_ERR, "http client data type error\n");
        return HTTP_RET_ERR;
    }

    g_http_priv.pdata = argv.value.string;

    return HTTP_RET_OK;
}

static int http_client_parse_method(arg_t argv)
{
    if (argv.type != ARG_TYPE_NUMBER ||
        argv.value.number < HTTP_CMD_METHOD_HEAD ||
        argv.value.number >= HTTP_CMD_METHOD_MAX) {
            os_printf(LM_CMD, LL_ERR, "http client method type %d value %d not support\n", argv.type, argv.value.number);
            return HTTP_RET_ERR;
    }
    g_http_priv.method = argv.value.number;

    return HTTP_RET_OK;
}

static http_ret_e http_client_parse_head_req(size_t argc, arg_t *argv)
{
    const char *cq = NULL;
    const char *cp = NULL;
    int index, svlen;

    for (index = 0; index < argc; index++) {
        if (argv[index].type != ARG_TYPE_STRING) {
            os_printf(LM_CMD, LL_ERR, "head request type %d invalid.\n", argv[index].type);
            return HTTP_RET_ERR;
        }

        svlen = strlen(argv[index].value.string);
        if (svlen == 0) {
            continue;
        }

        cp = argv[index].value.string;
        cq = strstr(argv[index].value.string, HTTP_HEADER_KEY_VALUE_DELIMITER);
        if (cq == NULL) {
            os_printf(LM_CMD, LL_ERR, "head request %s invalid.\n", argv[index].value.string);
            return HTTP_RET_ERR;
        }

        cq += strlen(HTTP_HEADER_KEY_VALUE_DELIMITER);
        if (http_request_user_msg_header_add(cp, cq-cp, cq, svlen+cp-cq) != HTTP_RET_OK) {
            return HTTP_RET_ERR;
        }
    }

    return HTTP_RET_OK;
}

static http_ret_e http_client_parse_config(int kind, size_t argc, arg_t *argv)
{
    size_t reqcnt = argc;

    if (kind == DCE_WRITE) {
        if (http_client_parse_method(argv[HTTP_CLIENT_PARA_METHOD]) != HTTP_RET_OK) {
            return HTTP_RET_ERR;
        }

        reqcnt--;

        if (http_client_parse_url(argv[HTTP_CLIENT_PARA_URL]) != HTTP_RET_OK) {
            return HTTP_RET_ERR;
        }
        reqcnt--;

        if (g_http_priv.method == HTTP_CMD_METHOD_POST) {
            if (http_client_parse_data(argv[HTTP_CLIENT_PARA_DATA]) != HTTP_RET_OK) {
                return HTTP_RET_ERR;
            }
            reqcnt--;
        }

        os_printf(LM_APP, LL_DBG, "http request header count %d.\r\n", reqcnt);
        if (reqcnt > 0) {
            if (http_client_parse_head_req(reqcnt, &argv[argc - reqcnt]) != HTTP_RET_OK) {
                return HTTP_RET_ERR;
            }
        }
    }

    return HTTP_RET_OK;
}

static int http_client_handle_server_data(void *data)
{
    http_event_t *event = data;
    http_client_t *client = event->context;
    char buff[64] = {0};

    if (client == NULL) {
        return HTTP_RET_ERR;
    }

    os_printf(LM_APP, LL_DBG, "event type 0x%x len %d totlen %d\n", event->type, event->len, client->total);

    if (event->type == http_event_type_on_headers) {
        g_http_priv.status = client->interceptor->response.status;
        if (g_http_priv.status != http_response_status_ok && g_http_priv.status != http_response_status_partial_content) {
            return HTTP_RET_ERR;
        }

        g_http_priv.head.contentlen = client->total;
        if (g_http_priv.args.param == NULL) {
            snprintf(buff, sizeof(buff), "%s%ld", "+HTTPCLIENT:", client->total);
        } else {
            snprintf(buff, sizeof(buff), "%s%ld", "+HTTPCLIENTCFG:", client->total);
        }
        dce_emit_pure_response(g_http_priv.dce, buff, strlen(buff));
    }

    if (event->type == http_event_type_on_body && (g_http_priv.status == http_response_status_ok || g_http_priv.status == http_response_status_partial_content)) {
#ifdef CONFIG_HTTP_RANGE
        g_http_priv.head.dlofft += event->len;
#endif
        dce_emit_pure_response(g_http_priv.dce, ",", 1);
        dce_emit_extended_result_code(g_http_priv.dce, (char *)event->data, event->len, 1);
    }

    return HTTP_RET_OK;
}

static http_ret_e http_client_request_handle(void)
{
    int ret = HTTP_RET_OK;
#ifdef CONFIG_HTTP_RANGE
    unsigned int loop = CONFIG_HTTP_RANGE;
#else
    unsigned int loop = 0;
#endif
    do {
        http_request_method_t method = HTTP_REQUEST_METHOD_INVALID;
        http_client_t *client = http_client_init(NULL);

        if (client == NULL) {
            os_printf(LM_CMD, LL_ERR, "http client create failed\n");
            return HTTP_RET_ERR;
        }

        switch (g_http_priv.method) {
            case HTTP_CMD_METHOD_GET:
                method = HTTP_REQUEST_METHOD_GET;
                break;
            case HTTP_CMD_METHOD_POST:
                method = HTTP_REQUEST_METHOD_POST;
                break;
            case HTTP_CMD_METHOD_HEAD:
                method = HTTP_REQUEST_METHOD_HEAD;
                break;
            default:
                return HTTP_RET_ERR;
        }
        os_printf(LM_APP, LL_DBG, "http request method %d url %s\n", method, g_http_priv.url);
        http_client_set_data(client, (void *)g_http_priv.pdata);

#ifdef CONFIG_HTTP_RANGE
        char rv[64] = {0};
        snprintf(rv, sizeof(rv), "bytes=%d-", g_http_priv.head.dlofft);
        http_request_user_msg_header_add("Range: ", strlen("Range: "), rv, strlen(rv));
#endif
        ret = http_client_method_request(client, method, g_http_priv.url, http_client_handle_server_data);
        if (ret != HTTP_SOCKET_TIMEOUT_ERROR) {
            if (ret != HTTP_RET_OK || (g_http_priv.status != http_response_status_ok && g_http_priv.status != http_response_status_partial_content)) {
                os_printf(LM_APP, LL_ERR, "http request method %d with status %d\n", method, g_http_priv.status);
                ret = HTTP_RET_ERR;
            }
            loop = 0;
        }
        http_client_exit(client);
    } while (loop--);

    http_request_user_msg_header_release(0);

    return ret;
}

static dce_result_t http_commands_handle_client(dce_t *dce, void *group_ctx, int kind, size_t argc, arg_t *argv)
{
    memset(&g_http_priv, 0, sizeof(g_http_priv));

    if (argc < HTTP_CLIENT_PARA_DATA) {
        dce_emit_basic_result_code(dce, DCE_RC_ERROR);
        os_printf(LM_CMD, LL_ERR, "http commands paramter invalid\n");
        return DCE_FAILED;
    }

    g_http_priv.dce = dce;
    if (http_client_parse_config(kind, argc, argv) != HTTP_RET_OK) {
        dce_emit_basic_result_code(dce, DCE_RC_ERROR);
        return DCE_FAILED;
    }

    if (http_client_request_handle() != HTTP_RET_OK) {
        dce_emit_basic_result_code(dce, DCE_RC_ERROR);
        return DCE_FAILED;
    }

    dce_emit_basic_result_code(dce, DCE_RC_OK);
    return DCE_OK;
}

void http_commands_get_config_data(void *param, const char *data, size_t size)
{
    int idx;
    unsigned char endIn = 0;

    for (idx = 0; idx < size; idx++) {
        if (data[idx] == '\n') {
            g_http_priv.args.param[g_http_priv.args.pos] = 0;
            if (g_http_priv.args.pos > 1 && g_http_priv.args.param[g_http_priv.args.pos - 1] == '\r') {
                g_http_priv.args.param[g_http_priv.args.pos - 1] = 0;
                g_http_priv.args.pos--;
            }
            endIn = 1;
        } else {
            g_http_priv.args.param[g_http_priv.args.pos] = data[idx];
            g_http_priv.args.pos++;
        }

        if (endIn || g_http_priv.args.pos >= g_http_priv.args.len) {
            dce_register_data_input_cb(NULL);
            target_dec_switch_input_state(COMMAND_STATE);
            os_sem_post(g_http_priv.args.httpsem);
        }
    }
}

static void http_client_cfg_task(void *arg)
{
    size_t argc = 0;
    arg_t args[DCE_MAX_ARGS];
    dce_result_code_t ret;

    os_sem_wait(g_http_priv.args.httpsem, portMAX_DELAY);
    os_printf(LM_APP, LL_DBG, "http param %s len %d(%d)\n", g_http_priv.args.param, g_http_priv.args.len, g_http_priv.args.pos);
    if (dce_parse_args((const char *)g_http_priv.args.param, g_http_priv.args.pos, NULL, &argc, args) != DCE_OK) {
        ret = DCE_RC_ERROR;
    } else if (http_client_parse_config(DCE_WRITE, argc, args) != HTTP_RET_OK) {
        ret = DCE_RC_ERROR;
    } else if (http_client_request_handle() != HTTP_RET_OK) {
        ret = DCE_RC_ERROR;
    } else {
        ret = DCE_RC_OK;
    }

    dce_emit_basic_result_code(g_http_priv.dce, ret);
    os_sem_destroy(g_http_priv.args.httpsem);
    os_free(g_http_priv.args.param);
    g_http_priv.args.param = NULL;
}

static dce_result_t http_commands_handle_client_cfg(dce_t *dce, void *ctx, int kind, size_t argc, arg_t *argv)
{
    if (argc != 1 || argv[0].type != ARG_TYPE_NUMBER) {
        dce_emit_basic_result_code(dce, DCE_RC_ERROR);
        os_printf(LM_CMD, LL_ERR, "http commands paramter invalid\n");
        return DCE_FAILED;
    }

    if (g_http_priv.args.param != NULL) {
        dce_emit_basic_result_code(dce, DCE_RC_ERROR);
        os_printf(LM_CMD, LL_ERR, "http busy..\n");
        return DCE_FAILED;
    }

    memset(&g_http_priv, 0, sizeof(g_http_priv));
    g_http_priv.dce = dce;
    g_http_priv.args.len = argv[0].value.number;

    g_http_priv.args.param = os_malloc(g_http_priv.args.len + 1);
    if (g_http_priv.args.param == NULL) {
        dce_emit_basic_result_code(dce, DCE_RC_ERROR);
        os_printf(LM_CMD, LL_ERR, "http commands cannot alloc %d mem\n", g_http_priv.args.len);
        return DCE_FAILED;
    }

    memset(g_http_priv.args.param, 0, g_http_priv.args.len + 1);

    g_http_priv.args.httpsem = os_sem_create(1, 0);
    os_task_create("http_client_cfg", 4, 4096, http_client_cfg_task, dce);

    dce_register_data_input_cb(http_commands_get_config_data);
    target_dec_switch_input_state(ONLINE_DATA_STATE);
    dce_emit_basic_result_code(dce, DCE_RC_OK);
    target_dce_transmit(">\r\n", strlen(">\r\n"));

    return DCE_OK;
}

static const command_desc_t g_http_commands[] = {
    {"HTTPCLIENT", &http_commands_handle_client, DCE_EXEC | DCE_WRITE},
    {"HTTPCLIENTCFG", &http_commands_handle_client_cfg, DCE_EXEC | DCE_WRITE},
};

void dce_register_http_commands(dce_t* dce)
{
    dce_register_command_group(dce, "HTTP", g_http_commands, sizeof(g_http_commands)/sizeof(g_http_commands[0]), 0);
}

