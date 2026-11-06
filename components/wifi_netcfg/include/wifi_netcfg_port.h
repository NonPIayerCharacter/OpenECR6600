#ifndef WIFI_NETCFG_PORT_H
#define WIFI_NETCFG_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "oshal.h"
#include "system_event.h"
#include "system_network.h"
#include "system_wifi.h"
#include "system_config.h"
#include "hal_sntp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_NETCFG_DEFAULT_TAG "WIFI_NETCFG"

#define WIFI_NETCFG_LOGI(tag, fmt, ...) \
    do { \
        const char* wifi_netcfg_tag__ = ((tag) != NULL) ? (tag) : WIFI_NETCFG_DEFAULT_TAG; \
        os_printf(LM_APP, LL_INFO, "[%s]" fmt "\n", wifi_netcfg_tag__, ##__VA_ARGS__); \
    } while (0)

#define WIFI_NETCFG_LOGE(tag, fmt, ...) \
    do { \
        const char* wifi_netcfg_tag__ = ((tag) != NULL) ? (tag) : WIFI_NETCFG_DEFAULT_TAG; \
        os_printf(LM_APP, LL_ERR, "[%s]" fmt "\n", wifi_netcfg_tag__, ##__VA_ARGS__); \
    } while (0)

#define WIFI_NETCFG_LOGD(tag, fmt, ...) \
    do { \
        const char* wifi_netcfg_tag__ = ((tag) != NULL) ? (tag) : WIFI_NETCFG_DEFAULT_TAG; \
        os_printf(LM_APP, LL_DBG, "[%s]" fmt "\n", wifi_netcfg_tag__, ##__VA_ARGS__); \
    } while (0)

static inline int wifi_netcfg_port_sta_stop(void)
{
    return wifi_stop_station();
}

static inline sys_err_t wifi_netcfg_port_scan_start(bool block,
                                                    const wifi_scan_config_t* config)
{
    return wifi_scan_start(block, config);
}

static inline int wifi_netcfg_port_scan_get_count(void)
{
    return wpa_get_scan_num();
}

static inline int wifi_netcfg_port_scan_get_result(unsigned int index,
                                                   wifi_info_t* info)
{
    return wifi_get_scan_result(index, info);
}

static inline bool wifi_netcfg_port_get_sta_ip_info(struct ip_info* info)
{
    return wifi_get_ip_info(STATION_IF, info);
}

static inline wifi_status_e wifi_netcfg_port_get_sta_status(void)
{
    return wifi_get_status(STATION_IF);
}

static inline void wifi_netcfg_port_refresh_sntp_timezone(float timezone)
{
    hal_sntp_timezone_set(timezone, HAL_SNTP_SYNC_NTP);
}

#ifdef __cplusplus
}
#endif

#endif /* WIFI_NETCFG_PORT_H */
