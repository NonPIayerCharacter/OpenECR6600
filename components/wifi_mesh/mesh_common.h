#ifndef __WIFI_COMMON_H__
#define __WIFI_COMMON_H__

#ifndef _FREERTOS
#define _FREERTOS
#endif
#ifndef OS_NO_C_LIB_DEFINES
#define OS_NO_C_LIB_DEFINES
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "utils/list.h"
#include "oshal.h"
#include "includes.h"
#include "common.h"
#include "ieee802_11_defs.h"
#include "system_lwip.h"
#include "hal_hash.h"
#include "system_event.h"

#ifndef __INLINE
#define __INLINE static __attribute__((__always_inline__)) inline
#endif

#define MESH_DBG_NAME  "mesh"
#define MESH_DBG_E     4
#define MESH_DBG_W     3
#define MESH_DBG_I     2
#define MESH_DBG_D     1

#define MESH_DBG_LEVLE   MESH_DBG_D

#if MESH_DBG_E >= MESH_DBG_LEVLE
#define mesh_loge(name, fmt, ...)  os_printf(LM_WIFI, LL_INFO, "[%s]%s"fmt"\r\n",name, "e> ", ##__VA_ARGS__)
#else
#define mesh_loge(name, fmt, ...)
#endif
#if MESH_DBG_E >= MESH_DBG_LEVLE
#define mesh_logw(name, fmt, ...)  os_printf(LM_WIFI, LL_INFO, "[%s]%s"fmt"\n",name, "w> ", ##__VA_ARGS__)
#else
#define mesh_logw(name, fmt, ...)
#endif
#if MESH_DBG_E >= MESH_DBG_LEVLE
#define mesh_logi(name, fmt, ...)  os_printf(LM_WIFI, LL_INFO, "[%s]%s"fmt"\n",name, "i> ", ##__VA_ARGS__)
#else
#define mesh_logi(name, fmt, ...)
#endif
#if MESH_DBG_D >= MESH_DBG_LEVLE
#define mesh_logd(name, fmt, ...)  os_printf(LM_WIFI, LL_INFO, "[%s]%s"fmt"\n",name, "d> ", ##__VA_ARGS__)
#else
#define mesh_logd(name, fmt, ...)
#endif

#define mesh_e(...)  mesh_loge(MESH_DBG_NAME, ##__VA_ARGS__)
#define mesh_w(...)  mesh_logw(MESH_DBG_NAME, ##__VA_ARGS__)
#define mesh_i(...)  mesh_logi(MESH_DBG_NAME, ##__VA_ARGS__)
#define mesh_d(...)  mesh_logd(MESH_DBG_NAME, ##__VA_ARGS__)

#define MESH_MAC_STR				    "%02X:%02X:%02X:%02X:%02X:%02X"
#define MESH_MAC_VALUE(a)			(a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

#define ABS(x)      	(((x) > 0) ? (x) : (0 - (x)))
#ifndef MIN
#define MIN(x,y)		((x)<(y)?(x):(y))
#endif
#ifndef MAX
#define MAX(x,y)		((x)>(y)?(x):(y))
#endif


#define MESH_SOFTAP_SSID     "ESWIN_MESH"
#define MESH_SOFTAP_PWD       "12345678"
#define MESH_SOFTAP_DEFAULT_CHANNEL   (6)
#define MESH_ZERO_MAC   "\x0\x0\x0\x0\x0\x0"
#define MESH_BROADCAST_MAC  "\xff\xff\xff\xff\xff\xff"
#define MESH_CHANNEL_SWITCH_TIMEMS        (110)
#define MESH_PROBE_SEND_TIMEMS            (40)
#define MESH_LAYER_MAX                    (255)

