#ifndef WIFI_NETCFG_ADAPTER_H
#define WIFI_NETCFG_ADAPTER_H

#include "wifi_netcfg_manager.h"
#include "system_event.h"
#include "oshal.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*WifiNetcfgAdapterBeforeConnectedHook)(void* user_data);
typedef bool (*WifiNetcfgAdapterDisconnectHook)(void* user_data,
                                                system_event_t* event,
                                                bool is_lost_ip);
typedef enum {
    WIFI_NETCFG_ADAPTER_SCAN_DONE_CONTINUE = 0,
    WIFI_NETCFG_ADAPTER_SCAN_DONE_SKIP_MANAGER,
    WIFI_NETCFG_ADAPTER_SCAN_DONE_CONSUMED,
} WifiNetcfgAdapterScanDoneAction;
typedef WifiNetcfgAdapterScanDoneAction (*WifiNetcfgAdapterScanDoneHook)(void* user_data,
                                                                         system_event_t* event);
typedef void (*WifiNetcfgAdapterAfterScanDoneHook)(void* user_data,
                                                   system_event_t* event);

typedef struct {
    WifiNetcfgAdapterBeforeConnectedHook before_connected;
    WifiNetcfgAdapterDisconnectHook allow_disconnect_notify;
    WifiNetcfgAdapterScanDoneHook resolve_scan_done_action;
    WifiNetcfgAdapterAfterScanDoneHook after_scan_done;
} WifiNetcfgAdapterHooks;

typedef struct {
    WifiNetcfgManager* mgr;
    const char* log_tag;
    const WifiNetcfgAdapterHooks* hooks;
    void* user_data;
    os_timer_handle_t update_timer;
    bool disconnect_event_pending;
    uint8_t auth_fail_ignore_disconnect_events;
} WifiNetcfgAdapterContext;

void wifi_netcfg_adapter_init_context(WifiNetcfgAdapterContext* ctx,
                                      WifiNetcfgManager* mgr,
                                      const char* log_tag,
                                      const WifiNetcfgAdapterHooks* hooks,
                                      void* user_data);
void wifi_netcfg_adapter_bind_manager(WifiNetcfgAdapterContext* ctx,
                                      WifiNetcfgManager* mgr);
void wifi_netcfg_adapter_reset_runtime(WifiNetcfgAdapterContext* ctx);
int wifi_netcfg_adapter_start_update_timer(WifiNetcfgAdapterContext* ctx,
                                           const char* timer_name,
                                           uint32_t period_ms);
void wifi_netcfg_adapter_stop_update_timer(WifiNetcfgAdapterContext* ctx,
                                           bool destroy);
void wifi_netcfg_adapter_refresh_sntp_timezone(float timezone);
void wifi_netcfg_adapter_timer_callback(os_timer_handle_t timer);
void wifi_netcfg_adapter_handle_scan_done(WifiNetcfgAdapterContext* ctx,
                                          system_event_t* event);
sys_err_t wifi_netcfg_adapter_handle_system_event(WifiNetcfgAdapterContext* ctx,
                                                  system_event_t* event);

#endif /* WIFI_NETCFG_ADAPTER_H */



