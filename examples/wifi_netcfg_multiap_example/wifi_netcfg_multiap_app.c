#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "smartconfig.h"
#include "system_event.h"
#include "system_wifi.h"
#include "oshal.h"
#include "wifi_netcfg_port.h"
#include "wifi_netcfg_adapter.h"
#include "wifi_netcfg_manager.h"
#include "wifi_netcfg_multiap_example.h"

#define WIFI_NETCFG_MULTIAP_TAG "WIFI_NETCFG_EXAMPLE"
#define WIFI_NETCFG_CMD_TAG "[sc_test]"
#define LOG_INFO WIFI_NETCFG_LOGI
#define LOG_ERROR WIFI_NETCFG_LOGE

static WifiNetcfgManager* g_wifi_mgr = NULL;
static WifiNetcfgAdapterContext g_wifi_adapter;
static bool g_wifi_example_ready = false;
static int g_wifi_startup_task_handle = 0;
static bool g_smartcfg_pending_credentials = false;
static bool g_smartcfg_session_active = false;
static smartconfig_status_t g_smartcfg_last_status = SC_STATUS_STOP;
static uint8_t g_smartcfg_channel = 0;
static char g_smartcfg_ssid[sizeof(((sc_result_t*)0)->ssid)] = {0};
static char g_smartcfg_password[sizeof(((sc_result_t*)0)->pwd)] = {0};

typedef enum {
    WIFI_NETCFG_STARTUP_ACTION_NONE = 0,
    WIFI_NETCFG_STARTUP_ACTION_AUTO_CONNECT,
    WIFI_NETCFG_STARTUP_ACTION_ENTER_CONFIG,
} WifiNetcfgStartupAction;

static WifiNetcfgStartupAction g_wifi_startup_action =
    WIFI_NETCFG_STARTUP_ACTION_NONE;

static const char* wifi_netcfg_multiap_smartcfg_status_to_string(smartconfig_status_t status)
{
    switch (status) {
        case SC_STATUS_CONTINUE:
            return "continue";
        case SC_STATUS_LOCK_CHANNEL:
            return "lock_channel";
        case SC_STATUS_GOT_SSID_PSWD:
            return "got_ssid_pswd";
        case SC_STATUS_GOT_IP:
            return "got_ip";
        case SC_STATUS_TIMEOUT:
            return "timeout";
        case SC_STATUS_STOP:
            return "stop";
        default:
            return "unknown";
    }
}

static void wifi_netcfg_multiap_smartcfg_callback(smartconfig_status_t status,
                                                  void* pdata)
{
    g_smartcfg_last_status = status;

    switch (status) {
        case SC_STATUS_LOCK_CHANNEL:
            if (pdata) {
                int channel = 0;
                memcpy(&channel, pdata, sizeof(channel));
                g_smartcfg_channel =
                    (channel > 0 && channel <= 13) ? (uint8_t)channel : 0;
                LOG_INFO(WIFI_NETCFG_MULTIAP_TAG,
                         "Smartconfig locked channel %d",
                         channel);
            }
            break;

        case SC_STATUS_GOT_SSID_PSWD:
            if (pdata) {
                const sc_result_t* result = (const sc_result_t*)pdata;
                size_t ssid_len = result->ssid_length;
                size_t pwd_len = result->pwd_length;

                if (ssid_len >= sizeof(g_smartcfg_ssid)) {
                    ssid_len = sizeof(g_smartcfg_ssid) - 1;
                }
                if (pwd_len >= sizeof(g_smartcfg_password)) {
                    pwd_len = sizeof(g_smartcfg_password) - 1;
                }

                memset(g_smartcfg_ssid, 0, sizeof(g_smartcfg_ssid));
                memset(g_smartcfg_password, 0, sizeof(g_smartcfg_password));
                memcpy(g_smartcfg_ssid, result->ssid, ssid_len);
                memcpy(g_smartcfg_password, result->pwd, pwd_len);
                g_smartcfg_pending_credentials = true;

                LOG_INFO(WIFI_NETCFG_MULTIAP_TAG,
                         "Smartconfig got credentials: ssid=%s",
                         g_smartcfg_ssid);
            }

            (void)smartconfig_stop();
            break;

        case SC_STATUS_STOP:
            LOG_INFO(WIFI_NETCFG_MULTIAP_TAG, "Smartconfig stopped");
            if (g_smartcfg_pending_credentials) {
                g_smartcfg_pending_credentials = false;
                (void)wifi_netcfg_multiap_example_start_config_mode();
                (void)wifi_netcfg_notify_ble_config_with_channel(g_wifi_mgr,
                                                                 g_smartcfg_ssid,
                                                                 g_smartcfg_password,
                                                                 g_smartcfg_channel);
            }
            g_smartcfg_session_active = false;
            break;

        case SC_STATUS_TIMEOUT:
            LOG_ERROR(WIFI_NETCFG_MULTIAP_TAG, "Smartconfig timeout");
            g_smartcfg_pending_credentials = false;
            g_smartcfg_session_active = false;
            break;

        case SC_STATUS_GOT_IP:
            LOG_INFO(WIFI_NETCFG_MULTIAP_TAG, "Smartconfig got IP notification");
            break;

        case SC_STATUS_CONTINUE:
        default:
            break;
    }
}

