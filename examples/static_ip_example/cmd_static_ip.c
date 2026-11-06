#include <stdio.h>
#include <string.h>

#include "cli.h"
#include "oshal.h"
#include "system_network.h"
#include "system_wifi.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "cmd_static_ip.h"

#define STATIC_IP_TAG "[static_ip]"

typedef struct {
    bool configured[MAX_IF];
    bool dhcp_stopped[MAX_IF];
    static_ip_config_t config[MAX_IF];
} static_ip_runtime_t;

static static_ip_runtime_t g_static_ip_ctx;

static int static_ip_is_valid_vif(int vif)
{
    return (vif == STATION_IF) || (vif == SOFTAP_IF);
}

static void static_ip_status_reset(int vif)
{
    if (!static_ip_is_valid_vif(vif)) {
        return;
    }

    g_static_ip_ctx.configured[vif] = false;
    g_static_ip_ctx.dhcp_stopped[vif] = false;
    memset(&g_static_ip_ctx.config[vif], 0, sizeof(g_static_ip_ctx.config[vif]));
}

static int static_ip_parse_ip_info(const static_ip_config_t *cfg, ip_info_t *info)
{
    if ((cfg == NULL) || (info == NULL)) {
        return -1;
    }

    memset(info, 0, sizeof(*info));

    if (ipaddr_aton(cfg->ip, &info->ip) == 0) {
        os_printf(LM_CMD, LL_ERR, STATIC_IP_TAG " Invalid IP address: %s\r\n", cfg->ip);
        return -1;
    }

    if (ipaddr_aton(cfg->netmask, &info->netmask) == 0) {
        os_printf(LM_CMD, LL_ERR, STATIC_IP_TAG " Invalid netmask: %s\r\n", cfg->netmask);
        return -1;
    }

    if (ipaddr_aton(cfg->gateway, &info->gw) == 0) {
        os_printf(LM_CMD, LL_ERR, STATIC_IP_TAG " Invalid gateway: %s\r\n", cfg->gateway);
        return -1;
    }

    if ((cfg->dns1[0] != '\0') && (ipaddr_aton(cfg->dns1, &info->dns1) == 0)) {
        os_printf(LM_CMD, LL_ERR, STATIC_IP_TAG " Invalid DNS1: %s\r\n", cfg->dns1);
        return -1;
    }

    if ((cfg->dns2[0] != '\0') && (ipaddr_aton(cfg->dns2, &info->dns2) == 0)) {
        os_printf(LM_CMD, LL_ERR, STATIC_IP_TAG " Invalid DNS2: %s\r\n", cfg->dns2);
        return -1;
    }

    return 0;
}

static void static_ip_ntoa(const ip_addr_t *addr, char *buf, size_t len)
{
    if ((addr == NULL) || (buf == NULL) || (len == 0U)) {
        return;
    }

    if (ip_addr_isany(addr)) {
        snprintf(buf, len, "0.0.0.0");
        return;
    }

    ipaddr_ntoa_r(addr, buf, (int)len);
}

static int static_ip_apply_sta(const static_ip_config_t *cfg, const ip_info_t *info)
{
    if (wifi_station_dhcpc_stop(STATION_IF) != 0) {
        os_printf(LM_CMD, LL_ERR, STATIC_IP_TAG " Failed to stop STA DHCP client\r\n");
        return -1;
    }

    set_sta_ipconfig((struct ip_info *)info);
    wifi_set_ip_info(STATION_IF, (struct ip_info *)info);

    g_static_ip_ctx.configured[STATION_IF] = true;
    g_static_ip_ctx.dhcp_stopped[STATION_IF] = true;
    g_static_ip_ctx.config[STATION_IF] = *cfg;

    os_printf(LM_CMD, LL_INFO, STATIC_IP_TAG " STA static IP configured\r\n");
    return 0;
}

static int static_ip_apply_ap(const static_ip_config_t *cfg, const ip_info_t *info)
{
    dhcp_status_t dhcps_status;

    set_softap_ipconfig((struct ip_info *)info);
    wifi_set_ip_info(SOFTAP_IF, (struct ip_info *)info);

    dhcps_status = wifi_softap_dhcps_status(SOFTAP_IF);
    if (dhcps_status == TCPIP_DHCP_STARTED) {
        wifi_softap_dhcps_stop(SOFTAP_IF);
        wifi_softap_dhcps_start(SOFTAP_IF);
    }

    g_static_ip_ctx.configured[SOFTAP_IF] = true;
    g_static_ip_ctx.dhcp_stopped[SOFTAP_IF] = false;
    g_static_ip_ctx.config[SOFTAP_IF] = *cfg;

    os_printf(LM_CMD, LL_INFO, STATIC_IP_TAG " SoftAP static IP configured\r\n");
    return 0;
}

