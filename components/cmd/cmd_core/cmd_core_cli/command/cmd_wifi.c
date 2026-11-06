/**
 * @file cmd_wifi.c
 * @brief This is a brief description
 * @details This is the detail description
 * @author liuyong
 * @date 2021-6-8
 * @version V0.1
 * @par Copyright by http://eswin.com/.
 * @par History 1:
 *      Date:
 *      Version:
 *      Author:
 *      Modification:
 *
 * @par History 2:
 */


/*--------------------------------------------------------------------------
*												Include files
--------------------------------------------------------------------------*/
	
#include "cli.h"
#include "system_def.h"
#include "system_config.h"
#include "format_conversion.h"
#include "oshal.h"
#ifdef CONFIG_WIFI_FHOST
#include "system_wifi.h"
#include "common.h"
#endif
#ifdef ENABLE_LWIP_NAPT
#include "system_lwip.h"
#endif
#ifdef CONFIG_SNIFFER
#include "wifi_sniffer.h"
#endif

#define MAC_LEN 17
/*--------------------------------------------------------------------------
* 	                                           	Local Macros
--------------------------------------------------------------------------*/
/** Description of the macro */

/*--------------------------------------------------------------------------
* 	                                           	Local Types
--------------------------------------------------------------------------*/
/**
 * @brief The brief description
 * @details The detail description
 */

/*--------------------------------------------------------------------------
* 	                                           	Local Constants
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
* 	                                           	Local Function Prototypes
--------------------------------------------------------------------------*/
#ifdef CONFIG_WIFI_PRODUCT_FHOST
extern int8_t fhost_cntrl_map_is_valid(void);
extern int fhost_set_reset_mac(void);
#endif

/*--------------------------------------------------------------------------
* 	                                          	Global Constants
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
* 	                                          	Global Variables
--------------------------------------------------------------------------*/
/**  Description of global variable  */

/*--------------------------------------------------------------------------
* 	                                          	Global Function Prototypes
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
* 	                                          	Function Definitions
--------------------------------------------------------------------------*/

#ifdef CONFIG_WIRELESS_WPA_SUPPLICANT
extern sys_err_t wifi_stop_station(void);
static int cmd_wifi_stop_station(   int argc, char *argv[])
{
	wifi_stop_station();
	return CMD_RET_SUCCESS;
}
CLI_CMD(wifi_stop_sta, cmd_wifi_stop_station,  "wifi_stop_sta", "wifi_stop_sta");
extern uint8_t g_enable_cmw;
extern wifi_work_mode_e wifi_get_opmode(void);
static int cmd_wifi_get_opmode(int argc, char *argv[])
{
    int mode;
    char *mode_str;
    mode = wifi_get_opmode();
    switch (mode)
        {
        case WIFI_MODE_STA:
            mode_str = "sta";
            break;
        case WIFI_MODE_AP:
            mode_str = "ap";
            break;
        case WIFI_MODE_AP_STA:
            mode_str = "ap+sta";
            break;
        default:
            mode_str = "error :not any mode";
        }

    os_printf(LM_CMD, LL_INFO, "now mode is %s\n",mode_str);
    return CMD_RET_SUCCESS;
}
CLI_CMD(wifi_get_opmode, cmd_wifi_get_opmode,  "wifi_get_opmode", "wifi_get_mode");

static int cmd_wifi_set_opmode(int argc, char *argv[])
{
    if (argc < 2) {
        return CMD_RET_USAGE;
    }

    if (strcmp(argv[1], "sta") == 0) {
        wifi_set_opmode(WIFI_MODE_STA);
        return CMD_RET_SUCCESS;
    }
    
    if (strcmp(argv[1], "ap") == 0) {
        wifi_set_opmode(WIFI_MODE_AP);
        return CMD_RET_SUCCESS;
    }

    if (strcmp(argv[1], "sta+ap") == 0) {
        wifi_set_opmode(WIFI_MODE_AP_STA);
        return CMD_RET_SUCCESS;
    }

    return CMD_RET_USAGE;
}
CLI_CMD(wifi_set_opmode, cmd_wifi_set_opmode,  "wifi_set_opmode", "wifi_set_opmode sta/ap/sta+ap");

extern sys_err_t wifi_get_wifi_info(wifi_info_t *info);
static int cmd_wifi_get_wifi_info(int argc, char *argv[])
{
    wifi_info_t info;
    char *cipher,*auth;

    wifi_get_wifi_info(&info);

    switch (info.cipher)
        {
        case 0:
            cipher = "none";
            break;
        case 1:
            cipher = "wep_40";
            break;
        case 2:
            cipher = "wep_104";
            break;
        case 3:
            cipher = "tkip";
            break;
        case 4:
            cipher = "ccmp";
            break;
        default:
            cipher = "error :not belong any cipher mode";
        }

    switch (info.auth)
        {
        case 0:
            auth = "open";
            break;
        case 1:
            auth = "wep";
            break;
        case 2:
            auth = "wpa_psk";
            break;
        case 3:
            auth = "wpa2_psk";
            break;
        case 4:
            auth = "wpa_wpa2_psk";
            break;
        default:
            auth = "error :not belong any auth mode";
        }

    os_printf(LM_CMD, LL_INFO, "now auth mode is %s and cipher is %s\n",auth,cipher);
    os_printf(LM_CMD, LL_INFO, "now channel is %d\n",info.channel);
    os_printf(LM_CMD, LL_INFO, "now rssi is %d\n",info.rssi);
    return 0;

}
CLI_CMD(wifi_get_info, cmd_wifi_get_wifi_info,  "wifi_get_info", "wifi_get_info");
extern int hexstr2bin(const char *hex, uint8_t *buf, size_t len);
static int process_str(char *str_src, char *str_dest, char max_len)
{
    char *src = str_src;
    char *dest = str_dest;
    if (*src == '"')
    {
        const char *pos;
        uint8_t len = 0;
        src++;
        pos = strrchr(src, '"');
        if (pos == NULL || pos[1] != '\0')
        {
            return CMD_RET_USAGE;
        }
        len = pos - src;
        if ((len == 0) ||len > (max_len - 1))
        {
            return CMD_RET_USAGE;
        }
        memcpy((char *)dest, src, len);
        dest[len] = '\0';
        return 0;
    }
    else
    {
        uint8_t tlen, hlen = strlen(str_src);
        if (hlen & 1)
        {
            return CMD_RET_USAGE;
        }
        tlen = hlen / 2;
        if (tlen > (max_len - 1))
        {
            return CMD_RET_USAGE;
        }
        if (hexstr2bin(str_src, (unsigned char*)str_dest, tlen)) 
        {
            return CMD_RET_USAGE;
        }
        str_dest[tlen] = '\0';
        return 0;
    }
}

