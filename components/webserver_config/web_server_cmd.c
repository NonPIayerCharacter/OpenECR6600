#include <stdio.h>

#ifdef CONFIG_WEBSERVER_CONFIG

#include "http_server_service.h"
#include "system_wifi.h"
#include "system_lwip.h"
#include "timers.h"
#include "event_groups.h"
#include "common_define.h"
#include "net_def.h"
#include "cJSON.h"
#ifdef CONFIG_CMD_AT
#include "at.h"
#endif
#include "hal_system.h"
#include "web_html.h"
#include "easyflash.h"
#include "flash.h"
#include "ota.h"
#include "eswin_sdk_version.h"
#include "web_server_cmd.h"
// AT web can use fatfs to storge html or use embeded file to storge html.
// If use fatfs,we should enable AT FS Command support.
#ifdef CONFIG_AT_WEB_USE_FATFS

#endif
#ifdef CONFIG_WEB_CAPTIVE_PORTAL_ENABLE
#include "web_dns_server.h"
static char *s_at_web_redirect_url = NULL;
#endif
int webserver_log_level = TRANS_LOG_DEBUG;

typedef struct router_obj {
    uint8_t ssid[32];
    int8_t rssi;
    uint8_t mac[6];
    SLIST_ENTRY(router_obj) next;
} router_obj_t;

typedef struct web_server_context {
    char base_path[VFS_PATH_MAX + 1];
    char scratch[AT_WEB_SCRATCH_BUFSIZE];
} web_server_context_t;

typedef struct {
    uint8_t ssid[33];
    uint8_t password[65];
} wifi_sta_connect_config_t;

typedef enum {
    AT_WIFI_STA_NOT_START = 0x0,
    AT_WIFI_STA_CONFIG_DONE = 0x1,
    AT_WIFI_STA_CONNECTING = 0x2,
    AT_WIFI_STA_CONNECT_FAIL = 0x3,
    AT_WIFI_STA_CONNECT_OK = 0x4,
    AT_WIFI_STA_RESULT_CHECKED = 0x5,
} At_web_config_wifi_status;

typedef struct {
    At_web_config_wifi_status config_status;
    char sta_ip[AT_WEB_IPV4_MAX_IP_LEN_DEFAULT];
} wifi_sta_connection_info_t;

// new add -----------------------------------------------------------------------------------------------------------

// end new add----------------------------------------------------------------------------------------------------------
static web_server_context_t *s_web_context = NULL;
static httpd_handle_t s_server = NULL;
static int32_t s_at_web_wifi_reconnect_timeout = AT_WEB_WIFI_MAX_RECONNECT_TIMEOUT;
static wifi_sta_connection_info_t s_wifi_sta_connection_info = {0};
static wifi_sta_connect_config_t s_wifi_sta_connect_config = {0};
static TimerHandle_t s_wifi_sta_connect_timer_handler = NULL;
static EventGroupHandle_t s_wifi_sta_connect_event_group = NULL;
static uint8_t s_mobile_phone_mac[6] = {0};
static bool s_sta_got_ip_flag = false;
#ifdef CONFIG_CMD_AT
static const char *s_wifi_start_connect_response = "+WEBSERVERRSP:1\r\n";
static const char *s_wifi_conncet_finish_response = "+WEBSERVERRSP:2\r\n";
static const char *s_ota_start_response = "+WEBSERVERRSP:3\r\n";
static const char *s_ota_receive_success_response = "+WEBSERVERRSP:4\r\n";
static const char *s_ota_receive_fail_response = "+WEBSERVERRSP:5\r\n";
#endif
static SLIST_HEAD(router_fail_list_head_, router_obj) s_router_fail_list = SLIST_HEAD_INITIALIZER(s_router_fail_list);

static wifi_sta_connection_info_t *at_web_get_sta_connection_info(void);
static void at_web_clear_sta_connect_config(void);
// AT web can use fatfs to storge html or use embeded file to storge html.
// If use fatfs,we should enable AT FS Command support.
#ifdef CONFIG_AT_WEB_USE_FATFS
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE; // Handle of the wear levelling library instance
static BYTE pdrv = 0xFF;
#endif

static uint8_t at_web_get_mac_match_len(uint8_t *mac1, uint8_t *mac2, uint8_t mac_length)
{
    uint8_t match_len = 0;
    uint8_t i = 0;
    if (mac1 == NULL || mac2 == NULL) {
        return 0;
    }

    for (i = 0; i < mac_length; i++) {
        if (mac1[i] == mac2[i]) {
            match_len++;
        }
    }

    return match_len;
}

/**
 * @brief Apply ssid、password、bssid to Wi-Fi config and start connect.
 *
 * @param[in] ssid - ssid used for Wi-Fi connect config
 * @param[in] password - password used for Wi-Fi connect config
 * @param[in] bssid - bssid used for Wi-Fi connect config, can be null.
 * @param[in] connect_event - the handler of wifi connect status eventgroup, used to feedback try connect result.
 * 
 * @return
 * - WBS_OK : success
 * - Others : fail
 */
static wbs_err_t at_web_try_connect(uint8_t *ssid, uint8_t *password, uint8_t *bssid, EventGroupHandle_t connect_event)
{
    wbs_err_t ret = WBS_FAIL;
    EventBits_t bits;
    char temp_ssid[AT_WEB_WIFI_SSID_LEN_DEFAULT + 1] = {0};
    //wifi_sta_config_t sta = {0};
    int passwd_len = 0;
    wifi_config_u sta_cfg;		
	memset(&sta_cfg, 0, sizeof(sta_cfg));
    memcpy(sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid));
    passwd_len = strlen((const char *)password);
    if(password == NULL || passwd_len == 0)
    {
        memset(sta_cfg.sta.password, 0, sizeof(sta_cfg.sta.password));
    }
    else
    {
        if(passwd_len == 5 || (passwd_len >= 8 && passwd_len <= WIFI_PWD_MAX_LEN - 1))
        {
            memcpy(sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password));
        }
        else
        {
            WEB_LOGE( "password is error\n");
            return ret;
        }
    }
    memcpy(temp_ssid, ssid, sizeof(sta_cfg.sta.ssid));

    if (bssid != NULL) {
        //sta.bssid_set = 1;
        memcpy(sta_cfg.sta.bssid, bssid, sizeof(sta_cfg.sta.bssid));
    }

    wifi_stop_station();
    ret = wifi_start_station(&sta_cfg);
    if (ret != SYS_OK) {
        WEB_LOGE( "wifi set config fail");
        return ret;
    }
    
    if (connect_event != NULL) { // need to wait wifi connect result, now it's phone config wifi and ssid is null
        bits = xEventGroupWaitBits(connect_event,
            AT_WEB_WIFI_CONNECTED_BIT | AT_WEB_WIFI_FAIL_BIT,
            pdTRUE,
            pdFALSE,
            AT_WEB_WIFI_TRY_CONNECT_TIMEOUT / portTICK_PERIOD_MS); // wait until timeout

        if (bits & AT_WEB_WIFI_CONNECTED_BIT) {
            WEB_LOGI("connected to ap SSID:%s", temp_ssid);
        } else if (bits & AT_WEB_WIFI_FAIL_BIT) {
            WEB_LOGI("connecting to SSID:%s, reconnect timeout", temp_ssid);
            ret = ERR_TIMEOUT;
        } else if (bits == 0) { // timeout expeird
            WEB_LOGI("try connected to ap SSID:%s timeout", temp_ssid);
            ret = WBS_FAIL;
        } else {
            WEB_LOGE( "UNEXPECTED EVENT");
            ret = ERR_INVALID_STATE;
        }
    } else { // don't need to wait wifi connect result
        printf("connect config finish\r\n");
    }
    return ret;
}

/**
 * @brief Get mobile phone's mac connecting to the  AP
 * 
 * @note If there is more than one station connecting to the  AP, the function will return the last station's mac.
 *
 * @param[out] mobile_phone_mac
 * 
 * @return
 *    - WBS_OK
 *    - WBS_FAIL
 */
static wbs_err_t at_web_get_mobile_phone_mac(uint8_t *mobile_phone_mac)
{
    wbs_err_t err;
    struct softap_sta_info sta_list;
    uint32_t num;

    err = wifi_softap_get_sta_list(&sta_list,&num);
    if ((err != WBS_OK) && (err != ERR_WIFI_MODE)) {
        WEB_LOGE( "Get sta list fail");
        return WBS_FAIL;
    } else if (err == ERR_WIFI_MODE) {
        WEB_LOGW("WiFi mode is wrong");
        return WBS_FAIL;
    }
    WEB_LOGI("Find mobile phone mac is : %02X:%02X:%02X:%02X:%02X:%02X", MAC_VALUE(sta_list.mac_addr));
    memcpy(mobile_phone_mac, sta_list.mac_addr, sizeof(sta_list.mac_addr));
    return WBS_OK;
}

// return false means repeat try error connect
static bool check_fail_list(uint8_t *bssid)
{
    if (bssid == NULL) {
        WEB_LOGI("ERROR bssid");
        return false;
    }

    if (!SLIST_EMPTY(&s_router_fail_list)) {
        for (router_obj_t *fail_item = SLIST_FIRST(&s_router_fail_list); fail_item != NULL; fail_item = SLIST_NEXT(fail_item, next)) {
            if (at_web_get_mac_match_len(fail_item->mac, bssid, sizeof(fail_item->mac)) == 6) {
                WEB_LOGI("Skip ssid: %s", fail_item->ssid);
                return false;
            }
        }
    }

    return true;
}