static int wifi_netcfg_multiap_parse_index(const char* arg, int* index)
{
    char* end = NULL;
    long value;

    if (!arg || !index || (*arg == '\0')) {
        return -1;
    }

    if (!isdigit((unsigned char)arg[0])) {
        return -1;
    }

    value = strtol(arg, &end, 10);
    if ((end == NULL) || (*end != '\0') || (value < 0) || (value > 127)) {
        return -1;
    }

    *index = (int)value;
    return 0;
}

static int wifi_netcfg_multiap_resolve_target(const char* token)
{
    int index = -1;

    if (!g_wifi_mgr || !token) {
        return -1;
    }

    if (wifi_netcfg_multiap_parse_index(token, &index) == 0) {
        return index;
    }

    return wifi_storage_find(&g_wifi_mgr->storage, token);
}

static void wifi_netcfg_multiap_on_state_changed(WifiNetcfgState old_state,
                                                 WifiNetcfgState new_state)
{
    LOG_INFO(WIFI_NETCFG_MULTIAP_TAG, "State: %d -> %d", old_state, new_state);
}

static void wifi_netcfg_multiap_on_connected(const char* ssid)
{
    LOG_INFO(WIFI_NETCFG_MULTIAP_TAG, "Connected to: %s", ssid ? ssid : "<unknown>");
}

static void wifi_netcfg_multiap_on_disconnected(void)
{
    LOG_INFO(WIFI_NETCFG_MULTIAP_TAG, "Disconnected, waiting for manager retry");
}

static void wifi_netcfg_multiap_on_scan_complete(uint8_t count)
{
    LOG_INFO(WIFI_NETCFG_MULTIAP_TAG, "Scan complete: %u APs", count);
    if (g_wifi_mgr) {
        wifi_netcfg_print_nv_and_scan(g_wifi_mgr);
    }
}

static void wifi_netcfg_multiap_on_config_success(const char* ssid)
{
    LOG_INFO(WIFI_NETCFG_MULTIAP_TAG, "Config success: %s", ssid ? ssid : "<unknown>");
}

static void wifi_netcfg_multiap_on_config_failed(const char* ssid)
{
    LOG_ERROR(WIFI_NETCFG_MULTIAP_TAG, "Config failed: %s", ssid ? ssid : "<unknown>");
}

static void wifi_netcfg_multiap_on_notice(void* user_data,
                                          WifiNetcfgNoticeType notice)
{
    (void)user_data;

    switch (notice) {
        case WIFI_NETCFG_NOTICE_BLE_START:
            LOG_INFO(WIFI_NETCFG_MULTIAP_TAG,
                     "Manager entered config mode, waiting for credential input");
            break;
        case WIFI_NETCFG_NOTICE_WIFI_SSID_ERR:
            LOG_ERROR(WIFI_NETCFG_MULTIAP_TAG, "WiFi SSID error");
            break;
        case WIFI_NETCFG_NOTICE_WIFI_PWD_ERR:
            LOG_ERROR(WIFI_NETCFG_MULTIAP_TAG, "WiFi password error");
            break;
        default:
            break;
    }
}

