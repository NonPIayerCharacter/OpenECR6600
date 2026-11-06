#include "wifi_netcfg_adapter.h"
#include "wifi_netcfg_port.h"

#include <string.h>

#define LOG_INFO WIFI_NETCFG_LOGI
#define LOG_ERROR WIFI_NETCFG_LOGE
#define LOG_DEBUG WIFI_NETCFG_LOGD

static void wifi_netcfg_adapter_reset_disconnect_state(WifiNetcfgAdapterContext* ctx)
{
    if (!ctx) {
        return;
    }

    ctx->disconnect_event_pending = false;
    ctx->auth_fail_ignore_disconnect_events = 0;
}

static bool wifi_netcfg_adapter_should_consume_scan_done(const WifiNetcfgAdapterContext* ctx)
{
    if (!ctx || !ctx->mgr) {
        return false;
    }

    switch (ctx->mgr->state) {
        case WIFI_STATE_SCANNING:
        case WIFI_STATE_SLEEP_SCANNING:
        case WIFI_STATE_DISCONNECTED:
            return true;

        default:
            return false;
    }
}

static WifiNetcfgAdapterScanDoneAction wifi_netcfg_adapter_resolve_scan_done_action(
    const WifiNetcfgAdapterContext* ctx,
    system_event_t* event)
{
    if (!ctx || !ctx->hooks || !ctx->hooks->resolve_scan_done_action) {
        return WIFI_NETCFG_ADAPTER_SCAN_DONE_CONTINUE;
    }

    return ctx->hooks->resolve_scan_done_action(ctx->user_data, event);
}

__attribute__((weak)) void wifi_netcfg_adapter_init_context(WifiNetcfgAdapterContext* ctx,
                                                            WifiNetcfgManager* mgr,
                                                            const char* log_tag,
                                                            const WifiNetcfgAdapterHooks* hooks,
                                                            void* user_data)
{
    if (!ctx) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->mgr = mgr;
    ctx->log_tag = log_tag ? log_tag : "WIFI_NETCFG_ADAPTER";
    ctx->hooks = hooks;
    ctx->user_data = user_data;
}

__attribute__((weak)) void wifi_netcfg_adapter_bind_manager(WifiNetcfgAdapterContext* ctx,
                                                            WifiNetcfgManager* mgr)
{
    if (!ctx) {
        return;
    }

    ctx->mgr = mgr;
}

__attribute__((weak)) void wifi_netcfg_adapter_reset_runtime(WifiNetcfgAdapterContext* ctx)
{
    wifi_netcfg_adapter_reset_disconnect_state(ctx);
}

__attribute__((weak)) int wifi_netcfg_adapter_start_update_timer(WifiNetcfgAdapterContext* ctx,
                                                                 const char* timer_name,
                                                                 uint32_t period_ms)
{
    const char* name = timer_name ? timer_name : "wifi_update";
    const char* log_tag = "WIFI_NETCFG_ADAPTER";

    if (!ctx) {
        return -1;
    }

    if (ctx->log_tag) {
        log_tag = ctx->log_tag;
    }

    if (!ctx->update_timer) {
        ctx->update_timer = os_timer_create(name,
                                            period_ms,
                                            1,
                                            wifi_netcfg_adapter_timer_callback,
                                            ctx);
        if (!ctx->update_timer) {
            LOG_ERROR(log_tag, "Create update timer failed");
            return -1;
        }
    } else {
        os_timer_changeperiod(ctx->update_timer, period_ms);
    }

    return os_timer_start(ctx->update_timer);
}

__attribute__((weak)) void wifi_netcfg_adapter_stop_update_timer(WifiNetcfgAdapterContext* ctx,
                                                                 bool destroy)
{
    if (!ctx || !ctx->update_timer) {
        return;
    }

    os_timer_stop(ctx->update_timer);

    if (destroy) {
        os_timer_delete(ctx->update_timer);
        ctx->update_timer = NULL;
    }
}

__attribute__((weak)) void wifi_netcfg_adapter_refresh_sntp_timezone(float timezone)
{
    wifi_netcfg_port_refresh_sntp_timezone(timezone);
}

__attribute__((weak)) void wifi_netcfg_adapter_timer_callback(os_timer_handle_t timer)
{
    WifiNetcfgAdapterContext* ctx =
        (WifiNetcfgAdapterContext*)os_timer_get_arg(timer);

    if (!ctx || !ctx->mgr) {
        return;
    }

    wifi_netcfg_update(ctx->mgr);
}