static wbs_err_t stop_scan_filter(void)
{
    if (!SLIST_EMPTY(&s_router_fail_list)) {
        for (router_obj_t *fail_item = SLIST_FIRST(&s_router_fail_list); fail_item != NULL; fail_item = SLIST_NEXT(fail_item, next)) {
            SLIST_REMOVE(&s_router_fail_list, fail_item, router_obj, next);
            os_free(fail_item);
        }
    }
    return WBS_OK;
}

static void insert_fail_list(router_obj_t *item)
{
    if (item == NULL) {
        return;
    }
    
    SLIST_INSERT_HEAD(&s_router_fail_list, item, next);
}

static void ap_record_sort_by_rssi(wifi_info_t *ap_record_array, int len)
{
    int i, j;
    wifi_info_t tmp_ap_record;
    int flag;
    for (i = 0; i < len - 1; i++) {
        flag = 1;
        for (j = 0; j < len - i - 1; j++) {
            if (ap_record_array[j].rssi < ap_record_array[j + 1].rssi) {
                tmp_ap_record = ap_record_array[j];
                ap_record_array[j] = ap_record_array[j + 1];
                ap_record_array[j + 1] = tmp_ap_record;
                flag = 0;
            }
        }
        if (flag == 1) {
            break;
        }  
    }
}

/**
  * @brief Start one scan, and get AP list found in the scan.
  *
  * @param[in,out] number As input param, it stores max AP number ap_records can hold. 
  *                As output param, it receives the actual AP number this API returns.
  * @param[out]    ap_records  wifi_ap_record_t array to hold the found APs
  *
  * @return
  *    - WBS_OK: succeed
  *    - ERR_WIFI_NOT_INIT: WiFi is not initialized by wifi_init
  *    - ERR_WIFI_NOT_STARTED: WiFi is not started by wifi_start
  *    - ERR_INVALID_ARG: invalid argument
  *    - ERR_NO_MEM: out of memory
  */
wbs_err_t at_web_wifi_scan_get_ap_records(uint32_t *number, wifi_info_t *ap_records)
{
    if ((number == NULL) || (ap_records == NULL)) {
        return ERR_INVALID_ARG;
    }
    wbs_err_t ret = WBS_FAIL;

    //ret = at_wifi_scan_start(NULL, true);
    ret = wifi_scan_start(true, NULL);
    if (ret != WBS_OK) {
        WEB_LOGE( "scan start fail");
        return ret;
    }
    int ap_num = wpa_get_scan_num();
    if (ap_num == 0) {
        WEB_LOGW("There is no ap here");
        return WBS_FAIL;
    }
    ap_num = ap_num > AT_WEB_AP_SCAN_NUM_DEFAULT ? AT_WEB_AP_SCAN_NUM_DEFAULT:ap_num;
    *number = ap_num; 
    WEB_LOGI("Total APs scanned = %u", ap_num);
    for(unsigned int i= 0; i < ap_num;i++)
    {   
        ret |= wifi_get_scan_result(i, &ap_records[i]);
    }
    //ret = wifi_scan_get_ap_records(number, ap_records);
    if (ret != WBS_OK) {
        WEB_LOGE( "get scan fail");
        return ret;
    }
    
    // sort ap_record according to rssi 
    ap_record_sort_by_rssi(ap_records, *number);

    return WBS_OK;
}

static wbs_err_t at_web_check_ap_info(wifi_info_t *ap_info)
{
    if (ap_info == NULL) {
        return ERR_INVALID_ARG;
    }
    // check ssid
    if (strlen((const char *)ap_info->ssid) == 0) {
        WEB_LOGD("Ignore hidden ssid");
        goto check_err;
    }
    // check encryption
    if (ap_info->auth > 5) {
        WEB_LOGD("error encryption %d, ssid: %s", ap_info->auth, ap_info->ssid);
        goto check_err;
    }
    if (ap_info->auth < 2) {
        WEB_LOGD("Ignore unsafe password, ssid: %s", ap_info->ssid);
        goto check_err;
    }
    // check rssi
    if (ap_info->rssi < AT_WEB_SCAN_RSSI_THRESHOLD) {
        WEB_LOGD("Ignore low rssi, ssid: %s", ap_info->ssid);
        goto check_err;
    }
    return WBS_OK;
check_err:
    return WBS_FAIL;
}

/**
 * @brief Start Wi-Fi scan and try connect
 *
 * @param[in] phone_mac - the mac of phone which post the connect data.
 * @param[in] password - web server received Wi-Fi connect password.
 * @param[in] max_connect_time - the Max connection time allowed to attempt(include scan delay and try connect time, unit: s).
 * @param[in] connect_event - the handler of wifi connect status eventgroup, used to feedback try connect result.
 * 
 * @return
 *    - WBS_OK
 *    - ERR_INVALID_ARG
 *    - WBS_FAIL
 */