static void wifi_netcfg_multiap_before_connected(void* user_data)
{
    (void)user_data;
    wifi_netcfg_adapter_refresh_sntp_timezone(8.0f);
}

static bool wifi_netcfg_multiap_allow_disconnect_notify(void* user_data,
                                                        system_event_t* event,
                                                        bool is_lost_ip)
{
    (void)user_data;
    (void)event;

    if (!g_smartcfg_session_active) {
        return true;
    }

    LOG_INFO(WIFI_NETCFG_MULTIAP_TAG,
             "Ignore %s while Smartconfig bridge owns WiFi state",
             is_lost_ip ? "lost-ip" : "disconnect");
    return false;
}

static const WifiNetcfgAdapterHooks g_wifi_netcfg_multiap_hooks = {
    .before_connected = wifi_netcfg_multiap_before_connected,
    .allow_disconnect_notify = wifi_netcfg_multiap_allow_disconnect_notify,
    .resolve_scan_done_action = NULL,
    .after_scan_done = NULL,
};

static sys_err_t wifi_netcfg_multiap_event_handler(void* ctx, system_event_t* event)
{
    (void)ctx;
    return wifi_netcfg_adapter_handle_system_event(&g_wifi_adapter, event);
}

static void wifi_netcfg_multiap_startup_task(void* arg)
{
    uint32_t wait_ms = 0;
    WifiNetcfgStartupAction action;

    (void)arg;

    while (!wifi_is_ready()) {
        os_msleep(20);
        wait_ms += 20;
    }

    if (wait_ms > 0) {
        LOG_INFO(WIFI_NETCFG_MULTIAP_TAG,
                 "WiFi core ready after %lu ms, continue startup",
                 (unsigned long)wait_ms);
    }

    action = g_wifi_startup_action;
    g_wifi_startup_action = WIFI_NETCFG_STARTUP_ACTION_NONE;

    if (!g_wifi_mgr) {
        g_wifi_startup_task_handle = 0;
        os_task_delete(0);
        return;
    }

    if (action == WIFI_NETCFG_STARTUP_ACTION_AUTO_CONNECT) {
        LOG_INFO(WIFI_NETCFG_MULTIAP_TAG, "Found stored WiFi, auto-connect starts");
        (void)wifi_netcfg_start_auto_connect(g_wifi_mgr);
    } else if (action == WIFI_NETCFG_STARTUP_ACTION_ENTER_CONFIG) {
        LOG_INFO(WIFI_NETCFG_MULTIAP_TAG, "No stored WiFi, enter config mode");
        (void)wifi_netcfg_start_ble_config(g_wifi_mgr);
    }

    g_wifi_startup_task_handle = 0;
    os_task_delete(0);
}

int wifi_netcfg_multiap_example_start_auto_connect(void)
{
    if (!g_wifi_mgr) {
        return -1;
    }

    return wifi_netcfg_start_auto_connect(g_wifi_mgr);
}

int wifi_netcfg_multiap_example_start_config_mode(void)
{
    if (!g_wifi_mgr) {
        return -1;
    }

    return wifi_netcfg_start_ble_config(g_wifi_mgr);
}

int wifi_netcfg_multiap_example_submit_ble_config(const char* ssid,
                                                  const char* password)
{
    if (!g_wifi_mgr) {
        return -1;
    }

    return wifi_netcfg_notify_ble_config(g_wifi_mgr, ssid, password);
}

int wifi_netcfg_multiap_example_disconnect(void)
{
    return wifi_netcfg_port_sta_stop();
}

int wifi_netcfg_multiap_example_trigger_scan(void)
{
    if (!g_wifi_mgr) {
        return -1;
    }

    return wifi_netcfg_trigger_scan(g_wifi_mgr);
}

