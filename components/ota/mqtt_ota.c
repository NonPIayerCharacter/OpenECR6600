/**
 * @file mqtt_ota.c
 * @brief MQTT OTA implementation
 * @details This file contains the implementation of MQTT OTA functionality
 * @author liuyong
 * @date 2021-04-15
 * @version V0.1
 * @copyright © 2025 BEIJING ESWIN COMPUTING TECHNOLOGY CO., LTD and its affiliates ("ESWIN").
 *            All rights reserved. Any modification, reproduction, adaption, translation,
 *            distribution is prohibited without consent.
 * @par History 1:
 *      Date:
 *      Version:
 *      Author:
 *      Modification:
 *
 * @par History 2:
 */

/*--------------------------------------------------------------------------
 *                                 Include files
 *--------------------------------------------------------------------------*/
#include <stdio.h>
#include "mqtt_client.h"
#include "oshal.h"
#include "cli.h"
#include "system_wifi.h"
#include "platform_tr6600.h"
#include "easyflash.h"
#include "cJSON.h"
#include "hal_system.h"
#include "flash.h"
#include "eswin_sdk_version.h"
#include "hal_rtc.h"
#include "mqtt_ota.h"
#include "http_ota.h"

/*--------------------------------------------------------------------------
 *                                 Local Macros
 *--------------------------------------------------------------------------*/
/** MQTT OTA topic prefix */
#define ESWIN_TOPIC_UPDT_PRE "ESWIN/upgrade"
/** MQTT task topic prefix */
#define ESWIN_TOPIC_TASK_PRE "ESWIN/event/task"
/** MQTT service URL length */
#define MQTT_SERVICE_URL_LEN (64)
/** MQTT service username length */
#define MQTT_SERVICE_USERNAME_LEN (32)
/** MQTT service password length */
#define MQTT_SERVICE_PASSWD_LEN (32)

/*--------------------------------------------------------------------------
 *                                 Local Types
 *--------------------------------------------------------------------------*/
/**
 * @brief MQTT event handle structure
 * @details Contains information about MQTT events
 */

/*--------------------------------------------------------------------------
 *                                 Local Constants
 *--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
 *                                 Local Function Prototypes
 *--------------------------------------------------------------------------*/
static int mqtt_service_get_product_id(char *device_id, unsigned int len);
static int mqtt_service_get_product_key(char *product_key, unsigned int len);
static void service_ota_task(void *arg);

/*--------------------------------------------------------------------------
 *                                 Global Constants
 *--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
 *                                 Global Variables
 *--------------------------------------------------------------------------*/
/** MQTT service URL */
char MQTT_SERVICE_URL[MQTT_SERVICE_URL_LEN] = {0};
/** MQTT service username */
char MQTT_SERVICE_USERNAME[MQTT_SERVICE_USERNAME_LEN] = {0};
/** MQTT service password */
char MQTT_SERVICE_PASSWD[MQTT_SERVICE_PASSWD_LEN] = {0};

/** OTA timer task handle */
os_timer_handle_t ota_timer_task_handle = NULL;
/** OTA task info buffer */
static char g_ota_task_info[8] = {0};
/** OTA task time buffer */
static char g_ota_task_time[24] = {0};
/** OTA downloading flag */
unsigned char g_ota_is_downloading = 0;

/** MQTT client configuration */
trs_mqtt_client_config_t g_mqtt_service_config;
/** MQTT client handle */
trs_mqtt_client_handle_t mq_client;
/** OTA task handle */
int g_service_ota_task_handle = 0;

/*--------------------------------------------------------------------------
 *                                 Function Definitions
 *--------------------------------------------------------------------------*/

/**
 * @brief Get product ID from environment
 * @param[out] device_id Buffer to store product ID
 * @param[in] len Length of buffer
 * @return 0 on success, -1 on failure
 */
static int mqtt_service_get_product_id(char *device_id, unsigned int len)
{
    if (amt_get_env_blob(OTA_PROJECT_ID, device_id, len, NULL) == 0) {
        os_printf(LM_APP, LL_ERR, "OTA:Can not find OTA_PROJECT_ID in dnv\n");

        return -1;
    }

    //os_printf(LM_APP, LL_INFO, "OTA:project_id %s\n", device_id);
    return 0;
}

