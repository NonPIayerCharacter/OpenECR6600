/*******************************************************************************
 * Copyright by Transa Semi.
 *
 * File Name:    
 * File Mark:    
 * Description:  
 * Others:        
 * Version:       V1.0
 * Author:        lixiao
 * Date:          2018-12-12
 * History 1:      
 *     Date: 
 *     Version:
 *     Author: 
 *     Modification:  
 * History 2: 
  ********************************************************************************/

/****************************************************************************
* 	                                           Include files
****************************************************************************/
#include <string.h>
#include "system_wifi.h"
#include "system_network.h"
#include "system_event.h"
#include "lwip/netif.h"
#include "lwip/dns.h"
#include "apps/dhcpserver/dhcpserver.h"
#include "system_log.h"
#ifdef CONFIG_PSM_SURPORT
#include "psm_system.h"
#endif
#include "system_config.h"
//#include "nv_config.h"
//#include "easyflash.h"
#include "utils/os.h"
#include "utils/eloop.h"
#include "format_conversion.h"
#include "hal_system.h"
#include "system_lwip.h"
#include "ieee802_11_defs.h"

/****************************************************************************
*                                            Local Macros
****************************************************************************/
#define WIFI_PASSWD_LEN 64
#define WEP_PASSWD_LEN 5
#define WIFI_CMD_LEN                        128
#define CHIP_TYPE_ADDR_EFUSE                4
#define MAC_OTP_ADDR_GD25Q80E_FLASH         0x1030
#define read_mac_valid                      0x0
#define PHY_INFO_CHAN(__x) (((__x.info1) & 0xFFFF0000) >> 16)

#define  SCAN_DELAY_TIMEMS                   (1500)
/****************************************************************************
* 	                                           Local Types
****************************************************************************/


/****************************************************************************
* 	                                           Local Constants
****************************************************************************/

/****************************************************************************
* 	                                           Local Function Prototypes
****************************************************************************/

/****************************************************************************
* 	                                          Global Constants
****************************************************************************/

/****************************************************************************
* 	                                          Global Variables
****************************************************************************/
network_db_t network_db;
static wifi_nv_info_t wifi_nv_info;
static bool is_sta_pwd_on = false;
static bool wifi_init_complete_flag = false;
static wifi_ap_config_t g_softap_config = {0};

/****************************************************************************
* 	                                          Global Function Prototypes
****************************************************************************/
extern uint8_t csa_switch;
extern uint8_t wifi_ax_config;
extern uint8_t g_wirelessmode_sta;
extern uint8_t g_wirelessmode_softap;
extern uint8_t g_balconn;
extern char wpa_cmd_receive_str(int vif_id, char *cmd);
extern struct wpa_supplicant *wpa_get_ctrl_iface(int vif_id);
extern int wifi_drv_get_ap_rssi(char *rssi);
extern int ap_for_each_sta_wrap(void *wpa,
                         int (*cb)(const unsigned char *sta_mac, void *ctx),
                         void *ctx);
extern int fhost_change_mac_addr(uint8_t *mac);
extern int fhost_set_channel(int ch);
extern void get_channel(uint8_t *ch);
extern int wpa_drv_set_country(country_code_t *country_code);
extern void wpa_drv_get_country(country_code_t *country_code);
extern void wpa_drv_copy_country();
extern int wpa_drv_restore_country();
extern wifi_discon_reason_e wpa_disconnect_reason();
extern u16 wpa_get_reason_code();
extern void fhost_set_heartbeat_enable(uint8_t enable);
#if defined(CONFIG_FAST_CONNECT)
int wpa_receive_frist_connect(int vif_id, wifi_config_u *config);
#endif
extern int fhost_get_sta_freq(void);
extern int fhost_get_softap_freq(void);
extern int fhost_get_chan2G4_cnt(void);

/****************************************************************************
* 	                                          Function Definitions
****************************************************************************/
    
/*caller must ensure idx is valid index.*/
inline struct netif *get_netif_by_index(int idx)
{
    if (!IS_VALID_VIF(idx)) {
        SYS_LOGE("!!!!get netif[%d], return null\n", idx);
        return NULL;
    }
    return network_db.netif_db[idx].net_if;
}

inline void set_netif_by_index(struct netif *nif, int idx)
{
    if (!IS_VALID_VIF(idx)) {
        SYS_LOGV("get netif error \n");
        return;
    }

    network_db.netif_db[idx].net_if = nif;
}

inline netif_db_t *get_netdb_by_index(int idx)
{
    if (!IS_VALID_VIF(idx)){
        SYS_LOGV("get netdb error \n");
        return NULL;
    }

    return &network_db.netif_db[idx];
}

wifi_nv_info_t* get_wifi_nv_info()
{
    return &wifi_nv_info;
}

wifi_ap_config_t* get_wifi_ap_config()
{
    return &g_softap_config;
}
/*******************************************************************************
 * Function: wifi_ctrl_iface
 * Description:  send cmd to wpa supplicant
 * Parameters: 
 *   Input: cmd to send, format as "set_network 0 ssid "test_ap_ccmp""
 *
 *   Output:
 *
 * Returns: 
 *
 *
 * Others: 
 ********************************************************************************/
int wifi_ctrl_iface(int vif, char *cmd)
{
    if (!cmd || !cmd[0]){
        return -1;
    }
#if !defined(CONFIG_WIFI_MESH)
    SYS_LOGV("--%s--vif[%d]:%s\n", __func__, vif, cmd);
#endif
    return wpa_cmd_receive_str(vif, cmd);
}

void wifi_set_opmode(wifi_work_mode_e opmode)
{
    if ((opmode != WIFI_MODE_NONE) && (opmode != WIFI_MODE_STA) && (opmode != WIFI_MODE_AP) && (opmode != WIFI_MODE_AP_STA)) {
        return;
    }

    network_db.mode = opmode;

    return;
}

wifi_work_mode_e wifi_get_opmode(void)
{
    return network_db.mode;
}

void wifi_set_ready_flag(bool init_complete_flag)
{
    if (init_complete_flag != false) {
        wifi_init_complete_flag = true;
    } else {
        wifi_init_complete_flag = false;
    }
}

void wifi_set_ax_flag(uint8_t ax_config)
{
    if (ax_config != 0) {
        wifi_ax_config = 1;
    } else {
        wifi_ax_config = 0;
    }
}


bool wifi_is_ready(void)
{
    return wifi_init_complete_flag;
}

/* static void wifi_send_deauth(unsigned char *sa, unsigned char *da, unsigned char *bssid)
{
    struct ieee80211_mgmt *mgmt;

    mgmt = os_zalloc(sizeof(struct ieee80211_mgmt));
    if (!mgmt) {
        return ;
    }

    mgmt->frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
        WLAN_FC_STYPE_DEAUTH);
    mgmt->frame_control |= WLAN_FC_ISWEP;
    memcpy(mgmt->da, da, ETH_ALEN);
    memcpy(mgmt->sa, sa, ETH_ALEN);
    memcpy(mgmt->bssid, bssid, ETH_ALEN);
    mgmt->u.deauth.reason_code = host_to_le16(1);

    net_packet_send(STATION_IF, (u8 *)mgmt, IEEE80211_HDRLEN + sizeof(mgmt->u.deauth), 1, 1);
    os_free(mgmt);
} */

static void wifi_reset_handler(void *arg)
{
    if (wifi_get_status(STATION_IF) == STA_STATUS_CONNECTED)
    {
        wifi_stop_station();
        os_msleep(300);
    }

    if (wifi_get_status(SOFTAP_IF) == AP_STATUS_STARTED)
    {
        wifi_stop_softap();
        os_msleep(300);
    }

#if  defined(CONFIG_FAST_CONNECT)
	wifi_stop_station();
	os_msleep(50);
#endif
}

sys_err_t wifi_system_init(void)
{
    wifi_work_mode_e wifi_mode;
    wifi_ap_config_t *softap_config = get_wifi_ap_config();
    
    memset(&network_db, 0, sizeof(network_db));
    memset(softap_config, 0, sizeof(wifi_ap_config_t));

#ifdef CONFIG_APP_AT_COMMAND
    wifi_set_default_softap(softap_config);
#endif

    wifi_load_nv_info(NULL); // Should be called only once.

    if (hal_system_get_config(CUSTOMER_NV_WIFI_OP_MODE, &wifi_mode, sizeof(wifi_mode))) {
        network_db.mode = wifi_mode;
    } else {
#ifdef CONFIG_APP_AT_COMMAND
        network_db.mode = WIFI_MODE_AP;
#else
	network_db.mode = WIFI_MODE_AP_STA;
#endif
    }

#if defined(CONFIG_WIFI_MESH)
    network_db.mode = WIFI_MODE_AP_STA;
#endif
    hal_system_register_reset_handler(wifi_reset_handler, NULL);

    return SYS_OK;
}

