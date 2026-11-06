#ifndef WEB_SERVER_CMD_H
#define WEB_SERVER_CMD_H

#include <stdio.h>
#include <stdint.h>
int webserver_log_level;

#define AT_WEB_SERVER_CHECK(a, str, goto_tag, ...)                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(a))                                                                      \
        {                                                                              \
            WEB_LOGE("%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                             \
        }                                                                              \
    } while (0)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define ERR_WIFI_BASE           0x3000  /*!< Starting number of WiFi error codes */
#define ERR_WIFI_NOT_INIT    (ERR_WIFI_BASE + 1)   /*!< WiFi driver was not installed by wifi_init */
#define ERR_WIFI_NOT_STARTED (ERR_WIFI_BASE + 2)   /*!< WiFi driver was not started by wifi_start */
#define ERR_WIFI_NOT_STOPPED (ERR_WIFI_BASE + 3)   /*!< WiFi driver was not stopped by wifi_stop */
#define ERR_WIFI_IF          (ERR_WIFI_BASE + 4)   /*!< WiFi interface error */
#define ERR_WIFI_MODE        (ERR_WIFI_BASE + 5)   /*!< WiFi mode error */
#define ERR_WIFI_STATE       (ERR_WIFI_BASE + 6)   /*!< WiFi internal state error */
#define ERR_WIFI_CONN        (ERR_WIFI_BASE + 7)   /*!< WiFi internal control block of station or soft-AP error */
#define ERR_WIFI_NVS         (ERR_WIFI_BASE + 8)   /*!< WiFi internal NVS module error */
#define ERR_WIFI_MAC         (ERR_WIFI_BASE + 9)   /*!< MAC address is invalid */
#define ERR_WIFI_SSID        (ERR_WIFI_BASE + 10)   /*!< SSID is invalid */
#define ERR_WIFI_PASSWORD    (ERR_WIFI_BASE + 11)  /*!< Password is invalid */
#define ERR_WIFI_TIMEOUT     (ERR_WIFI_BASE + 12)  /*!< Timeout error */
#define ERR_WIFI_WAKE_FAIL   (ERR_WIFI_BASE + 13)  /*!< WiFi is in sleep state(RF closed) and wakeup fail */
#define ERR_WIFI_WOULD_BLOCK (ERR_WIFI_BASE + 14)  /*!< The caller would block */
#define ERR_WIFI_NOT_CONNECT (ERR_WIFI_BASE + 15)  /*!< Station still in disconnect status */

#define ERR_WIFI_POST        (ERR_WIFI_BASE + 18)  /*!< Failed to post the event to WiFi task */
#define ERR_WIFI_INIT_STATE  (ERR_WIFI_BASE + 19)  /*!< Invalid WiFi state when init/deinit is called */
#define ERR_WIFI_STOP_STATE  (ERR_WIFI_BASE + 20)  /*!< Returned when WiFi is stopping */
#define ERR_WIFI_NOT_ASSOC   (ERR_WIFI_BASE + 21)  /*!< The WiFi connection is not associated */
#define ERR_WIFI_TX_DISALLOW (ERR_WIFI_BASE + 22)  /*!< The WiFi TX is disallowed */
#define VFS_PATH_MAX    100
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define AT_WEB_FILE_PATH_MAX                       (VFS_PATH_MAX + 128)
#define AT_WEB_SCRATCH_BUFSIZE                     320
#define AT_WEB_WIFI_MAX_RECONNECT_TIMEOUT          60     // 60sec
#define AT_WEB_WIFI_MIN_RECONNECT_TIMEOUT          21     // 21sec
#define AT_WEB_MOUNT_POINT                         "/www"
#define AT_WEB_TIMER_POLLING_PERIOD                500    // 500ms
#define AT_WEB_BROADCAST_TIMES_DEFAULT             20     // When connect without ssid, for multicast wifi connect result
#define AT_WEB_BROADCAST_INTERVAL_DEFAULT          500000 // 500000us
#define AT_WEB_UDP_PORT_DEFAULT                    3339   // When connect without ssid, for multicast wifi connect result
#define AT_WEB_IPV4_MAX_IP_LEN_DEFAULT             32
#define AT_WEB_RECEIVED_ACK_MESSAGE                "udp_received"
#define AT_WEB_AP_SCAN_NUM_DEFAULT                 20
#define AT_WEB_AP_RECORD_JSON_STR_LEN              1024
#define AT_WEB_WIFI_CONNECTED_BIT                  BIT0
#define AT_WEB_WIFI_FAIL_BIT                       BIT1
#define AT_WEB_SCAN_RSSI_THRESHOLD                 -50
#define AT_WEB_SCAN_LIST_SIZE                      20
#define AT_WEB_ENABLE_VIRTUAL_MAC_MATCH            0
#define AT_WEB_ENABLE_CONNECT_HIGHEST_RSSI         1
#define AT_WEB_HIGH_RSSI_CONNECT_COUNT             1
#define AT_WEB_WIFI_TRY_CONNECT_TIMEOUT            8000 // try connect timeout is 8000ms
#define AT_WEB_WIFI_SSID_LEN_DEFAULT               32
#define AT_WEB_WIFI_LAST_SCAN_TIMEOUT              10   // 10s
#define AT_WEB_ROOT_DIR_DEFAULT                    "/"
#define AT_WEB_REDIRECT_URL_PREFIX_LEN             24

#define AT_WEB_HEADER_AUTH_NAME                    ("Object")
#define AT_UPGRADE_PARTITION_NAME                  ("cpu")

typedef int wbs_err_t;
#define WEB_LOGI(format, ...) {if(TRANS_LOG_INFO <= webserver_log_level)os_printf(LM_WIFI, LL_INFO,"[I][%s:%d]"format"\n", __FUNCTION__,__LINE__,##__VA_ARGS__);}
#define WEB_LOGE(format, ...) {if(TRANS_LOG_ERROR <= webserver_log_level)os_printf(LM_WIFI, LL_INFO,"[E][%s:%d]"format"\n",__FUNCTION__,__LINE__, ##__VA_ARGS__);}
#define WEB_LOGD(format, ...) {if(TRANS_LOG_DEBUG <= webserver_log_level)os_printf(LM_WIFI, LL_INFO,"[D][%s:%d]"format"\n",__FUNCTION__,__LINE__, ##__VA_ARGS__);}
#define WEB_LOGW(format, ...) {if(TRANS_LOG_WARN <= webserver_log_level)os_printf(LM_WIFI, LL_INFO,"[W][%s:%d]"format"\n", __FUNCTION__,__LINE__,##__VA_ARGS__);}
#define WEB_LOGV(format, ...) {if(TRANS_LOG_ALWAYS <= webserver_log_level)os_printf(LM_WIFI, LL_INFO,"[A][%s:%d]"format"\n",__FUNCTION__,__LINE__, ##__VA_ARGS__);}
#define WBS_FAIL -1
#define WBS_OK 0

wbs_err_t at_web_start(uint16_t server_port);
wbs_err_t at_web_destory(void);

void at_web_update_sta_reconnect_timeout(int32_t timeout);
int32_t at_web_get_sta_reconnect_timeout(void);
#endif