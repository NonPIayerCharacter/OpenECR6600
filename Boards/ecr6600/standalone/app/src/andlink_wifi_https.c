/**
* @file andlink_wifi_https.c
* @author 小王同学
* @brief andlink_wifi_https module is used to 
* @version 0.1
* @date 2023-05-04
*
*/
#include <stdio.h>
#include <stddef.h>
#include "oshal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "andlink_wifi_https.h"
#include "system_wifi_def.h"
#include "system_wifi.h"

#include "../../../../../components/trs-tls/trs_tls.h"
#include "../../../../../components/cjson/cJSON.h"
// #include "http_parser.h"
#include "url_parser.h"
#include "ipotek_prov.h"
/***********************************************************
*************************micro define***********************
***********************************************************/
#define MAX_REQUEST_DATA_LEN        (1024)
#define MAX_REQUEST_URL_LEN         (128)

/***********************************************************
***********************typedef define***********************
***********************************************************/
 
 
/***********************************************************
***********************variable define**********************
***********************************************************/
static QueueHandle_t https_queue_handle = NULL;
static char *p_request_data = NULL;
static char *p_request_url = NULL;
TimerHandle_t test_timer_handle = NULL;

const unsigned char ca_pem_str[] =
"-----BEGIN CERTIFICATE----- \r\n\
MIIDxTCCAq2gAwIBAgIBADANBgkqhkiG9w0BAQsFADCBgzELMAkGA1UEBhMCVVMx\
EDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNjb3R0c2RhbGUxGjAYBgNVBAoT\
EUdvRGFkZHkuY29tLCBJbmMuMTEwLwYDVQQDEyhHbyBEYWRkeSBSb290IENlcnRp\
ZmljYXRlIEF1dGhvcml0eSAtIEcyMB4XDTA5MDkwMTAwMDAwMFoXDTM3MTIzMTIz\
NTk1OVowgYMxCzAJBgNVBAYTAlVTMRAwDgYDVQQIEwdBcml6b25hMRMwEQYDVQQH\
EwpTY290dHNkYWxlMRowGAYDVQQKExFHb0RhZGR5LmNvbSwgSW5jLjExMC8GA1UE\
AxMoR28gRGFkZHkgUm9vdCBDZXJ0aWZpY2F0ZSBBdXRob3JpdHkgLSBHMjCCASIw\
DQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAL9xYgjx+lk09xvJGKP3gElY6SKD\
E6bFIEMBO4Tx5oVJnyfq9oQbTqC023CYxzIBsQU+B07u9PpPL1kwIuerGVZr4oAH\
/PMWdYA5UXvl+TW2dE6pjYIT5LY/qQOD+qK+ihVqf94Lw7YZFAXK6sOoBJQ7Rnwy\
DfMAZiLIjWltNowRGLfTshxgtDj6AozO091GB94KPutdfMh8+7ArU6SSYmlRJQVh\
GkSBjCypQ5Yj36w6gZoOKcUcqeldHraenjAKOc7xiID7S13MMuyFYkMlNAJWJwGR\
tDtwKj9useiciAF9n9T521NtYJ2/LOdYq7hfRvzOxBsDPAnrSTFcaUaz4EcCAwEA\
AaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYE\
FDqahQcQZyi27/a9BUFuIMGU2g/eMA0GCSqGSIb3DQEBCwUAA4IBAQCZ21151fmX\
WWcDYfF+OwYxdS2hII5PZYe096acvNjpL9DbWu7PdIxztDhC2gV7+AJ1uP2lsdeu\
9tfeE8tTEH6KRtGX+rcuKxGrkLAngPnon1rpN5+r5N9ss4UXnT3ZJE95kTXWXwTr\
gIOrmgIttRD02JDHBHNA7XIloKmf7J6raBKZV8aPEjoJpL1E/QYVN8Gb5DKj7Tjo\
2GTzLH4U/ALqn83/B2gX2yKQOC16jdFU8WnjXzPKej17CuPKf1855eJ1usV2GDPO\
LPAvTK33sefOT6jEm0pUBsV/fdUID+Ic/n4XuKxe9tQWskMJDE32p2u0mYRlynqI\
4uJEvlz36hz1\r\n\
-----END CERTIFICATE-----";