static int cmd_wifi_sta(   int argc, char *argv[])
{
    wifi_config_u config;
    char *ssid = NULL;
    char *pwd = NULL;
    if (argc < 2 || argc > 3)
    {
        return CMD_RET_USAGE;
    }
    ssid = argv[1];
    memset(&config, 0, sizeof(config));
    if (process_str(ssid, (char*)(config.sta.ssid), WIFI_SSID_MAX_LEN))
    {
        return CMD_RET_USAGE;
    }
    if (argc > 2)
    {
        pwd = argv[2];
        if (process_str(pwd, config.sta.password, WIFI_PWD_MAX_LEN))
        {
            return CMD_RET_USAGE;
        }
    }

    wifi_stop_station();
    os_msleep(500);
    g_enable_cmw = 1;
    return wifi_start_station(&config) ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}
CLI_CMD(wifi_sta, cmd_wifi_sta,  "connect wifi as station", "wifi_sta <ssid> [password]");

static int cmd_wifi_sta_wps( int argc, char *argv[])
{
    wifi_config_u config;
    if (argc > 1)
    {
        return CMD_RET_USAGE;
    }
    config.sta.wps_pbc = 1;
    wifi_stop_station();
    os_msleep(500);
    return wifi_start_station(&config) ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}
CLI_CMD(wifi_sta_wps, cmd_wifi_sta_wps,  "connect wifi as station", "wifi_sta_wps");


extern uint16_t get_macsw_sta_max_cnt(void);
static int cmd_wifi_softap(int argc, char *argv[])
{
    wifi_config_u config = {0};
    char *ssid = NULL;
    char *pwd = NULL;
    int i = 1;

    wifi_set_default_softap(&config.ap);

    while (i < argc) {
        char *opt = argv[i];

        if (i + 1 >= argc && (strcmp(opt, "--help") != 0)) {
            return CMD_RET_USAGE;
        }

        if (strcmp(opt, "--ssid") == 0 || strcmp(opt, "-s") == 0) {
            ssid = argv[++i];
            if (process_str(ssid, (char *)config.ap.ssid, WIFI_SSID_MAX_LEN)) {
                return CMD_RET_USAGE;
            }
        } else if (strcmp(opt, "--password") == 0 || strcmp(opt, "-p") == 0) {
            pwd = argv[++i];
            if (process_str(pwd, (char *)config.ap.password, WIFI_PWD_MAX_LEN)) {
                return CMD_RET_USAGE;
            }
            if (config.ap.authmode == AUTH_OPEN) {
                config.ap.authmode = AUTH_WPA_WPA2_PSK;
            }
        } else if (strcmp(opt, "--authmode") == 0 || strcmp(opt, "-a") == 0) {
            char *mode = argv[++i];
            if (strcmp(mode, "WEP") == 0)
                config.ap.authmode = AUTH_WEP;
            else if (strcmp(mode, "WPA") == 0)
                config.ap.authmode = AUTH_WPA_PSK;
            else if (strcmp(mode, "WPA2") == 0)
                config.ap.authmode = AUTH_WPA2_PSK;
            else if (strcmp(mode, "WPA/WPA2") == 0)
                config.ap.authmode = AUTH_WPA_WPA2_PSK;
            else if (strcmp(mode, "OPEN") == 0)
                config.ap.authmode = AUTH_OPEN;
            else {
                return CMD_RET_FAILURE;
            }
        } else if (strcmp(opt, "--channel") == 0 || strcmp(opt, "-c") == 0) {
            config.ap.channel = atoi(argv[++i]);
        } else if (strcmp(opt, "--max_connect") == 0 || strcmp(opt, "-m") == 0) {
            config.ap.max_connect = atoi(argv[++i]);
        } else if (strcmp(opt, "--hidden_ssid") == 0 || strcmp(opt, "-h") == 0) {
            config.ap.hidden_ssid = atoi(argv[++i]);
        } else {
            return CMD_RET_USAGE;
        }
        i++;
    }

    if (config.ap.authmode == AUTH_OPEN && pwd) {
        return CMD_RET_USAGE;
    } else if (config.ap.authmode != AUTH_OPEN && !pwd) {
        return CMD_RET_USAGE;
    }

    if (pwd) {
        size_t pwd_len = strlen(pwd) - 2; // for removing quotes 
        switch (config.ap.authmode) {
            case AUTH_WEP:
                if (pwd_len != 5 && pwd_len != 13) {
                    return CMD_RET_USAGE;
                }
                break;
            case AUTH_WPA_PSK:
            case AUTH_WPA2_PSK:
            case AUTH_WPA_WPA2_PSK:
                if (pwd_len < 8 || pwd_len > 63) {
                    return CMD_RET_USAGE;
                }
                break;
            default:
                break;
        }
    }

    if (config.ap.channel < 1 || config.ap.channel > 14) {
        return CMD_RET_USAGE;
    }
    if (config.ap.max_connect <= 0 || config.ap.max_connect > get_macsw_sta_max_cnt()) {
        return CMD_RET_USAGE;
    }
    if (config.ap.hidden_ssid < 0 || config.ap.hidden_ssid > 1) {
        return CMD_RET_USAGE;
    }

    wifi_stop_softap();
    os_msleep(100);
    return wifi_start_softap(&config) ? CMD_RET_USAGE : CMD_RET_SUCCESS;
}