#define MESH_JOIN_TIMEOUTMS               (10000)
#define MESH_VOTE_TIMEOUTMS               (10000)
#define MESH_VOTE_UPDATE_PERIOD           (100)
#define MESH_NETWORK_CFG_TIMEOUTMS        (5000)
#define MESH_FIND_WAIT_TIMEMS             (1000)
#define MESH_RECONNECT_TIMEMS             (30000)
#define MESH_NODE_BROADCAST_PERIOD        (100)
#define MESH_MOVE_CHECK_PERIOD            (1000*60)
#define MESH_TABLE_REFRESH_PERIOD         (10000)
#define MESH_DELAY_JOIN_TIMEMS            (1500)
#define MESH_DELAY_AP_ALLOW_TIMEMS        (15000)
#define MESH_ROOT_LOST_DELAY_JOIN_TIMEMS  (3000)
#define MESH_NETWORK_CFG_COUNTDOWN_TIMMS     (6000) /*10min*/
#define MESH_NETWORK_CFG_STATEWSWITCH_TIMMS  (50) /*5s*/
#define MESH_NODE_BLACKLIST_REFRESH_PERIOD   (1000)
#define MESH_NODE_BLACKLIST_KEEP_TIMEMS  (180000)

#define MESH_AUTH_UNLOCK_CHANNEL_TIMEMS   (3000)
#define MESH_SOFTAP_UNLOCK_CHANNEL_TIMEMS (1000 * 60)

#define MESH_EID_VENDOR_SPECIFIC        (221)
#define MESH_OUI              "\x11\x22\x33"
#define MESH_OUI_LEN          (3)
#define MESH_VENDOR_OUI_LEN   (4)
#define MESH_TLV_MAX_LEN      (251)
#define MESH_MAC_AADR_LEN     (6)
#define MESH_ID_LEN           (MESH_MAC_AADR_LEN)
#define MESH_TLV_DATA_OFFSET  (6) /*t + l + ou + oui tupe)*/

#define MESH_HIGH_RSSI_THD   (-40)
#define MESH_LOW_RSSI_THD    (-70)

#define MESH_LOSS_RSSI             (6)
#define MESH_RSSI_MIN_THD          (-96)
#define MESH_SOFTAP_MAX_STA_CNT    (8)
#define MESH_MOVE_DIFF_RSSI        (9)

#define MESH_NODE_ENABLE_JOIN      (0)
#define MESH_NODE_DISABLE_JOIN     (0x80)
#define MESH_MODE_NODE_JOIN_CNT_MASK   (0x3f)
#define MESH_NODE_IS_ENABLE_JOIN(status)  (!(status & MESH_NODE_DISABLE_JOIN))

#define MESH_NODE_CONNECT_AP    (0x40)
#define MESH_NODE_IS_CONNECT_AP(status)   (!!(status & MESH_NODE_CONNECT_AP))

#define MESH_NODE_ALIVE_PERIOD     (8000)
#define MESH_NODE_IS_ACTIVE(now, last_active) (now > last_active + MESH_NODE_ALIVE_PERIOD)

#define MESH_STA_IP_AID_OFFSET     (DHCPS_MAX_LEASE)

#define MESH_DEAULT_BEACON_INTERVAL   (100)  /*100 ms*/

#define MESH_MAX_RSSI_RECORD_NUM                  (10)

typedef enum {
    MESH_OUI_TPYE_VOTE = 0,
    MESH_OUI_TPYE_NOTITY = 1,
    MESH_OUI_TPYE_NETWORKCFG = 2,
    MESH_OUI_TPYE_CFG = 3,
    MESH_OUI_TPYE_MAX
}mesh_oui_type_e;

typedef enum mesh_machine_state {
    MESH_INIT_STATE,
    MESH_CFG_STATE,
    MESH_FIND_STATE,
    MESH_FAST_ROOT_STATE,
    MESH_VOTE_STATE,
    MESH_ELECTED_ROOT_STATE,
    MESH_JOIN_STATE,
    MESH_MOVE_STATE,
    MESH_CONNETED_STATE,
    MESH_STATE_MAX,
}mesh_machine_state_e;

typedef struct mesh_tlv {
    unsigned char eid;
    unsigned char len;
    unsigned char val[0];
}__packed mesh_tlv_t;

typedef struct      mesh_vendor_ie {
    uint8_t eid;
    uint8_t len;
    uint8_t oui[MESH_OUI_LEN];
    uint8_t oui_type;
}__packed mesh_vendor_ie_t;