/**
 * @brief Get product key from environment
 * @param[out] product_key Buffer to store product key
 * @param[in] len Length of buffer
 * @return 0 on success, -1 on failure
 */
static int mqtt_service_get_product_key(char *product_key, unsigned int len)
{
    if (amt_get_env_blob(OTA_SN, product_key, len, NULL) == 0) {
        os_printf(LM_APP, LL_ERR, "OTA:Can not find OTA_SN in dnv\n");

        return -1;
    }

    //os_printf(LM_APP, LL_INFO, "OTA:SN:%s\n", product_key);
    return 0;
}

/**
 * @brief Publish error information with code
 * @param[in] mq_client MQTT client handle
 * @param[in] code Error code
 * @param[in] errstr Error description
 * @return 0 on success, -1 on failure
 */
int mqtt_service_pub_errinfo_with_code(trs_mqtt_client_handle_t mq_client, int code, const char *errstr)
{
    char project_id[OTA_PROJECT_ID_LEN] = {0};
    char sn_key[OTA_SN_LEN] = {0};
    char mac[6]			= {0};
    char mac_string[32] = {0};

    if (mqtt_service_get_product_id(project_id, sizeof(project_id)) != 0) {
        return -1;
    }

    if (mqtt_service_get_product_key(sn_key, sizeof(sn_key)) != 0) {
        return -1;
    }

    if ((strlen((char *)project_id) == 0) || (strlen((char *)sn_key) == 0)) {
        os_printf(LM_APP, LL_ERR, "OTA:please set project_id and sn_key ..\n");

        return -1;
    }

    wifi_get_mac_addr(STATION_IF, (unsigned char *)mac);
    snprintf(mac_string, sizeof(mac_string), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
    char *device_topic = os_zalloc(DATA_MAX_LEN);
    snprintf(device_topic, DATA_MAX_LEN, "ESWIN/event/log/%s/%s/%s", MQTT_SERVICE_OEM, project_id, sn_key);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "protocol_version", MQTT_SERVICE_PROTOCOL);
    cJSON_AddStringToObject(root, "sn", sn_key);
    cJSON_AddStringToObject(root, "mac", mac_string);
    cJSON_AddStringToObject(root, "oem", MQTT_SERVICE_OEM);
    cJSON_AddStringToObject(root, "productid", project_id);
    cJSON_AddNumberToObject(root, "code", code);
    cJSON_AddStringToObject(root, "errsrting", errstr);
    char *data = cJSON_Print(root);

    os_printf(LM_APP, LL_DBG, "OTA:pub errinfo:%s\n", device_topic);
    int ret = trs_mqtt_client_publish(mq_client, device_topic, data, strlen(data), 1, 0);
    os_free(device_topic);
    os_free(data);
    cJSON_Delete(root);

    return ret;
}

/**
 * @brief Publish device information
 * @param[in] mq_client MQTT client handle
 * @return 0 on success, -1 on failure
 */
int mqtt_service_pub_deviceinfo_with_code(trs_mqtt_client_handle_t mq_client)
{
    char project_id[OTA_PROJECT_ID_LEN] = {0};
    char sn_key[OTA_SN_LEN] = {0};
    char mac[6]			= {0};
    char mac_string[32] = {0};

    if (mqtt_service_get_product_id(project_id, sizeof(project_id)) != 0) {
        return -1;
    }

    if (mqtt_service_get_product_key(sn_key, sizeof(sn_key)) != 0) {
        return -1;
    }

    if ((strlen((char *)project_id) == 0) || (strlen((char *)sn_key) == 0)) {
        os_printf(LM_APP, LL_ERR, "OTA:please set project_id and projectid ..\n");

        return -1;
    }

    wifi_get_mac_addr(STATION_IF, (unsigned char *)mac);
    snprintf(mac_string, sizeof(mac_string), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
    char *device_topic = os_zalloc(DATA_MAX_LEN);
    snprintf(device_topic, DATA_MAX_LEN, "ESWIN/event/info/%s/%s/%s", MQTT_SERVICE_OEM, project_id, sn_key);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "protocol_version", MQTT_SERVICE_PROTOCOL);
    cJSON_AddStringToObject(root, "sn", sn_key);
    cJSON_AddStringToObject(root, "mac", mac_string);
    cJSON_AddStringToObject(root, "oem", MQTT_SERVICE_OEM);
    cJSON_AddStringToObject(root, "productid", project_id);
    cJSON_AddStringToObject(root, "softver", (char *)RELEASE_VERSION);
    cJSON_AddStringToObject(root, "hardver", MQTT_HARDWARE_VERSION);
    cJSON_AddStringToObject(root, "sdkver", (char *)sdk_version);
    cJSON_AddStringToObject(root, "cpu", MQTT_BOARD);
    char *data = cJSON_Print(root);

    os_printf(LM_APP, LL_DBG, "OTA:pub deviceinfo:%s\n", device_topic);
    int ret = trs_mqtt_client_publish(mq_client, device_topic, data, strlen(data), 1, 0);
    os_free(device_topic);
    os_free(data);
    cJSON_Delete(root);

    return ret;
}