/***********************************************************
***********************function define**********************
***********************************************************/
static void __hex2str(uint8_t *input, uint16_t input_len, char *output)
{
    char *hexEncode = "0123456789ABCDEF";
    int i = 0, j = 0;

    for (i = 0; i < input_len; i++) {
        output[j++] = hexEncode[(input[i] >> 4) & 0xf];
        output[j++] = hexEncode[(input[i]) & 0xf];
    }
}

static void __get_device_mac_str(char *mac_str)
{
    uint8_t eth_mac[6];
    wifi_get_mac_addr(STATION_IF, eth_mac); // 获取mac地址
    __hex2str(eth_mac, 6, mac_str);

    os_printf(LM_WIFI, LL_INFO, "device mac:%s\r\n", mac_str);
}


/**
 * @brief: 设备注册组包
 *
 * @returns: 
 * 
 * @note: 
 */
void andlink_device_regist_add_packet(char *data, char *url)
{
    if ((NULL == data) || (NULL == url)) {
        return;
    }

    char device_auth_https_reques[200];
    char gw_add[50];
    char keys[33];
    char *str_post = "POST ";
    char *str_http = " HTTP/1.0\r\n";
    // char *str_header =  "Host: "WEB_SERVER"\r\n"
    //                     "Content-Type: application/json\r\n"
    //                     "Accept-Charset: utf-8\r\n";

    char *str_header =  "Content-Type: application/json\r\n"
                        "Accept-Charset: utf-8\r\n";
    char content_str_buf[30];
    char *content_str = "Content-Length: ";
    char *User_Key = "User-Key: ";
    char *add_str = "\r\n";
    char *gateway_address_default = "https://cgw.komect.com/device/inform/bootstrap";
    char *url_path = "/device/inform/bootstrap";
    char i2str[6];
    u16_t content_len = 0;

    os_printf(LM_WIFI, LL_INFO, "andlink_device_regist_add_packet\r\n");

    memset(device_auth_https_reques, 0, sizeof(device_auth_https_reques));
    memset(url, 0, MAX_REQUEST_URL_LEN);
    memset(data, 0, MAX_REQUEST_DATA_LEN);
    memset(content_str_buf, 0, sizeof(content_str_buf));
    memset(i2str, 0, sizeof(i2str));
    prov_get_gw_address2(gw_add);
    strcat(device_auth_https_reques, str_post);
    if (SYS_OK == prov_get_gw_address2(gw_add)) { // 保存的信息里有云网关地址
        memcpy(url, gw_add, strlen(gw_add)-4);          //把端口号去掉
        strcat(device_auth_https_reques, url);
        strcat(device_auth_https_reques, url_path);
        strcat(url, url_path);
    } else {
        memcpy(url, gateway_address_default, strlen(gateway_address_default));
        strcat(device_auth_https_reques, gateway_address_default);
    }
    
    strcat(device_auth_https_reques, str_http);
    strcat(device_auth_https_reques, str_header);
    strcat(device_auth_https_reques, User_Key);

    if(SYS_OK == prov_get_user_key(keys)) {                 //没有userkey信息就用默认userkey
        strcat(device_auth_https_reques, keys);
    } else {
        strcat(device_auth_https_reques, DEFAULT_USER_KEY);
    }
    strcat(device_auth_https_reques,add_str);

    cJSON *root =NULL;
    cJSON *deviceExtInfo = NULL;
    cJSON *chips = NULL;
    cJSON *mcu_info = NULL;

    root = cJSON_CreateObject();
    deviceExtInfo = cJSON_CreateObject();
    chips = cJSON_CreateArray();
    mcu_info = cJSON_CreateObject();
    if(root == NULL) {
        return;
    }
    if(deviceExtInfo == NULL) {
        cJSON_Delete(root);
        return;
    }
    if(chips == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(deviceExtInfo);
        return;
    }
    if(mcu_info == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(deviceExtInfo);
        cJSON_Delete(chips);
        return;
    }

    char str_mac[13] = {0};
    __get_device_mac_str(str_mac);

    cJSON_AddStringToObject(root, "deviceMac", (const char *)str_mac);
    cJSON_AddStringToObject(root, "productToken", ANDLINK_PRODUCT_TOKEN);
    cJSON_AddStringToObject(root, "deviceType", ANDLINK_DEVICE_TYPE);
    cJSON_AddNumberToObject(root, "timestamp", 1683 * 1000);
    cJSON_AddStringToObject(root, "protocolVersion", "V3.2");

    cJSON_AddStringToObject(deviceExtInfo, "cmei", (const char *)str_mac);
    cJSON_AddNumberToObject(deviceExtInfo, "authMode", 0);
    cJSON_AddStringToObject(deviceExtInfo, "authId", "");
    cJSON_AddStringToObject(deviceExtInfo, "mac", (const char *)str_mac);
    cJSON_AddStringToObject(deviceExtInfo, "sn", (const char *)str_mac);
    cJSON_AddStringToObject(deviceExtInfo, "reserve", "hy");
    cJSON_AddStringToObject(deviceExtInfo, "manuDate", "2023-02");
    cJSON_AddStringToObject(deviceExtInfo, "OS", "FreeRtos");

    cJSON_AddStringToObject(mcu_info, "type", "wifi");
    cJSON_AddStringToObject(mcu_info, "factory", "eswin");
    cJSON_AddStringToObject(mcu_info, "model", "ECR6600");

    cJSON_AddItemToArray(chips, mcu_info);
    cJSON_AddItemToObject(deviceExtInfo, "chips", chips);
    cJSON_AddItemToObject(root, "deviceExtInfo", deviceExtInfo);

    char *out = cJSON_Print(root);

    content_len = strlen(out);
    sprintf(i2str, "%d", content_len);
    strcat(content_str_buf,content_str);
    strcat(content_str_buf,i2str);
    strcat(content_str_buf,add_str);
    strcat(device_auth_https_reques,content_str_buf);
    strcat(data, device_auth_https_reques);
    strcat(data, out);
    // log_warn("data :\r\n%s",data);

    // log_warn("json:\r\n %s",out);
    // log_warn("jsonUnformatted:\r\n %s",out2);

    // os_printf(LM_WIFI, LL_INFO, "data:\r\n%s\r\n", data);
    if(root) {
        cJSON_Delete(root);
    }
    if(out) {
        free(out);
    }
}