static wbs_err_t at_web_start_scan_filter(uint8_t *phone_mac, uint8_t *password, int32_t max_connect_time, EventGroupHandle_t connect_event)
{
    wbs_err_t ret = WBS_FAIL;
    router_obj_t *last = NULL;
    uint8_t try_connect_count = 0;
    // Calculate the max number of try to connect
    uint8_t max_try_connect_num = (max_connect_time * 1000) / AT_WEB_WIFI_TRY_CONNECT_TIMEOUT;
    int32_t current_available_time = max_connect_time;
    uint32_t ap_scan_number = 0;
    int32_t loop = 0;
    bool last_scan = false;
    uint64_t start = 0;
    uint64_t end = 0;
    router_obj_t *item = NULL;
    router_obj_t *head_item = NULL;
    wifi_info_t *ap_info = NULL;
    uint8_t highest_rssi_connect_count = 0;
    static uint8_t s_connect_success_flag = 0;
    SLIST_HEAD(router_all_list_head_, router_obj) s_router_all_list = SLIST_HEAD_INITIALIZER(s_router_all_list);

    if ((password == NULL) || (max_connect_time <= 0)) {
        return ERR_INVALID_ARG;
    }

    ap_info = (wifi_info_t*) os_malloc(AT_WEB_SCAN_LIST_SIZE * sizeof(wifi_info_t));
    if (ap_info == NULL) {
        WEB_LOGE( "ap info malloc fail");
        return ERR_NO_MEM;
    }

    WEB_LOGD("max connect time is %d", max_connect_time);

    while (max_try_connect_num > 0) {
        start = os_time_get();
        if (current_available_time <= AT_WEB_WIFI_LAST_SCAN_TIMEOUT) {
            last_scan = true;
        }
        // clear the value of the variable
        try_connect_count = 0;
        ap_scan_number = AT_WEB_SCAN_LIST_SIZE;
        memset(ap_info, 0, AT_WEB_SCAN_LIST_SIZE * sizeof(wifi_info_t));

        ret = at_web_wifi_scan_get_ap_records(&ap_scan_number, ap_info);
        if (ret != WBS_OK) {
            WEB_LOGE( "get scan fail");
            goto err;
        }

        for (loop = 0; loop < ap_scan_number; loop++) {
            if (at_web_check_ap_info(&ap_info[loop]) != WBS_OK) {
                continue;
            }

            item = (router_obj_t*) os_malloc(sizeof(router_obj_t));
            if (item == NULL) {
                WEB_LOGE( "router malloc fail");
                break;
            }
            memset(item, 0x0, sizeof(router_obj_t));
            // copy mac
            memcpy(item->mac, ap_info[loop].bssid, sizeof(item->mac));
            // copy ssid
            memcpy(item->ssid, ap_info[loop].ssid, sizeof(item->ssid));
            // cpoy rssi
            item->rssi = ap_info[loop].rssi;

            if (last == NULL) {
                SLIST_INSERT_HEAD(&s_router_all_list, item, next);
            } else {
                SLIST_INSERT_AFTER(last, item, next);
            }

            last = item;
        }

        if (SLIST_EMPTY(&s_router_all_list)) {
            WEB_LOGE( "Not find router");
            goto err;
        }

        // If have mobile phone mac, first consider connect the router which has similar mac
        if (phone_mac != NULL) {
            WEB_LOGI("Try to macth MAC %02X:%02X:%02X:%02X:%02X:%02X\n",phone_mac);
            for (item = SLIST_FIRST(&s_router_all_list); (item != NULL) && (try_connect_count < max_try_connect_num); item = SLIST_NEXT(item, next)) {
                // some phone(like XIAOMI10), the difference between SOFTAP and STA is two bytes
                if ((at_web_get_mac_match_len(phone_mac, item->mac, sizeof(item->mac)) >= 5)) {
                    if (check_fail_list(item->mac)) { // ignore fail connect mac
                        try_connect_count++;
                        ret = at_web_try_connect(item->ssid, password, item->mac, connect_event);
                        if (ret != WBS_OK) {
                            WEB_LOGW("match mac connect error");
                            SLIST_REMOVE(&s_router_all_list, item, router_obj, next);
                            insert_fail_list(item);
                        } else {
                            s_connect_success_flag = 1;
                        }
                        break; // find ssid, skip search
                    }
                }
            }
            if (item == NULL) {
                WEB_LOGI("No macth MAC");
            }
        }
#if AT_WEB_ENABLE_VIRTUAL_MAC_MATCH
        // if connect fail, try to connect the router which has virtual mac
        if (last_scan == true && s_connect_success_flag == 0 && try_connect_count < max_try_connect_num) {
            WEB_LOGI("Try to connect router through virtual MAC");
            for (item = SLIST_FIRST(&s_router_all_list); (item != NULL) && (try_connect_count < max_try_connect_num); item = SLIST_NEXT(item, next)) {
                if ((item->mac[1]) & 0x2) { // Compare the first byte
                    WEB_LOGI("Find a virtual MAC: %02X:%02X:%02X:%02X:%02X:%02X ssid: %s", MAC_VALUE(item->mac), item->ssid);
                    if (check_fail_list(item->mac)) {
                        try_connect_count++;
                        ret = at_web_try_connect(item->ssid, password, item->mac, connect_event);
                        if (ret != WBS_OK) {
                            WEB_LOGW("virtual mac connect error");
                            SLIST_REMOVE(&s_router_all_list, item, router_obj, next);
                            insert_fail_list(item);
                        } else {
                            s_connect_success_flag = 1;
                        }
                        break; // find ssid, skip seach
                    }
                }
            }
            if (item == NULL) {
                WEB_LOGI("No virtual MAC");
            }
        }
#endif

#if AT_WEB_ENABLE_CONNECT_HIGHEST_RSSI
        // if connect fail, try to connect the router which has highest rssi
        if (last_scan == true && s_connect_success_flag == 0 && try_connect_count < max_try_connect_num) {
            WEB_LOGI("Try to connect highest RSSI SSID");
            head_item = SLIST_FIRST(&s_router_all_list);
            highest_rssi_connect_count = AT_WEB_HIGH_RSSI_CONNECT_COUNT;
            while ((highest_rssi_connect_count > 0) && (try_connect_count < max_try_connect_num)) {
                if (check_fail_list(head_item->mac)) {
                    WEB_LOGI("Try to connect highest rssi ssid %s, rssi: %d", head_item->ssid, head_item->rssi);
                    try_connect_count++;
                    ret = at_web_try_connect(head_item->ssid, password, head_item->mac, connect_event);
                    if (ret != WBS_OK) {
                        WEB_LOGW("rssi connect error");
                        SLIST_REMOVE(&s_router_all_list, head_item, router_obj, next);
                        insert_fail_list(head_item);
                    } else {
                        s_connect_success_flag = 1;
                        break;
                    }
                }
                highest_rssi_connect_count--;
                head_item = SLIST_NEXT(head_item, next);
                if (head_item == NULL) {
                    WEB_LOGE( "List is NULL");
                    break;
                }

            }
        }
#endif
        // delete scan list
        for (item = SLIST_FIRST(&s_router_all_list); item != NULL; item = SLIST_NEXT(item, next)) {
            WEB_LOGD("Delete SSID:%s", item->ssid);
            SLIST_REMOVE(&s_router_all_list, item, router_obj, next);
            os_free(item);
        }

        if (s_connect_success_flag) {
            s_connect_success_flag = 0;
            os_free(ap_info);
            ap_info = NULL;
            stop_scan_filter();
            WEB_LOGI("try connect count is %d", try_connect_count);
            return WBS_OK;
        }

        last = NULL;
        end = os_time_get();
        if (end > start) {
            WEB_LOGI("scan and try connect use time is %d", (int32_t)((end - start) / 1000));
            current_available_time -= ((end - start) / 1000);
        } else if (end < start) {
            WEB_LOGI("scan and try connect use time is %d", (int32_t)((end + 0xFFFFFFFFUL - start)/ 1000));
            current_available_time -= ((end + 0xFFFFFFFFUL - start)/ 1000);
        } else {
            WEB_LOGE( "time interval fatal error");
            break;
        }

        if ((current_available_time <= 0) || (current_available_time > max_connect_time)) {
            break;
        } else {
            max_try_connect_num = (current_available_time * 1000) / AT_WEB_WIFI_TRY_CONNECT_TIMEOUT;
            WEB_LOGI("current avail time is %d, max_try_connect_num is %d", current_available_time, max_try_connect_num);
        }
    }
    if (ap_info != NULL) {
        os_free(ap_info);
        ap_info = NULL;
    }
    // delete fail connect list
    stop_scan_filter();
    WEB_LOGW("scan filter timeout");
    return WBS_FAIL;
err:
    os_free(ap_info);
    ap_info = NULL;
    // delete fail connect list
    stop_scan_filter();
    WEB_LOGW("scan filter error");
    return WBS_FAIL;
}

static int at_web_url_decode(char *src, int src_len, char *des, int des_len)
{
    int len = MIN(src_len, des_len);

    memset(des, 0, des_len);
    memcpy(des, src, len);

    return len;
}

/**
 * @brief Find a specific arg in a string of get- or post-data.
 * Line is the string of post/get-data, arg is the name of the value to find. The
 * zero-terminated result is written in buff, with at most buffLen bytes used. The
 * function returns the length of the result, or -1 if the value wasn't found. The
 * returned string will be urldecoded already.
 *
 */
static int at_web_find_arg(char *line, char *arg, char *buff, int buffLen)
{
    char *p,  *e;

    if (line == NULL) {
        return -1;
    }

    p = line;

    while (p != NULL && *p != '\n' && *p != '\r' && *p != 0) {
        if (strncmp(p, arg, strlen(arg)) == 0 && p[strlen(arg)] == '=') {
            p += strlen(arg) + 1; // move p to start of value
            e = (char*) strstr(p, "&");

            if (e == NULL) {
                e = p + strlen(p);
            }

            return at_web_url_decode(p, (e - p), buff, buffLen);
        }

        p = (char*) strstr(p, "&");

        if (p != NULL) {
            p += 1;
        }
    }

    WEB_LOGD("Finding %s in %s: Not found :/\n", arg, line);
    return -1; // not found
}

// AT web can use fatfs to storge html or use embeded file to storge html.
// If use fatfs,we should enable AT FS Command support.
#ifdef CONFIG_AT_WEB_USE_FATFS
/* Send HTTP response with the contents of the requested file */
static wbs_err_t web_common_get_handler(httpd_req_t *req)
{
    char filepath[AT_WEB_FILE_PATH_MAX];
    wbs_err_t err = WBS_FAIL;
    web_server_context_t *s_web_context = (web_server_context_t*) req->user_ctx;
    strlcpy(filepath, s_web_context->base_path, sizeof(filepath));
    strlcat(filepath, "/index.html", sizeof(filepath)); // Now, we just send the index html for the common handler
    
    WEB_LOGW("open file : %s", filepath);
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        WEB_LOGE( "Failed to open file : %s, errno =%d", filepath, errno);
        /*Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return WBS_FAIL;
    }

    httpd_resp_set_type(req, "text/html");

    char *chunk = s_web_context->scratch;
    ssize_t read_bytes;
    do {
        /* Read file in chunks into the scratch buffer */
        read_bytes = read(fd, chunk, AT_WEB_SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            WEB_LOGE( "Failed to read file : %s", filepath);
        } else if (read_bytes > 0) {
            /* Send the buffer contents as HTTP response chunk */
            err = httpd_resp_send_chunk(req, chunk, read_bytes);
            if (err != WBS_OK) {
                close(fd);
                WEB_LOGE( "File sending failed!,err: %d,read_bytes: %d", err, read_bytes);
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return WBS_FAIL;
            }
        }
    } while (read_bytes > 0);
    /* Close file after sending complete */
    close(fd);
    WEB_LOGD("File sending complete");
    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return WBS_OK;
}
#else
static wbs_err_t index_html_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    /* Add file upload form and script which on execution sends a POST request to /upload */
    httpd_resp_send_chunk(req, (const char*) setting_ssid_html_gz, setting_ssid_html_gz_len);
    /* Respond with an empty chunk to signal HTTP response completion */
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* Send HTTP response with the contents of the requested file */
static wbs_err_t web_common_get_handler(httpd_req_t *req)
{
    wifi_sta_connection_info_t *connection_info = at_web_get_sta_connection_info();
    if(connection_info->config_status == AT_WIFI_STA_CONNECT_FAIL ||\
        connection_info->config_status == AT_WIFI_STA_NOT_START)
    {
        at_web_clear_sta_connect_config();
        connection_info->config_status = AT_WIFI_STA_NOT_START;
    }
    return index_html_get_handler(req);

}
#endif

/* A help function to get post request data */
static wbs_err_t recv_post_data(httpd_req_t *req, char *buf)
{
    int total_len = req->content_len;
    int cur_len = 0;
    int received = 0;

    if (total_len >= AT_WEB_SCRATCH_BUFSIZE) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_500(req);
        WEB_LOGE( "context too long");
        return WBS_FAIL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_500(req);
            WEB_LOGE( "Failed to post control value");
            return WBS_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0'; // now ,the post is str format, like ssid=yuxin&pwd=TestPWD&chl=1&ecn=0&maxconn=1&ssidhidden=0
    WEB_LOGI("Post data is : %s\n", buf);
	
    return WBS_OK;
}