/**
 * @brief Subscribe to OTA update topics
 * @param[in] mq_client MQTT client handle
 * @return 0 on success, -1 on failure
 */
int mqtt_service_sub_otaupdate(trs_mqtt_client_handle_t mq_client)
{
    char topic[256] = {0};
    char project_id[OTA_PROJECT_ID_LEN] = {0};
    char sn_key[OTA_SN_LEN] = {0};

    if (mqtt_service_get_product_id(project_id, sizeof(project_id)) != 0) {
        return -1;
    }

    if (mqtt_service_get_product_key(sn_key, sizeof(sn_key)) != 0) {
        return -1;
    }

    if ((strlen((char *)project_id) == 0) || (strlen((char *)sn_key) == 0)) {
        os_printf(LM_APP, LL_ERR, "OTA:please set project_id and sn_key ..\n");

        return -1;
    }
    snprintf(topic, sizeof(topic), "ESWIN/upgrade/%s/%s/%s", MQTT_SERVICE_OEM, project_id, sn_key);
    trs_mqtt_client_subscribe(mq_client, topic, 1);

    snprintf(topic, sizeof(topic), "ESWIN/event/task/%s/%s/%s", MQTT_SERVICE_OEM, project_id, sn_key);
    trs_mqtt_client_subscribe(mq_client, topic, 1);

    return 0;
}

/**
 * @brief Handle OTA update event
 * @param[in] event MQTT event handle
 * @return 0 on success, -1 on failure
 */
int mqtt_service_update_handle(trs_mqtt_event_handle_t event)
{
    trs_mqtt_client_handle_t client = event->client;
    cJSON *		 root = cJSON_Parse(event->data);
    cJSON *		 item;
    unsigned int fm_len;
    unsigned int fm_crc;

    os_printf(LM_APP, LL_DBG, "cJson:%s\n", cJSON_Print(root));

    if (root == NULL) {
        os_printf(LM_APP, LL_ERR, "OTA:cJson data parse failed\n");

        return -1;
    }

    item = cJSON_GetObjectItem(root, "target_version");
    if (item == NULL) {
        os_printf(LM_APP, LL_ERR, "OTA:cJson get target_version failed\n");

        return -1;
    }
    if(0 == strcmp(RELEASE_VERSION, item->valuestring)) {
        os_printf(LM_APP, LL_INFO, "OTA:Same release version: V%s\n", RELEASE_VERSION);

        return -1;
    } else {
        os_printf(LM_APP, LL_INFO, "OTA:Release version V%s to V%s\n", RELEASE_VERSION, item->valuestring);
    }


    item = cJSON_GetObjectItem(root, "filecrc");
    if (item == NULL) {
        os_printf(LM_APP, LL_ERR, "OTA:cJson get filecrc failed\n");

        return -1;
    }
    fm_crc = item->valuedouble;
    os_printf(LM_APP, LL_DBG, "OTA:fm_crc:0x%08x\n", fm_crc);

    item = cJSON_GetObjectItem(root, "filesize");
    if (item == NULL) {
        os_printf(LM_APP, LL_ERR, "OTA:cJson get filesize failed\n");

        return -1;
    }
    fm_len = item->valueint;
    os_printf(LM_APP, LL_DBG, "OTA:fm_len:0x%08x\n", fm_len);
    //	if (ota_firmware_check(fm_len, fm_crc) != 0)
    //	{
    //		mqtt_service_pub_errinfo_with_code(client, 2, "crc check failed");
    //		return -1;
    //	}

    item = cJSON_GetObjectItem(root, "fileurl");
    if (item == NULL) {
        os_printf(LM_APP, LL_ERR, "OTA:cJson get fileurl failed\n");

        return -1;
    }
    os_printf(LM_APP, LL_DBG, "fileurl:%s\n", item->valuestring);

    if(ota_timer_task_handle != NULL) {
        os_timer_stop(ota_timer_task_handle);
        ota_timer_task_handle = NULL;
    }

    if(0 == g_ota_is_downloading) {
        g_ota_is_downloading = 1;
        if (http_client_download_file(item->valuestring, true) != 0) {
            mqtt_service_pub_errinfo_with_code(client, 1, "download failed");
            os_printf(LM_APP, LL_ERR, "OTA:download failed!\n");
            // http_client_download_file 内部已调用 ota_done(0)
            g_ota_is_downloading = 0;

            return -1;
        }
        // 如果成功，http_client_download_file 内部调用 ota_done(1) 会重启，不会返回到这里
        g_ota_is_downloading = 0;
    } else {
        os_printf(LM_APP, LL_ERR, "OTA:ota is downloading!\n");
    }

    return 0;
}