void wifi_set_status(int vif, wifi_status_e status)
{
    netif_db_t *nif = NULL;

    if (!IS_VALID_VIF(vif)) {
        return;
    }

    nif = get_netdb_by_index(vif);
    if (nif) {
        nif->info.wifi_status = status;
    } else {
        SYS_LOGV("nif get error \n");
    }
}

wifi_status_e wifi_get_status(int vif)
{
    netif_db_t *nif = NULL;

    if (!IS_VALID_VIF(vif)) {
        return STA_STATUS_STOP;
    }

    nif = get_netdb_by_index(vif);
    if (nif) {
         return nif->info.wifi_status;
    } else {
        SYS_LOGV("nif get error \n");
        return STATUS_ERROR;
    }
}

wifi_status_e wifi_get_ap_status(void)
{
	return wifi_get_status(SOFTAP_IF);
}
wifi_status_e wifi_get_sta_status(void)
{
	return wifi_get_status(STATION_IF);
}

int8_t wifi_get_vif_is_idle(wifi_work_mode_e opmode)
{
    wifi_status_e ap_status = wifi_get_ap_status();
    wifi_status_e sta_status = wifi_get_sta_status();

    if (opmode == WIFI_MODE_AP_STA) 
    {
        if ((sta_status == STA_STATUS_STOP || sta_status == STA_STATUS_DISCON) && (ap_status == AP_STATUS_STOP)) {
            return 0;
        }
    } 
    else if (opmode == WIFI_MODE_AP) 
    {
        if (ap_status == AP_STATUS_STOP) {
            return 0;
        }
    } 
    else if (opmode == WIFI_MODE_STA) 
    {
        if (sta_status == STA_STATUS_STOP || sta_status == STA_STATUS_DISCON) {
            return 0;
        }
    }
    else
    {
        os_printf(LM_APP, LL_ERR, "error opmode = WIFI_MODE_MAX\n");
        return 0;
    }

    return -1;
}

bool check_wifi_link_on(int vif)
{
    if (!IS_VALID_VIF(vif)) {
        return false;
    }

    wifi_status_e status = wifi_get_status(vif);
    if ((status == STA_STATUS_CONNECTED) || (status == AP_STATUS_STARTED)) {
        return true;
    }

    return false;
}

sys_err_t wifi_change_mac_addr(uint8_t *mac)
{
    return (sys_err_t)fhost_change_mac_addr(mac);
}

void wifi_handle_sta_connect_event(system_event_t *event, bool nv_save)
{
    netif_db_t *nif = NULL;

    if ((!event) || (!IS_VALID_VIF(event->vif))) {
        return;
    }

    nif = get_netdb_by_index(event->vif);
    if (!nif) {
        return;
    }

    if (event->event_id == SYSTEM_EVENT_STA_DISCONNECTED) {
        wifi_set_status(event->vif, STA_STATUS_DISCON);
        #ifndef CONFIG_CUSTOM_LXW
        wifi_station_dhcpc_stop2(event->vif, false);
        #endif
        nif->info.channel = 0;
    } else if (event->event_id == SYSTEM_EVENT_STA_CONNECTED) {
        wifi_set_status(event->vif, STA_STATUS_CONNECTED);
        #ifndef CONFIG_CUSTOM_LXW
        wifi_station_dhcpc_start(event->vif);
        #endif
        nif->info.channel = event->event_info.connected.channel;

#ifndef CONFIG_APP_AT_COMMAND
        if (nv_save) {
            wifi_update_nv_sta_connected((char *)event->event_info.connected.ssid, event->event_info.connected.channel);
        }
#endif
    }
}

void wifi_handle_ap_staipassigned_event(int vif, struct dhcps_msg* m)
{
    system_event_t event;
    char ipaddr[16] = {0};

    if(m == NULL){
        return;
    }

    memset(&event, 0, sizeof(system_event_t));
    event.event_id = SYSTEM_EVENT_AP_STAIPASSIGNED;
    snprintf(ipaddr, 16, IP4_ADDR_STR, m->yiaddr[0], m->yiaddr[1], m->yiaddr[2], m->yiaddr[3]);
    ipaddr_aton(ipaddr, &event.event_info.sta_connected.ip_info.ip);
    memcpy(event.event_info.sta_connected.mac, m->chaddr, ETH_ALEN);
    event.vif = vif;

    sys_event_send(&event);

    return;
}

void wifi_handle_ap_connect_event(int vif, const uint8_t *addr, uint16_t aid, uint8_t type)
{
    system_event_t event;

    if (addr == NULL) {
        return;
    }

    memset(&event, 0, sizeof(system_event_t));
    event.vif = vif;
    memcpy(event.event_info.sta_connected.mac, addr, ETH_ALEN);
    event.event_info.sta_connected.aid = aid;
    event.event_id = type ? SYSTEM_EVENT_AP_STACONNECTED : SYSTEM_EVENT_AP_STADISCONNECTED;

    sys_event_send(&event);

    return;
}

uint16_t wifi_channel_to_freq(int channel)
{
    if ((channel >= WIFI_CHANNEL_1) && (channel <= WIFI_CHANNEL_14)) {
        if (channel == WIFI_CHANNEL_14) {
            return CHANNEL_14_FREQ;
        } else {
            return CHANNEL_1_FREQ + CHANNEL_2G4_BANDWIDTH(channel);
        }
    } else if ((channel >= WIFI_CHANNEL_36) && (channel <= WIFI_CHANNEL_165)) {
        return CHANNEL_36_FREQ + CHANNEL_5G_BANDWIDTH(channel);
    }

    return 0;
}