static void at_web_response_ok(httpd_req_t *req)
{
    const char *temp_str = "{\"state\": 0}";
    httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    httpd_resp_set_status(req, HTTPD_200);
    
    httpd_resp_send(req, temp_str, strlen(temp_str));
}

static void at_web_response_error(httpd_req_t *req, const char *status)
{
    const char *temp_str = "{\"state\": 1}";
    httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    httpd_resp_set_status(req, status);
    
    httpd_resp_send(req, temp_str, strlen(temp_str));
}

static void at_web_update_sta_connect_config(wifi_sta_connect_config_t *connect_conf)
{
    memcpy(&s_wifi_sta_connect_config, connect_conf, sizeof(wifi_sta_connect_config_t));
}

static wifi_sta_connect_config_t *at_web_get_sta_connect_config(void)
{
    return &s_wifi_sta_connect_config;
}

static void at_web_clear_sta_connect_config(void)
{
    memset(&s_wifi_sta_connect_config, 0x0, sizeof(wifi_sta_connect_config_t));
}

static void at_web_update_sta_connection_info(wifi_sta_connection_info_t *connection_info)
{
    memcpy(&s_wifi_sta_connection_info, connection_info, sizeof(wifi_sta_connection_info_t));
}

static wifi_sta_connection_info_t *at_web_get_sta_connection_info(void)
{
    return &s_wifi_sta_connection_info;
}

void at_web_update_sta_reconnect_timeout(int32_t timeout)
{
    s_at_web_wifi_reconnect_timeout = timeout;
}

int32_t at_web_get_sta_reconnect_timeout(void)
{
    return s_at_web_wifi_reconnect_timeout;
}

void at_web_update_sta_got_ip_flag(bool flag)
{
    s_sta_got_ip_flag = flag;
}

static bool at_web_get_sta_got_ip_flag(void)
{
    return s_sta_got_ip_flag;
}

static void listen_sta_connect_status_timer_cb(TimerHandle_t timer)
{
    wifi_sta_connection_info_t connection_info = {0};
    int32_t reconnnect_timeout = at_web_get_sta_reconnect_timeout();
    int32_t connect_max_count = (reconnnect_timeout * 1000) / AT_WEB_TIMER_POLLING_PERIOD;
    wbs_err_t ret = WBS_FAIL;
    bool sta_got_ip = false;
    static int connect_count = 1;
    WEB_LOGD("Connect callback timer %p count = %d", timer, connect_count);
    struct ip_info sta_ip = {0};
    if (connect_count < connect_max_count) {
        sta_got_ip = at_web_get_sta_got_ip_flag(); // to check whether sta has connnected to appointed ap(like at_wifi_station_get_connect_status())
        if (sta_got_ip != true) {
            connect_count++;
            return;
        } else {
            WEB_LOGI("Connect SSID success");
            ret = wifi_get_ip_info(STATION_IF, &sta_ip);
            if (ret != true) {
                WEB_LOGE( "get sta ip fail");
            }
            WEB_LOGI("Congratulate, got ip: "IP4_ADDR_STR"", IP4_ADDR_VALUE(ip_2_ip4(&sta_ip.ip)));
            sprintf(connection_info.sta_ip, IP4_ADDR_STR,IP4_ADDR_VALUE(ip_2_ip4(&sta_ip.ip)));
            connection_info.config_status = AT_WIFI_STA_CONNECT_OK;
            goto connect_finish;
        }
    } else {
        WEB_LOGW("Listen connect %d times and connect fail", connect_count);
        connection_info.config_status = AT_WIFI_STA_CONNECT_FAIL;
        wifi_stop_station();
        goto connect_finish;
    }
    return;

connect_finish:
    connect_count = 1;
    at_web_update_sta_got_ip_flag(false);
    at_web_update_sta_connection_info(&connection_info);
    //todo:
    #ifdef CONFIG_CMD_AT
    at_printf("%s",s_wifi_conncet_finish_response);
    #endif
    //at_wifi_reconnect_stop();
    xTimerStop(s_wifi_sta_connect_timer_handler, portMAX_DELAY);
    xTimerDelete(s_wifi_sta_connect_timer_handler, portMAX_DELAY);
}

static int udp_create(uint16_t port, char *bind_ip)
{
    struct sockaddr_in dest_addr = {0};
    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in*) &dest_addr;
    int ip_protocol = 0;
    const int on = 1;
    inet_aton(bind_ip, &dest_addr_ip4->sin_addr.s_addr); // bind sta ip
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(port);
    ip_protocol = IPPROTO_IP;

    int sock = socket(AF_INET, SOCK_DGRAM, ip_protocol);
    if (sock < 0) {
        WEB_LOGE( "Unable to create socket: errno %d", errno);
        return -1;
    }

    int err = bind(sock, (struct sockaddr*) &dest_addr, sizeof(dest_addr));
    if (err < 0) {
        WEB_LOGE( "Socket unable to bind: errno %d", errno);
        shutdown(sock, 0);
        close(sock);
        return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
        WEB_LOGE( "reuse addr fail");
        close(sock);
        return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) != 0) {
        WEB_LOGE( "broadcast enable fail");
        close(sock);
        return -1;
    }

    return sock;
}

static int readable_check(int fd, int sec, int usec)
{
    fd_set rset;
    struct timeval tv;

    FD_ZERO(&rset);
    FD_SET(fd, &rset);

    tv.tv_sec = sec;
    tv.tv_usec = usec;
    /* > 0 if descriptor is readable */
    return(select(fd + 1, &rset, NULL, NULL, &tv));
}

static void listen_sta_connect_success_timer_cb(TimerHandle_t timer)
{
    bool sta_got_ip = false;
    wifi_sta_connection_info_t *current_connection_info = at_web_get_sta_connection_info();

    if (current_connection_info->config_status == AT_WIFI_STA_CONNECTING) {
        sta_got_ip = at_web_get_sta_got_ip_flag(); // to check whether sta has connnected to appointed ap(like at_wifi_station_get_connect_status())
        if (sta_got_ip != true) {
            return;
        } else {
            WEB_LOGI("Connect SSID success");
            at_web_update_sta_got_ip_flag(false); // clear connect status flag
            xEventGroupSetBits(s_wifi_sta_connect_event_group, AT_WEB_WIFI_CONNECTED_BIT);
            return;
        }
    }
}

/**
 * @brief Apply WiFi connect info to try connect
 *
 * @param[in] udp_port: indicates the device in use, -1: brower, others: WeChat;
 *  when Wechat is in use and ssid is null, we use udp send wifi connect result.
 *
 * @return
 *  - WBS_OK: Success
 *  - Others: Fail
 */