static void static_ip_print_config(const char *title, const static_ip_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }

    os_printf(LM_CMD, LL_INFO, STATIC_IP_TAG " %s\r\n", title);
    os_printf(LM_CMD, LL_INFO, STATIC_IP_TAG "   IP:      %s\r\n", cfg->ip);
    os_printf(LM_CMD, LL_INFO, STATIC_IP_TAG "   Netmask: %s\r\n", cfg->netmask);
    os_printf(LM_CMD, LL_INFO, STATIC_IP_TAG "   Gateway: %s\r\n", cfg->gateway);
    os_printf(LM_CMD, LL_INFO, STATIC_IP_TAG "   DNS1:    %s\r\n", cfg->dns1);
    os_printf(LM_CMD, LL_INFO, STATIC_IP_TAG "   DNS2:    %s\r\n", cfg->dns2);
}

static int static_ip_parse_vif(int argc, char *argv[], int *idx)
{
    int vif = STATION_IF;

    if ((idx != NULL) && (*idx < argc)) {
        if (strcmp(argv[*idx], "sta") == 0) {
            vif = STATION_IF;
            (*idx)++;
        } else if (strcmp(argv[*idx], "ap") == 0) {
            vif = SOFTAP_IF;
            (*idx)++;
        }
    }

    return vif;
}

void static_ip_get_defaults(static_ip_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }

    snprintf(cfg->ip, sizeof(cfg->ip), "192.168.1.100");
    snprintf(cfg->netmask, sizeof(cfg->netmask), "255.255.255.0");
    snprintf(cfg->gateway, sizeof(cfg->gateway), "192.168.1.1");
    snprintf(cfg->dns1, sizeof(cfg->dns1), "8.8.8.8");
    snprintf(cfg->dns2, sizeof(cfg->dns2), "8.8.4.4");
}

int static_ip_set(int vif, const static_ip_config_t *cfg)
{
    ip_info_t info;

    if (!static_ip_is_valid_vif(vif) || (cfg == NULL)) {
        return -1;
    }

    if (static_ip_parse_ip_info(cfg, &info) != 0) {
        return -1;
    }

    if (vif == STATION_IF) {
        if (static_ip_apply_sta(cfg, &info) != 0) {
            return -1;
        }
    } else {
        if (static_ip_apply_ap(cfg, &info) != 0) {
            return -1;
        }
    }

    static_ip_print_config("Current configuration:", cfg);
    return 0;
}

int static_ip_get(int vif, static_ip_config_t *cfg)
{
    ip_info_t info;
    static_ip_config_t current;
    const ip_addr_t *dns0;
    const ip_addr_t *dns1;

    if (!static_ip_is_valid_vif(vif)) {
        return -1;
    }

    if (!wifi_get_ip_info((wifi_interface_e)vif, &info)) {
        os_printf(LM_CMD, LL_ERR, STATIC_IP_TAG " Failed to get IP info\r\n");
        return -1;
    }

    memset(&current, 0, sizeof(current));
    static_ip_ntoa(&info.ip, current.ip, sizeof(current.ip));
    static_ip_ntoa(&info.netmask, current.netmask, sizeof(current.netmask));
    static_ip_ntoa(&info.gw, current.gateway, sizeof(current.gateway));

    dns0 = dns_getserver(0);
    dns1 = dns_getserver(1);
    static_ip_ntoa(dns0, current.dns1, sizeof(current.dns1));
    static_ip_ntoa(dns1, current.dns2, sizeof(current.dns2));

    static_ip_print_config("Current IP configuration:", &current);

    if (vif == STATION_IF) {
        dhcp_status_t dhcp_status = wifi_station_dhcpc_status(STATION_IF);
        os_printf(LM_CMD, LL_INFO, STATIC_IP_TAG "   Mode:    %s\r\n",
                  (dhcp_status == TCPIP_DHCP_STATIC) ? "Static" : "DHCP");
        os_printf(LM_CMD, LL_INFO, STATIC_IP_TAG "   DHCP:    %s\r\n",
                  (dhcp_status == TCPIP_DHCP_STARTED) ? "Running" : "Stopped");
    } else {
        dhcp_status_t dhcps_status = wifi_softap_dhcps_status(SOFTAP_IF);
        os_printf(LM_CMD, LL_INFO, STATIC_IP_TAG "   Mode:    %s\r\n",
                  g_static_ip_ctx.configured[SOFTAP_IF] ? "Static" : "Default");
        os_printf(LM_CMD, LL_INFO, STATIC_IP_TAG "   DHCPS:   %s\r\n",
                  (dhcps_status == TCPIP_DHCP_STARTED) ? "Running" : "Stopped");
    }

    if (cfg != NULL) {
        *cfg = current;
    }

    return 0;
}

