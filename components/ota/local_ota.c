/*
 * @file local_ota.c
 * @brief Local OTA implementation
 * @details This file contains the implementation of local OTA (Over-The-Air) update functionality
 * @author your_name
 * @date today
 * @version V0.1
 * @Copyright © 2025 BEIJING ESWIN COMPUTING TECHINOLOGY CO., LTD and its affiliates ("ESWIN").
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
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "stdio.h"
#include "easyflash.h"
#include "system_wifi.h"
#include "eswin_sdk_version.h"
#include "local_ota.h"
#include "http_ota.h"

/*--------------------------------------------------------------------------
 *                                 Local Macros
 *--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
 *                                 Local Types structs menus
 *--------------------------------------------------------------------------*/
/** Local OTA callback function type */
typedef void (*ota_cb)(void);

/**
 * @brief Local OTA callback structure
 */
typedef struct {
    ota_cb ota_success_cb;
    ota_cb ota_fail_cb;
    ota_cb ota_action_cb;
} ota_cb_t;

/**
 * @brief Local OTA packet type enumeration
 */
typedef enum {
    OTA_PACKET_BROADCAST = 0x01,
    OTA_PACKET_REPORT	 = 0x10,
    OTA_PACKET_ACTION	 = 0x11,
} ota_packet_type;

/**
 * @brief Local OTA action enumeration
 */
typedef enum {
    LOCAL_OTA_ACTION_SUCESS = 0,
    LOCAL_OTA_ACTION_UPDATE = 1,
    LOCAL_OTA_ACTION_ILLEGAL = 2,
    LOCAL_OTA_ACTION_FAIL = 3,
} local_ota_action;

/**
 * @brief Local OTA receive packet structure
 */
typedef struct {
    unsigned char type;
    unsigned char len;
    char		  url[LOCAL_OTA_DEFAULT_BUFF_LEN];
    unsigned char srcver[LOCAL_OTA_VERSION_NUM];
    unsigned char destver[LOCAL_OTA_VERSION_NUM];
    unsigned int  fm_len;
    unsigned int  fm_crc;
} ota_recv_pack_t;

/**
 * @brief Local OTA send packet structure
 */
typedef struct {
    unsigned char type;
    unsigned char mac[6];
    unsigned char ip[4];
    unsigned char version[LOCAL_OTA_VERSION_NUM];
    unsigned char action;
    unsigned char crc;
} ota_send_pack_t;

/**
 * @brief Local OTA information structure
 */
typedef struct {
    unsigned char	   version[LOCAL_OTA_VERSION_NUM];
    char			   path[LOCAL_OTA_DEFAULT_BUFF_LEN];
    unsigned char	   src_ip[4];
    char			   dst_ip[16];
    unsigned char	   mac[6];
    struct sockaddr_in client_addr;
} ota_info_t;

/**
 * @brief Local OTA information structure
 */
static ota_info_t g_local_ota_info;

/**
 * @brief Local OTA callback structure
 */
static ota_cb_t g_local_ota_callback;

/*--------------------------------------------------------------------------
 *                                 Global Constants
 *--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
 *                                 Global Variables
 *--------------------------------------------------------------------------*/
/** Local OTA SSID configuration */
char CONFIG_LOCAL_OTA_SSID[32] = {0};

/** Local OTA password configuration */
char CONFIG_LOCAL_OTA_PASSWD[32] = {0};

/** Local OTA channel configuration */
char CONFIG_LOCAL_OTA_CHANNEL = 0;

int g_local_ota_task_handle;

/*--------------------------------------------------------------------------
 *                                 Local Function Prototypes
 *--------------------------------------------------------------------------*/
static ota_info_t *get_local_ota_info(void);
static int local_ota_socket_init(void);
static int local_ota_message_send(ota_packet_type type, local_ota_action action);
static int local_ota_message_parse(unsigned char *buf, unsigned int len,
                                   ota_recv_pack_t *message);
static int local_ota_version_compare(unsigned char *src, unsigned char *dest);
static void local_ota_task(void *arg);

/*--------------------------------------------------------------------------
 *                                 Global Function Prototypes
 *--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
 *                                 Function Definitions
 *--------------------------------------------------------------------------*/

/**
 * @brief Get local OTA information
 * @return Pointer to local OTA information structure
 */