static wbs_err_t at_web_apply_wifi_connect_info(int32_t udp_port)
{
    wbs_err_t ret;
    wifi_sta_connect_config_t *connect_config = at_web_get_sta_connect_config();
    int32_t reconnnect_timeout = at_web_get_sta_reconnect_timeout();
    wifi_sta_connection_info_t connection_info = {0};
    int send_count = AT_WEB_BROADCAST_TIMES_DEFAULT;
    int udp_socket = -1;
    int len = 0;
    char gateway[AT_WEB_IPV4_MAX_IP_LEN_DEFAULT] = {0};
    struct sockaddr_in broadcast_addr = {0};
    struct sockaddr_in unicast_addr = {0};
    char sendline[64] = {0};
    char rx_buffer[32] = {0};
    // Calculate the max connect time,unit: s
    int32_t connect_timeout = reconnnect_timeout - AT_WEB_BROADCAST_TIMES_DEFAULT * AT_WEB_BROADCAST_INTERVAL_DEFAULT / 1000000;

    at_web_update_sta_got_ip_flag(false);

    if ((strlen((char *)connect_config->ssid) != 0) || (udp_port == -1)) {
        WEB_LOGI("Use SSID %s direct connect", (char *)connect_config->ssid);
        ret = at_web_try_connect(connect_config->ssid, connect_config->password, NULL, NULL);
        if (ret != WBS_OK) {
            WEB_LOGE( "Apply connect fail");
            goto err;
        } else {
            WEB_LOGI("Apply connect success");
            connection_info.config_status = AT_WIFI_STA_CONNECTING;
            at_web_update_sta_connection_info(&connection_info);
            #ifdef CONFIG_CMD_AT
            os_msleep(500);
            at_printf("%s",s_wifi_start_connect_response);
            #endif
        }

        s_wifi_sta_connect_timer_handler = xTimerCreate("listen_sta_connect_status", AT_WEB_TIMER_POLLING_PERIOD / portTICK_PERIOD_MS, pdTRUE,
            NULL, listen_sta_connect_status_timer_cb);
        xTimerStart(s_wifi_sta_connect_timer_handler, 5);
    } else {
        // if have connect to a ap, then disconnect
        wifi_stop_station();
        os_msleep(500);
        s_wifi_sta_connect_event_group = xEventGroupCreate();

        s_wifi_sta_connect_timer_handler = xTimerCreate("listen_sta_connect_success", AT_WEB_TIMER_POLLING_PERIOD / portTICK_PERIOD_MS, pdTRUE,
            NULL, listen_sta_connect_success_timer_cb);
        xTimerStart(s_wifi_sta_connect_timer_handler, 5);
        connection_info.config_status = AT_WIFI_STA_CONNECTING;
        at_web_update_sta_connection_info(&connection_info);
        #ifdef CONFIG_CMD_AT
        at_printf("%s",s_wifi_start_connect_response);
        #endif
        // begin scan and try connect,attention, this function will block until connect successfully or reach max_scan_time or reach connect_num
        ret = at_web_start_scan_filter(s_mobile_phone_mac, connect_config->password, connect_timeout, s_wifi_sta_connect_event_group);
        // when function return, it means have finished scan and try connect.
        // clear connect resource, include Timer and EventGroup.
        if (s_wifi_sta_connect_timer_handler != NULL) {
            xTimerStop(s_wifi_sta_connect_timer_handler, portMAX_DELAY);
            xTimerDelete(s_wifi_sta_connect_timer_handler, portMAX_DELAY);
            s_wifi_sta_connect_timer_handler = NULL;
        }

        if (s_wifi_sta_connect_event_group != NULL) {
            vEventGroupDelete(s_wifi_sta_connect_event_group);
            s_wifi_sta_connect_event_group = NULL;
        }

        memset(s_mobile_phone_mac, 0, sizeof(s_mobile_phone_mac));
        at_web_clear_sta_connect_config();

        struct ip_info sta_ip = {0};
        if (ret != WBS_OK) { // connect fail
            WEB_LOGW("Scan filter fail, timeout");
            connection_info.config_status = AT_WIFI_STA_CONNECT_FAIL;
            wifi_stop_station();
            at_web_update_sta_connection_info(&connection_info);
            #ifdef CONFIG_CMD_AT
            at_printf("%s",s_wifi_conncet_finish_response);
            #endif
        } else { // connect ok
            WEB_LOGI("Connect ap success");
            //ret = net_if_get_ip(sta_if, &sta_ip.ip,&sta_ip.netmask,&sta_ip.gw);
            ret = wifi_get_ip_info(STATION_IF, &sta_ip);
            if (ret != WBS_OK) {
                WEB_LOGE( "get sta ip fail");
            }
            WEB_LOGI("Congratulate, got ip:" IPSTR, IP2STR(ip_2_ip4(&sta_ip.ip)));
            WEB_LOGI("got gw:" IPSTR, IP2STR(ip_2_ip4(&sta_ip.gw))); // got gateway, Actually, it's mobile phone's ap ip.

            sprintf(connection_info.sta_ip, IPSTR, IP2STR(ip_2_ip4(&sta_ip.ip)));
            sprintf(gateway, IPSTR, IP2STR(ip_2_ip4(&sta_ip.gw)));
            
            sprintf(sendline, "ip=%d.%d.%d.%d&port=%d", IP2STR(ip_2_ip4(&sta_ip.ip)), AT_WEB_UDP_PORT_DEFAULT);
            WEB_LOGD("udp send str is %s", sendline);

            connection_info.config_status = AT_WIFI_STA_CONNECT_OK;
            at_web_update_sta_connection_info(&connection_info);
            // creat udp to send wifi connect success to mobile phone.
            udp_socket = udp_create(AT_WEB_UDP_PORT_DEFAULT, connection_info.sta_ip);
            if (udp_socket == -1) {
                goto err;
            }

            broadcast_addr.sin_family = AF_INET;
            broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255"); // broadcast ip (*.*.*.255 can also work).
            broadcast_addr.sin_port = htons(udp_port);

            unicast_addr.sin_family = AF_INET;
            unicast_addr.sin_addr.s_addr = inet_addr(gateway); // unicast ip
            unicast_addr.sin_port = htons(udp_port);

            do {
                len = sendto(udp_socket, sendline, strlen(sendline), 0, (struct sockaddr*) &broadcast_addr, sizeof(broadcast_addr));
                if (len < 0) {
                    WEB_LOGE( "Error occurred during sending: errno %d", errno);
                }
                len = sendto(udp_socket, sendline, strlen(sendline), 0, (struct sockaddr*) &unicast_addr, sizeof(unicast_addr));
                if (len < 0) {
                    WEB_LOGE( "Error occurred during sending: errno %d", errno);
                }
                WEB_LOGI("send ok udp");
                if (readable_check(udp_socket, 0, AT_WEB_BROADCAST_INTERVAL_DEFAULT) > 0) { // wait 500ms to check whether mobile phone send "received" ack
                    len = recvfrom(udp_socket, rx_buffer, sizeof(rx_buffer) - 1, 0, NULL, NULL);
                    if (len < 0) {
                        WEB_LOGE( "recvfrom failed: errno %d", errno);
                    } else { // Data received
                        rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
                        WEB_LOGI("receive %s", rx_buffer);
                        if (strcmp(rx_buffer, AT_WEB_RECEIVED_ACK_MESSAGE) == 0) {
                            WEB_LOGI("Received expected message, finished");
                            break;
                        }
                    }
                }
                send_count--;
            } while (send_count > 0);

            if (udp_socket != -1) {
                shutdown(udp_socket, 0);
                close(udp_socket);
            }
            if (send_count == 0) { 
                WEB_LOGW("no message from WeChat");
            } else { 
                #ifdef CONFIG_CMD_AT
                at_printf("%s",s_wifi_conncet_finish_response);
                #endif
            }
        }
    }
    return WBS_OK;

err:
    connection_info.config_status = AT_WIFI_STA_NOT_START;
    at_web_update_sta_connection_info(&connection_info);
    return WBS_FAIL;
}

static wbs_err_t at_get_wifi_info_from_json_str(char *buffer, wifi_sta_connect_config_t *config)
{
    char ssid[33] = {0}, password[65] = {0};
    int32_t ssid_len = 0, password_len = 0;
    cJSON *root = NULL, *item = NULL/*, *value_item = NULL*/;

    root = cJSON_Parse(buffer);
    if (!root) {
        WEB_LOGE( "Invalid format: [%s]", cJSON_GetErrorPtr());
        return WBS_FAIL;
    }

    int json_item_num = cJSON_GetArraySize(root);
    WEB_LOGD("Total JSON Items:%d", json_item_num);

    item = cJSON_GetObjectItem(root, "ssid");
    if (item) {
        ssid_len = strlen(item->valuestring);
        WEB_LOGD("ssid:%s", item->valuestring);
        if (ssid_len > 32) {
            WEB_LOGE( "ssid is too long");
            return WBS_FAIL;
        } else {
            strncpy(ssid, item->valuestring, ssid_len);
        }
    }

    item = cJSON_GetObjectItem(root, "password");
    if (item) {
        password_len = strlen(item->valuestring);
        WEB_LOGD("password:%s", item->valuestring);
        if (password_len > 64) {
            WEB_LOGE( "password is too long");
            return WBS_FAIL;
        } else {
            strncpy(password, item->valuestring, password_len);
        }
    }
    cJSON_Delete(root);

    memcpy(config->ssid, ssid, ssid_len);
    memcpy(config->password, password, password_len);

    return WBS_OK;
}

static wbs_err_t config_wifi_post_handler(httpd_req_t *req)
{
    char *buf = ((web_server_context_t*) (req->user_ctx))->scratch;
    wifi_sta_connect_config_t wifi_config = {0};
    int str_len = 0;
    int32_t udp_port = -1;
    char temp_str[32] = {0};
    char *args = (char *)req->uri;
    bool ssid_is_null = false;
    wifi_work_mode_e current_wifi_mode;
    wifi_sta_connection_info_t *connection_info = at_web_get_sta_connection_info();
    WEB_LOGD("recv %s\n",req->uri);
    memset(buf, '\0', AT_WEB_SCRATCH_BUFSIZE * sizeof(char));
    current_wifi_mode = wifi_get_opmode();
    if (current_wifi_mode != WIFI_MODE_AP_STA) {
        printf("Error, wifi mode is not correct\r\n");
        goto error_handle;
    }
    // only wifi config not start or have success apply one connection,allow to apply new connect
    if ((connection_info->config_status == AT_WIFI_STA_NOT_START) ||
        (connection_info->config_status == AT_WIFI_STA_CONNECT_FAIL) ||
        (connection_info->config_status == AT_WIFI_STA_CONNECT_OK)) {
        if (recv_post_data(req, buf) != WBS_OK) {
            at_web_response_error(req, HTTPD_500);
            WEB_LOGE( "recv post data error");
            goto error_handle;
        }

        if (at_get_wifi_info_from_json_str(buf, &wifi_config) != WBS_OK) {
            WEB_LOGE( "failed to parse wifi info, json str: %s", buf);
            goto error_handle;
        }
        WEB_LOGD("ssid(%d):%s password:(%d):%s\r\n",
            strlen((char *)wifi_config.ssid), wifi_config.ssid, strlen((char *)wifi_config.password), wifi_config.password);

        // check the validity of ssid and password
        if (strlen((char *)&wifi_config.ssid) == 0) {
            ssid_is_null = true;
        }
        if ((ssid_is_null == true) && (strlen((char*)&wifi_config.password) == 0)) {
            WEB_LOGE( "Error, ssid and password all is null");
            goto error_handle;
        }

        args = strchr(args,'?');
        args += 1;
        str_len = at_web_find_arg(args, "udp_port", (char*)temp_str, sizeof(temp_str));
        if (str_len == -1) {
            WEB_LOGD("udp port is null");
        } else {
            if (strlen(temp_str) == 0) {
                WEB_LOGE( "Error, udp_port is null");
                goto error_handle;
            } else {
                udp_port = atoi(temp_str); // Now, It's WeChat post data to config wifi
            }
        }
        WEB_LOGD("udp port is %d\n",udp_port);
        at_web_update_sta_connect_config(&wifi_config);

        at_web_response_ok(req);
        vTaskDelay(300/portTICK_PERIOD_MS); // to avoid wifi ap channel changed so quickly that the response can not be sent.
        // begin connect
        if (ssid_is_null != true) {
            if (at_web_apply_wifi_connect_info(udp_port) != WBS_OK) {
                WEB_LOGE( "WiFi config connect fail");
                return WBS_FAIL;
            }
        } else {
            // find mac and then start scan to try connect
            if (at_web_get_mobile_phone_mac(s_mobile_phone_mac) != WBS_OK) {
                WEB_LOGW("Cannot find Mobile phone mac");
            }
            if (at_web_apply_wifi_connect_info(udp_port) != WBS_OK) {
                WEB_LOGE( "WiFi config connect fail");
                return WBS_FAIL;
            }
        }
        return WBS_OK;
    }
error_handle:
    at_web_response_error(req, HTTPD_400);
    return WBS_FAIL;
}