CLI_CMD(wifi_softap, cmd_wifi_softap, "start a wifi softap",
        "wifi_softap --ssid/-s [\"SSID\"] [--password/-p <\"PWD\">] [--authmode/-a <WEP|WPA|WPA2|WPA/WPA2|OPEN>] "
        "[--channel/-c 1-14] [--max_connect/-m NUM] [--hidden_ssid/-h 0|1]");

static int cmd_wifi_softap_sta_list(int argc, char *argv[])
{
    struct softap_sta_info *sta_list;
    uint32_t num = get_macsw_sta_max_cnt();
    uint32_t i;
    sys_err_t ret;

    if (argc != 1) {
        return CMD_RET_USAGE;
    }

    sta_list = os_malloc(num * sizeof(*sta_list));
    if (sta_list == NULL) {
        return CMD_RET_FAILURE;
    }

    ret = wifi_softap_get_sta_list(sta_list, &num);
    if (ret == SYS_OK) {
        os_printf(LM_CMD, LL_INFO, "softap sta count: %u\n", num);
        for (i = 0; i < num; i++) {
            os_printf(LM_CMD, LL_INFO, "%u: mac=" MAC_STR " hostname=%s\n",
                      i, MAC_VALUE(sta_list[i].mac_addr),
                      sta_list[i].hostname[0] ? sta_list[i].hostname : "<unavailable>");
        }
    }

    os_free(sta_list);
    return ret == SYS_OK ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
}
CLI_CMD(wifi_softap_sta_list, cmd_wifi_softap_sta_list, "show softap station list",
        "wifi_softap_sta_list");

static int cmd_wifi_stop_softap( int argc, char * argv[])
{
    wifi_stop_softap();
    return CMD_RET_SUCCESS;
}
CLI_CMD(wifi_stop_softap, cmd_wifi_stop_softap, "stop a wifi softap", "wifi_stop_softap");

#if 1
static int cmd_wifi_scan(   int argc, char *argv[])
{
    int i;
    wifi_scan_config_t config;
    wifi_info_t info;
    wifi_scan_config_t *pcfg = NULL;
    char *auth_str[] = {"open", "wep", "wpa", "wpa2", "wpa/wpa2", "wpa3", "wpa2/wpa3"};
    char *cipher_str[] = {"", "", "", "-tkip", "-ccmp"};

    if (argc > 2)
    {
        return CMD_RET_USAGE;
    }

    if (argc > 1)
    {
        memset(&config, 0, sizeof(config));
        config.channel = atoi(argv[1]);
        pcfg = &config;
    }

    if (!wifi_scan_start(1, pcfg))
    {
        if (wpa_get_scan_num())
        {
            os_printf(LM_CMD, LL_INFO, "bssid               channel   rssi   encrypt          ssid\n");

            for (i = 0; i < wpa_get_scan_num(); i++)
            {
                memset(&info, 0, sizeof(info));
                if (wpa_get_scan_result(i, &info))
                    break;

                os_printf(LM_CMD, LL_INFO, MACSTR"   %7d   %4d   %9s%-5s   %s\n", 
                          MAC2STR(info.bssid), info.channel, info.rssi, auth_str[info.auth], cipher_str[info.cipher], info.ssid);
            }
        }
        else
        {
            os_printf(LM_CMD, LL_INFO, "not found bss.\n");
        }

        return CMD_RET_SUCCESS;
    }
    else
    {
	    return CMD_RET_FAILURE;
	}
}
CLI_CMD(wifi_scan, cmd_wifi_scan,  "scan wifi as station", "wifi_scan [channel]");
#endif

extern int wpa_cmd_receive(int vif_id, int argc, char *argv[]);
static int cmd_wpa(   int argc, char *argv[])
{
	//os_printf(LM_CMD, LL_INFO, "--wpa cli argc %d, %s %s\n", argc, argv[0] , argc > 1 ? argv[1] : "");
	if (wpa_cmd_receive(0, argc, argv) == 0)
		return CMD_RET_SUCCESS;

	return CMD_RET_FAILURE;
}

static int cmd_wpb(   int argc, char *argv[])
{
	//os_printf(LM_CMD, LL_INFO, "--wpb cli argc %d, %s %s\n", argc, argv[0] , argc > 1 ? argv[1] : "");
	if (wpa_cmd_receive(1, argc, argv) == 0)
		return CMD_RET_SUCCESS;

	return CMD_RET_FAILURE;
}

CLI_CMD(wpa, cmd_wpa,  "wpa cli",	 "wpa [command]");
CLI_CMD(wpb, cmd_wpb,  "wpb cli",	 "wpb [command]");
#endif  //CONFIG_WIRELESS_WPA_SUPPLICANT

#if CONFIG_WIFI_PRODUCT_FHOST
static os_timer_handle_t g_wifi_mac_check_timer = NULL;

static void wifi_check_mac_timeout(os_timer_handle_t timer)
{
    if (g_wifi_mac_check_timer)
        os_timer_stop(g_wifi_mac_check_timer);

    wifi_set_promiscuous(false);
}

static void wifi_check_mac_rx(void *buf, int len, wifi_promiscuous_pkt_type_t type)
{
    uint8_t *sa;
    uint8_t *payload;
    uint8_t sta_mac[6];
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;

    if (type != WIFI_PKT_NULL)
    {
        payload = pkt->payload;

        if ((len >= 24) && (payload[1] & 0x1))
        {
            if (payload[1] & 0x2)
            {
                if (len >= 30)
                    sa = &payload[24];
                else
                    return;
            }
            else
            {
                sa = &payload[10];
            }

            hal_system_get_sta_mac(sta_mac);

            if (!memcmp(sta_mac, sa, 6))
            {
                os_printf(LM_CMD, LL_INFO, "\n\nsta mac address may be duplicate\n\n");
            }
        }
    }
}