#if  1 //def COMPILE_WARNING_OPTIMIZE_WPA
 uint16_t wifi_freq_to_channel(int freq)
{
    if ((freq >= CHANNEL_1_FREQ) && (freq <= CHANNEL_14_FREQ)) {
        if (freq == CHANNEL_14_FREQ) {
            return WIFI_CHANNEL_14;
        } else {
            return WIFI_CHANNEL_1 + FREQ_TO_CHANNEL_2G4(freq);
        }
    } else if ((freq >= CHANNEL_36_FREQ) && (freq <= CHANNEL_165_FREQ)) {
        return WIFI_CHANNEL_36 + FREQ_TO_CHANNEL_5G(freq);
    }

    return 0;
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */

char wifi_remove_config_all(int vif)
{
    return wifi_ctrl_iface(vif, "remove_network all");
}

char wifi_add_config(int vif)
{
    return wifi_ctrl_iface(vif, "add_network");
}

char wifi_start_wps_pbc(int vif)
{
    return wifi_ctrl_iface(vif, "wps_pbc");
}

char wifi_set_scan_expire(int vif, unsigned int age)
{
    char buf[WIFI_CMD_LEN] = {0};
    sprintf(buf, "bss_expire_age %d", age);
    return wifi_ctrl_iface(vif, buf);
}

char wifi_flush_scan_results(int vif)
{
    return wifi_ctrl_iface(vif, "flush_bss 0 ");
}

char wifi_config_ssid(int vif, unsigned char *ssid)
{
    char buf[WIFI_CMD_LEN] = {0};
    char *pos = buf;
    char *end = buf + WIFI_CMD_LEN - 1;
    uint8_t ssidLen = 0;

    if (!ssid || !ssid[0]){
        return -1;
    }

    sprintf(buf, "%s", "set_network 0 ssid ");
    pos += strlen(buf);
    while ((pos != end) && (ssid[ssidLen] != '\0') && (ssidLen < WIFI_SSID_MAX_LEN)) {
        sprintf(pos, "%2X", ssid[ssidLen++]);
        pos += 2;
    }

    return wifi_ctrl_iface(vif, buf);
}

int wifi_config_hidden_ssid(int vif, uint8_t hidden)
{
    char buf[WIFI_CMD_LEN] = {0};

    sprintf(buf, "set_network %d ignore_broadcast_ssid %d", 0, hidden ? 1 : 0);
    return wifi_ctrl_iface(vif, buf);
}

char wifi_config_ap_mode(int vif)
{
    char buf[WIFI_CMD_LEN] = {0};

    sprintf(buf, "set_network %d mode 2", 0);
    return wifi_ctrl_iface(vif, buf);
}

char wifi_config_channel(int vif, int channel)
{
    uint16_t frequency;
    char buf[WIFI_CMD_LEN] = {0};

    if ((channel < WIFI_CHANNEL_1) || (channel > WIFI_CHANNEL_14)){
        return -1;
    }
    
    frequency = wifi_channel_to_freq(channel);
    snprintf(buf, sizeof(buf), "set_network %d frequency %d", 0, frequency);
    return wifi_ctrl_iface(vif, buf);
}

char wifi_config_pmf(int vif, int pmf)
{
    char buf[WIFI_CMD_LEN] = {0};

    if ((pmf < NO_MGMT_FRAME_PROTECTION) || (pmf > MGMT_FRAME_PROTECTION_DEFAULT)) {
        return -1;
    }

    snprintf(buf, sizeof(buf), "set_network %d ieee80211w %d", 0, pmf);
    return wifi_ctrl_iface(vif, buf);
}

char wifi_config_commit(int vif)
{
    char buf[WIFI_CMD_LEN] = {0};

    sprintf(buf, "select_network %d", 0);
    return wifi_ctrl_iface(vif, buf);
}

char wifi_config_encrypt(int vif, char *pwd, wifi_auth_mode_e mode)
{
    char buf[WIFI_CMD_LEN] = {0};
    char *pairwise = NULL;
    char *group = NULL;
    char *key_mgmt = NULL;

    if (!pwd || !pwd[0]) {
        mode = AUTH_OPEN;
    } else {
        if (mode == AUTH_OPEN) {
            mode = AUTH_WPA_WPA2_PSK;
        }
    }

    switch (mode) {
        case AUTH_OPEN:
        case AUTH_WEP:
            pairwise = key_mgmt = "NONE";
            group = "GTK_NOT_USED";
            break;

        case AUTH_WPA_PSK:
            pairwise = group = "TKIP";
            key_mgmt = "WPA-PSK";
            break;

        case AUTH_WPA2_PSK:
        case AUTH_WPA_WPA2_PSK:
            pairwise = group = "CCMP";
            key_mgmt = "WPA-PSK";
            break;

        default:
            break;
    }

    if (pairwise && group && key_mgmt) {
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "set_network %d pairwise %s", 0, pairwise);
        wifi_ctrl_iface(vif, buf);
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "set_network %d group %s", 0, group);
        wifi_ctrl_iface(vif, buf);
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "set_network %d key_mgmt %s", 0, key_mgmt); 
        wifi_ctrl_iface(vif, buf);
    }

    if (mode == AUTH_WPA_WPA2_PSK) {
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "set_network %d proto RSN WPA", 0);
        wifi_ctrl_iface(vif, buf);
    } else if (mode == AUTH_WPA2_PSK) {
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "set_network %d proto RSN", 0);
        wifi_ctrl_iface(vif, buf);
    } else if (mode == AUTH_WPA_PSK) {
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "set_network %d proto WPA", 0);
        wifi_ctrl_iface(vif, buf);
    }

    if (mode == AUTH_WPA_WPA2_PSK || mode == AUTH_WPA2_PSK || mode == AUTH_WPA_PSK) {
        wifi_set_password(vif, pwd);
    } else if (mode == AUTH_WEP) {
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "set_network %d wep_key0 \"%s\"", 0, pwd);
        wifi_ctrl_iface(vif, buf);
    }

    return SYS_OK;
}

sys_err_t wifi_rf_set_channel(uint8_t channel)
{
    return fhost_set_channel(channel) ? SYS_ERR : SYS_OK;
}

uint8_t wifi_rf_get_channel(void)
{
    uint8_t channel = 0;
    //struct wifi_channel_info phy_info;
    //phy_get_channel(&phy_info, 0);
    get_channel(&channel);
    //channel = wifi_freq_to_channel(PHY_INFO_CHAN(phy_info));
    
    return channel;
}

sys_err_t wifi_sniffer_start(wifi_promiscuous_cb_t cb, wifi_promiscuous_filter_t *filter)
{
    wifi_set_promiscuous_filter(filter);
    wifi_set_promiscuous_rx_cb(cb);
    wifi_set_promiscuous(true);

    return SYS_OK;
}

sys_err_t wifi_sniffer_stop(void)
{
    wifi_set_promiscuous(false);
    wifi_set_promiscuous_rx_cb(NULL);

    return SYS_OK;
}

extern uint8_t g_wifi_sniffer_reply_ack;

sys_err_t wifi_sniffer_set_reply_ack(uint8_t reply_ack)
{
    g_wifi_sniffer_reply_ack = reply_ack;

    return SYS_OK;
}

sys_err_t wifi_send_raw_pkt(const uint8_t *frame, const uint16_t len)
{
    if (!wifi_drv_raw_xmit(STATION_IF, frame, len)) {
        return SYS_OK;
    }
    else{
        return SYS_ERR;
    }
}

sys_err_t wifi_sta_auto_conn_conf(uint8_t *val, uint8_t type)
{
    return SYS_OK;
}

sys_err_t wifi_sta_auto_conn_start(void)
{
    wifi_nv_info_t *nv_info = &wifi_nv_info;

    if (nv_info->auto_conn == 1 && nv_info->sta_ssid[0] != 0) {
        wifi_set_sta_by_nv(nv_info);
    }

    return SYS_OK;
}

#ifdef CONFIG_APP_AT_COMMAND
sys_err_t wifi_softap_auto_start(void)
{
    wifi_config_u wificonfig = {0};
    wifi_work_mode_e wifi_mode = wifi_get_opmode();
    wifi_ap_config_t *softap_config = get_wifi_ap_config();

    if (wifi_mode != WIFI_MODE_AP && wifi_mode != WIFI_MODE_AP_STA)
        return SYS_ERR;

    if (wifi_load_ap_nv_info(&wificonfig.ap) == SYS_OK) {
        wifi_start_softap(&wificonfig);
    } else if (strlen((const char *)softap_config->ssid) != 0) {
        memcpy(&wificonfig.ap, softap_config, sizeof(wifi_ap_config_t));
        wifi_start_softap(&wificonfig);
    }

    return SYS_OK;
}
#endif

sys_err_t wifi_load_ap_nv_info(wifi_ap_config_t *softap_info)
{
    if (hal_system_get_config(CUSTOMER_NV_WIFI_AP_SSID, softap_info->ssid , sizeof(softap_info->ssid) - 1) == 0 ||
        hal_system_get_config(CUSTOMER_NV_WIFI_AP_PWD, softap_info->password, sizeof(softap_info->password)) == 0 ||
        hal_system_get_config(CUSTOMER_NV_WIFI_AP_CHANNEL, &softap_info->channel, sizeof(softap_info->channel)) == 0 ||
        hal_system_get_config(CUSTOMER_NV_WIFI_AP_AUTH, &softap_info->authmode, sizeof(softap_info->authmode)) == 0) {
        SYS_LOGE("load ap info nv fail");
        return SYS_ERR;
    }

    hal_system_get_config(CUSTOMER_NV_WIFI_MAX_CON, &softap_info->max_connect, sizeof(softap_info->max_connect));
    hal_system_get_config(CUSTOMER_NV_WIFI_HIDDEN_SSID, &softap_info->hidden_ssid, sizeof(softap_info->hidden_ssid));

    return SYS_OK;
}

sys_err_t wifi_save_ap_nv_info(wifi_ap_config_t *softap_info)
{
    int ret = 0;

    if (!softap_info) {
        return SYS_ERR_INVALID_ARG;
    }

    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_AP_SSID, softap_info->ssid , sizeof(softap_info->ssid) - 1);
    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_AP_PWD, softap_info->password, sizeof(softap_info->password));
    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_AP_CHANNEL, &softap_info->channel, sizeof(softap_info->channel));
    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_AP_AUTH, &softap_info->authmode, sizeof(softap_info->authmode));
    if (softap_info->max_connect != 0) {
        ret |= hal_system_set_config(CUSTOMER_NV_WIFI_MAX_CON, &softap_info->max_connect, sizeof(softap_info->max_connect));
    }
    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_HIDDEN_SSID, &softap_info->hidden_ssid, sizeof(softap_info->hidden_ssid));

    if (ret) {
        hal_system_del_config(CUSTOMER_NV_WIFI_AP_SSID);
        hal_system_del_config(CUSTOMER_NV_WIFI_AP_PWD);
        hal_system_del_config(CUSTOMER_NV_WIFI_AP_CHANNEL);
        hal_system_del_config(CUSTOMER_NV_WIFI_AP_AUTH);
        hal_system_del_config(CUSTOMER_NV_WIFI_MAX_CON);
        hal_system_del_config(CUSTOMER_NV_WIFI_HIDDEN_SSID);
        return SYS_ERR;
    }

    return SYS_OK;
}

