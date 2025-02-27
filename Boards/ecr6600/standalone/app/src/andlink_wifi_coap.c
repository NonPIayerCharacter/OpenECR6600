/**
* @file andlink_wifi_coap.c
* @author 小王同学
* @brief andlink_wifi_coap module is used to 
* @version 0.1
* @date 2023-04-27
*
*/
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/def.h"
#include "coap.h"
#include "../../../../../components/cjson/cJSON.h"
#include "ipotek_prov.h"
#include "andlink_wifi_connect.h"
/***********************************************************
*************************micro define***********************
***********************************************************/
#define PORT 5683
 
/***********************************************************
***********************typedef define***********************
***********************************************************/
 
 
/***********************************************************
***********************variable define**********************
***********************************************************/
bool get_wifi_info = false;
static uint8_t scratch_raw[32];
static coap_rw_buffer_t scratch_buf = {scratch_raw, sizeof(scratch_raw)};
const uint16_t user_rsplen = 1500;
static char user_rsp[1500] = "";

static const coap_endpoint_path_t path_net = {2, {"qlink", "netinfo"}};

static int ipotek_post(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo);
const coap_endpoint_t app_endpoints[] =
{
    {COAP_METHOD_POST, ipotek_post, &path_net, "ct=50"},
    {(coap_method_t)0, NULL, NULL, NULL}
};