static int cmd_wifi_mac_check(   int argc, char *argv[])
{
    int v;

    if (argc < 2)
        return CMD_RET_FAILURE;

    v = atoi(argv[1]);
    os_printf(LM_CMD, LL_INFO, "%s check mac is duplicate\n", v ? "enable" : "disable");

    if (v)
    {
        if (!g_wifi_mac_check_timer)
            g_wifi_mac_check_timer = os_timer_create("mac_check", 60 * 1000, 1, wifi_check_mac_timeout, NULL);

        os_timer_start(g_wifi_mac_check_timer);

        wifi_promiscuous_filter_t filter;
        filter.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;
        wifi_set_promiscuous_filter(&filter);
        wifi_set_promiscuous_rx_cb(wifi_check_mac_rx);
        wifi_set_promiscuous(true);
    }
    else
    {
        if (g_wifi_mac_check_timer)
        {
            os_timer_delete(g_wifi_mac_check_timer);
            g_wifi_mac_check_timer = NULL;
        }

        wifi_set_promiscuous(false);
    }

    if (argc > 2)
    {
        v = atoi(argv[2]);
        os_printf(LM_CMD, LL_INFO, "switch channel %d\n", v);
        wifi_rf_set_channel(v);
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD(wifi_check_mac, cmd_wifi_mac_check,  "check mac is duplicate", "wifi_check_mac <en> [ch]");
#endif

#ifdef CONFIG_LWIP
#ifndef CONFIG_CUSTOM_LXW
extern void ping_run(char *cmd);
static int cmd_wifi_format_actual_len(int format_ret, int remaining)
{
    if (format_ret < 0 || remaining <= 0)
    {
        return 0;
    }

    if (format_ret >= remaining)
    {
        return remaining - 1;
    }

    return format_ret;
}

int cmd_ping(   int argc, char *argv[])
{
    int i, len = 0;
    char buf[512] = {0};

    for (i = 1; i < argc; i++)
    {
        int remaining = sizeof(buf) - len;
        if (remaining <= 0)
        {
            break;
        }
        len += cmd_wifi_format_actual_len(snprintf(buf + len, remaining, "%s ", argv[i]), remaining);
    }

    ping_run(buf);
    return CMD_RET_SUCCESS;
}
CLI_CMD(ping, cmd_ping, "ping command", "ping [IP address]");

extern void wifi_ifconfig(char* cmd);
int cmd_ifconfig(   int argc, char *argv[])
{
	int i, len = 0;
	char buf[64] = {0};

    for (i = 1; i < argc; i++)
    {
        int remaining = sizeof(buf) - len;
        if (remaining <= 0)
        {
            break;
        }
        len += cmd_wifi_format_actual_len(snprintf(buf + len, remaining, "%s ", argv[i]), remaining);
    }

	wifi_ifconfig(buf);
	return CMD_RET_SUCCESS;
}
CLI_CMD(ifconfig, cmd_ifconfig, "ifconfig command", "ifconfig -h");
#endif
#endif  //CONFIG_LWIP

#ifdef CONFIG_WIFI_PRODUCT_FHOST

int set_mac_func(   int argc, char *argv[])
{
	if(argc != 2)
	{
		os_printf(LM_CMD, LL_ERR, "ARG NUM(: %d) IS ERROR!!\n", argc);
		return CMD_RET_FAILURE;
	}	
	
	if(hal_system_set_mac(MAC_SAVE_TO_AMT,argv[1]))
	{
		return CMD_RET_FAILURE;
	}	
    hal_system_set_primary_if(STATION_IF);
	return CMD_RET_SUCCESS;
}
CLI_CMD(set_mac, set_mac_func, "set_mac 00:11:22:33:44:55", "set_mac 00:11:22:33:44:55");


int get_mac_func(   int argc, char *argv[])
{
	//int i = 0;
	//char tmp_mac[18] = {0};
	unsigned char mac[6] = {0};
	if(!hal_system_get_config(STA_MAC,mac,sizeof(mac)))
	{
		return CMD_RET_FAILURE;
	}
	#if 0
	for (i=0; i<6; i++)
	{
		mac[i] = hex2num(tmp_mac[i*3]) * 0x10 + hex2num(tmp_mac[i*3+1]);
	}
	#endif
	os_printf(LM_CMD, LL_ERR, "sta mac = %02x:%02x:%02x:%02x:%02x:%02x\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
	memset(mac,0,6);
	hal_system_get_sta_mac(mac);
	os_printf(LM_CMD, LL_INFO, "sta %02x:%02x:%02x:%02x:%02x:%02x\n,", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	memset(mac,0,6);
	hal_system_get_ap_mac(mac);
	os_printf(LM_CMD, LL_INFO, "ap %02x:%02x:%02x:%02x:%02x:%02x\n,", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	memset(mac,0,6);
	hal_system_get_ble_mac(mac);
	os_printf(LM_CMD, LL_INFO, "ble %02x:%02x:%02x:%02x:%02x:%02x\n,", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return CMD_RET_SUCCESS;
}
CLI_CMD(get_mac, get_mac_func, "get_mac", "get_mac");

static int wifi_proto(int argc, char *argv[])
{
    static const char usage_str[] =
        "Usage: wifi_proto <mode: 0-bgnax 1-bg 2-bgn 3-b> [vif: 0-sta 1-softap (default all)]\n"
        "       wifi_proto ?  Show current settings\n";

#define VALIDATE_NUMBER(str, min, max)                                                        \
    do {                                                                                      \
        char *endptr = NULL;                                                                  \
        long val = strtol((str), &endptr, 10);                                                \
        if (*endptr != '\0' || endptr == (str)) {                                             \
            os_printf(LM_CMD, LL_ERR, "Invalid number: %s\n", (str));                         \
            return CMD_RET_USAGE;                                                             \
        }                                                                                     \
        if (val < (min) || val > (max)) {                                                     \
            os_printf(LM_CMD, LL_ERR, "Value %ld out of range [%d-%d]\n", val, (min), (max)); \
            return CMD_RET_USAGE;                                                             \
        }                                                                                     \
    } while (0)

    if (argc < 2 || argc > 3) {
        os_printf(LM_CMD, LL_ERR, "Invalid argument count\n%s", usage_str);
        return CMD_RET_USAGE;
    }

    if (argc == 2 && strcmp(argv[1], "?") == 0) {
        os_printf(LM_CMD, LL_ERR, "STA protocol: %d\nSoftAP protocol: %d\n", wifi_get_wireless_mode(STATION_IF),
                  wifi_get_wireless_mode(SOFTAP_IF));
        return CMD_RET_SUCCESS;
    }

    VALIDATE_NUMBER(argv[1], WIRELESSMODE_80211BGNAX, WIRELESSMODE_80211B);
    int mode = atoi(argv[1]);

    if (argc == 2) {
        wifi_set_wireless_mode(mode, STATION_IF);
        wifi_set_wireless_mode(mode, SOFTAP_IF);
        os_printf(LM_CMD, LL_ERR, "Set both interfaces to mode %d\n", mode);
    } else {
        VALIDATE_NUMBER(argv[2], STATION_IF, SOFTAP_IF);
        int vif = atoi(argv[2]);

        wifi_set_wireless_mode(mode, vif);
        os_printf(LM_CMD, LL_ERR, "Set interface %d to mode %d\n", vif, mode);
    }

    return CMD_RET_SUCCESS;

#undef VALIDATE_NUMBER
}
CLI_CMD(wifi_proto, wifi_proto, "wifi_proto", "wifi_proto <mode: 0 bgnax, 1 bg, 2 bgn, 3 b> [vif: 0 sta, 1 softap, NULL for all] or wifi_proto ?");

typedef struct sc_head
{
    unsigned char dummyap[26];
    unsigned char dummy[32];
} sc_head_t;

static sc_head_t sc_head;
static unsigned char source_mac[6];
static unsigned char destination_mac[6];

static int process_frame(void *packet, int size, wifi_promiscuous_pkt_type_t type)
{
    int isvalid = 0;
    int seq_num = 0;
    int fromDS_flag = 0;
    int toDS_flag = 0;
    int i;
    unsigned char ch;
    char frameType[5];
    int cts_flag = 0;
    // after discover
    uint16_t duration = 0;

    for (i = 0; i < 24; i++) {
        ch = *((unsigned char*)packet + i);
        sc_head.dummy[i] = ch;
    }

    if ((type == WIFI_PKT_CTRL) && ((sc_head.dummy[0] & 0xf0) == 0xc0)) {
        cts_flag = 1;
    }

    if (!cts_flag) {
        if (memcmp(source_mac, &sc_head.dummy[10], 6) != 0) {
            goto invalid;
        }
    }
    if (memcmp(destination_mac, &sc_head.dummy[4], 6) != 0) {
        goto invalid;
    }

    if((sc_head.dummy[1] & 0x02) == 0x02){
        fromDS_flag = 1;
    }

    if ((sc_head.dummy[1] & 0x01) == 0x01) {
        toDS_flag = 1;
    }

    seq_num = (sc_head.dummy[22] + sc_head.dummy[23] * 256) >> 4;

    isvalid = 1;

    if (type == WIFI_PKT_MGMT) {
        memcpy(frameType, "MGMT", 5);
    } else if (type == WIFI_PKT_CTRL) {
        memcpy(frameType, "CTRL", 5);
    } else if (type == WIFI_PKT_DATA) {
        memcpy(frameType, "DATA", 5);
    } else {
        goto invalid;
    }

    if (type == WIFI_PKT_CTRL) {
        duration = sc_head.dummy[3];
        duration = (duration << 8) | sc_head.dummy[2];
        if ((sc_head.dummy[0] & 0xf0) == 0xc0) {
            os_printf(LM_WIFI, LL_ERR, "[%s]-[CTS]-[Duration-%d]\n", frameType, duration);
        } else if ((sc_head.dummy[0] & 0xf0) == 0xb0) {
            os_printf(LM_WIFI, LL_ERR, "[%s]-[RTS]-[Duration-%d]\n", frameType, duration);
        } else {
            os_printf(LM_WIFI, LL_ERR, "[%s]-[seq_num-%d]-[FromDS-%d]-[ToDS-%d]\n", frameType, seq_num, fromDS_flag, toDS_flag);
        }
    } else {
        os_printf(LM_WIFI, LL_ERR, "[%s]-[seq_num-%d]-[FromDS-%d]-[ToDS-%d]\n", frameType, seq_num, fromDS_flag, toDS_flag);
    }
invalid:
    return isvalid;
}

static void SnifferCallback(void *buf, int len, wifi_promiscuous_pkt_type_t type)
{
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf; 

    if (type != WIFI_PKT_NULL) {
        process_frame((char *)pkt->payload, len, type);
    }
}

int set_sniffer_start(   int argc, char *argv[])
{
    int ch;
    int mgmt, ctrl, data;
    char *source_macstr;
    char *des_macstr;

    wifi_remove_config_all(STATION_IF);
    wifi_remove_config_all(SOFTAP_IF);

    ch = atoi(argv[1]);
    wifi_rf_set_channel(ch);

    mgmt = atoi(argv[2]);
    ctrl = atoi(argv[3]);
    data = atoi(argv[4]);
    source_macstr = argv[5];
    des_macstr = argv[6];

    system_printf("==source_mac=");
    for (int i = 0; i < 6 ; i++) {
        source_mac[i] = hex2num(source_macstr[i*3]) * 0x10 + hex2num(source_macstr[i*3+1]);
        system_printf("=%x=", source_mac[i]);
    }

    system_printf("\n==destination_mac=");
    for (int i = 0; i < 6 ; i++) {
        destination_mac[i] = hex2num(des_macstr[i*3]) * 0x10 + hex2num(des_macstr[i*3+1]);
        system_printf("=%x=", destination_mac[i]);
    }
    system_printf("\n");
    if (mgmt == 1) {
        mgmt = WIFI_PROMIS_FILTER_MASK_MGMT;
    } else {
        mgmt = 0;
    }
    if (ctrl == 1) {
        ctrl = WIFI_PROMIS_FILTER_MASK_CTRL;
    } else {
        ctrl = 0;
    }
    if (data == 1) {
        data = WIFI_PROMIS_FILTER_MASK_DATA;
    } else {
        data = 0;
    }
    wifi_promiscuous_filter_t filter = {0};
    filter.filter_mask = mgmt | ctrl | data;
    wifi_set_promiscuous_filter(&filter);
    wifi_set_promiscuous_rx_cb(SnifferCallback);
    wifi_set_promiscuous(true);

    return 0;
}
CLI_CMD(sniffer_start, set_sniffer_start, "set_sniffer_start", "set_sniffer_start");

int set_sniffer_stop(   int argc, char *argv[])
{
    wifi_set_promiscuous(false);

    return 0;
}
CLI_CMD(sniffer_stop, set_sniffer_stop, "set_sniffer_stop", "set_sniffer_stop");

#ifdef CONFIG_SNIFFER
// RSSI statistics structure
typedef struct {
    uint32_t rssi_gt_minus20_fcs_ok;      // RSSI > -20 dBm, FCS OK
    uint32_t rssi_gt_minus20_fcs_bad;     // RSSI > -20 dBm, FCS BAD
    uint32_t rssi_minus20_30_fcs_ok;      // -20 to -30 dBm, FCS OK
    uint32_t rssi_minus20_30_fcs_bad;     // -20 to -30 dBm, FCS BAD
    uint32_t rssi_minus30_40_fcs_ok;      // -30 to -40 dBm, FCS OK
    uint32_t rssi_minus30_40_fcs_bad;     // -30 to -40 dBm, FCS BAD
    uint32_t rssi_minus40_50_fcs_ok;      // -40 to -50 dBm, FCS OK
    uint32_t rssi_minus40_50_fcs_bad;     // -40 to -50 dBm, FCS BAD
    uint32_t rssi_minus50_60_fcs_ok;      // -50 to -60 dBm, FCS OK
    uint32_t rssi_minus50_60_fcs_bad;     // -50 to -60 dBm, FCS BAD
    uint32_t rssi_minus60_70_fcs_ok;      // -60 to -70 dBm, FCS OK
    uint32_t rssi_minus60_70_fcs_bad;     // -60 to -70 dBm, FCS BAD
    uint32_t rssi_minus70_80_fcs_ok;      // -70 to -80 dBm, FCS OK
    uint32_t rssi_minus70_80_fcs_bad;     // -70 to -80 dBm, FCS BAD
    uint32_t rssi_lt_minus80_fcs_ok;      // RSSI < -80 dBm, FCS OK
    uint32_t rssi_lt_minus80_fcs_bad;     // RSSI < -80 dBm, FCS BAD
} rssi_stats_t;

static rssi_stats_t g_rssi_stats;
static os_timer_handle_t g_rssi_stats_timer = NULL;
static bool g_rssi_sniffer_enabled = false;
static uint32_t g_rssi_sniffer_period_ms = 1000; // Default 1 second

static void rssi_stats_print_and_reset(os_timer_handle_t timer)
{
    // Limit values to 9999
    uint32_t val1a = g_rssi_stats.rssi_gt_minus20_fcs_ok > 9999 ? 9999 : g_rssi_stats.rssi_gt_minus20_fcs_ok;
    uint32_t val1b = g_rssi_stats.rssi_gt_minus20_fcs_bad > 9999 ? 9999 : g_rssi_stats.rssi_gt_minus20_fcs_bad;
    uint32_t val2a = g_rssi_stats.rssi_minus20_30_fcs_ok > 9999 ? 9999 : g_rssi_stats.rssi_minus20_30_fcs_ok;
    uint32_t val2b = g_rssi_stats.rssi_minus20_30_fcs_bad > 9999 ? 9999 : g_rssi_stats.rssi_minus20_30_fcs_bad;
    uint32_t val3a = g_rssi_stats.rssi_minus30_40_fcs_ok > 9999 ? 9999 : g_rssi_stats.rssi_minus30_40_fcs_ok;
    uint32_t val3b = g_rssi_stats.rssi_minus30_40_fcs_bad > 9999 ? 9999 : g_rssi_stats.rssi_minus30_40_fcs_bad;
    uint32_t val4a = g_rssi_stats.rssi_minus40_50_fcs_ok > 9999 ? 9999 : g_rssi_stats.rssi_minus40_50_fcs_ok;
    uint32_t val4b = g_rssi_stats.rssi_minus40_50_fcs_bad > 9999 ? 9999 : g_rssi_stats.rssi_minus40_50_fcs_bad;
    uint32_t val5a = g_rssi_stats.rssi_minus50_60_fcs_ok > 9999 ? 9999 : g_rssi_stats.rssi_minus50_60_fcs_ok;
    uint32_t val5b = g_rssi_stats.rssi_minus50_60_fcs_bad > 9999 ? 9999 : g_rssi_stats.rssi_minus50_60_fcs_bad;
    uint32_t val6a = g_rssi_stats.rssi_minus60_70_fcs_ok > 9999 ? 9999 : g_rssi_stats.rssi_minus60_70_fcs_ok;
    uint32_t val6b = g_rssi_stats.rssi_minus60_70_fcs_bad > 9999 ? 9999 : g_rssi_stats.rssi_minus60_70_fcs_bad;
    uint32_t val7a = g_rssi_stats.rssi_minus70_80_fcs_ok > 9999 ? 9999 : g_rssi_stats.rssi_minus70_80_fcs_ok;
    uint32_t val7b = g_rssi_stats.rssi_minus70_80_fcs_bad > 9999 ? 9999 : g_rssi_stats.rssi_minus70_80_fcs_bad;
    uint32_t val8a = g_rssi_stats.rssi_lt_minus80_fcs_ok > 9999 ? 9999 : g_rssi_stats.rssi_lt_minus80_fcs_ok;
    uint32_t val8b = g_rssi_stats.rssi_lt_minus80_fcs_bad > 9999 ? 9999 : g_rssi_stats.rssi_lt_minus80_fcs_bad;

    // Output format: each value on a separate line
    os_printf(LM_CMD, LL_INFO, "1A%04u\n", val1a);
    os_printf(LM_CMD, LL_INFO, "1B%04u\n", val1b);
    os_printf(LM_CMD, LL_INFO, "2A%04u\n", val2a);
    os_printf(LM_CMD, LL_INFO, "2B%04u\n", val2b);
    os_printf(LM_CMD, LL_INFO, "3A%04u\n", val3a);
    os_printf(LM_CMD, LL_INFO, "3B%04u\n", val3b);
    os_printf(LM_CMD, LL_INFO, "4A%04u\n", val4a);
    os_printf(LM_CMD, LL_INFO, "4B%04u\n", val4b);
    os_printf(LM_CMD, LL_INFO, "5A%04u\n", val5a);
    os_printf(LM_CMD, LL_INFO, "5B%04u\n", val5b);
    os_printf(LM_CMD, LL_INFO, "6A%04u\n", val6a);
    os_printf(LM_CMD, LL_INFO, "6B%04u\n", val6b);
    os_printf(LM_CMD, LL_INFO, "7A%04u\n", val7a);
    os_printf(LM_CMD, LL_INFO, "7B%04u\n", val7b);
    os_printf(LM_CMD, LL_INFO, "8A%04u\n", val8a);
    os_printf(LM_CMD, LL_INFO, "8B%04u\n", val8b);

    // Check if all statistics are zero (possible RX stuck issue)
    uint32_t total_count = val1a + val1b + val2a + val2b + val3a + val3b + val4a + val4b +
                           val5a + val5b + val6a + val6b + val7a + val7b + val8a + val8b;

    if (total_count == 0) {
        fhost_set_reset_mac();
    }

    // Reset statistics
    memset(&g_rssi_stats, 0, sizeof(rssi_stats_t));
}

static void rssi_sniffer_callback(void *buf, int len, wifi_promiscuous_pkt_type_t type)
{
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    int8_t rssi;
    bool fcs_ok;

    // Get RSSI from rx_ctrl
    rssi = pkt->rx_ctrl.rssi;

    // Get FCS status from packet
    fcs_ok = pkt->fcs_ok ? true : false;

    // Update statistics based on RSSI range and FCS status
    if (rssi > -20) {
        if (fcs_ok)
            g_rssi_stats.rssi_gt_minus20_fcs_ok++;
        else
            g_rssi_stats.rssi_gt_minus20_fcs_bad++;
    } else if (rssi > -30) {
        if (fcs_ok)
            g_rssi_stats.rssi_minus20_30_fcs_ok++;
        else
            g_rssi_stats.rssi_minus20_30_fcs_bad++;
    } else if (rssi > -40) {
        if (fcs_ok)
            g_rssi_stats.rssi_minus30_40_fcs_ok++;
        else
            g_rssi_stats.rssi_minus30_40_fcs_bad++;
    } else if (rssi > -50) {
        if (fcs_ok)
            g_rssi_stats.rssi_minus40_50_fcs_ok++;
        else
            g_rssi_stats.rssi_minus40_50_fcs_bad++;
    } else if (rssi > -60) {
        if (fcs_ok)
            g_rssi_stats.rssi_minus50_60_fcs_ok++;
        else
            g_rssi_stats.rssi_minus50_60_fcs_bad++;
    } else if (rssi > -70) {
        if (fcs_ok)
            g_rssi_stats.rssi_minus60_70_fcs_ok++;
        else
            g_rssi_stats.rssi_minus60_70_fcs_bad++;
    } else if (rssi > -80) {
        if (fcs_ok)
            g_rssi_stats.rssi_minus70_80_fcs_ok++;
        else
            g_rssi_stats.rssi_minus70_80_fcs_bad++;
    } else {
        if (fcs_ok)
            g_rssi_stats.rssi_lt_minus80_fcs_ok++;
        else
            g_rssi_stats.rssi_lt_minus80_fcs_bad++;
    }
}

static int cmd_rssi_sniffer_start(int argc, char *argv[])
{
    int channel = 1;
    uint32_t period_sec = 1;

    // Parse parameters: channel and period
    if (argc < 3) {
        os_printf(LM_CMD, LL_ERR, "Usage: rssi_sniffer_start <channel> <period_sec>\n");
        return CMD_RET_USAGE;
    }

    channel = atoi(argv[1]);
    if (channel < 1 || channel > 14) {
        os_printf(LM_CMD, LL_ERR, "Invalid channel: %d (must be 1-14)\n", channel);
        return CMD_RET_USAGE;
    }

    period_sec = atoi(argv[2]);
    if (period_sec < 1) {
        os_printf(LM_CMD, LL_ERR, "Invalid period: %u (must be >= 1 second)\n", period_sec);
        return CMD_RET_USAGE;
    }

    // Convert period to milliseconds
    g_rssi_sniffer_period_ms = period_sec * 1000;

    // Remove any existing WiFi configurations
    wifi_remove_config_all(STATION_IF);
    wifi_remove_config_all(SOFTAP_IF);

    // Wait for VIF to be deleted completely
    os_printf(LM_CMD, LL_INFO, "Waiting for STA/AP VIF to be removed...\n");
    int wait_count = 0;
    while (fhost_cntrl_map_is_valid() != 0) {
        os_msleep(10);
        wait_count++;
        if (wait_count > 500) {  // 5 seconds timeout
            os_printf(LM_CMD, LL_ERR, "Timeout waiting for VIF removal\n");
            return CMD_RET_FAILURE;
        }
    }
    os_printf(LM_CMD, LL_INFO, "VIF removed successfully (waited %d ms)\n", wait_count * 10);

    // Stop any existing sniffer
    if (g_rssi_sniffer_enabled) {
        os_printf(LM_CMD, LL_INFO, "Stopping existing RSSI sniffer...\n");
        wifi_set_promiscuous(false);
        if (g_rssi_stats_timer) {
            os_timer_stop(g_rssi_stats_timer);
        }
    }

    // Clear statistics
    memset(&g_rssi_stats, 0, sizeof(rssi_stats_t));

    // Configure promiscuous filter to receive all packet types
    wifi_promiscuous_filter_t filter = {0};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;
    wifi_set_promiscuous_filter(&filter);

    // Set promiscuous callback
    wifi_set_promiscuous_rx_cb(rssi_sniffer_callback);

    // Register FCS error callback
    wifi_set_fcs_error_cb(process_fcs_error_frame);

    // Enable promiscuous mode FIRST (this creates monitor VIF)
    wifi_set_promiscuous(true);

    // THEN set channel (requires monitor VIF to exist)
    wifi_rf_set_channel(channel);
    os_printf(LM_CMD, LL_INFO, "Set channel to %d\n", channel);

    // Create timer if not exists, or update period if timer exists
    if (g_rssi_stats_timer) {
        os_timer_stop(g_rssi_stats_timer);
        os_timer_delete(g_rssi_stats_timer);
        g_rssi_stats_timer = NULL;
    }

    g_rssi_stats_timer = os_timer_create("rssi_stats", g_rssi_sniffer_period_ms, 1, rssi_stats_print_and_reset, NULL);
    if (!g_rssi_stats_timer) {
        os_printf(LM_CMD, LL_ERR, "Failed to create timer\n");
        wifi_set_promiscuous(false);
        wifi_set_fcs_error_cb(NULL);
        return CMD_RET_FAILURE;
    }

    // Start timer
    os_timer_start(g_rssi_stats_timer);

    g_rssi_sniffer_enabled = true;
    os_printf(LM_CMD, LL_INFO, "RSSI sniffer started on channel %d\n", channel);
    os_printf(LM_CMD, LL_INFO, "Statistics will be printed every %u second(s)\n", period_sec);

    return CMD_RET_SUCCESS;
}

static int cmd_rssi_sniffer_stop(int argc, char *argv[])
{
    if (!g_rssi_sniffer_enabled) {
        os_printf(LM_CMD, LL_INFO, "RSSI sniffer is not running\n");
        return CMD_RET_SUCCESS;
    }

    // Stop timer
    if (g_rssi_stats_timer) {
        os_timer_stop(g_rssi_stats_timer);
    }

    // Disable promiscuous mode
    wifi_set_promiscuous(false);

    // Unregister FCS error callback
    wifi_set_fcs_error_cb(NULL);

    g_rssi_sniffer_enabled = false;
    os_printf(LM_CMD, LL_INFO, "RSSI sniffer stopped\n");

    return CMD_RET_SUCCESS;
}

static int cmd_rssi_sniffer_set_ch(int argc, char *argv[])
{
    uint8_t channel;

    if (!g_rssi_sniffer_enabled) {
        os_printf(LM_CMD, LL_ERR, "RSSI sniffer is not running\n");
        os_printf(LM_CMD, LL_INFO, "Please start sniffer first using: rssi_sniffer_start\n");
        return CMD_RET_FAILURE;
    }

    channel = atoi(argv[1]);
    if (channel < 1 || channel > 14) {
        os_printf(LM_CMD, LL_ERR, "Invalid channel: %d (must be 1-14)\n", channel);
        return CMD_RET_USAGE;
    }

    // Clear statistics when changing channel
    memset(&g_rssi_stats, 0, sizeof(rssi_stats_t));

    // Set new channel
    wifi_rf_set_channel(channel);
    os_printf(LM_CMD, LL_INFO, "RSSI sniffer channel changed to %d\n", channel);
    os_printf(LM_CMD, LL_INFO, "Statistics cleared\n");

    return CMD_RET_SUCCESS;
}

CLI_CMD(rssi_sniffer_start, cmd_rssi_sniffer_start, "Start RSSI statistics sniffer", "rssi_sniffer_start <channel> <period_sec>");
CLI_CMD(rssi_sniffer_stop, cmd_rssi_sniffer_stop, "Stop RSSI statistics sniffer", "rssi_sniffer_stop");
CLI_CMD(rssi_sniffer_set_ch, cmd_rssi_sniffer_set_ch, "Set RSSI sniffer channel", "rssi_sniffer_set_ch <channel>");
#endif // CONFIG_SNIFFER

#endif // CONFIG_WIFI_PRODUCT_FHOST

#ifdef ENABLE_LWIP_NAPT
extern void napt_debug_print();
int lwip_napt_set(   int argc, char *argv[])
{
    if (argc < 2)
        return CMD_RET_USAGE;

    if (0 == strcmp("enable", argv[1])) {
        enable_lwip_napt(SOFTAP_IF, 1);
    } else if (0 == strcmp("disable", argv[1])) {
        enable_lwip_napt(SOFTAP_IF, 0);
    } else if (0 == strcmp("show", argv[1])) {
        napt_debug_print();
    } else {
        return CMD_RET_USAGE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD(napt, lwip_napt_set, "lwip napt", "napt enable/disable/show");
#endif

extern int call_wifi_dbg_func(int argc, char *argv[]);
int wifi_dbg_cmd(   int argc, char *argv[])
{
    return call_wifi_dbg_func(argc - 1, argv + 1) ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}
CLI_CMD(wd, wifi_dbg_cmd, "wifi debug", "wd [para]");

#ifdef NX_ESWIN_LMAC_TEST
	int modem_cmd(   int argc, char *argv[]);
	CLI_CMD(lmt, modem_cmd, "", "");
	CLI_CMD(rf, modem_cmd, "", "");
	CLI_CMD(macbyp, modem_cmd, "", "");
#endif