sys_err_t wifi_get_softap_info(wifi_ap_config_t *softap_info)
{
    wifi_work_mode_e wifi_opmode = wifi_get_opmode();

    if ((wifi_opmode != WIFI_MODE_AP) && (wifi_opmode != WIFI_MODE_AP_STA)) {
        return SYS_ERR_WIFI_MODE;
    }

    softap_info = get_wifi_ap_config();

    return SYS_OK;
}

sys_err_t wifi_softap_set_max_sta(uint16_t max_sta)
{
    return wifi_set_max_sta(max_sta);
}

sys_err_t wifi_start_softap(wifi_config_u *config)
{
    netif_db_t *nif = NULL;
    wifi_work_mode_e wifi_opmode = wifi_get_opmode();
    wifi_ap_config_t *softap_config = get_wifi_ap_config();

    if ((wifi_opmode != WIFI_MODE_AP) && (wifi_opmode != WIFI_MODE_AP_STA)) {
        return SYS_ERR_WIFI_MODE;
    }

    // fix the channel,  should set sta's channel if sta started.
    if (wifi_opmode == WIFI_MODE_AP_STA) {
        nif = get_netdb_by_index(STATION_IF);
        if (!nif) {
            return SYS_ERR;
        }
        if (nif->info.channel) {
            config->ap.channel = nif->info.channel;
        }
    } else if (wifi_opmode == WIFI_MODE_AP) {
        wifi_remove_config_all(STATION_IF); // clear sta config.
    }

    memcpy(softap_config, &config->ap, sizeof(wifi_ap_config_t));

    wifi_remove_config_all(SOFTAP_IF);
    wifi_add_config(SOFTAP_IF);

    wifi_softap_set_max_sta(config->ap.max_connect);
    wifi_config_ssid(SOFTAP_IF, config->ap.ssid);
    wifi_config_hidden_ssid(SOFTAP_IF, config->ap.hidden_ssid);
    wifi_config_encrypt(SOFTAP_IF, config->ap.password, config->ap.authmode);
    wifi_config_channel(SOFTAP_IF, config->ap.channel);
    wifi_config_ap_mode(SOFTAP_IF);
    wifi_config_commit(SOFTAP_IF);

    return SYS_OK;
}

sys_err_t wifi_set_softap_by_nv(wifi_ap_config_t *nv_info)
{
    wifi_config_u wifi_config;

    memset(&wifi_config, 0, sizeof(wifi_config));

    if (!nv_info)
        nv_info = get_wifi_ap_config();
    if (!nv_info->ssid[0])
        return SYS_ERR;

    SYS_LOGI("wifi_set_softap_by_nv: ssid[%s],password[%s],channel[%d],authmode[%d],max_connect[%d],hidden_ssid[%d]",
             nv_info->ssid, nv_info->password, nv_info->channel, nv_info->authmode, nv_info->max_connect,
             nv_info->hidden_ssid);
    memcpy(&wifi_config.ap, nv_info, sizeof(wifi_ap_config_t));
    if (wifi_start_softap(&wifi_config))
        return SYS_ERR;

    return SYS_OK;
}

sys_err_t wifi_stop_softap(void)
{
    #ifndef CONFIG_CUSTOM_LXW
    wifi_softap_dhcps_stop(SOFTAP_IF);
    #endif
    wifi_remove_config_all(SOFTAP_IF);

    return SYS_OK;
}

sys_err_t wifi_softap_deauth_sta(const uint8_t *sta_mac, uint16_t reason)
{
    char buf[WIFI_CMD_LEN] = {0};

    sprintf(buf, "DEAUTHENTICATE %02x:%02x:%02x:%02x:%02x:%02x reason=%d", sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5], reason);

    return wifi_ctrl_iface(SOFTAP_IF, buf);
}

extern uint16_t get_macsw_sta_max_cnt(void);
void wifi_set_default_softap(wifi_ap_config_t *softap_config)
{
    #define ESWIN_SSID_PREIX "ESWIN_"
    const uint8_t valid_channels[] = {1, 6, 11};
    unsigned char ap_mac[NETIF_MAX_HWADDR_LEN];

    hal_system_get_ap_mac(ap_mac);
    strcpy((char *)softap_config->ssid, ESWIN_SSID_PREIX);
    sprintf((char *)softap_config->ssid + strlen(ESWIN_SSID_PREIX), "%02X%02X%02X", ap_mac[3], ap_mac[4], ap_mac[5]);
    softap_config->channel = valid_channels[rand() % 3];
    softap_config->authmode = 0;
    softap_config->max_connect = get_macsw_sta_max_cnt();
    softap_config->hidden_ssid = 0;
}

struct get_sta_msg {
    struct softap_sta_info *info;

    uint32_t num;
    uint32_t current;

    int status;
    os_sem_handle_t finish;
};

static int softap_get_sta_callback(const uint8_t *sta_mac, void *ctx)
{
    struct get_sta_msg *msg = (struct get_sta_msg *)ctx;
    struct softap_sta_info *info = msg->info;

    memcpy(info[msg->current].mac_addr, sta_mac, 6);
    dhcps_get_sta_hostname(sta_mac, info[msg->current].hostname,
                           sizeof(info[msg->current].hostname));

    msg->current++;
    if (msg->current >= msg->num)
    {
        return 1;
    }

    return 0;
}

static void wpa_get_sta_list(void *eloop_data, void *user_ctx)
{
    struct wpa_supplicant *wpa_s;
    struct get_sta_msg *msg;

	wpa_s = (struct wpa_supplicant *)eloop_data;
    msg = (struct get_sta_msg *)user_ctx;

    ap_for_each_sta_wrap(wpa_s, softap_get_sta_callback, msg);

	msg->status = 0;
	os_sem_post(msg->finish);
}

sys_err_t wifi_softap_get_sta_list(struct softap_sta_info *info, uint32_t *num)
{
    int ret;
    struct get_sta_msg msg;
    struct wpa_supplicant *wpa_s;

    if (!num)
        return SYS_ERR_INVALID_ARG;

    memset(&msg, 0, sizeof(msg));
    msg.finish = os_sem_create(1, 0);
    if (!msg.finish)
        return SYS_ERR_NO_MEM;

    msg.status = -1;
    msg.info = info;
    msg.num = *num;
    msg.current = 0;

    wpa_s = wpa_get_ctrl_iface(SOFTAP_IF);

    eloop_register_timeout(0, 0, wpa_get_sta_list, wpa_s, &msg);

    ret = os_sem_wait(msg.finish, WAIT_FOREVER);
    os_sem_destroy(msg.finish);
    if (ret || msg.status)
    {
        return SYS_ERR;
    }

    *num = msg.current;

    return SYS_OK;
}