int wifi_netcfg_multiap_example_print_status(void)
{
    if (!g_wifi_mgr) {
        return -1;
    }

    wifi_netcfg_print_status(g_wifi_mgr);
    wifi_netcfg_print_nv_and_scan(g_wifi_mgr);
    return 0;
}

int wifi_netcfg_multiap_example_print_storage(void)
{
    uint8_t i;

    if (!g_wifi_mgr) {
        return -1;
    }

    os_printf(LM_CMD, LL_INFO,
              WIFI_NETCFG_CMD_TAG " Stored WiFi count: %u\r\n",
              g_wifi_mgr->storage.count);

    for (i = 0; i < g_wifi_mgr->storage.count; i++) {
        const WifiCredential* cred = &g_wifi_mgr->storage.credentials[i];
        os_printf(LM_CMD, LL_INFO,
                  WIFI_NETCFG_CMD_TAG " [%u] ssid=%s channel=%u current=%s\r\n",
                  i,
                  cred->ssid,
                  cred->channel,
                  (g_wifi_mgr->storage.current_index == (int8_t)i) ? "yes" : "no");
    }

    return 0;
}

int wifi_netcfg_multiap_example_add(const char* ssid, const char* password)
{
    int8_t existing_idx;
    int8_t idx;

    if (!g_wifi_mgr || !ssid || !password) {
        return -1;
    }

    existing_idx = wifi_storage_find(&g_wifi_mgr->storage, ssid);
    idx = wifi_storage_add(&g_wifi_mgr->storage, ssid, password);
    if (idx < 0) {
        os_printf(LM_CMD, LL_ERR,
                  WIFI_NETCFG_CMD_TAG " add failed for %s\r\n",
                  ssid);
        return -1;
    }

    if (existing_idx < 0) {
        g_wifi_mgr->storage.credentials[idx].channel = 0;
    }

    if (wifi_storage_save(&g_wifi_mgr->storage) != 0) {
        os_printf(LM_CMD, LL_ERR,
                  WIFI_NETCFG_CMD_TAG " save failed after add\r\n");
        return -1;
    }

    os_printf(LM_CMD, LL_INFO,
              WIFI_NETCFG_CMD_TAG " saved %s at index %d\r\n",
              ssid,
              idx);
    return 0;
}

int wifi_netcfg_multiap_example_delete(const char* token)
{
    int index;

    if (!g_wifi_mgr || !token) {
        return -1;
    }

    index = wifi_netcfg_multiap_resolve_target(token);
    if ((index < 0) || ((uint8_t)index >= g_wifi_mgr->storage.count)) {
        os_printf(LM_CMD, LL_ERR,
                  WIFI_NETCFG_CMD_TAG " target not found: %s\r\n",
                  token);
        return -1;
    }

    if (wifi_netcfg_remove_wifi(g_wifi_mgr, (int8_t)index) != 0) {
        os_printf(LM_CMD, LL_ERR,
                  WIFI_NETCFG_CMD_TAG " delete failed for %s\r\n",
                  token);
        return -1;
    }

    os_printf(LM_CMD, LL_INFO,
              WIFI_NETCFG_CMD_TAG " deleted entry %d\r\n",
              index);
    return 0;
}

int wifi_netcfg_multiap_example_clear(void)
{
    if (!g_wifi_mgr) {
        return -1;
    }

    (void)wifi_netcfg_multiap_example_disconnect();
    if (wifi_netcfg_clear_all(g_wifi_mgr) != 0) {
        os_printf(LM_CMD, LL_ERR,
                  WIFI_NETCFG_CMD_TAG " clear failed\r\n");
        return -1;
    }

    (void)wifi_netcfg_multiap_example_start_config_mode();
    os_printf(LM_CMD, LL_INFO,
              WIFI_NETCFG_CMD_TAG " all stored WiFi cleared\r\n");
    return 0;
}