static ota_info_t *get_local_ota_info(void)
{
    return &g_local_ota_info;
}

/**
 * @brief Initialize local OTA socket
 * @return Socket descriptor on success, -1 on failure
 */
static int local_ota_socket_init(void)
{
    struct sockaddr_in server_addr;
    int			recv_socket = -1;
    ip_addr_t	addr;
    ota_info_t *ota_info = get_local_ota_info();

    #ifdef CONFIG_IPV6
    if (wifi_get_ip_addr(STATION_IF, (unsigned int *)&(addr.u_addr.ip4.addr)) != 0)
    #else
    if (wifi_get_ip_addr(STATION_IF, (unsigned int *)&(addr.addr)) != 0)
    #endif
    {
        return -1;
    }

    recv_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (recv_socket < 0) {
        os_printf(LM_APP, LL_ERR, "local ota bc socket create failed\n");

        return -1;
    }
    //get myself ip addr/mac
    #ifdef CONFIG_IPV6
    memcpy(ota_info->src_ip, &addr.u_addr.ip4.addr, 4);
    #else
    memcpy(ota_info->src_ip, &addr.addr, 4);
    #endif
    wifi_get_mac_addr(STATION_IF, ota_info->mac);

    #ifdef CONFIG_IPV6
    server_addr.sin_addr.s_addr = addr.u_addr.ip4.addr;
    #else
    server_addr.sin_addr.s_addr = addr.addr;
    #endif
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(LOCAL_OTA_RECV_PORT);
    os_printf(LM_APP, LL_INFO, "%s[%d] mac %02x:%02x:%02x:%02x:%02x:%02x\n", __FUNCTION__, __LINE__,
              ota_info->mac[0], ota_info->mac[1], ota_info->mac[2], ota_info->mac[3], ota_info->mac[4],
              ota_info->mac[5]);
    if (bind(recv_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        os_printf(LM_APP, LL_ERR, "can not bind\r\n");

        return -1;
    }

    return recv_socket;
}

/**
 * @brief Send local OTA message
 * @param type Message type
 * @param action Action type
 * @return 0 on success, -1 on failure
 */
static int local_ota_message_send(ota_packet_type type, local_ota_action action)
{
    int send_socket = -1;
    struct sockaddr_in client_addr;
    ota_send_pack_t	   send_pkg = {0};
    ota_info_t *	   ota_info = get_local_ota_info();

    send_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_socket < 0) {
        os_printf(LM_APP, LL_ERR, "send socket create failed\n");

        return -1;
    }
    client_addr.sin_addr.s_addr = inet_addr(ota_info->dst_ip);
    client_addr.sin_family		= AF_INET;
    client_addr.sin_port		= htons(LOCAL_OTA_SEND_PORT);

    send_pkg.type = type;
    memcpy(send_pkg.mac, ota_info->mac, 6);
    memcpy(send_pkg.ip, ota_info->src_ip, 4);
    memcpy(send_pkg.version, ota_info->version, 6);
    send_pkg.action = action;
    send_pkg.crc = ef_calc_crc8(0, (unsigned char *)&send_pkg,
                                sizeof(send_pkg) - sizeof(send_pkg.crc));

    sendto(send_socket, &send_pkg, sizeof(send_pkg), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
    close(send_socket);

    return 0;
}

/**
 * @brief Parse received OTA message
 * @param buf Received message buffer
 * @param len Message length
 * @param message Output message structure
 * @return 0 on success, -1 on failure
 */
static int local_ota_message_parse(unsigned char *buf, unsigned int len,
                                   ota_recv_pack_t *message)
{
    unsigned char crc = ef_calc_crc8(0, buf, len - 1);
    ota_info_t *  ota_info = get_local_ota_info();
    int	  parse_len = 0;
    char *ptr1, *ptr2;

    crc = ef_calc_crc8(0, buf, len - 1);
    if (crc != buf[len - 1]) {
        os_printf(LM_APP, LL_ERR, "crc 0x%x check error, buf crc 0x%x\n", crc, buf[len - 1]);

        return -1;
    }

    message->type = buf[parse_len];
    parse_len	 += 1;
    //os_printf(LM_APP, LL_ERR, "type=%d\n", message->type);
    if (message->type == OTA_PACKET_ACTION) {
        if (memcmp(ota_info->mac, buf + parse_len, 6) != 0) {
            return 0;
        }

        if (g_local_ota_callback.ota_action_cb != NULL) {
            g_local_ota_callback.ota_action_cb();
        }

        return 0;
    }

    message->len = buf[parse_len];
    parse_len	+= 1;
    memset(message->url, 0, sizeof(message->url));
    memcpy(message->url, buf + parse_len, message->len);
    //os_printf(LM_APP, LL_INFO, "url=%s\n", message->url);
    parse_len += message->len;
    memcpy(message->srcver, buf + parse_len, LOCAL_OTA_VERSION_NUM);
    parse_len += LOCAL_OTA_VERSION_NUM;
    memcpy(message->destver, buf + parse_len, LOCAL_OTA_VERSION_NUM);
    parse_len += LOCAL_OTA_VERSION_NUM;
    memcpy(&message->fm_len, buf + parse_len, sizeof(message->fm_len));
    //os_printf(LM_APP, LL_INFO, "fmlen=0x%x\n", message->fm_len);
    parse_len += sizeof(message->fm_len);
    memcpy(&message->fm_crc, buf + parse_len, sizeof(message->fm_crc));
    //os_printf(LM_APP, LL_INFO, "fmcrc=0x%x\n", message->fm_crc);
    parse_len += sizeof(message->fm_crc);

    ptr1 = strstr(message->url, "http://");
    if (ptr1 == NULL) {
        os_printf(LM_APP, LL_ERR, "url %s not correct\n", message->url);

        return -1;
    }
    ptr1 += strlen("http://");
    ptr2  = strstr(ptr1, "/");

    if (ptr2 == NULL) {
        os_printf(LM_APP, LL_ERR, "url %s not correct\n", ptr1);

        return -1;
    }
    memset(ota_info->dst_ip, 0, sizeof(ota_info->dst_ip));
    memcpy(ota_info->dst_ip, ptr1, ptr2 - ptr1);
    //os_printf(LM_APP, LL_INFO, "dst ip %s\n", ota_info->dst_ip);

    return 0;
}

#if 0
/**
 * @brief Register local OTA callbacks
 * @param cb Callback structure
 * @return 0 on success
 */
static int local_ota_cb_register(ota_cb_t *cb)
{
    g_local_ota_callback.ota_success_cb = cb->ota_success_cb;
    g_local_ota_callback.ota_fail_cb	= cb->ota_fail_cb;
    g_local_ota_callback.ota_action_cb	= cb->ota_action_cb;

    return 0;
}
#endif

/**
 * @brief Compare OTA versions
 * @param src Source version
 * @param dest Destination version
 * @return Comparison result
 */
static int local_ota_version_compare(unsigned char *src, unsigned char *dest)
{
    int i;

    for (i = 0; i < LOCAL_OTA_VERSION_NUM; i++) {
        if (src[LOCAL_OTA_VERSION_NUM - i - 1] == dest[LOCAL_OTA_VERSION_NUM - i - 1]) {
            continue;
        }

        if (src[LOCAL_OTA_VERSION_NUM - i - 1] > dest[LOCAL_OTA_VERSION_NUM - i - 1]) {
            return LOCAL_OTA_VERSION_GREATER;
        } else {
            return LOCAL_OTA_VERSION_LESS;
        }
    }

    return 0;
}

/**
 * @brief Register local OTA version
 * @param version Version string
 * @return 0 on success, -1 on failure
 */
int local_ota_version_register(const char *version)
{
    ota_info_t *ota_info = get_local_ota_info();
    int			i, j = 0;

    memset(ota_info->version, 0, sizeof(ota_info->version));
    for (i = 0; i < strlen(version) && i < LOCAL_OTA_VERSION_NUM; i++) {
        if (version[i] == '.') {
            continue;
        }

        if ((version[i] < 0x30) || (version[i] > 0x39)) {
            return -1;
        }

        ota_info->version[LOCAL_OTA_VERSION_NUM - ++j] = version[i] - 0x30;
    }

    return 0;
}

/**
 * @brief Start local OTA process
 * @return 0 on success, -1 on failure
 */
int local_ota_start(void)
{
    int recv_len,recv_sock;
    unsigned char buf[LOCAL_OTA_DEFAULT_BUFF_LEN];
    bool		  cb_flag = false;
    int update_count	  = 0;
    ota_recv_pack_t recv_message;
    ota_info_t *	ota_info = get_local_ota_info();

    recv_sock = local_ota_socket_init();
    if (recv_sock < 0) {
        return -1;
    }
    os_printf(LM_APP, LL_INFO, "@@@@@@@@the version %d %d %d %d %d %d\r\n",
              ota_info->version[5], ota_info->version[4], ota_info->version[3],
              ota_info->version[2], ota_info->version[1], ota_info->version[0]);

    /** success link factory ssid */
    while (1) {
        recv_len = recv(recv_sock, buf, LOCAL_OTA_DEFAULT_BUFF_LEN, 0);
        if (recv_len <= 0) {
            continue;
        }

        memset(&recv_message, 0, sizeof(recv_message));
        if (local_ota_message_parse(buf, recv_len, &recv_message) != 0) {
            continue;
        }

        if (recv_message.type == OTA_PACKET_ACTION) {
            continue;
        }

        if (local_ota_version_compare(recv_message.destver, ota_info->version) == 0) {
            if ((g_local_ota_callback.ota_success_cb != NULL) && (cb_flag != true)) {
                cb_flag = true;
                g_local_ota_callback.ota_success_cb();
            }

            local_ota_message_send(OTA_PACKET_REPORT, LOCAL_OTA_ACTION_SUCESS);
            //os_printf(LM_APP, LL_ERR, "src version is dest version\r\n");
            continue;
        }
        // && (ota_firmware_check(recv_message.fm_len, recv_message.fm_crc) == 0)
        if (local_ota_version_compare(recv_message.srcver, ota_info->version) == 0) {
            if((g_local_ota_callback.ota_success_cb != NULL) && (cb_flag != true)) {
                cb_flag = true;
                g_local_ota_callback.ota_success_cb();
            }
            local_ota_message_send(OTA_PACKET_REPORT, LOCAL_OTA_ACTION_UPDATE);
            update_count++;
        } else {
            if((g_local_ota_callback.ota_fail_cb != NULL) && (cb_flag != true)) {
                cb_flag = true;
                g_local_ota_callback.ota_fail_cb();
            }

            os_printf(LM_APP, LL_ERR, "illegal update, same version\r\n");
            local_ota_message_send(OTA_PACKET_REPORT, LOCAL_OTA_ACTION_ILLEGAL);
            continue;
        }

        if (update_count == LOCAL_OTA_REPORT_CNT) {
            update_count = 0;

            os_printf(LM_APP, LL_INFO, "recv_message.url=%s  recv_message.len=%d\n", recv_message.url,
                      recv_message.len);
            if (http_client_download_file(recv_message.url, true) == 0) {
                // 成功会重启，不会返回到这里
                break;
            }
            // 下载失败，http_client_download_file 内部已调用 ota_done(0)
            if((g_local_ota_callback.ota_fail_cb != NULL) && (cb_flag != true)) {
                cb_flag = true;
                g_local_ota_callback.ota_fail_cb();
            }
            local_ota_message_send(OTA_PACKET_REPORT, LOCAL_OTA_ACTION_FAIL);
            continue;
        }
    }

    return 0;
}

/**
 * @brief Local OTA task
 * @param arg Task argument
 */
static void local_ota_task(void *arg)
{
    extern bool is_local_ota_conn;
    int			num = 0;
    while (!is_local_ota_conn) {
        os_msleep(1000);
        if(num++ > 30) {
            num = -1;
            break;
        }
    }
    //After a while (30s), if is_local_ota_conn is still false, then local_ota_task can be deleted

    if(-1 != num ) {
        os_msleep(2000);

        local_ota_version_register(RELEASE_VERSION);
        ota_confirm_update();
        local_ota_start();
        os_printf(LM_APP, LL_INFO, "local service exit\r\n");
    } else {
        os_printf(LM_APP, LL_INFO, "local service not connect wifi\r\n");
    }
    os_task_delete(g_local_ota_task_handle);
}

/**
 * @brief Main local OTA function
 */
void local_ota_main()
{
    g_local_ota_task_handle = os_task_create("local_ota", 5, (4 * 1024),
                                             (task_entry_t)local_ota_task, NULL);

    return;
}