typedef struct mesh_rssi_record{
    unsigned char full;
    char average;
    char rssi[MESH_MAX_RSSI_RECORD_NUM];
    unsigned short idx;
    int sum;
}__packed mesh_rssi_record_t;

typedef struct mesh_notity {
    unsigned char major_id[MESH_ID_LEN];
    unsigned char minor_id[MESH_ID_LEN];
    unsigned char parent[MESH_MAC_AADR_LEN];
    unsigned char join_status; // bit7 1:allow join bit6 1 connected ap,bit0-5 join cnt
    unsigned char node_layer;
    unsigned char node_status;
    signed char   link_rssi;
    unsigned int  node_ip;
}__packed mesh_notity_t;

typedef struct mesh_network_cfg {
    unsigned int timestamp;
    unsigned short switch_count_down;
    unsigned short broadcast_count_down;
    unsigned char from_bssid[MESH_MAC_AADR_LEN];
    unsigned char to_bssid[MESH_MAC_AADR_LEN];
    unsigned char passwd_len;
    unsigned char passwd[WIFI_PWD_MAX_LEN];
}__packed mesh_network_cfg_t;

typedef struct mesh_ap_info {
    unsigned long long tsf;
    unsigned char bssid[MESH_MAC_AADR_LEN];
    unsigned char ssid[33];
    unsigned char channel;
    signed char rssi;
}mesh_ap_info_t;

typedef struct mesh_ap_list {
    struct dl_list list;
    long long timestamp;
    mesh_ap_info_t node;
} mesh_aplist_t;

typedef struct mesh_node {
    long long timestamp;
    unsigned char node_mac[MESH_MAC_AADR_LEN];
    unsigned char disallow;
    unsigned char channel;
    signed char   link_rssi;
    mesh_rssi_record_t rssi_record;
    mesh_notity_t notity;
}mesh_node_t;

typedef struct mesh_node_list {
    struct dl_list list;
    mesh_node_t node;
}mesh_node_list_t;

typedef struct mesh_blacklist{
    struct dl_list list;
    long long timestamp;
    unsigned char mac[MESH_MAC_AADR_LEN];
}__packed mesh_blacklist_t;

typedef struct mesh_config {
    unsigned short count_down;
    unsigned short seqno;
    unsigned short vote_timems;
    unsigned short beacon_interval;
    unsigned char max_layer;
    unsigned char linkrssi_loss;
    signed char high_rssi_thd;
    signed char low_rssi_thd;
    unsigned char move_diff_rssi;
    unsigned char use_bssid_as_major_id;
    unsigned char major_id[MESH_ID_LEN];
    unsigned char minor_id[MESH_ID_LEN];
}__packed mesh_config_t;

typedef enum mesh_event_id {
    MESH_TABLE_EVENT,
    MESH_SYSTEM_EVENT,
    MESH_EVENT_MAX,
} mesh_event_id_t;

typedef union mesh_table_event_info{
    mesh_aplist_t  ap_list;
    mesh_node_list_t node_list;
    mesh_network_cfg_t network_cfg;
}mesh_table_event_info_t;

typedef struct mesh_table_event {
    int table_event_id;
    mesh_table_event_info_t table_event_info;
} mesh_table_event_t;

typedef union {
    mesh_table_event_t       table_event;
    system_event_t           system_event;
} mesh_event_info_t;

typedef struct {
    mesh_event_id_t  event_id;
    mesh_event_info_t   event_info;
} mesh_event_t;

extern uint16_t get_macsw_sta_max_cnt(void);
extern int hal_system_get_ap_mac(unsigned char *mac);
extern int fhost_get_sta_aid_bssid(unsigned char *bssid, unsigned short *aid);
extern int fhost_get_sta_freq(void);
extern int fhost_get_softap_freq(void);
extern uint16_t wifi_freq_to_channel(int freq);
extern void bam_set_agg_enable(uint8_t enable);
extern void wifi_mesh_monitor_enable_set(uint8_t enable);
extern void softap_he_enable_set(uint8_t enable);
extern void wifi_mesh_bssid_set(uint8_t *bssid);

#endif