int wifi_netcfg_multiap_example_connect(const char* token)
{
    int index;

    if (!g_wifi_mgr || !token) {
        return -1;
    }

    index = wifi_netcfg_multiap_resolve_target(token);
    if ((index < 0) || ((uint8_t)index >= g_wifi_mgr->storage.count)) {
        os_printf(LM_CMD, LL_ERR,
                  WIFI_NETCFG_CMD_TAG " target not found: %s\r\n",
                  token);
        return -1;
    }

    if (wifi_netcfg_connect_wifi(g_wifi_mgr, (int8_t)index) != 0) {
        os_printf(LM_CMD, LL_ERR,
                  WIFI_NETCFG_CMD_TAG " connect failed for %s\r\n",
                  token);
        return -1;
    }

    os_printf(LM_CMD, LL_INFO,
              WIFI_NETCFG_CMD_TAG " connecting to [%d] %s\r\n",
              index,
              g_wifi_mgr->storage.credentials[index].ssid);
    return 0;
}

int wifi_netcfg_multiap_example_smartcfg_start(void)
{
    if (!g_wifi_mgr) {
        return -1;
    }

    if (smartconfig_is_already_started()) {
        LOG_INFO(WIFI_NETCFG_MULTIAP_TAG, "Smartconfig already running");
        return 0;
    }

    g_smartcfg_pending_credentials = false;
    g_smartcfg_channel = 0;
    memset(g_smartcfg_ssid, 0, sizeof(g_smartcfg_ssid));
    memset(g_smartcfg_password, 0, sizeof(g_smartcfg_password));
    g_smartcfg_last_status = SC_STATUS_CONTINUE;
    g_smartcfg_session_active = true;

    (void)wifi_netcfg_multiap_example_start_config_mode();
    (void)smartconfig_set_timeout(60);

    if (smartconfig_start(wifi_netcfg_multiap_smartcfg_callback) != 0) {
        g_smartcfg_session_active = false;
        LOG_ERROR(WIFI_NETCFG_MULTIAP_TAG, "Smartconfig start failed");
        return -1;
    }

    LOG_INFO(WIFI_NETCFG_MULTIAP_TAG,
             "Smartconfig started, waiting for real provisioning packets");
    return 0;
}

int wifi_netcfg_multiap_example_smartcfg_stop(void)
{
    if (!smartconfig_is_already_started()) {
        LOG_INFO(WIFI_NETCFG_MULTIAP_TAG, "Smartconfig already stopped");
        g_smartcfg_last_status = SC_STATUS_STOP;
        g_smartcfg_pending_credentials = false;
        g_smartcfg_channel = 0;
        return 0;
    }

    if (smartconfig_stop() != 0) {
        LOG_ERROR(WIFI_NETCFG_MULTIAP_TAG, "Smartconfig stop failed");
        return -1;
    }

    g_smartcfg_last_status = SC_STATUS_STOP;
    g_smartcfg_pending_credentials = false;
    g_smartcfg_channel = 0;
    LOG_INFO(WIFI_NETCFG_MULTIAP_TAG, "Smartconfig stop requested");
    return 0;
}

int wifi_netcfg_multiap_example_smartcfg_print_status(void)
{
    os_printf(LM_CMD, LL_INFO,
              "[smartcfg] running=%s last=%s pending_credentials=%s channel=%u ssid=%s\r\n",
              smartconfig_is_already_started() ? "yes" : "no",
              wifi_netcfg_multiap_smartcfg_status_to_string(g_smartcfg_last_status),
              g_smartcfg_pending_credentials ? "yes" : "no",
              g_smartcfg_channel,
              g_smartcfg_ssid[0] ? g_smartcfg_ssid : "<none>");
    return 0;
}

void wifi_netcfg_multiap_example_print_help(void)
{
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] sc_test list\r\n");
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] sc_test status\r\n");
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] sc_test add <ssid> <password>\r\n");
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] sc_test del <index|ssid>\r\n");
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] sc_test clear\r\n");
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] sc_test start\r\n");
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] sc_test connect <index|ssid>\r\n");
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] sc_test submit <ssid> <password>\r\n");
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] sc_test scan\r\n");
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] sc_test disc\r\n");
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] smartcfg start\r\n");
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] smartcfg stop\r\n");
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] smartcfg status\r\n");
}