/* Simple handler for WiFi_info_get handler */
static wbs_err_t config_wifi_get_handler(httpd_req_t *req)
{
    wifi_sta_connect_config_t *connect_config = at_web_get_sta_connect_config();
    wifi_sta_connection_info_t *connection_info = at_web_get_sta_connection_info();
    char temp_str[32] = {0};
    int32_t json_len = 0;
    char *temp_json_str = ((web_server_context_t*) (req->user_ctx))->scratch;

    httpd_resp_set_type(req, "application/json");

    // according wifi connect status, update state
    if (connection_info->config_status == AT_WIFI_STA_CONNECT_OK) {
        json_len += sprintf(temp_json_str + json_len, "{\"state\":1,"); // it means wifi connect success
    } else if (connection_info->config_status == AT_WIFI_STA_CONNECT_FAIL) {
        json_len += sprintf(temp_json_str + json_len, "{\"state\":2,"); // it means wifi connect fail
    } else {
        json_len += sprintf(temp_json_str + json_len, "{\"state\":0,"); // it means http context OK
    }

    // add ssid to json str
    // note: escape special non-control characters in json format, see https://www.json.org/json-en.html for more details
    json_len += sprintf(temp_json_str + json_len, "\"sta_ssid\":\"");
    int32_t ssid_len = strlen((char *)(connect_config->ssid));
    for (int i = 0; i < ssid_len; i++) {
        uint8_t c = connect_config->ssid[i];
        if (c == '\\' || c == '\"' || c == '/') {
            json_len += sprintf(temp_json_str + json_len, "\\");
        }
        json_len += sprintf(temp_json_str + json_len, "%c", c);
    }
    json_len += sprintf(temp_json_str + json_len, "\",");

    // add password to json str
    // note: escape special non-control characters in json format, see https://www.json.org/json-en.html for more details
    json_len += sprintf(temp_json_str + json_len, "\"sta_password\":\"");
    int32_t password_len = strlen((char *)(connect_config->password));
    for (int i = 0; i < password_len; i++) {
        uint8_t c = connect_config->password[i];
        if (c == '\\' || c == '\"' || c == '/') {
            json_len += sprintf(temp_json_str + json_len, "\\");
        }
        json_len += sprintf(temp_json_str + json_len, "%c", c);
    }
    json_len += sprintf(temp_json_str + json_len, "\",");

    switch (connection_info->config_status) {
    case AT_WIFI_STA_NOT_START:
        strcpy(temp_str, "waiting config");
        break;
    case AT_WIFI_STA_CONFIG_DONE:
        strcpy(temp_str, "config done");
        break;
    case AT_WIFI_STA_CONNECTING:
        strcpy(temp_str, "connecting");
        break;
    case AT_WIFI_STA_CONNECT_FAIL:
        strcpy(temp_str, "connect fail");
        break;
    case AT_WIFI_STA_CONNECT_OK:
        strcpy(temp_str, "connect OK");
        break;
    default:
        break;
    }
    json_len += sprintf(temp_json_str + json_len, "\"message\":\"%s\"}", temp_str);

    WEB_LOGD("now wifi get json str is %s\n", temp_json_str);
    httpd_resp_send(req, temp_json_str, (temp_json_str == NULL) ? 0 : strlen(temp_json_str));

    return WBS_OK;
}

static wbs_err_t accept_wifi_result_post_handler(httpd_req_t *req)
{
    char *buf = ((web_server_context_t*) (req->user_ctx))->scratch;
    int32_t received_flag;
    char temp[4] = {0};
    int str_len = 0;

    wifi_sta_connection_info_t wifi_connection_info = {0};
    wifi_sta_connection_info_t *connection_info = at_web_get_sta_connection_info();
    memset(buf, '\0', AT_WEB_SCRATCH_BUFSIZE * sizeof(char));
    WEB_LOGD("detect web close pages");

    switch (connection_info->config_status) {
    case AT_WIFI_STA_NOT_START:
        break;
    case AT_WIFI_STA_CONFIG_DONE:
    case AT_WIFI_STA_CONNECTING:
    #ifdef CONFIG_CMD_AT
        at_printf("%s",s_wifi_conncet_finish_response);
    #endif
        break;
    case AT_WIFI_STA_CONNECT_FAIL:
    case AT_WIFI_STA_CONNECT_OK:
        if (recv_post_data(req, buf) != WBS_OK) {
            at_web_response_error(req, HTTPD_500);
            WEB_LOGE( "recv post data error");
            return WBS_FAIL;
        }
		
        str_len = at_web_find_arg(buf, "received", (char*) temp, sizeof(temp));
        if (str_len == -1) {
            WEB_LOGE( "received flag is abnormal");
            goto error_handle;
        } else {
            received_flag = atoi(temp);
            WEB_LOGD("received_flag is %d", received_flag);
        }

        // clear wifi connect config and status info
        wifi_connection_info.config_status = AT_WIFI_STA_NOT_START;

        at_web_clear_sta_connect_config();
        at_web_update_sta_connection_info(&wifi_connection_info);

        break;
    default:
        break;
    }
    at_web_response_ok(req);
    return WBS_OK;
error_handle:
    at_web_response_error(req, HTTPD_400);
    return WBS_FAIL;
}

static wbs_err_t ap_record_get_handler(httpd_req_t *req)
{
    uint32_t ap_number = AT_WEB_AP_SCAN_NUM_DEFAULT;
    int loop = 0;
    int32_t json_len = 0;
    int valid_ap_count = 1;
    char *temp_json_str = NULL;
    wifi_info_t *ap_info = (wifi_info_t*) os_malloc(AT_WEB_AP_SCAN_NUM_DEFAULT * sizeof(wifi_info_t));
    if (ap_info == NULL) {
        WEB_LOGE( "ap info malloc fail");
        return WBS_FAIL;
    }
    memset(ap_info, 0, AT_WEB_AP_SCAN_NUM_DEFAULT * sizeof(wifi_info_t));
    
    if (at_web_wifi_scan_get_ap_records(&ap_number, ap_info) != WBS_OK) {
        at_web_response_error(req, HTTPD_500);
        goto error_handle;
    }

    temp_json_str = (char*) os_malloc(AT_WEB_AP_RECORD_JSON_STR_LEN * sizeof(char));
    if (temp_json_str == NULL) {
        WEB_LOGE( "temp_json_str malloc fail");
        goto error_handle;
    }
    memset(temp_json_str, 0, AT_WEB_AP_RECORD_JSON_STR_LEN * sizeof(char));

    httpd_resp_set_type(req, "application/json");
    json_len += sprintf(temp_json_str + json_len, "{\"state\":0,\"message\":\"scan done\",\"aplist\":["); // to get a json array format str

    for (loop = 0; loop < ap_number; loop++) {
        int32_t ssid_len = strlen((const char*)ap_info[loop].ssid);
        if (ssid_len != 0) { // ingore hidden ssid
            json_len += sprintf(temp_json_str + json_len, "{\"ssid\":\"");
            for (int i = 0; i < ssid_len; i++) {
                uint8_t c = ap_info[loop].ssid[i];
                // escape special non-control characters in json format, see https://www.json.org/json-en.html for more details
                if (c == '\\' || c == '\"' || c == '/') {
                    json_len += sprintf(temp_json_str + json_len, "\\");
                }
                json_len += sprintf(temp_json_str + json_len, "%c", c);
            }
            json_len += sprintf(temp_json_str + json_len, "\",\"auth_mode\":%d,\"rssi\":%d},", ap_info[loop].auth, ap_info[loop].rssi);

            valid_ap_count++;
        }
    }
    os_free(ap_info);
    ap_info = NULL;

    json_len += sprintf(temp_json_str + json_len - 1, "]}");
    WEB_LOGD("now, valid ap num is %d, ap records json str is %s\n", valid_ap_count, temp_json_str);
    httpd_resp_send(req, temp_json_str, (temp_json_str == NULL) ? 0 : strlen(temp_json_str));
    os_free(temp_json_str);
    temp_json_str = NULL;
    return WBS_OK;
error_handle:
    os_free(ap_info);
    ap_info = NULL;
    return WBS_FAIL;
}