static int __get_port(const char *url, struct http_parser_url *u)
{
    if (u->field_data[UF_PORT].len) {
        return strtol(&url[u->field_data[UF_PORT].off], NULL, 10);
    } else {
        if (strncasecmp(&url[u->field_data[UF_SCHEMA].off], "http", u->field_data[UF_SCHEMA].len) == 0) {
            return 80;
        } else if (strncasecmp(&url[u->field_data[UF_SCHEMA].off], "https", u->field_data[UF_SCHEMA].len) == 0) {
            return 443;
        }
    }
    return 0;
}

// trs_tls_t test_tls;
static trs_tls_t *trs_tls_conn_http_new(const char *url, const trs_tls_cfg_t *cfg)
{
    /* Parse URI */
    struct http_parser_url u;
    memset(&u, 0, sizeof(u));
    // http_parser_url_init(&u);
    http_parser_parse_url(url, strlen(url), 0, &u);
    os_printf(LM_WIFI, LL_INFO, "sizeof(trs_tls_t):%d\n", sizeof(trs_tls_t));

    trs_tls_t *tls = (trs_tls_t *)calloc(1, sizeof(trs_tls_t));
    if (!tls) {
        return NULL;
    }
    tls->server_fd.fd = -1;
    tls->sockfd = -1;
    os_printf(LM_WIFI, LL_INFO, "start tls:%p &tls->sockfd:%p sizeof(mbedtls_ssl_context):%d\n", tls, &tls->sockfd, sizeof(mbedtls_ssl_context));

    os_printf(LM_WIFI, LL_INFO, "start tls:%p tls->sockfd:%d tls->trs_tls_write:%p tls->trs_tls_read:%p\n", tls, tls->sockfd, tls->trs_tls_write, tls->trs_tls_read);

    if (trs_tls_conn_new_async(&url[u.field_data[UF_HOST].off], u.field_data[UF_HOST].len, __get_port(url, &u), cfg, tls) == 1) {
        os_printf(LM_WIFI, LL_INFO, "end tls:%p tls->sockfd:%d tls->trs_tls_write:%p tls->trs_tls_read:%p\n", tls, tls->sockfd, tls->trs_tls_write, tls->trs_tls_read);
        return tls;
    }
    trs_tls_conn_delete(tls);
    return NULL;

    // memset(&test_tls, 0, sizeof(trs_tls_t));
    // test_tls.server_fd.fd = -1;
    // test_tls.sockfd = -1;
    // os_printf(LM_WIFI, LL_INFO, "start tls:%p test_tls.sockfd:%d test_tls.trs_tls_write:%p test_tls.trs_tls_read:%p\n", &test_tls, test_tls.sockfd, test_tls.trs_tls_write, test_tls.trs_tls_read);

    // if (trs_tls_conn_new_async(&url[u.field_data[UF_HOST].off], u.field_data[UF_HOST].len, __get_port(url, &u), cfg, &test_tls) == 1) {
    //     os_printf(LM_WIFI, LL_INFO, "end tls:%p test_tls.sockfd:%d test_tls.trs_tls_write:%p test_tls.trs_tls_read:%p\n", &test_tls, test_tls.sockfd, test_tls.trs_tls_write, test_tls.trs_tls_read);
    //     return &test_tls;
    // }
    // return NULL;
    // trs_tls_t *tls = trs_tls_conn_new(&url[u.field_data[UF_HOST].off], u.field_data[UF_HOST].len, __get_port(url, &u), cfg);
    // os_printf(LM_WIFI, LL_INFO, "end tls:%p tls->sockfd:%d tls->trs_tls_write:%p tls->trs_tls_read:%p\n", tls, tls->sockfd, tls->trs_tls_write, tls->trs_tls_read);
    // vTaskDelay(pdTICKS_TO_MS(100));
    // return tls;
}