sys_err_t wifi_start_station(wifi_config_u *config)
{
    wifi_work_mode_e wifi_opmode = wifi_get_opmode();

    if (wifi_opmode != WIFI_MODE_STA && wifi_opmode != WIFI_MODE_AP_STA) {
        return SYS_ERR_WIFI_MODE;
    }

    if (wifi_opmode == WIFI_MODE_STA) {
        wifi_remove_config_all(SOFTAP_IF); // clear ap config.
    }

    wifi_remove_config_all(STATION_IF);
    if (config->sta.wps_pbc)
    {
        wifi_start_wps_pbc(STATION_IF);
        return SYS_OK;
    }
    wifi_add_config(STATION_IF);

    wifi_config_ssid(STATION_IF, config->sta.ssid);
    wifi_set_password(STATION_IF, config->sta.password);

    if (!(IS_ZERO_MAC(config->sta.bssid) || IS_MULTCAST_MAC(config->sta.bssid))) {
        wifi_set_bssid(config->sta.bssid);
    }

    wifi_config_channel(STATION_IF, config->sta.channel);
    wifi_config_pmf(STATION_IF, config->sta.pmf);

    wifi_config_commit(STATION_IF);
    wifi_set_status(STATION_IF, STA_STATUS_START);
    return SYS_OK;
}
#if defined(CONFIG_FAST_CONNECT)
sys_err_t wifi_frist_start_station(wifi_config_u *config)
{
    wifi_work_mode_e wifi_opmode = wifi_get_opmode();

    if (wifi_opmode != WIFI_MODE_STA && wifi_opmode != WIFI_MODE_AP_STA) {
        return SYS_ERR_WIFI_MODE;
    }

    wpa_receive_frist_connect(STATION_IF,config);

    wifi_set_status(STATION_IF, STA_STATUS_START);

    return SYS_OK;
}
#endif
#if 0 //#ifdef TEST_DNA_API_WIFI
#include "dna_api.h"
static sys_err_t wifi_set_psk(unsigned char type, unsigned char *psk, unsigned char len)
{
    char buf[WIFI_CMD_LEN], tmp[33];
    memset(buf, 0, sizeof(buf));
    memset(tmp, 0, sizeof(tmp));
    memcpy(tmp, psk, len<33?len:32);

    wifi_ctrl_iface(STATION_IF, "set_network 0 scan_ssid 1");

    if(type<2) 
    {
        wifi_ctrl_iface(STATION_IF, "set_network 0 key_mgmt NONE");//OPEN
        if(type==1) 
        {
            sprintf(buf, "set_network 0 wep_key0 \"%s\"", psk);//WEP
            wifi_ctrl_iface(STATION_IF, buf);
        }
    }
    else
    {
        wifi_ctrl_iface(STATION_IF, "set_network 0 key_mgmt WPA-PSK");
        if(type==2) //TKIP
        {
            wifi_ctrl_iface(STATION_IF, "set_network 0 pairwise TKIP");
            wifi_ctrl_iface(STATION_IF, "set_network 0 group TKIP");
        }
        else if(type==3) // CCMP
        {
        	wifi_ctrl_iface(STATION_IF, "set_network 0 pairwise CCMP");
        	wifi_ctrl_iface(STATION_IF, "set_network 0 group CCMP TKIP");
        }
        sprintf(buf, "set_network 0 psk \"%s\"", tmp); // AUTO
        wifi_ctrl_iface(STATION_IF, buf);
    }
    return SYS_OK;
}

sys_err_t wifi_start_station_test(wifi_info_t *config)
{
    wifi_work_mode_e wifi_opmode = wifi_get_opmode();

    if ((WIFI_MODE_STA != wifi_opmode) && (WIFI_MODE_AP_STA != wifi_opmode)) {
        return SYS_ERR_WIFI_MODE;
    }
    
    if (STA_STATUS_STOP < wifi_get_status(STATION_IF)) {
        return SYS_ERR_WIFI_BUSY;
    }
    
    if (WIFI_MODE_STA == wifi_opmode) {
        wifi_remove_config_all(SOFTAP_IF); // clear ap config.
    }

    wifi_remove_config_all(STATION_IF);
    wifi_add_config(STATION_IF);
    wifi_config_ssid(STATION_IF, (config->ssid));
    wifi_set_psk(config->cipher, config->pwd, config->pwd_len);
    wifi_config_commit(STATION_IF);
    wifi_set_status(STATION_IF, STA_STATUS_START);

    return SYS_OK;
}
#endif

sys_err_t wifi_stop_station(void)
{
    wifi_remove_config_all(STATION_IF);
    wifi_set_status(STATION_IF, STA_STATUS_STOP);

    return 0;
}

sys_err_t wifi_set_password(int vif, char *password)
{
    char buf[WIFI_CMD_LEN] = {0};
    int passwordLen;
    if (password && password[0]) {
        passwordLen = strnlen(password, WIFI_PASSWD_LEN);
        if (passwordLen == WEP_PASSWD_LEN) {
            wifi_ctrl_iface(vif, "set_network 0 key_mgmt NONE");
            sprintf(buf, "set_network 0 wep_key0 \"%s\"", password);
        } else if(passwordLen == WIFI_PASSWD_LEN) {
            sprintf(buf, "set_network 0 psk %.*s", passwordLen, password);
        } else {
            sprintf(buf, "set_network 0 psk \"%s\"", password);
        }
    } else {
        sprintf(buf, "set_network 0 key_mgmt NONE");
    }

    return wifi_ctrl_iface(vif, buf);
}