static wbs_err_t ota_info_get_handler(httpd_req_t *req)
{
    int32_t json_len = 0;
    char *temp_json_str = ((web_server_context_t*) (req->user_ctx))->scratch;
    //partition_t *cur_partition = NULL;
    char *partition_name = NULL;
    unsigned int addr, length;
    int i = 0;
    int valid_i = 0;
    httpd_resp_set_type(req, "application/json");
    // OTA information start
    json_len += sprintf(temp_json_str + json_len, "{");

    // OTA information
    json_len += sprintf(temp_json_str + json_len, "\"state\":0,"); // it means http context OK
    json_len += sprintf(temp_json_str + json_len, "\"fw_version\":\"%s\",", RELEASE_VERSION); // it means http context OK
    #ifdef CONFIG_CMD_AT
    json_len += sprintf(temp_json_str + json_len, "\"at_core_version\":\"%s\",",VERSION_STRING);
    #endif
    // partition information array start
    json_len += sprintf(temp_json_str + json_len, "\"partitions\":");
    json_len += sprintf(temp_json_str + json_len, "[");

    while(partion_info_get_by_index(i++, &partition_name, &addr, &length) == 0)
    {
        if(!strcmp(partition_name,PARTION_NAME_BOOT) 
        || !strcmp(partition_name,PARTION_NAME_OTA_STATUS) \
        || !strcmp(partition_name,"partition") )
        {
            continue;
        }
        else 
        {
            if(valid_i == 0)
            {
                json_len += sprintf(temp_json_str + json_len, "\"%s\"", partition_name);
            }
            else 
            {
                json_len += sprintf(temp_json_str + json_len, ",\"%s\"", partition_name);
            }
            valid_i++;
        }
    }

    // partition information array end
    json_len += sprintf(temp_json_str + json_len, "]");

    // OTA information end
    json_len += sprintf(temp_json_str + json_len, "}");

    WEB_LOGD("now ota get json str is %s\n", temp_json_str);
    httpd_resp_send(req, temp_json_str, strlen(temp_json_str));

    return WBS_OK;
}

static wbs_err_t ota_upgrade(httpd_req_t *req)
{
    char *buf = ((web_server_context_t*) (req->user_ctx))->scratch;
    int recv = 0;
    int total = req->content_len;
    int left = total;
    int ret = 0;
    unsigned int addr;
    unsigned int len;

    ret = partion_info_get(AT_UPGRADE_PARTITION_NAME, &addr, &len);
    if(ret != 0 || total > len)
    {
        WEB_LOGE( "ret: %d total:%d len:%d\n", ret,total,len);
        return WBS_FAIL;
    }
    WEB_LOGI("ota size is %d\n", total);
    memset(buf, 0x0, AT_WEB_SCRATCH_BUFSIZE * sizeof(char));
    ret = ota_init();
    if(ret != 0)
    {
        WEB_LOGE( "ota init failed:%d\n", ret);
        return WBS_FAIL;
    }
    while(left > 0)
    {
        recv = httpd_req_recv(req, buf, MIN(left, AT_WEB_SCRATCH_BUFSIZE));
        if (recv <= 0) 
        {
            if (recv == HTTPD_SOCK_ERR_TIMEOUT) 
            {
                /* Retry if timeout occurred */
                continue;
            }
            WEB_LOGE( "post ota data failed:%d",recv);
            ota_done(0);
            goto err_handler;
        }
        else
        {
            ret = ota_write((unsigned char *)buf,recv);
            if(ret != 0)
            {
                WEB_LOGE( "ota write failed:%d,total:%d left:%d\n",ret,total,left);
                ota_done(0);
                goto err_handler;
            }
            left -= recv;
            WEB_LOGE( "ota total:%d left:%d\n",total,left);
        }
    }
    ret = ota_done(0);

    if (ret != 0) {
        goto err_handler;
    }
    at_web_response_ok(req);
    #ifdef CONFIG_CMD_AT
    at_printf("%s\r\n",s_ota_receive_success_response);
    #endif
    WEB_LOGI("ota successed,restart to active v1");
    hal_system_reset(RST_TYPE_OTA);
    return WBS_OK;

err_handler:
    at_web_response_error(req, HTTPD_500);
    #ifdef CONFIG_CMD_AT
    at_printf("%s\r\n",s_ota_receive_fail_response);
    #endif
    return WBS_FAIL;
}

static wbs_err_t ws_partition_upgrade(httpd_req_t *req, char *buf, const int total, const char *name)
{
    int recv = 0;
    int received_len = 0;
    int left = total;
    int i = 0;
    unsigned int patition_addr = 0;
    unsigned int patition_len = 0;
    char *partition_name = NULL;
    bool find = false;
    //wbs_err_t err = WBS_FAIL;

    if ((buf == NULL) || (total == 0) || (name == NULL)) {
        return WBS_FAIL;
    }

    while(partion_info_get_by_index(i++, &partition_name, &patition_addr, &patition_len) == 0)
    {
        if(!strcmp(partition_name,name))
        {
            find = true;
            break;
        }
    }

    if (partition_name == NULL || find == false) {
        WEB_LOGE( "no custom partition: %s", partition_name);
        return WBS_FAIL;
    }

    if (total > patition_len) {
        WEB_LOGE( "received size %d is not equal %s size %d", total, partition_name, patition_len);
        return WBS_FAIL;
    }

    if(drv_spiflash_erase(patition_addr, patition_len) != WBS_OK){
        WEB_LOGE( "partition_erase_range failed!");
        return WBS_FAIL;
    }

    for (;;) {
        recv = httpd_req_recv(req, buf, MIN(left, AT_WEB_SCRATCH_BUFSIZE));
        if (recv <= 0) {
            if (recv == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry if timeout occurred */
                continue;
            }
            WEB_LOGE( "Failed to receive post ota data, err = %d", recv);
            return WBS_FAIL;
        } else {
            if (drv_spiflash_write(patition_addr, (unsigned char *)buf, patition_len) != WBS_OK) {
                WEB_LOGE( "partition_write failed!");
                return WBS_FAIL;
            }

            received_len += recv;
            left -= recv;

            if (received_len == total) {
                break;
            }
        }
        WEB_LOGE( "total:%d received_len:%d!,left:%d",total,received_len,left);
    }

    return WBS_OK;
}

static wbs_err_t at_customize_partition_upgrade(httpd_req_t *req, const char* partition_name)
{
    wbs_err_t err = WBS_OK;
    char *buf = ((web_server_context_t*) (req->user_ctx))->scratch;
    int bin_len = req->content_len;

    err = ws_partition_upgrade(req, buf, bin_len,partition_name);
    if (err == WBS_OK) {
        at_web_response_ok(req);
        #ifdef CONFIG_CMD_AT
        at_printf("%s\r\n",s_ota_receive_success_response);
        #endif
        return WBS_OK;
    } else {
        at_web_response_error(req, HTTPD_500);
        #ifdef CONFIG_CMD_AT
        at_printf("%s\r\n",s_ota_receive_fail_response);
        #endif
        WEB_LOGE( "%s partition upgrade failed", partition_name);
        return WBS_FAIL;
    }

    return WBS_FAIL;
}

static wbs_err_t ota_data_post_handler(httpd_req_t *req)
{
    char* obj_name = NULL;
    size_t obj_len = 0;
    wbs_err_t err = WBS_OK;

    obj_len = httpd_req_get_hdr_value_len(req, AT_WEB_HEADER_AUTH_NAME) + 1;
    if (obj_len) {
        obj_name = calloc(1, obj_len);

        if (httpd_req_get_hdr_value_str(req, AT_WEB_HEADER_AUTH_NAME, obj_name, obj_len) != WBS_OK) {
            WEB_LOGE( "%s get error", AT_WEB_HEADER_AUTH_NAME);

            os_free(obj_name);
            return WBS_FAIL;
        }

        // Send message.
        #ifdef CONFIG_CMD_AT
        at_printf("%s\r\n",s_ota_start_response);
        #endif

        if (strncmp(obj_name, AT_UPGRADE_PARTITION_NAME, strlen(AT_UPGRADE_PARTITION_NAME)) == 0) {
            err = ota_upgrade(req);
        } else {
            err = at_customize_partition_upgrade(req, obj_name);
        }

        os_free(obj_name);
    }

    return err;
}

#ifdef CONFIG_WEB_CAPTIVE_PORTAL_ENABLE
/* http 404/414 error handler that redirect all requests to the root page */
static wbs_err_t http_common_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
     /* Set status */
    httpd_resp_set_status(req, "302 Temporary Redirect");

    /* Redirect to the "/" root directory */
    httpd_resp_set_hdr(req, "Location", s_at_web_redirect_url);

    /* iOS require content in the response to detect a captive portal,
       simply redirecting is not sufficient.
     */
    httpd_resp_send(req, "Redirect to captive portal", HTTPD_RESP_USE_STRLEN);

    WEB_LOGI("Redirecting to root");

    return WBS_OK;
}
#endif