int static_ip_restore_dhcp(int vif)
{
    if (!static_ip_is_valid_vif(vif)) {
        return -1;
    }

    if (vif == STATION_IF) {
        clear_sta_ipconfig();
        if (wifi_station_dhcpc_start(STATION_IF) != 0) {
            os_printf(LM_CMD, LL_ERR, STATIC_IP_TAG " Failed to restart STA DHCP client\r\n");
            return -1;
        }

        static_ip_status_reset(STATION_IF);
        os_printf(LM_CMD, LL_INFO, STATIC_IP_TAG " STA DHCP restored\r\n");
        return 0;
    }

    {
        ip_info_t info;

        memset(&info, 0, sizeof(info));
        ipaddr_aton(SYSTEM_NETWORK_SOFTAP_DEFAULT_IP, &info.ip);
        ipaddr_aton(SYSTEM_NETWORK_SOFTAP_DEFAULT_IPMASK, &info.netmask);
        ipaddr_aton(SYSTEM_NETWORK_SOFTAP_DEFAULT_IP, &info.gw);

        set_softap_ipconfig(&info);
        wifi_set_ip_info(SOFTAP_IF, &info);

        if (wifi_softap_dhcps_status(SOFTAP_IF) == TCPIP_DHCP_STARTED) {
            wifi_softap_dhcps_stop(SOFTAP_IF);
        }
        wifi_softap_dhcps_start(SOFTAP_IF);
    }

    static_ip_status_reset(SOFTAP_IF);
    os_printf(LM_CMD, LL_INFO, STATIC_IP_TAG " SoftAP IP restored to default DHCP-server profile\r\n");
    return 0;
}

int static_ip_get_status(int vif, static_ip_status_t *status)
{
    if (!static_ip_is_valid_vif(vif) || (status == NULL)) {
        return -1;
    }

    status->configured = g_static_ip_ctx.configured[vif];
    status->dhcp_stopped = g_static_ip_ctx.dhcp_stopped[vif];
    status->vif_index = vif;
    status->config = g_static_ip_ctx.config[vif];

    return 0;
}

static int cmd_static_ip_handler(int argc, char *argv[])
{
    int vif;
    int arg_idx;

    if (argc < 2) {
        return CMD_RET_USAGE;
    }

    if (strcmp(argv[1], "set") == 0) {
        static_ip_config_t cfg;

        static_ip_get_defaults(&cfg);
        arg_idx = 2;
        vif = static_ip_parse_vif(argc, argv, &arg_idx);

        while (arg_idx < argc) {
            if ((arg_idx + 1) >= argc) {
                return CMD_RET_USAGE;
            }

            if (strcmp(argv[arg_idx], "ip") == 0) {
                snprintf(cfg.ip, sizeof(cfg.ip), "%s", argv[arg_idx + 1]);
            } else if (strcmp(argv[arg_idx], "mask") == 0) {
                snprintf(cfg.netmask, sizeof(cfg.netmask), "%s", argv[arg_idx + 1]);
            } else if (strcmp(argv[arg_idx], "gw") == 0) {
                snprintf(cfg.gateway, sizeof(cfg.gateway), "%s", argv[arg_idx + 1]);
            } else if (strcmp(argv[arg_idx], "dns1") == 0) {
                snprintf(cfg.dns1, sizeof(cfg.dns1), "%s", argv[arg_idx + 1]);
            } else if (strcmp(argv[arg_idx], "dns2") == 0) {
                snprintf(cfg.dns2, sizeof(cfg.dns2), "%s", argv[arg_idx + 1]);
            } else {
                os_printf(LM_CMD, LL_ERR, STATIC_IP_TAG " Unknown parameter: %s\r\n", argv[arg_idx]);
                return CMD_RET_FAILURE;
            }
            arg_idx += 2;
        }

        return (static_ip_set(vif, &cfg) == 0) ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    if (strcmp(argv[1], "get") == 0) {
        arg_idx = 2;
        vif = static_ip_parse_vif(argc, argv, &arg_idx);
        return (static_ip_get(vif, NULL) == 0) ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    if (strcmp(argv[1], "dhcp") == 0) {
        arg_idx = 2;
        vif = static_ip_parse_vif(argc, argv, &arg_idx);
        return (static_ip_restore_dhcp(vif) == 0) ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    return CMD_RET_USAGE;
}

CLI_CMD(static_ip,
        cmd_static_ip_handler,
        "Configure static IP address on STA or SoftAP",
        "static_ip set [sta|ap] ip <ip> mask <mask> gw <gw> [dns1 <d>] [dns2 <d>]\n"
        "static_ip get [sta|ap]\n"
        "static_ip dhcp [sta|ap]");