/**
 * @brief OTA timer task function
 * @param[in] timer Timer handle
 */
void ota_timer_task_function(os_timer_handle_t timer)
{
    trs_mqtt_client_handle_t client = mq_client;
    static int ota_timer_timout		= 0;

    char*	 set_data_str = (char*)g_ota_task_time;
    DATE_RTC data = {0};
    hal_rtc_utctime_to_date(hal_rtc_get_time(), &data);
    //	os_printf(LM_APP, LL_ERR, "OTA:year %d\n", data.year);
    //	os_printf(LM_APP, LL_ERR, "OTA:month %d\n", data.month);
    //	os_printf(LM_APP, LL_ERR, "OTA:day %d\n", data.day);
    //	os_printf(LM_APP, LL_ERR, "OTA:hour %d\n", data.hour);
    //	os_printf(LM_APP, LL_ERR, "OTA:min %d\n", data.min);
    //	os_printf(LM_APP, LL_ERR, "OTA:sec %d\n", data.sec);
    //	os_printf(LM_APP, LL_ERR, "OTA:wday %d\n", data.wday);
    //
    //	os_printf(LM_APP, LL_ERR, "OTA:set_data_str %s\n", set_data_str);

    char *rtc_string = os_zalloc(strlen((char*)set_data_str) + 20);
    snprintf(rtc_string, strlen((char*)set_data_str) + 20, "%04d-%02d-%02d %02d:%02d:%02d",
             data.year, data.month, data.day, data.hour, data.min, data.sec);

    if(strcmp(set_data_str, rtc_string) < 0) {
        os_printf(LM_APP, LL_ERR, "OTA:Time is up\n");
        os_printf(LM_APP, LL_ERR, "OTA:g_ota_task_info:%s\n", g_ota_task_info);

        if(strcmp(g_ota_task_info, "info") == 0) {
            os_printf(LM_APP, LL_ERR, "OTA:get: info\n");
            mqtt_service_pub_deviceinfo_with_code(client);
            ota_timer_timout++;
            if(ota_timer_timout>5) {
                os_timer_stop(ota_timer_task_handle);
                ota_timer_task_handle = NULL;
            }
        }
        if(strcmp(g_ota_task_info, "reboot") == 0) {
            os_printf(LM_APP, LL_ERR, "OTA:get: reboot\n\n\n");
            hal_system_reset(RST_TYPE_OTA);
        }
    } else {
        os_printf(LM_APP, LL_ERR, "OTA:set %s\n", set_data_str);
        os_printf(LM_APP, LL_ERR, "OTA:now %s\n", rtc_string);
    }
    os_free(rtc_string);
}