sys_err_t wifi_set_bssid(uint8_t *bssid)
{
    char buf[WIFI_CMD_LEN] = {0};

    sprintf(buf, "set_network 0 bssid %02x:%02x:%02x:%02x:%02x:%02x",
        bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    return wifi_ctrl_iface(STATION_IF, buf);
}

sys_err_t wifi_get_bssid(unsigned char *mac, wifi_info_t *info)
{
    if (wpa_get_wifi_info(info) != SYS_OK) {
        return SYS_ERR;
    }
    memcpy(mac, info->bssid, NETIF_MAX_HWADDR_LEN);

    return SYS_OK;
}

sys_err_t wifi_set_scan_hidden_ssid(void)
{
    char buf[WIFI_CMD_LEN] = {0};

    sprintf(buf, "set_network 0 scan_ssid 1");
    return wifi_ctrl_iface(STATION_IF, buf);
}

sys_err_t wifi_disconnect(void)
{
    char buf[WIFI_CMD_LEN] = {0};

    sprintf(buf, "disable_network 0");
    return wifi_ctrl_iface(STATION_IF, buf);
}

sys_err_t wifi_request_disconnect(const int vif)
{
    char buf[WIFI_CMD_LEN] = {0};

    sprintf(buf, "disconnect");
    return wifi_ctrl_iface(vif, buf);
}

sys_err_t wifi_radio_set(unsigned char on_off)
{
    return SYS_OK;
}

sys_err_t wifi_chip_powerup(void)
{
    return SYS_OK;
}

sys_err_t wifi_chip_powerdown(void)
{
    return SYS_OK;
}

sync_scan_db_t *get_snyc_scan_db(void)
{
    static sync_scan_db_t sync_scan_db = {0};
    static int8_t once = 0;

    if (!once) {
        once = 1;
        sync_scan_db.sem = os_sem_create(1,0);
    }

    return &sync_scan_db;
}

sys_err_t wifi_event_scan_handler(int vif, uint8_t type)
{
    sync_scan_db_t *db = get_snyc_scan_db();

    if (!db->sem && type != 2/*MANUAL SCAN*/) {
        return SYS_OK;
    }

    if (db->waiting) {
        os_sem_post(db->sem);
    }

    return SYS_OK;
}

sys_err_t wifi_wait_scan_done(unsigned int timeoutms)
{
    BaseType_t ret;
    sync_scan_db_t *db = get_snyc_scan_db();

    if (!db->sem) {
        return SYS_ERR;
    }

    db->waiting = 1;
    ret = os_sem_wait(db->sem, timeoutms);
    db->waiting = 0;

    if (ret == 0) {
        return SYS_OK;
    }

    return SYS_ERR;
}

sys_err_t wifi_scan_start(bool block, const wifi_scan_config_t *config)
{
    char buf[WIFI_CMD_LEN] = {0}, *pos, *end;
    unsigned int scan_time __maybe_unused;

    pos = buf;
    end = buf + WIFI_CMD_LEN;

    sprintf(buf, "flush_bss 0");
    wifi_ctrl_iface(STATION_IF, buf);
    memset(buf, 0, sizeof(buf));

    pos += snprintf(pos, end - pos - 1, "%s ", "scan  glance=1");
    if (config) {
        if (config->max_item) {
            pos += snprintf(pos, end - pos - 1, "max=%d ", config->max_item);
            if (*(pos - 1) == '\0') --pos; // when format has %d, this toolchain's snprintf will add '\0', we should delete it. 
        }
        if (config->passive) {
            pos += snprintf(pos, end - pos - 1, "passive=%d ", config->passive ? 1 : 0);
            if (*(pos - 1) == '\0') --pos;
        }
        if (config->scan_time) {
            pos += snprintf(pos, end - pos - 1, "scan_time=%ld ", config->scan_time);
            if (*(pos - 1) == '\0') --pos;
        }
        if ((config->channel > 0) && (config->channel <= 13)) {
            pos += snprintf(pos, end - pos - 1, "freq=%d ", wifi_channel_to_freq(config->channel));
            if (*(pos - 1) == '\0') --pos;
        }
        if (config->bssid) {
            pos += snprintf(pos, end - pos - 1, "bssid="MAC_STR" ", MAC_VALUE(config->bssid));
        }
        if (config->glance) {
            pos += snprintf(pos, end - pos - 1, "glance=%d ", config->glance ? 1 : 0);
            if (*(pos - 1) == '\0') --pos;
        }
        // must be the last one.
        if ((config->ssid) && (config->ssid[0])) {
            pos += snprintf(pos, end - pos - 1, "scan_ssid=\"%s\"", config->ssid);
        }
    }

    SYS_LOGI("--%s", buf);

    wifi_ctrl_iface(STATION_IF, buf);

    if (!block) {
        return SYS_OK;
    }

    return wifi_wait_scan_done(portMAX_DELAY);
}

sys_err_t wifi_get_scan_result(unsigned int index, wifi_info_t *info)
{
    return wpa_get_scan_result(index, info);
}


sys_err_t wifi_get_wifi_info(wifi_info_t *info)
{
    return wpa_get_wifi_info(info);
}

sys_err_t wifi_set_mac_addr(int vif, uint8_t *mac)
{
    struct netif *nif = NULL;

    if (!IS_VALID_VIF(vif)) {
        return SYS_ERR_INVALID_ARG;
    }

    nif = get_netif_by_index(vif);

    if (nif) {
        at_update_netif_mac(vif, mac);
        wpa_update_mac_addr(vif, mac);
    }

    return SYS_OK;
}

sys_err_t wifi_get_mac_addr(wifi_interface_e vif, unsigned char *mac)
{
    struct netif *nif = NULL;

    if (!IS_VALID_VIF(vif) || !mac) {
        return SYS_ERR_INVALID_ARG;
    }

    nif = get_netif_by_index(vif);
    memcpy(mac, nif->hwaddr, NETIF_MAX_HWADDR_LEN);

    return SYS_OK;
}

#ifdef COMPILE_WARNING_OPTIMIZE_WPA
//static uint8_t hex2num(char c)
//{
//    if (c >= '0' && c <= '9') {
//        return c - '0';
//    } else if (c >= 'a' && c <= 'f') {
//        return c - 'a' + 10;
//    } else if (c >= 'A' && c <= 'F') {
//        return c - 'A' + 10;
//    }
//
//    return -1;
//}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */

/*
static void delay(volatile unsigned int data)
{
    volatile unsigned int indx;

    for (indx = 0; indx < data; indx++) {
        //NOP
    }
}
*/

/*
static uint8_t read_mac_addr(wifi_read_mac_mode_e mode, uint8_t *mac)
{
    return SYS_OK;
}
*/

sys_err_t wifi_load_mac_addr(int vif_id, uint8_t *mac)
{
    return SYS_OK;
}

sys_err_t wifi_get_ip_addr(wifi_interface_e vif, unsigned int *ip)
{
    struct netif *nif = NULL;

    if (!ip) {
        return SYS_ERR_INVALID_ARG;
    }

    nif = get_netif_by_index(vif);
    if (nif) {
		#ifdef CONFIG_IPV6
		*ip = nif->ip_addr.u_addr.ip4.addr;
		#else
        *ip = nif->ip_addr.addr;
		#endif
    }

    return SYS_OK;
}

sys_err_t wifi_get_mask_addr(wifi_interface_e vif, unsigned int *mask)
{
    struct netif *nif = NULL;

    if (!mask) {
        return SYS_ERR_INVALID_ARG;
    }

    nif = get_netif_by_index(vif);
    if (nif) {
		#ifdef CONFIG_IPV6
		*mask = nif->netmask.u_addr.ip4.addr;
		#else
        *mask = nif->netmask.addr;
		#endif
    }

    return SYS_OK;
}

sys_err_t wifi_get_gw_addr(wifi_interface_e vif, unsigned int *gw)
{
    struct netif *nif = NULL;

    if (!gw) {
        return SYS_ERR_INVALID_ARG;
    }

    nif = get_netif_by_index(STATION_IF);
    if (nif) {
		#ifdef CONFIG_IPV6
		*gw = nif->gw.u_addr.ip4.addr;
		#else
        *gw = nif->gw.addr;
		#endif
    }

    return SYS_OK;
}

#ifndef CONFIG_CUSTOM_LXW
sys_err_t wifi_get_dns_addr(unsigned int index, unsigned int *dns)
{
    if ((index > DNS_MAX_SERVERS) || (!dns)) {
        return SYS_ERR_INVALID_ARG;
    }

	#ifdef CONFIG_IPV6
	*dns = dns_getserver(index)->u_addr.ip4.addr;
	#else
    *dns = dns_getserver(index)->addr;
	#endif
	
    return SYS_OK;
}
#endif

void wifi_init_nv_info(wifi_nv_info_t *nv_info)
{
    if (!nv_info)
        nv_info = &wifi_nv_info;

    memset(nv_info, 0, sizeof(wifi_nv_info_t));
    nv_info->auto_conn = 1;
    nv_info->pci_en = PCI_DEFAULT;
    nv_info->listen_interval = DEFAULT_LISTEN_INTERVAL;
    nv_info->recon_param.interval_second = DEFAULT_INTERVAL_SECOND;
    nv_info->recon_param.repeat_count = DEFAULT_REPEAT_CONUT;
    nv_info->recon_param.passive_disconnect = false;
    nv_info->scan_mode = WIFI_FAST_SCAN;
    nv_info->jap_timeout = DEFAULT_JAP_TIMEOUT;
    nv_info->pmf = MGMT_FRAME_PROTECTION_OPTIONAL;
}


sys_err_t wifi_load_nv_cfg(void)
{
    return SYS_OK;
}

sys_err_t wifi_load_nv_info(wifi_nv_info_t *nv_info)
{
    if (!nv_info)
        nv_info = &wifi_nv_info;

    wifi_init_nv_info(nv_info);
    if (hal_system_get_config(CUSTOMER_NV_WIFI_STA_SSID, nv_info->sta_ssid, sizeof(nv_info->sta_ssid) - 1) <= 0) {
        return SYS_ERR;
    }
    if (hal_system_get_config(CUSTOMER_NV_WIFI_STA_PWD, nv_info->sta_pwd, sizeof(nv_info->sta_pwd) - 1) > 0) {
        is_sta_pwd_on = true;
    }
    hal_system_get_config(CUSTOMER_NV_WIFI_AUTO_CONN, &nv_info->auto_conn, sizeof(nv_info->auto_conn));
    #if defined(CONFIG_WIFI_MESH)
        nv_info->auto_conn = 0;
    #endif
#ifdef CONFIG_APP_AT_COMMAND
    hal_system_get_config(CUSTOMER_NV_WIFI_PCI_EN, &nv_info->pci_en, sizeof(nv_info->pci_en));
    hal_system_get_config(CUSTOMER_NV_WIFI_LISTEN_INTERVAL, &nv_info->listen_interval,
                          sizeof(nv_info->listen_interval));
    hal_system_get_config(CUSTOMER_NV_WIFI_INTERVALSECOND, &nv_info->recon_param.interval_second,
                          sizeof(nv_info->recon_param.interval_second));
    hal_system_get_config(CUSTOMER_NV_WIFI_REPEATCOUNT, &nv_info->recon_param.repeat_count,
                          sizeof(nv_info->recon_param.repeat_count));
    hal_system_get_config(CUSTOMER_NV_WIFI_SCAN_MODE, &nv_info->scan_mode, sizeof(nv_info->scan_mode));
    hal_system_get_config(CUSTOMER_NV_WIFI_JAP_TIMEOUT, &nv_info->jap_timeout, sizeof(nv_info->jap_timeout));
    hal_system_get_config(CUSTOMER_NV_WIFI_PMF, &nv_info->pmf, sizeof(nv_info->pmf));
    hal_system_get_config(CUSTOMER_NV_WIFI_STA_BSSID, nv_info->sta_bssid, sizeof(nv_info->sta_bssid));
#endif
    nv_info->sync = 1;

    SYS_LOGV("wifi_load_nv_info: ssid[%s],pwd[%s],bssid["MACSTR"],auto[%d],pci[%d],listen[%d],reconn[%d],repeat[%d]"\
             ",scan[%d],timeout[%d],pmf[%d]",\
             nv_info->sta_ssid, nv_info->sta_pwd, MAC2STR(nv_info->sta_bssid), nv_info->auto_conn, nv_info->pci_en,\
             nv_info->listen_interval, nv_info->recon_param.interval_second, nv_info->recon_param.repeat_count, nv_info->scan_mode,\
             nv_info->jap_timeout, nv_info->pmf);

    return SYS_OK;
}

sys_err_t wifi_save_nv_info(wifi_nv_info_t *nv_info)
{
    int ret = 0;

    if (!nv_info)
        return SYS_ERR_INVALID_ARG;
    if (!nv_info->sta_ssid[0])
        return SYS_OK;

    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_STA_SSID, nv_info->sta_ssid, strlen(nv_info->sta_ssid));
    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_STA_PWD, nv_info->sta_pwd, strlen(nv_info->sta_pwd));