__attribute__((weak)) void wifi_netcfg_adapter_handle_scan_done(WifiNetcfgAdapterContext* ctx,
                                                                system_event_t* event)
{
    uint16_t event_count = 0;
    WifiNetcfgAdapterScanDoneAction action;

    if (!ctx || !ctx->mgr) {
        return;
    }

    action = wifi_netcfg_adapter_resolve_scan_done_action(ctx, event);
    if (action == WIFI_NETCFG_ADAPTER_SCAN_DONE_CONSUMED ||
        action == WIFI_NETCFG_ADAPTER_SCAN_DONE_SKIP_MANAGER) {
        return;
    }

    if (!wifi_netcfg_adapter_should_consume_scan_done(ctx)) {
        if (ctx->hooks && ctx->hooks->after_scan_done) {
            ctx->hooks->after_scan_done(ctx->user_data, event);
        }
        return;
    }

    if (event) {
        event_count = event->event_info.scan_done.number;
    }

    uint16_t ap_count = wifi_netcfg_port_scan_get_count();
    if (ap_count == 0) {
        if (event_count > 0) {
            LOG_INFO(ctx->log_tag,
                     "Scan event reports %d APs, but cached result count is 0",
                     event_count);
            WifiScanResult empty[1];
            wifi_netcfg_notify_scan_complete(ctx->mgr, empty, 0);
            if (ctx->hooks && ctx->hooks->after_scan_done) {
                ctx->hooks->after_scan_done(ctx->user_data, event);
            }
            return;
        }

        if (event) {
            LOG_INFO(ctx->log_tag,
                     "Scan done event received, but cached result count is 0");
            WifiScanResult empty[1];
            wifi_netcfg_notify_scan_complete(ctx->mgr, empty, 0);
            if (ctx->hooks && ctx->hooks->after_scan_done) {
                ctx->hooks->after_scan_done(ctx->user_data, event);
            }
            return;
        }

        LOG_INFO(ctx->log_tag, "No AP found");
        WifiScanResult empty[1];
        wifi_netcfg_notify_scan_complete(ctx->mgr, empty, 0);
        if (ctx->hooks && ctx->hooks->after_scan_done) {
            ctx->hooks->after_scan_done(ctx->user_data, event);
        }
        return;
    }

    WifiScanResult* results = (WifiScanResult*)os_zalloc(ap_count * sizeof(WifiScanResult));
    if (!results) {
        LOG_ERROR(ctx->log_tag, "Alloc scan result buffer failed");
        return;
    }

    for (uint16_t i = 0; i < ap_count; i++) {
        wifi_info_t info = {0};
        wifi_netcfg_port_scan_get_result(i, &info);
        strlcpy(results[i].ssid, (const char*)info.ssid, WIFI_SSID_MAX_LEN);
        results[i].rssi = info.rssi;
        results[i].channel = info.channel;
        results[i].auth_mode = info.auth;
    }

    LOG_INFO(ctx->log_tag, "Scan complete: %d APs", ap_count);
    wifi_netcfg_notify_scan_complete(ctx->mgr, results, ap_count);
    os_free(results);

    if (ctx->hooks && ctx->hooks->after_scan_done) {
        ctx->hooks->after_scan_done(ctx->user_data, event);
    }
}

static sys_err_t wifi_netcfg_adapter_handle_disconnect_like(WifiNetcfgAdapterContext* ctx,
                                                            system_event_t* event,
                                                            bool is_lost_ip)
{
    if (ctx->auth_fail_ignore_disconnect_events > 0) {
        ctx->auth_fail_ignore_disconnect_events--;
        LOG_INFO(ctx->log_tag,
                 "[WIFI_AUTH_FAIL] Ignore follow-up %s after 4-way failure, remain=%d",
                 is_lost_ip ? "lost-ip" : "disconnect",
                 ctx->auth_fail_ignore_disconnect_events);
        return 0;
    }

    if (ctx->disconnect_event_pending) {
        LOG_INFO(ctx->log_tag,
                 "[WIFI_DISCONNECT] Duplicate %s event ignored",
                 is_lost_ip ? "lost-ip" : "disconnect");
        return 0;
    }

    ctx->disconnect_event_pending = true;

    if (ctx->hooks && ctx->hooks->allow_disconnect_notify) {
        if (!ctx->hooks->allow_disconnect_notify(ctx->user_data, event, is_lost_ip)) {
            return 0;
        }
    }

    wifi_netcfg_notify_disconnected(ctx->mgr);
    return 0;
}

__attribute__((weak)) sys_err_t wifi_netcfg_adapter_handle_system_event(WifiNetcfgAdapterContext* ctx,
                                                                        system_event_t* event)
{
    if (!ctx || !ctx->mgr || !event) {
        return 0;
    }

    switch (event->event_id) {
        case SYSTEM_EVENT_STA_CONNECTED:
            wifi_netcfg_adapter_reset_disconnect_state(ctx);
            wifi_netcfg_notify_link_connected(ctx->mgr);
            break;

        case SYSTEM_EVENT_STA_GOT_IP:
            wifi_netcfg_adapter_reset_disconnect_state(ctx);
            if (ctx->hooks && ctx->hooks->before_connected) {
                ctx->hooks->before_connected(ctx->user_data);
            }
            wifi_netcfg_notify_connected(ctx->mgr);
            break;

        case SYSTEM_EVENT_STA_4WAY_HS_FAIL:
            LOG_ERROR(ctx->log_tag,
                      "[WIFI_AUTH_FAIL] 4-Way handshake failed, trigger fast recovery");
            ctx->auth_fail_ignore_disconnect_events = 2;
            ctx->disconnect_event_pending = false;
            wifi_netcfg_handle_auth_failure(ctx->mgr);
            break;

        case SYSTEM_EVENT_STA_DISCONNECTED:
            return wifi_netcfg_adapter_handle_disconnect_like(ctx, event, false);

        case SYSTEM_EVENT_STA_LOST_IP:
            return wifi_netcfg_adapter_handle_disconnect_like(ctx, event, true);

        case SYSTEM_EVENT_SCAN_DONE:
            wifi_netcfg_adapter_handle_scan_done(ctx, event);
            break;

        default:
            break;
    }

    return 0;
}