static wbs_err_t start_web_server(const char *base_path, uint16_t server_port)
{
    AT_WEB_SERVER_CHECK(base_path, "wrong base path", err);
    s_web_context = calloc(1, sizeof(web_server_context_t));
    AT_WEB_SERVER_CHECK(s_web_context, "No memory for rest context", err);
    strlcpy(s_web_context->base_path, base_path, sizeof(s_web_context->base_path));

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.max_open_sockets = 8; // It cannot be less than 7.
    config.server_port = server_port;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 5;
    config.keep_alive_interval = 5;
    config.keep_alive_count = 1;
#ifdef CONFIG_WEB_CAPTIVE_PORTAL_ENABLE
    config.backlog_conn = 8;
    /* this is an important option that isn't set up by default.*/
    config.lru_purge_enable = true;
#endif

    WEB_LOGD("Starting HTTP Server");
    AT_WEB_SERVER_CHECK(httpd_start(&s_server, &config) == WBS_OK, "Start server failed", err_start);

    httpd_uri_t httpd_uri_array[] = {
        {"/getstainfo", HTTP_GET, config_wifi_get_handler, s_web_context},
        {"/setstainfo", HTTP_POST, config_wifi_post_handler, s_web_context},
        {"/getresult", HTTP_POST, accept_wifi_result_post_handler, s_web_context},
        {"/getaprecord", HTTP_GET, ap_record_get_handler, s_web_context},
        {"/getotainfo", HTTP_GET, ota_info_get_handler, s_web_context},
        {"/setotadata", HTTP_POST, ota_data_post_handler, s_web_context},
        {"/", HTTP_GET, web_common_get_handler,s_web_context},
    };

    for (int i = 0; i < sizeof(httpd_uri_array) / sizeof(httpd_uri_t); i++) {
        if (httpd_register_uri_handler(s_server, &httpd_uri_array[i]) != WBS_OK) {
            WEB_LOGE( "httpd register uri_array[%d] fail", i);
        }
    }
#ifdef CONFIG_WEB_CAPTIVE_PORTAL_ENABLE
    size_t redirect_url_sz = AT_WEB_REDIRECT_URL_PREFIX_LEN + strlen(AT_WEB_ROOT_DIR_DEFAULT) + 1; /* strlen(http://255.255.255.255) + strlen("/") + 1 for \0 */
    s_at_web_redirect_url = os_malloc(sizeof(char) * redirect_url_sz);
    *s_at_web_redirect_url = '\0';
    struct ip_info ap_info;
    wifi_get_ip_info(SOFTAP_IF, &ap_info);
    snprintf(s_at_web_redirect_url, redirect_url_sz, "http://"IPSTR"%s", IP2STR(ip_2_ip4(&ap_info.ip)), AT_WEB_ROOT_DIR_DEFAULT);
    
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, http_common_error_handler);
    httpd_register_err_handler(s_server, HTTPD_414_URI_TOO_LONG, http_common_error_handler);
    httpd_register_err_handler(s_server, HTTPD_405_METHOD_NOT_ALLOWED, http_common_error_handler);
#endif

    return WBS_OK;
err_start:
    os_free(s_web_context);
err:
    return WBS_FAIL;
}

static wbs_err_t stop_web_server(void)
{
    AT_WEB_SERVER_CHECK(httpd_stop(s_server) == WBS_OK, "Stop server failed", err);
    os_free(s_web_context);
    s_web_context = NULL;
    s_server = NULL;
    WEB_LOGI("Stop HTTP Server");
#ifdef CONFIG_WEB_CAPTIVE_PORTAL_ENABLE
    if (s_at_web_redirect_url) {
        os_free(s_at_web_redirect_url);
        s_at_web_redirect_url = NULL;
    }
#endif

    return WBS_OK;
err:
    return WBS_FAIL;
}


#ifdef CONFIG_AT_WEB_USE_FATFS 
static wbs_err_t at_web_fatfs_spiflash_mount(const char *base_path,
    const char *partition_label,
    const vfs_fat_mount_config_t *mount_config,
    wl_handle_t *wl_handle)
{
    wbs_err_t result = WBS_OK;

    const partition_t *data_partition = (partition_t*) at_custom_partition_find(PARTITION_TYPE_DATA, PARTITION_SUBTYPE_DATA_FAT, partition_label);
    if (data_partition == NULL) {
        WEB_LOGE( "Failed to find FATFS partition (type='data'(%d), subtype='fat'(%d), partition_label='%s'). Check the partition table.",
            PARTITION_TYPE_DATA, PARTITION_SUBTYPE_DATA_FAT, partition_label);
        return ERR_NOT_FOUND;
    }

    result = wl_mount(data_partition, wl_handle);
    if (result != WBS_OK) {
        WEB_LOGE( "failed to mount wear levelling layer. result = %i", result);
        return result;
    }
    // connect driver to FATFS
    pdrv = 0xFF;
    if (ff_diskio_get_drive(&pdrv) != WBS_OK) {
        WEB_LOGD("the maximum count of volumes is already mounted");
        return ERR_NO_MEM;
    }
    WEB_LOGD("using pdrv=%i", pdrv);
    char drv[3] = { (char) ('0' + pdrv), ':', 0 };

    result = ff_diskio_register_wl_partition(pdrv, *wl_handle);
    if (result != WBS_OK) {
        WEB_LOGE( "ff_diskio_register_wl_partition failed pdrv=%i, error - 0x(%x)", pdrv, result);
        goto fail;
    }
    FATFS *fs;
    result = vfs_fat_register(base_path, drv, mount_config->max_files, &fs);
    if (result == ERR_INVALID_STATE) {
        // it's okay, already registered with VFS
    } else if (result != WBS_OK) {
        WEB_LOGD("vfs_fat_register failed 0x(%x)", result);
        goto fail;
    }

    // Try to mount partition
    FRESULT fresult = f_mount(fs, drv, 1);
    if (fresult != FR_OK) {
        WEB_LOGW("f_mount failed (%d)", fresult);
        result = WBS_FAIL;
        goto fail;
    }
    return WBS_OK;

fail:
    vfs_fat_unregister_path(base_path);
    ff_diskio_unregister(pdrv);
    return result;
}

/*
*  Init file system, assert we can mount the fs.
*/
static wbs_err_t at_web_fatfs_spiflash_init(void)
{
    const vfs_fat_mount_config_t mount_config = {
        .max_files = 5,
        .format_if_mount_failed = false // If cannot mount fs, no need to go down
    };

    wbs_err_t err = at_web_fatfs_spiflash_mount(AT_WEB_MOUNT_POINT, "fatfs", &mount_config, &s_wl_handle);

    if (err != WBS_OK) {
        printf("Failed to mount FATFS (0x%x)", err);
        return WBS_FAIL;
    }

    printf("Mount FATFS success\n");
    return WBS_OK;
}

static wbs_err_t at_web_fatfs_spiflash_deinit(void)
{
    wbs_err_t ret;
    ff_diskio_unregister(pdrv);
    ff_diskio_clear_pdrv_wl(s_wl_handle);
    wl_unmount(s_wl_handle);
    ret = vfs_fat_unregister_path(AT_WEB_MOUNT_POINT);

    return ret;
}
#endif

wbs_err_t at_web_start(uint16_t server_port)
{
    wbs_err_t err;

    if (s_server == NULL) {
        /*AT web can use fatfs to storge html or use embeded file to storge html.If use fatfs, we should enable AT FS Command support*/
#ifdef CONFIG_AT_WEB_USE_FATFS
        err = at_web_fatfs_spiflash_init();
        if (err != WBS_OK) {
            return err;
        }
#endif
        err = start_web_server(AT_WEB_MOUNT_POINT, server_port);
        if (err != WBS_OK) {
            return err;
        }
#ifdef CONFIG_WEB_CAPTIVE_PORTAL_ENABLE
        at_dns_server_start();
#endif
    }

    return WBS_OK;
}

wbs_err_t at_web_destory(void)
{
    wbs_err_t err;

    if (s_server != NULL) {
        if ((err = stop_web_server()) != WBS_OK) {
            return err;
        }
#ifdef CONFIG_WEB_CAPTIVE_PORTAL_ENABLE
        at_dns_server_stop();
#endif
#ifdef CONFIG_AT_WEB_USE_FATFS 
        if ((err = at_web_fatfs_spiflash_deinit()) != WBS_OK) {
            return err;
        }
#endif
    }

    return WBS_OK;
}

#include "system_wifi.h"
extern uint16_t get_macsw_sta_max_cnt(void);
static int webserver_just_test( int argc, char *argv[])
{
    wifi_config_u wifi_config = {0};
    int value = atoi(argv[1]);
    if(value == 1)
    {
        memcpy(&wifi_config.ap.ssid, "WIFI_NET_CONFIG", strlen("WIFI_NET_CONFIG"));
        wifi_config.ap.channel = 6;
        wifi_config.ap.authmode  = AUTH_OPEN;
        wifi_config.ap.max_connect = get_macsw_sta_max_cnt();
        wifi_config.ap.hidden_ssid = 0;
        if (wifi_start_softap(&wifi_config)) {
            printf("wifi_start_softap fail\r\n");
            return -1;
        }
        at_web_start(80);
    }
    else if(value == 0)
    {
        at_web_destory();
        wifi_stop_softap();
    }
    return 0;
}
CLI_CMD(webserver, webserver_just_test, "webserver_just_test", "webserver_just_test");

#endif