/***********************************************************
***********************function define**********************
***********************************************************/
static int ipotek_post(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{
    sys_err_t op_ret = SYS_OK;
    USER_PROV_INFO_T prov_info = {0};
    cJSON *pJson = cJSON_Parse((const char *)inpkt->payload.p);

    memset(&prov_info, 0, sizeof(USER_PROV_INFO_T));

    if (NULL == pJson) {
        os_printf(LM_WIFI, LL_INFO, "--->:ERROR: pMsg is NULL\n");
        return -1;
    }

    cJSON *pSSID = cJSON_GetObjectItem(pJson, "SSID");
    if (NULL != pSSID) {
        os_printf(LM_WIFI, LL_INFO,"ssid is :%s\r\n", pSSID->valuestring);
        memcpy(prov_info.ssid, pSSID->valuestring, strlen(pSSID->valuestring));
        // coap_receive_wifi_info_flag = 1;
    }

    cJSON *ppassword = cJSON_GetObjectItem(pJson, "password");
    if(NULL != ppassword) {
        os_printf(LM_WIFI, LL_INFO, "password is :%s\r\n", ppassword->valuestring);
        memcpy(prov_info.password,ppassword->valuestring,strlen(ppassword->valuestring));
    }

    cJSON *pchannel = cJSON_GetObjectItem(pJson, "channel");
    if(NULL != pchannel) {
        os_printf(LM_WIFI, LL_INFO, "channel is :%d\r\n", pchannel->valueint);
        prov_info.channel = (unsigned char)pchannel->valueint;
    }

    cJSON *pencrypt = cJSON_GetObjectItem(pJson, "encrypt");
    if(NULL != pencrypt) {
        os_printf(LM_WIFI, LL_INFO, "encrypt is :%s\r\n", pencrypt->valuestring);
    }

    cJSON *pCGW = cJSON_GetObjectItem(pJson, "CGW");
    if(pCGW != NULL) {
        cJSON *pgwAddress = cJSON_GetObjectItem(pCGW, "gwAddress");
        if(NULL != pgwAddress) {
            os_printf(LM_WIFI, LL_INFO, "gwAddress is :%s\r\n", pgwAddress->valuestring);
            memcpy(prov_info.gw_address,pgwAddress->valuestring,strlen(pgwAddress->valuestring));
        }

        cJSON *pgwAddress2 = cJSON_GetObjectItem(pCGW, "gwAddress2");
        if(NULL != pgwAddress2) {
            os_printf(LM_WIFI, LL_INFO, "gwAddress2 is :%s\r\n", pgwAddress2->valuestring);
            memcpy(prov_info.gw_address2,pgwAddress2->valuestring,strlen(pgwAddress2->valuestring));
        }

        cJSON *puserkey = cJSON_GetObjectItem(pCGW, "user_key");
        if(NULL != puserkey) {
            os_printf(LM_WIFI, LL_INFO, "userkey is :%s\r\n", puserkey->valuestring);
            memcpy(prov_info.user_key,puserkey->valuestring,strlen(puserkey->valuestring));
        }
    }

    op_ret = ipotek_prov_data_save(&prov_info);
    if (op_ret != SYS_OK) {
        // 一次保存失败后再尝试一次
        os_printf(LM_WIFI, LL_ERR, "ipotek_prov_data_save1 error :%s\r\n", op_ret);
        op_ret = ipotek_prov_data_save(&prov_info);
        if (op_ret != SYS_OK) {
            // 保存失败直接退出
            os_printf(LM_WIFI, LL_ERR, "ipotek_prov_data_save2 error :%s\r\n", op_ret);
            return (-1);
        }
    }
    get_wifi_info = true;
    cJSON_Delete(pJson);

    static char send_data[100];
    static int send_data_len = 0;
    char *response_data;
    cJSON *root;
    root = cJSON_CreateObject();
	if(NULL == root){
        os_printf(LM_WIFI, LL_ERR, "creat cjson error");
        return -1;
	}
    cJSON_AddNumberToObject(root, "respCode", 1);
    cJSON_AddStringToObject(root, "respCont", "success");
    cJSON_AddStringToObject(root, "stbId", "");

    response_data = cJSON_PrintUnformatted(root);
    os_printf(LM_WIFI, LL_INFO, "response:%s\r\n", response_data);

    memset(send_data,0,sizeof(send_data));
    memcpy(send_data,response_data,strlen(response_data));
    send_data_len = strlen(response_data);

    cJSON_Delete(root);
    free(response_data);

    return coap_make_response(scratch, outpkt, (const uint8_t *)&send_data, send_data_len, id_hi, id_lo, &inpkt->tok, COAP_RSPCODE_CONTENT, COAP_CONTENTTYPE_APPLICATION_JSON);
    // return coap_make_response(scratch, outpkt, (const uint8_t *)&light, 1, id_hi, id_lo, &inpkt->tok, COAP_RSPCODE_CONTENT, COAP_CONTENTTYPE_TEXT_PLAIN);
}

static void __app_endponits_set(void)
{
    uint16_t len = user_rsplen;
    const coap_endpoint_t *ep = app_endpoints;
    int i;

    len--; // Null-terminated string

    while(NULL != ep->handler) {
        if (NULL == ep->core_attr) {
            ep++;
            continue;
        }

        if (0 < strlen(user_rsp)) {
            strncat(user_rsp, ",", len);
            len--;
        }

        strncat(user_rsp, "<", len);
        len--;

        for (i = 0; i < ep->path->count; i++) {
            strncat(user_rsp, "/", len);
            len--;

            strncat(user_rsp, ep->path->elems[i], len);
            len -= strlen(ep->path->elems[i]);
        }

        strncat(user_rsp, ">;", len);
        len -= 2;

        strncat(user_rsp, ep->core_attr, len);
        len -= strlen(ep->core_attr);

        ep++;
    }
}


static void __andlink_coap_start_task(void *arg)
{
    int fd;
    uint8_t buf[4096];
    struct sockaddr_in servaddr, cliaddr;
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        os_printf(LM_WIFI, LL_ERR, "Socket Error!\n");
        goto coap_task_back;
    }
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);
    memset(&(servaddr.sin_zero), 0, sizeof(servaddr.sin_zero));
    if ((fd = bind(fd, (struct sockaddr *)&servaddr, sizeof(servaddr))) == -1) {
        os_printf(LM_WIFI, LL_ERR, "bind Error!\n");
        goto coap_task_back;
    }

    __app_endponits_set();
    while(1) {
        int n, rc;
        socklen_t len = sizeof(cliaddr);
        coap_packet_t pkt;

        n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&cliaddr, &len);
        if (0 != (rc = coap_parse(&pkt, buf, n))) {
            os_printf(LM_WIFI, LL_ERR, "Bad packet rc=%d\n", rc);
        } else {
            size_t rsplen = sizeof(buf);
            coap_packet_t rsppkt;
#ifdef DEBUG
            coap_dumpPacket(&pkt);
#endif
            coap_handle_req(&scratch_buf, &pkt, &rsppkt);

            if (0 != (rc = coap_build(buf, &rsplen, &rsppkt))) {
                os_printf(LM_WIFI, LL_ERR, "coap_build failed rc=%d\n", rc);
            } else {
#ifdef DEBUG
                os_printf(LM_WIFI, LL_ERR,"Sending: ");
                coap_dump(buf, rsplen, false);
                coap_dumpPacket(&rsppkt);
#endif
                sendto(fd, buf, rsplen, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
                if (true == get_wifi_info) {
                    andlink_connect_wifi();
                    get_wifi_info = false;
                    break;
                }
            }
        }
    }
coap_task_back:
    if (fd != (-1)) {
        closesocket(fd);
    }
    vTaskDelay((TickType_t)NULL);
}

void andlink_coap_start(void)
{
    xTaskCreate(&__andlink_coap_start_task, "andlink_coap_start_task", 1024 * 2, NULL, 15, NULL); // 创建扫描任务
}