#ifdef CONFIG_APP_AT_COMMAND
    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_STA_BSSID, nv_info->sta_bssid, sizeof(nv_info->sta_bssid));
    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_STA_CHANNEL, &nv_info->channel, sizeof(nv_info->channel));
    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_SCAN_MODE, &nv_info->scan_mode, sizeof(nv_info->scan_mode));
    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_JAP_TIMEOUT, &nv_info->jap_timeout, sizeof(nv_info->jap_timeout));
    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_PMF, &nv_info->pmf, sizeof(nv_info->pmf));
    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_PCI_EN, &nv_info->pci_en, sizeof(nv_info->pci_en));
    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_LISTEN_INTERVAL, &nv_info->listen_interval,
                                 sizeof(nv_info->listen_interval));
    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_INTERVALSECOND, &nv_info->recon_param.interval_second,
                                 sizeof(nv_info->recon_param.interval_second));
    ret |= hal_system_set_config(CUSTOMER_NV_WIFI_REPEATCOUNT, &nv_info->recon_param.repeat_count,
                                 sizeof(nv_info->recon_param.repeat_count));
#endif

    SYS_LOGI("wifi_save_nv_info:RET:%d ssid[%s],pwd[%s],bssid["MACSTR"],auto[%d],pci[%d],listen[%d],reconn[%d],repeat[%d]"\
             ",scan[%d],timeout[%d],pmf[%d]",\
             ret, nv_info->sta_ssid, nv_info->sta_pwd, MAC2STR(nv_info->sta_bssid), nv_info->auto_conn, nv_info->pci_en,\
             nv_info->listen_interval, nv_info->recon_param.interval_second, nv_info->recon_param.repeat_count, nv_info->scan_mode,\
             nv_info->jap_timeout, nv_info->pmf);

    if (ret) {  // roll back.
        hal_system_del_config(CUSTOMER_NV_WIFI_STA_SSID);
        hal_system_del_config(CUSTOMER_NV_WIFI_STA_PWD);
#ifdef CONFIG_APP_AT_COMMAND
        hal_system_del_config(CUSTOMER_NV_WIFI_STA_BSSID);
        hal_system_del_config(CUSTOMER_NV_WIFI_STA_CHANNEL);
        hal_system_del_config(CUSTOMER_NV_WIFI_SCAN_MODE);
        hal_system_del_config(CUSTOMER_NV_WIFI_JAP_TIMEOUT);
        hal_system_del_config(CUSTOMER_NV_WIFI_PMF);
        hal_system_del_config(CUSTOMER_NV_WIFI_PCI_EN);
        hal_system_del_config(CUSTOMER_NV_WIFI_LISTEN_INTERVAL);
        hal_system_del_config(CUSTOMER_NV_WIFI_INTERVALSECOND);
        hal_system_del_config(CUSTOMER_NV_WIFI_REPEATCOUNT);
#endif
        return SYS_ERR;
    }
    nv_info->sync = 1;

    return SYS_OK;
}

sys_err_t wifi_clean_nv_info(void)
{
    return SYS_OK;
}

extern void set_wep_ok_flag(int wep_flag);
sys_err_t wifi_set_sta_by_nv(wifi_nv_info_t *nv_info)
{
    wifi_config_u sta_config;

    memset(&sta_config, 0, sizeof(sta_config));

    if (!nv_info)
        nv_info = &wifi_nv_info;
    if (!nv_info->sta_ssid[0])
        return SYS_ERR;

    nv_info->recon_param.retry_counts = 0;

 #ifdef CONFIG_APP_AT_COMMAND
    if (wifi_set_scan_method(nv_info->scan_mode)) {
        return SYS_ERR;
    }
 #endif

    sta_config.sta.pmf = nv_info->pmf;
    memcpy(sta_config.sta.ssid, nv_info->sta_ssid, strlen(nv_info->sta_ssid));
    memcpy(sta_config.sta.bssid, nv_info->sta_bssid, sizeof(nv_info->sta_bssid));
#if defined(CONFIG_FAST_CONNECT)
    sta_config.sta.channel = nv_info->channel;
#else
    // sta_config.sta.channel = nv_info->channel;
#endif

    if (is_sta_pwd_on || strlen(nv_info->sta_pwd)) {
        memcpy(sta_config.sta.password, nv_info->sta_pwd, strlen(nv_info->sta_pwd));
    }
    if (nv_info->pci_en && (strlen(nv_info->sta_pwd) == 0 || strlen(nv_info->sta_pwd) == 5)) {
        return SYS_ERR;
    }
    set_wep_ok_flag(!nv_info->pci_en);

    SYS_LOGI("wifi_set_sta_by_nv: ssid[%s],pwd[%s],bssid["MACSTR"],auto[%d],pci[%d],listen[%d],reconn[%d],repeat[%d]"\
             ",scan[%d],timeout[%d],pmf[%d],passive_disconnect[%d]",\
             nv_info->sta_ssid, nv_info->sta_pwd, MAC2STR(nv_info->sta_bssid), nv_info->auto_conn, nv_info->pci_en,\
             nv_info->listen_interval, nv_info->recon_param.interval_second, nv_info->recon_param.repeat_count, nv_info->scan_mode,\
             nv_info->jap_timeout, nv_info->pmf, nv_info->recon_param.passive_disconnect);
#if defined(CONFIG_FAST_CONNECT)
    // return wifi_frist_start_station(&sta_config);
    return wifi_start_station(&sta_config);
#else
    return wifi_start_station(&sta_config);
#endif
}

#if 0
static int set_nv_wifi( int argc, char *argv[])
{
    wifi_set_sta_by_nv(&wifi_nv_info);
    return CMD_RET_SUCCESS;
}
SUBCMD(set, nv_wifi, set_nv_wifi,  "", "");
#endif

/*int wifi_update_nv_sta_start(char *ssid, char *pwd, uint8_t *pmk)
{
    wifi_nv_info_t *nv_info = &wifi_nv_info;

    SYS_LOGE("wifi_update_nv_sta_start()!!!, ssid: %s, pwd: %s", ssid, pwd);

    if (!ssid)
        return SYS_ERR;

    if (strcmp(nv_info->sta_ssid, ssid)) {
        nv_info->sync = 0;
        strlcpy(nv_info->sta_ssid, ssid, sizeof(nv_info->sta_ssid));
    }

    if (!pwd && strlen(nv_info->sta_pwd) != 0) {
        nv_info->sync = 0;
        memset(nv_info->sta_pwd, 0, sizeof(nv_info->sta_pwd));
    } else if (pwd && strcmp(nv_info->sta_pwd, pwd)) {
        nv_info->sync = 0;
        strcpy(nv_info->sta_pwd, pwd);
    }

    if (!nv_info->sync) {
        if (pmk)
            memcpy(nv_info->sta_pmk, pmk, sizeof(nv_info->sta_pmk));
        else
            memset(nv_info->sta_pmk, 0, sizeof(nv_info->sta_pmk));
    }

    return SYS_OK;
}*/