/**
 * @brief Handle OTA task event
 * @param[in] event MQTT event handle
 * @return 0 on success, -1 on failure
 */
int mqtt_service_task_handle(trs_mqtt_event_handle_t event)
{
    //trs_mqtt_client_handle_t client = event->client;
    cJSON *root = cJSON_Parse(event->data);
    cJSON *item;

    if (root == NULL) {
        os_printf(LM_APP, LL_ERR, "OTA:cJson data parse failed\n");

        return -1;
    }

    item = cJSON_GetObjectItem(root, "time");
    if (item == NULL) {
        os_printf(LM_APP, LL_ERR, "OTA:cJson get time failed\n");

        return -1;
    }
    os_printf(LM_APP, LL_INFO, "OTA:time:%s\n", item->valuestring);
    if(ota_timer_task_handle == NULL) {
        memcpy(g_ota_task_time, item->valuestring, strlen(item->valuestring));
    }

    item = cJSON_GetObjectItem(root, "task");
    if (item == NULL) {
        os_printf(LM_APP, LL_ERR, "OTA:cJson get time failed\n");

        return -1;
    }
    os_printf(LM_APP, LL_INFO, "OTA:task:%s\n", item->valuestring);

    if(ota_timer_task_handle == NULL) {
        os_printf(LM_APP, LL_INFO, "OTA:Create ota timer thread\n");
        memcpy(g_ota_task_info, item->valuestring, strlen(item->valuestring));
        ota_timer_task_handle = os_timer_create("ota_timer_task", 60000, 1, ota_timer_task_function, NULL);
        os_timer_start(ota_timer_task_handle);
    } else {
        os_printf(LM_APP, LL_INFO, "OTA:Have ota timer thread\n");
    }

    return 0;
}

/**
 * @brief Handle MQTT events
 * @param[in] event MQTT event handle
 * @return 0 on success, -1 on failure
 */