void andlink_https_start_task(void *arg)
{
    HTTPS_REQUEST_E http_request_cmd;
    size_t written_bytes = 0;
    char buf[1024];
    int len;
    ssize_t op_ret;
    p_request_data = (char *)calloc(sizeof(char) * MAX_REQUEST_DATA_LEN, sizeof(char));
    p_request_url = (char *)calloc(sizeof(char) * MAX_REQUEST_URL_LEN, sizeof(char));

    while (1) {
        if (pdTRUE == xQueueReceive(https_queue_handle, (void *)&http_request_cmd, portMAX_DELAY)) {
            os_printf(LM_WIFI, LL_ERR, "http_request_cmd:%d!\n", http_request_cmd);

            if (HTTPS_REQUEST_NUM_DEVICE_AUTH == http_request_cmd) {
                andlink_device_regist_add_packet(p_request_data, p_request_url);
            }

            os_printf(LM_WIFI, LL_ERR, "--->p_request_data:\r\n%s", p_request_data);
            os_printf(LM_WIFI, LL_ERR, "%s!\r\n", p_request_data+477);
            os_printf(LM_WIFI, LL_ERR, "--->p_request_url:\r\n%s\n", p_request_url);

            trs_tls_cfg_t cfg = {
                .cacert_pem_buf = ca_pem_str,
                .cacert_pem_bytes = strlen((const char *)ca_pem_str) + 1,
            };

            trs_tls_t *tls = trs_tls_conn_http_new(p_request_url, &cfg);
            // trs_tls_t *tls = trs_tls_conn_http_new(p_request_url, NULL);
            if(tls == NULL) {
                os_printf(LM_WIFI, LL_ERR, "trs_tls_conn_http_new failed\n");
                goto exit;
            }
            
            do {
                op_ret = trs_tls_conn_write(tls, p_request_data + written_bytes, strlen(p_request_data) - written_bytes);
                if (op_ret >= 0) {
                    os_printf(LM_WIFI, LL_INFO, "%d bytes written \r\n", op_ret);
                    written_bytes += op_ret;
                } else if (op_ret != MBEDTLS_ERR_SSL_WANT_READ  || op_ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                    os_printf(LM_WIFI, LL_INFO, "bytes written error:-0x%x\r\n", -op_ret);
                    goto exit;
                }
            } while(written_bytes < strlen(p_request_data));

            do {
                len = sizeof(buf) - 1;
                memset(buf, 0, sizeof(buf));

                op_ret = trs_tls_conn_read(tls, (char *)buf, len);
                if (op_ret == MBEDTLS_ERR_SSL_WANT_READ  || op_ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                    continue;
                }
                
                if(op_ret < 0) {
                    os_printf(LM_WIFI, LL_INFO, "esp_tls_conn_read  returned -0x%x", -op_ret);
                    break;
                }

                if(op_ret == 0) {
                    os_printf(LM_WIFI, LL_INFO, "connection closed");
                    break;
                }

                len = op_ret;
                os_printf(LM_WIFI, LL_INFO, "%d bytes read", len);
                /* Print response directly to stdout as it is read */
                for(int i = 0; i < len; i++) {
                    putchar(buf[i]);
                }

                // if(len > 0)
                // https_receive_data_deal(buf,len);  
            
            } while(1);
exit:
            trs_tls_conn_delete(tls);
            vTaskDelay(pdMS_TO_TICKS(10000));
            static HTTPS_REQUEST_E data = HTTPS_REQUEST_NUM_DEVICE_AUTH;
            xQueueSend(https_queue_handle, (const void *)&data, 0);
            written_bytes = 0;
        }
        // os_printf(LM_WIFI, LL_INFO, "andlink_https_start_task running!\n");
        // vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelay((TickType_t)NULL);
}

void test_timer_cb(TimerHandle_t xTimer)
{
    // static HTTPS_REQUEST_E data = HTTPS_REQUEST_NUM_GET_GATEWAY_ADDRESS;
    os_printf(LM_WIFI, LL_INFO, "test_timer_cb running!\n");
    // xQueueSend(https_queue_handle, (const void *)&data, 0);
    // if (data++ > HTTPS_REQUEST_NUM_TERMINAL_DATA_REPORT) {
    //     data = 0;
    // }
}

void andlink_https_start(void)
{
    static TaskHandle_t https_task_handle = NULL;

    if (NULL == https_queue_handle) {
        https_queue_handle = xQueueCreate(10, sizeof(HTTPS_REQUEST_E));
        if (NULL == https_queue_handle) {
            os_printf(LM_WIFI, LL_ERR, "https_queue_handle creat error!\n");
            return;
        } else {
            os_printf(LM_WIFI, LL_DBG, "https_queue_handle creat sucess!\n");
        }
    }

    if (NULL == test_timer_handle) {
        test_timer_handle = xTimerCreate("test_timer", pdMS_TO_TICKS(1000), pdTRUE, (void *)0, test_timer_cb);
        xTimerStart(test_timer_handle, 0);
    }

    if (NULL == https_task_handle) {
        if (pdPASS != xTaskCreate(&andlink_https_start_task, "andlink_https_start_task", 1024 * 4, NULL, 14, &https_task_handle)) {
            os_printf(LM_WIFI, LL_ERR, "andlink_https_start_task creat error!\n");
            return;
        } else {
            os_printf(LM_WIFI, LL_DBG, "andlink_https_start_task creat sucess!\n");
        }
    }
    static HTTPS_REQUEST_E data = HTTPS_REQUEST_NUM_DEVICE_AUTH;
    xQueueSend(https_queue_handle, (const void *)&data, 0);
}