int wifi_update_nv_sta_connected(char *ssid, uint8_t channel)
{
    wifi_nv_info_t *nv_info = &wifi_nv_info;
    wifi_info_t info;
    int result;

    result = wpa_get_wifi_info(&info);
    if (result != SYS_OK)
    {
        SYS_LOGE("%s error", __func__);
        return SYS_ERR;
    }

    if (strcmp(nv_info->sta_ssid, (char *)info.ssid))
    {
        nv_info->sync = 0;
        memset(nv_info->sta_ssid, 0, WIFI_SSID_MAX_LEN);
        memcpy(nv_info->sta_ssid, (char *)info.ssid, info.ssid_len > (WIFI_SSID_MAX_LEN - 1) ? WIFI_SSID_MAX_LEN - 1 : info.ssid_len);
    }

    if (/*info.pwd_len && */strcmp(nv_info->sta_pwd, (char *)info.pwd))
    {
        nv_info->sync = 0;
        memset(nv_info->sta_pwd, 0, WIFI_PWD_MAX_LEN);
        memcpy(nv_info->sta_pwd, (char *)info.pwd, info.pwd_len > WIFI_PWD_MAX_LEN ? WIFI_PWD_MAX_LEN : info.pwd_len);
    }

    if (!(IS_ZERO_MAC(info.bssid) || IS_MULTCAST_MAC(info.bssid)))
    {
        if (memcmp(nv_info->sta_bssid, info.bssid, 6))
        {
            nv_info->sync = 0;
           memcpy(nv_info->sta_bssid, info.bssid, 6);
        }
    }

    if (nv_info->channel != info.channel)
    {
        nv_info->sync = 0;
        nv_info->channel = info.channel;
    }

#ifdef CONFIG_AI_VLLM
    if (nv_info->auto_conn)
#else
    if ((!nv_info->sync) && (nv_info->auto_conn))
#endif
    {
        if (wifi_save_nv_info(nv_info) != SYS_OK)
        {
            SYS_LOGE("wifi save nv failed!!!");
        }
    }

    return SYS_OK;
}

int wifi_use_nv_pmk(char *ssid, char *pwd, uint8_t *pmk)
{
    return SYS_OK;
}

sys_err_t wifi_get_ap_rssi(char *rssi)
{
    return SYS_OK;
}

/****************************************************************
*****************************************************************
****************************************************************/

int wifi_set_country_code(country_code_t *country_code)
{
    wifi_status_e ap_status = wifi_get_ap_status();

    wpa_drv_set_country(country_code);

    if (ap_status == AP_STATUS_STARTED) {
        wifi_set_softap_by_nv(NULL);
    }

    return SYS_OK;
}

int wifi_get_country_code(country_code_t *country_code)
{
    wpa_drv_get_country(country_code);

    return SYS_OK;
}

void wifi_copy_country_code()
{
    wpa_drv_copy_country();
}

int wifi_get_chan2G4_cnt(void)
{
    return fhost_get_chan2G4_cnt();
}

static int bin2hexstr(const unsigned char *src, unsigned int src_len, char *dst, unsigned int dst_len)
{
    char hex_table[] = "0123456789ABCDEF";
    int i;

    if(!src || !dst || dst_len < src_len *2) {
        return -1;
    }

    for(i = 0; i < src_len; i++) {
        *dst++ = hex_table[src[i] >> 4];
        *dst++ = hex_table[src[i] & 0xF];
    }

    return 0;
}


int wifi_set_ap_vendor_elements(const unsigned char *data, unsigned int len)
{
    char *cmd_str = "AP_VENDOR ";
    unsigned int cmd_len = strlen(cmd_str);
    unsigned int dst_len;
    char *dst;

    if (!data || !len) {
        return -1;
    }

    dst_len = cmd_len + len*2 + 1;
    dst = os_zalloc(dst_len);
    if(!dst) {
        return -2;
    }

    memcpy(dst, cmd_str, cmd_len);
    if(bin2hexstr(data, len, &dst[cmd_len], len*2) < 0) {
        os_free(dst);
        return -3;
    }

    wifi_ctrl_iface(SOFTAP_IF, dst);
    os_free(dst);
    wifi_ctrl_iface(SOFTAP_IF, "UPDATE_BEACON");

    return 0;
}

int wifi_set_ap_csa(unsigned short channel, unsigned char csa_count, unsigned char blocktx)
{
    char dst[64] = {0};
    unsigned short freq;

    if (channel > 0 && channel <= 14) {
        freq = wifi_channel_to_freq(channel);
    } else {
        freq = channel;
    }

    sprintf(dst, "CHAN_SWITCH %d %d blocktx %d", csa_count, freq, blocktx);
    wifi_ctrl_iface(SOFTAP_IF, dst);
    return 0;
}

int wifi_set_ap_beacon_interval(unsigned int val)
{
    char dst[32] = {0};

    sprintf(dst, "%s %d", "AP_BEACON_INT", val);
    wifi_ctrl_iface(SOFTAP_IF, dst);
    wifi_ctrl_iface(SOFTAP_IF, "UPDATE_BEACON");

    return 0;
}

void wifi_restore_country_code()
{
    wifi_status_e ap_status = wifi_get_ap_status();
    country_code_t country_code = {0};

    wifi_get_country_code(&country_code);
    if (country_code.policy == WIFI_COUNTRY_POLICY_AUTO) {
        wpa_drv_restore_country();

        if (ap_status == AP_STATUS_STARTED) {
            wifi_stop_softap();
            os_msleep(100);
            wifi_set_softap_by_nv(NULL);
        }
    }
}

extern int fhost_set_scan_method(int scan_method);
int wifi_set_scan_method(wifi_scan_method_e method)
{
    return fhost_set_scan_method(method);
}

wifi_discon_reason_e wifi_get_sta_disconnect_reason ()
{
    return wpa_disconnect_reason();
}

u16 wifi_get_sta_reason_code()
{
    return wpa_get_reason_code();
}

void wifi_csa_switch(uint8_t enable)
{
    csa_switch = enable;
}

void wifi_enable_balance(uint8_t enable)
{
    g_balconn = enable;
}

void wifi_softap_channel_sync(void)
{
    int sta_freq, softap_freq;

    if(wifi_get_ap_status() != AP_STATUS_STARTED) {
        return;
    }

    sta_freq = fhost_get_sta_freq();
    softap_freq = fhost_get_softap_freq();
    if(sta_freq < 0|| softap_freq < 0) {
        SYS_LOGD("%s sta_freq=%d softap:%d", __func__, sta_freq, softap_freq);
        return ;
    }

    if(sta_freq != softap_freq) {
        SYS_LOGD("%s csa %d -> %d", __func__, softap_freq, sta_freq);
        wifi_set_ap_csa(sta_freq, 10, 1);
    }

    return ;
}

int wifi_set_wireless_mode(wireless_mode_e mode, wifi_interface_e vif)
{
    wifi_status_e ap_status = wifi_get_ap_status();
    wifi_status_e sta_status = wifi_get_sta_status();
    int ret;

    if (vif == STATION_IF) {
        g_wirelessmode_sta = mode;

        ret = hal_system_set_config(CUSTOMER_NV_WIFI_WIRELESSMODE_STA, (void *)&(mode), sizeof(mode));
        if (ret) {
            hal_system_del_config(CUSTOMER_NV_WIFI_WIRELESSMODE_STA);
            return SYS_ERR;
        }

        if (sta_status == STA_STATUS_CONNECTED) {
            wifi_stop_station();
            os_msleep(100);
            wifi_set_sta_by_nv(NULL);
        }
    }

    if (vif == SOFTAP_IF) {
        g_wirelessmode_softap = mode;

        ret = hal_system_set_config(CUSTOMER_NV_WIFI_WIRELESSMODE_SOFTAP, (void *)&(mode), sizeof(mode));
        if (ret) {
            hal_system_del_config(CUSTOMER_NV_WIFI_WIRELESSMODE_SOFTAP);
            return SYS_ERR;
        }

        if (ap_status == AP_STATUS_STARTED) {
            wifi_stop_softap();
            os_msleep(100);
            wifi_set_softap_by_nv(NULL);
        }
    }

    return SYS_OK;
}

wireless_mode_e wifi_get_wireless_mode(wifi_interface_e vif)
{
    if (vif == STATION_IF) {
        return g_wirelessmode_sta;
    } else if (vif == SOFTAP_IF) {
        return g_wirelessmode_softap;
    } else {
        return -1;
    }
}

extern int fhost_get_11ax_flag(void);
int wifi_get_11ax_flag(void)
{
    return fhost_get_11ax_flag();
}

void wifi_set_accept_other_bssid_probe_req(uint8_t enable)
{
    extern uint8_t rxu_accept_other_bssid_probe_req;
    rxu_accept_other_bssid_probe_req = enable;
}

/****************************************************************
*****************************************************************
****************************************************************/