int mqtt_service_event_handle(trs_mqtt_event_handle_t event)
{
    trs_mqtt_client_handle_t client = event->client;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            os_printf(LM_APP, LL_INFO, "OTA:MQTT_EVENT_CONNECTED\n");
            ota_confirm_update();
            mqtt_service_sub_otaupdate(client);
            mqtt_service_pub_deviceinfo_with_code(client);
            break;

        case MQTT_EVENT_DISCONNECTED:
            os_printf(LM_APP, LL_INFO, "OTA:MQTT_EVENT_DISCONNECTED\n");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            os_printf(LM_APP, LL_INFO, "OTA:MQTT_EVENT_SUBSCRIBED, msg_id=%d\n", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            os_printf(LM_APP, LL_INFO, "OTA:MQTT_EVENT_UNSUBSCRIBED, msg_id=%d\n", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            os_printf(LM_APP, LL_INFO, "OTA:MQTT_EVENT_PUBLISHED, msg_id=%d\n", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            os_printf(LM_APP, LL_INFO, "OTA:MQTT_EVENT_DATA\n");
            os_printf(LM_APP, LL_INFO, "OTA:TOPIC=%.*s\n", event->topic_len, event->topic);
            if (strstr(event->topic, ESWIN_TOPIC_UPDT_PRE) != NULL) {
                os_printf(LM_APP, LL_INFO, "OTA:upgrade now\n");
                mqtt_service_update_handle(event);
            }

            if (strstr(event->topic, ESWIN_TOPIC_TASK_PRE) != NULL) {
                os_printf(LM_APP, LL_INFO, "OTA:timer task\n");
                mqtt_service_task_handle(event);
            }
            break;
        case MQTT_EVENT_ERROR:
            os_printf(LM_APP, LL_ERR, "OTA:MQTT_EVENT_ERROR\n");
            break;
        case MQTT_EVENT_ANY:
            os_printf(LM_APP, LL_ERR, "OTA:MQTT_EVENT_ANY\n");
            break;
        case MQTT_EVENT_BEFORE_CONNECT:
            os_printf(LM_APP, LL_ERR, "OTA:MQTT_EVENT_BEFORE_CONNECT\n");
            break;
        case MQTT_EVENT_DELETED:
            os_printf(LM_APP, LL_ERR, "OTA:MQTT_EVENT_DELETED\n");
            break;
        case MQTT_USER_EVENT:
            os_printf(LM_APP, LL_ERR, "OTA:MQTT_USER_EVENT\n");
            break;
    }

    return 0;
}

/**
 * @brief Initialize MQTT service configuration
 * @return 0 on success, -1 on failure
 */
int mqtt_service_config_init(void)
{
    g_mqtt_service_config.client_id = mqtt_malloc(OTA_SN_LEN);
    memset((void*)g_mqtt_service_config.client_id, 0, OTA_SN_LEN);

    if(0 != mqtt_service_get_product_key((char *)g_mqtt_service_config.client_id, OTA_SN_LEN)) {
        return -1;
    }
    //os_printf(LM_APP, LL_ERR, "OTA:client_id:%s\n", g_mqtt_service_config.client_id);

    g_mqtt_service_config.uri  = MQTT_SERVICE_URL;
    g_mqtt_service_config.port = atoi(MQTT_SERVICE_PORT);

    g_mqtt_service_config.username = MQTT_SERVICE_USERNAME;
    g_mqtt_service_config.password = MQTT_SERVICE_PASSWD;

    g_mqtt_service_config.event_handle = mqtt_service_event_handle;

    return 0;
}

/**
 * @brief OTA service task
 * @param[in] arg Task argument
 */
static void service_ota_task(void *arg)
{
    extern bool is_wifi_conn;

    while (!is_wifi_conn) {
        os_msleep(1000);
    }

    os_msleep(2000);

    if ((amt_get_env_blob("mqtt_url", MQTT_SERVICE_URL, MQTT_SERVICE_URL_LEN, NULL) == 0)) {
        strcpy(MQTT_SERVICE_URL, MQTT_SERVICE_URL_DEFAULT);
        os_printf(LM_APP, LL_ERR, "OTA:No mqtt_url in anv! default %s !\n", MQTT_SERVICE_URL);
    } else {
        os_printf(LM_APP, LL_ERR, "OTA:mqtt_url %s\n", MQTT_SERVICE_URL);
    }
    if (amt_get_env_blob("mqtt_user", MQTT_SERVICE_USERNAME, MQTT_SERVICE_USERNAME_LEN, NULL) == 0) {
        strcpy(MQTT_SERVICE_USERNAME, MQTT_SERVICE_USERNAME_DEFAULT);
        os_printf(LM_APP, LL_ERR, "OTA:No matt_user in anv! default %s !\n", MQTT_SERVICE_USERNAME);
    } else {
        os_printf(LM_APP, LL_ERR, "OTA:mqtt_user %s\n", MQTT_SERVICE_USERNAME);
    }
    if (amt_get_env_blob("mqtt_password", MQTT_SERVICE_PASSWD, MQTT_SERVICE_PASSWD_LEN, NULL) == 0) {
        strcpy(MQTT_SERVICE_PASSWD, MQTT_SERVICE_PASSWD_DEFAULT);
        os_printf(LM_APP, LL_ERR, "OTA:No matt_password in anv! default %s !\n", MQTT_SERVICE_PASSWD);
    } else {
        os_printf(LM_APP, LL_ERR, "OTA:mqtt_password %s\n", MQTT_SERVICE_PASSWD);
    }

    char sn_key[OTA_SN_LEN] = {0};
    if (mqtt_service_get_product_key(sn_key, sizeof(sn_key)) == 0) {
        mqtt_service_config_init();
        mq_client = trs_mqtt_client_init(&g_mqtt_service_config);

        if (trs_mqtt_client_start(mq_client) != 0) {
            os_printf(LM_APP, LL_INFO, "OTA:trs_mqtt_client_start error!\n");
            os_task_delete(g_service_ota_task_handle);
        }
    }
	else
	{
        os_printf(LM_APP, LL_INFO, "OTA:start error! No SN!\n");
    }

    os_task_delete(g_service_ota_task_handle);
}

/**
 * @brief Start OTA service
 * @return 0 on success, -1 on failure
 */
int service_ota_main(void)
{
    g_service_ota_task_handle = os_task_create("server_ota", 5, (4 * 1024), (task_entry_t)service_ota_task, NULL);

    return 0;
}