void app_example_main(void)
{
    int ret;
    WifiNetcfgConfig config = {
        .scan_interval_1s = 2000,
        .scan_interval_10s = 10000,
        .scan_interval_30s = 30000,
        .connect_timeout_ms = 150000,
        .max_connect_retries = 3,
        .max_scan_retries_normal = 10,
        .max_scan_retries_extend = 15,
        .enable_sleep_scanning = true,
    };

    if (g_wifi_example_ready) {
        return;
    }

    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] =========================================\r\n");
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] WiFi netcfg multi-AP example started\r\n");
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] WiFi ownership is handled by wifi_netcfg\r\n");

    g_wifi_mgr = wifi_netcfg_manager_create(&config);
    if (!g_wifi_mgr) {
        os_printf(LM_APP, LL_ERR,
                  "[WIFI_NETCFG_EXAMPLE] Failed to create manager\r\n");
        return;
    }

    wifi_netcfg_adapter_init_context(&g_wifi_adapter,
                                     g_wifi_mgr,
                                     WIFI_NETCFG_MULTIAP_TAG,
                                     &g_wifi_netcfg_multiap_hooks,
                                     NULL);

    wifi_netcfg_register_callbacks(g_wifi_mgr,
                                   wifi_netcfg_multiap_on_state_changed,
                                   wifi_netcfg_multiap_on_connected,
                                   wifi_netcfg_multiap_on_disconnected,
                                   wifi_netcfg_multiap_on_scan_complete,
                                   wifi_netcfg_multiap_on_config_success,
                                   wifi_netcfg_multiap_on_config_failed);
    wifi_netcfg_register_notice_callback(g_wifi_mgr,
                                         wifi_netcfg_multiap_on_notice,
                                         NULL);

    if (wifi_netcfg_adapter_start_update_timer(&g_wifi_adapter,
                                               "wifi_update",
                                               100) != 0) {
        os_printf(LM_APP, LL_ERR,
                  "[WIFI_NETCFG_EXAMPLE] Failed to start update timer\r\n");
    }

    sys_event_loop_set_cb(wifi_netcfg_multiap_event_handler, NULL);

    ret = wifi_netcfg_manager_init(g_wifi_mgr);
    g_wifi_example_ready = true;

    if (ret == 0) {
        g_wifi_startup_action = WIFI_NETCFG_STARTUP_ACTION_AUTO_CONNECT;
    } else {
        g_wifi_startup_action = WIFI_NETCFG_STARTUP_ACTION_ENTER_CONFIG;
    }

    g_wifi_startup_task_handle = os_task_create("wifi_netcfg_boot",
                                                SYSTEM_EVENT_LOOP_PRIORITY - 1,
                                                2048,
                                                wifi_netcfg_multiap_startup_task,
                                                NULL);
    if (g_wifi_startup_task_handle < 0) {
        os_printf(LM_APP, LL_ERR,
                  "[WIFI_NETCFG_EXAMPLE] Failed to create startup task\r\n");
        if (g_wifi_startup_action == WIFI_NETCFG_STARTUP_ACTION_AUTO_CONNECT) {
            os_printf(LM_APP, LL_INFO,
                      "[WIFI_NETCFG_EXAMPLE] Found stored WiFi, auto-connect starts\r\n");
            (void)wifi_netcfg_start_auto_connect(g_wifi_mgr);
        } else {
            os_printf(LM_APP, LL_INFO,
                      "[WIFI_NETCFG_EXAMPLE] No stored WiFi, enter config mode\r\n");
            (void)wifi_netcfg_start_ble_config(g_wifi_mgr);
        }
    }

    wifi_netcfg_multiap_example_print_help();
    os_printf(LM_APP, LL_INFO,
              "[WIFI_NETCFG_EXAMPLE] =========================================\r\n");
}
