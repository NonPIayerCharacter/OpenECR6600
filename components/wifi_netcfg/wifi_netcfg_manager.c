#include "wifi_netcfg_manager.h"
#include "wifi_netcfg_port.h"

#include <string.h>

#define WIFI_NETCFG_MANAGER_TAG "WIFI_NETCFG_MANAGER"
#define SM_LOGI(format, ...) \
    WIFI_NETCFG_LOGI(WIFI_NETCFG_MANAGER_TAG, "[I][%s:%d]" format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define SM_LOGE(format, ...) \
    WIFI_NETCFG_LOGE(WIFI_NETCFG_MANAGER_TAG, "[E][%s:%d]" format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define SM_LOGD(format, ...) \
    WIFI_NETCFG_LOGD(WIFI_NETCFG_MANAGER_TAG, "[D][%s:%d]" format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO WIFI_NETCFG_LOGI
#define LOG_ERROR WIFI_NETCFG_LOGE
#define LOG_DEBUG WIFI_NETCFG_LOGD

/* ========================================
 *  WiFi NV存储相关
 * ======================================== */

#define WIFI_STORAGE_NV_BASE_KEY   CUSTOMER_NV_FLAG "WIFI_CFG_"  /* WiFi存储基础键值字符串 */
#define WIFI_STORAGE_NV_INDEX_KEY  CUSTOMER_NV_FLAG "WIFI_INDEX" /* WiFi索引信息键值字符串 */

/* WiFi索引信息结构(用于管理5个WiFi槽位) - 只存储持久化信息 */
typedef struct {
    uint8_t count;              /* 当前存储的WiFi数量(0-5) */
    int8_t current_index;       /* 当前连接的WiFi索引(-1表示�? */
    uint8_t next_insert_index;  /* 下一个循环插入位�?0-4) */
} WifiStorageIndex;

static void wifi_scan_smart_limit(WifiNetcfgManager* mgr);
static int wifi_netcfg_handle_start_auto_connect_event(WifiNetcfgManager* mgr);
static int wifi_netcfg_handle_start_ble_config_event(WifiNetcfgManager* mgr);
static int wifi_netcfg_handle_submit_ble_config_event(WifiNetcfgManager* mgr,
                                                      const char* ssid,
                                                      const char* password,
                                                      uint8_t channel);
static int wifi_netcfg_handle_link_connected_event(WifiNetcfgManager* mgr);
static int wifi_netcfg_handle_got_ip_event(WifiNetcfgManager* mgr);
static int wifi_netcfg_handle_connect_failed_event(WifiNetcfgManager* mgr);
static int wifi_netcfg_handle_disconnected_event(WifiNetcfgManager* mgr);
static int wifi_netcfg_handle_auth_failure_event(WifiNetcfgManager* mgr);
static int wifi_netcfg_handle_scan_complete_event(WifiNetcfgManager* mgr,
                                                  WifiScanResult* results,
                                                  uint8_t count);
static void wifi_netcfg_process_timer_tick(WifiNetcfgManager* mgr, uint32_t now);

#define WIFI_AUTO_CONNECT_NOTICE_TIMEOUT_MS 60000U

static void wifi_netcfg_emit_notice(WifiNetcfgManager* mgr,
                                    WifiNetcfgNoticeType notice)
{
    if (!mgr || !mgr->on_notice || notice == WIFI_NETCFG_NOTICE_NONE) {
        return;
    }

    mgr->on_notice(mgr->callback_user_data, notice);
}

/* 获取指定索引的NV键值字符串 */
static void wifi_get_nv_key(uint8_t index, char* key_buf, size_t buf_size)
{
    snprintf(key_buf, buf_size, "%s%d", WIFI_STORAGE_NV_BASE_KEY, index);
    /* 生成: "wifi_cfg_0", "wifi_cfg_1", "wifi_cfg_2", "wifi_cfg_3", "wifi_cfg_4" */
}

/* ========================================
 *  状态转换辅助函�? * ======================================== */

static void wifi_netcfg_change_state(WifiNetcfgManager* mgr, WifiNetcfgState new_state)
{
    if (!mgr || mgr->state == new_state) {
        return;
    }

    WifiNetcfgState old_state = mgr->state;
    mgr->previous_state = old_state;
    mgr->state = new_state;
    mgr->state_enter_time = os_time_get();

    SM_LOGI("[WiFi] State: %d -> %d\n", old_state, new_state);

    /* 调用回调 */
    if (mgr->on_state_changed) {
        mgr->on_state_changed(old_state, new_state);
    }
}

static void wifi_netcfg_reset_connect_progress(WifiNetcfgManager* mgr)
{
    if (!mgr) {
        return;
    }

    mgr->is_connected = false;
    mgr->has_ip = false;
    mgr->link_connected = false;
    mgr->connect_phase = WIFI_CONNECT_PHASE_NONE;
    mgr->link_connected_time = 0;
}

static void wifi_netcfg_start_connect_attempt(WifiNetcfgManager* mgr,
                                              int8_t target_index,
                                              wifi_config_u* wifi_config,
                                              bool stop_first,
                                              uint32_t now)
{
    if (!mgr || !wifi_config) {
        return;
    }

    mgr->storage.current_index = target_index;
    mgr->pending_wifi_index = -1;
    wifi_netcfg_reset_connect_progress(mgr);
    mgr->connect_phase = WIFI_CONNECT_PHASE_L2_CONNECTING;

    wifi_netcfg_change_state(mgr, WIFI_STATE_CONNECTING);
    if (stop_first) {
        wifi_netcfg_port_sta_stop();
    }
    wifi_start_station(wifi_config);
    mgr->connect_start_time = now;
}

static void wifi_netcfg_handle_ble_connect_timeout(WifiNetcfgManager* mgr,
                                                   const char* source)
{
    const char* phase_desc;

    if (!mgr) {
        return;
    }

    phase_desc = mgr->link_connected ? "waiting for IP" : "connecting to AP";
    SM_LOGI("[WiFi Manager] %s timeout (BLE config mode, %s)\n",
            source, phase_desc);
    if (mgr->on_config_timeout) {
        char ssid[WIFI_SSID_MAX_LEN];

        strlcpy(ssid, mgr->pending_ssid, sizeof(ssid));
        mgr->new_wifi_pending = false;
        mgr->ble_config_pending = false;
        mgr->timeout_event_posted = true;
        mgr->auto_connect_start_time = 0;
        mgr->connect_start_time = 0;
        mgr->storage.current_index = -1;
        mgr->pending_wifi_index = -1;
        memset(&mgr->retry_ctx, 0, sizeof(mgr->retry_ctx));
        memset(mgr->pending_ssid, 0, sizeof(mgr->pending_ssid));
        memset(mgr->pending_password, 0, sizeof(mgr->pending_password));
        mgr->pending_channel = 0;
        wifi_netcfg_reset_connect_progress(mgr);
        wifi_netcfg_change_state(mgr, WIFI_STATE_BLE_CONFIGURING);
        mgr->on_config_timeout(ssid);
        return;
    }
    {
        WifiNetcfgEvent event = {
            .type = WIFI_NETCFG_EVENT_CONNECT_FAILED,
        };
        wifi_netcfg_dispatch_event(mgr, &event);
    }
}

/* ========================================
 *  WiFi存储管理实现
 * ======================================== */

/* 加载索引信息 */
static int wifi_storage_load_index(WifiStorageIndex* index)
{
    if (!index) {
        return -1;
    }

    memset(index, 0, sizeof(WifiStorageIndex));
    index->current_index = -1;

#if defined(CONFIG_NV)
    int ret = hal_system_get_config(WIFI_STORAGE_NV_INDEX_KEY,
                                    index,
                                    sizeof(WifiStorageIndex));
    if (ret > 0) {
        SM_LOGI("[WiFi Storage] Loaded index: count=%d\n",
                  index->count);
        return 0;
    }
#endif

    SM_LOGI("[WiFi Storage] No index found, creating new\n");
    return -1;
}

/* 保存索引信息 */
static int wifi_storage_save_index(const WifiStorageIndex* index)
{
    if (!index) {
        return -1;
    }

#if defined(CONFIG_NV)
    WifiStorageIndex index_copy = *index;
    int ret = hal_system_set_config(WIFI_STORAGE_NV_INDEX_KEY,
                                    &index_copy,
                                    sizeof(WifiStorageIndex));
    if (ret == 0) {
        SM_LOGI("[WiFi Storage] Saved index: count=%d\n",
                  index->count);
        return 0;
    }
#endif

    SM_LOGE("[WiFi Storage] Save index failed\n");
    return -1;
}

/* 加载所有WiFi信息 */
int wifi_storage_load(WifiStorageManager* storage)
{
    if (!storage) {
        return -1;
    }

    memset(storage, 0, sizeof(WifiStorageManager));
    storage->current_index = -1;
    storage->next_insert_index = 0;  /* 初始化循环插入索�?*/

    /* 1. 加载索引信息 */
    WifiStorageIndex index;
    if (wifi_storage_load_index(&index) != 0) {
        SM_LOGI("[WiFi Storage] No stored WiFi found\n");
        return -1;
    }

    if (index.count == 0) {
        SM_LOGI("[WiFi Storage] Index loaded but count=0\n");
        return -1;
    }

    /* 2. 逐个加载WiFi信息 */
    uint8_t loaded_count = 0;
    for (uint8_t i = 0; i < index.count && i < WIFI_MAX_STORED; i++) {
        char nv_key[32];
        wifi_get_nv_key(i, nv_key, sizeof(nv_key));

#if defined(CONFIG_NV)
        int ret = hal_system_get_config(nv_key,
                                        &storage->credentials[loaded_count],
                                        sizeof(WifiCredential));
        if (ret > 0) {
            /* 校验channel范围，防止旧NV格式升级后channel字段为随机值（兼容旧格式） */
            if (storage->credentials[loaded_count].channel > 13) {
                SM_LOGI("[WiFi Storage] Invalid channel(%d) reset to 0 for: %s\n",
                          storage->credentials[loaded_count].channel,
                          storage->credentials[loaded_count].ssid);
                storage->credentials[loaded_count].channel = 0;
            }
            SM_LOGI("[WiFi Storage] Loaded [%d] from NV key \"%s\": %s (ch=%d)\n",
                      loaded_count, nv_key,
                      storage->credentials[loaded_count].ssid,
                      storage->credentials[loaded_count].channel);
            loaded_count++;
        } else {
            SM_LOGI( "[WiFi Storage] Failed to load NV key \"%s\"\n", nv_key);
        }
#endif
    }

    storage->count = loaded_count;
    storage->current_index = (index.current_index < loaded_count) ?
                            index.current_index : -1;

    /* 恢复循环插入位置 */
    if (loaded_count >= WIFI_MAX_STORED) {
        /* 已满，使用保存的next_insert_index */
        storage->next_insert_index = index.next_insert_index % WIFI_MAX_STORED;
    } else {
        /* 未满，设置为count（下一个空位） */
        storage->next_insert_index = loaded_count;
    }

    SM_LOGI("[WiFi Storage] Restored next_insert_index: %d\n",
              storage->next_insert_index);

    if (loaded_count > 0) {
        SM_LOGI("[WiFi Storage] Loaded %d WiFi credentials successfully\n",
                  loaded_count);
        return 0;
    }

    /*
     * 共享索引残留但所有 cusWIFI_CFG_n 都不可读时，仅自愈索引层，
     * 上层仍按“无有效共享 WiFi”处理，避免每次启动重复落入同一脏状态。
     */
    if (index.count > 0) {
        WifiStorageIndex repaired_index;
        memset(&repaired_index, 0, sizeof(repaired_index));
        repaired_index.current_index = -1;

        if (wifi_storage_save_index(&repaired_index) == 0) {
            SM_LOGI("[WiFi Storage] Repaired stale index with no valid records\n");
        } else {
            SM_LOGE("[WiFi Storage] Failed to repair stale index with no valid records\n");
        }
    }

    SM_LOGI( "[WiFi Storage] No valid WiFi loaded\n");
    return -1;
}

/* 保存所有WiFi信息 */
int wifi_storage_save(const WifiStorageManager* storage)
{
    if (!storage) {
        return -1;
    }

    /* 1. 准备索引信息 */
    WifiStorageIndex index;
    memset(&index, 0, sizeof(WifiStorageIndex));
    index.count = storage->count;
    index.current_index = storage->current_index;
    index.next_insert_index = storage->next_insert_index;

    /* 2. 逐个保存WiFi信息 */
    for (uint8_t i = 0; i < storage->count && i < WIFI_MAX_STORED; i++) {
        char nv_key[32];
        wifi_get_nv_key(i, nv_key, sizeof(nv_key));

#if defined(CONFIG_NV)
        int ret = hal_system_set_config(nv_key,
                                        (void*)&storage->credentials[i],
                                        sizeof(WifiCredential));
        if (ret == 0) {
            SM_LOGI("[WiFi Storage] Saved [%d]: %s (channel=%d)\n",
                      i, storage->credentials[i].ssid,
                      storage->credentials[i].channel);
        } else {
            SM_LOGE("[WiFi Storage] Failed to save NV key \"%s\"\n", nv_key);
        }
#endif
    }

    /* 3. 保存索引信息 */
    if (wifi_storage_save_index(&index) != 0) {
        SM_LOGE("[WiFi Storage] Save failed\n");
        return -1;
    }

    SM_LOGI("[WiFi Storage] Saved %d WiFi credentials successfully\n",
              storage->count);
    return 0;
}

int8_t wifi_storage_find(const WifiStorageManager* storage, const char* ssid)
{
    if (!storage || !ssid) {
        return -1;
    }

    for (uint8_t i = 0; i < storage->count; i++) {
        if (strcmp(storage->credentials[i].ssid, ssid) == 0) {
            return i;
        }
    }

    return -1;
}

int wifi_storage_add(WifiStorageManager* storage,
                      const char* ssid,
                      const char* password)
{
    if (!storage || !ssid) {
        return -1;
    }

    /* 检查是否已存在 */
    int8_t existing_idx = wifi_storage_find(storage, ssid);
    if (existing_idx >= 0) {
        /* 更新现有WiFi */
        strlcpy(storage->credentials[existing_idx].password,
                password ? password : "",
                WIFI_PASSWORD_MAX_LEN);
        SM_LOGI("[WiFi Storage] Updated existing: %s (index: %d)\n",
                  ssid, existing_idx);
        return existing_idx;
    }

    /* 添加新WiFi - 使用循环插入策略 */
    int8_t insert_idx;

    if (storage->count < WIFI_MAX_STORED) {
        /* 未满，添加到下一个空�?*/
        insert_idx = storage->count;
        storage->count++;

        /* 更新下一个插入位置：如果刚好满了，循环回�?；否则指向下一个空�?*/
        if (storage->count >= WIFI_MAX_STORED) {
            storage->next_insert_index = 0;
        } else {
            storage->next_insert_index = storage->count;
        }

        SM_LOGI("[WiFi Storage] Add new: %s (index: %d, count: %d/%d)\n",
                  ssid, insert_idx, storage->count, WIFI_MAX_STORED);
        SM_LOGI("[WiFi Storage] Next insert index: %d\n",
                  storage->next_insert_index);
    } else {
        /* 已满，使用循环插入索引覆�?*/
        insert_idx = storage->next_insert_index;

        SM_LOGI("[WiFi Storage] Replace cyclic: %s -> %s (index: %d)\n",
                  storage->credentials[insert_idx].ssid, ssid, insert_idx);

        /* 更新下一个插入位置（循环�?*/
        storage->next_insert_index = (storage->next_insert_index + 1) % WIFI_MAX_STORED;

        SM_LOGI("[WiFi Storage] Next insert index: %d\n",
                  storage->next_insert_index);
    }

    /* 填充WiFi信息 */
    strlcpy(storage->credentials[insert_idx].ssid, ssid, WIFI_SSID_MAX_LEN);
    strlcpy(storage->credentials[insert_idx].password,
            password ? password : "",
            WIFI_PASSWORD_MAX_LEN);

    return insert_idx;
}

int wifi_storage_update(WifiNetcfgManager* mgr, int8_t index, int8_t rssi)
{
    if (!mgr || index < 0 || index >= mgr->storage.count) {
        return -1;
    }

    mgr->wifi_rssi[index] = rssi;
    mgr->wifi_last_connect[index] = os_time_get();
    return 0;
}

int wifi_storage_mark_failed(WifiNetcfgManager* mgr, int8_t index)
{
    if (!mgr || index < 0 || index >= mgr->storage.count) {
        return -1;
    }

    mgr->wifi_fail_count[index]++;

    /* 连续3次以上失败，标记为无�?*/
    if (mgr->wifi_fail_count[index] >= 3) {
        mgr->wifi_is_valid[index] = false;
        SM_LOGI( "[WiFi Storage] Marked as invalid: %s\n",
                  mgr->storage.credentials[index].ssid);
    }

    return 0;
}

int wifi_storage_reset_failed(WifiNetcfgManager* mgr, int8_t index)
{
    if (!mgr || index < 0 || index >= mgr->storage.count) {
        return -1;
    }

    mgr->wifi_fail_count[index] = 0;
    mgr->wifi_is_valid[index] = true;
    return 0;
}

int wifi_storage_remove(WifiStorageManager* storage, int8_t index)
{
    if (!storage || index < 0 || index >= storage->count) {
        return -1;
    }

    /* 移动后面的WiFi */
    for (uint8_t i = index; i < storage->count - 1; i++) {
        memcpy(&storage->credentials[i],
               &storage->credentials[i + 1],
               sizeof(WifiCredential));
    }

    storage->count--;
    memset(&storage->credentials[storage->count], 0, sizeof(WifiCredential));

    if (storage->current_index == index) {
        storage->current_index = -1;
    } else if (storage->current_index > index) {
        storage->current_index--;
    }

    storage->next_insert_index = storage->count;

    SM_LOGI("[WiFi Storage] Removed index: %d\n", index);
    return 0;
}

void wifi_storage_clear(WifiStorageManager* storage)
{
    if (!storage) {
        return;
    }

#if defined(CONFIG_NV)
    /* 清除索引信息 */
    WifiStorageIndex empty_index;
    memset(&empty_index, 0, sizeof(WifiStorageIndex));
    empty_index.current_index = -1;
    hal_system_set_config(WIFI_STORAGE_NV_INDEX_KEY,
                         &empty_index,
                         sizeof(WifiStorageIndex));

    /* 清除所有WiFi NV键�?*/
    WifiCredential empty_cred;
    memset(&empty_cred, 0, sizeof(WifiCredential));
    for (uint8_t i = 0; i < WIFI_MAX_STORED; i++) {
        char nv_key[32];
        wifi_get_nv_key(i, nv_key, sizeof(nv_key));
        hal_system_set_config(nv_key, &empty_cred, sizeof(WifiCredential));
    }
#endif

    memset(storage, 0, sizeof(WifiStorageManager));
    storage->current_index = -1;
    storage->next_insert_index = 0;  /* 重置循环插入索引 */
    SM_LOGI("[WiFi Storage] Cleared all NV storage\n");
}

/* ========================================
 *  扫描结果管理
 * ======================================== */

static int wifi_scan_compare_rssi(const void* a, const void* b)
{
    const WifiScanResult* r1 = (const WifiScanResult*)a;
    const WifiScanResult* r2 = (const WifiScanResult*)b;

    /* RSSI降序排序 (信号越强越靠�? */
    return r2->rssi - r1->rssi;
}

void wifi_scan_sort_by_rssi(WifiScanManager* scan_mgr)
{
    if (!scan_mgr || scan_mgr->count == 0) {
        return;
    }

    qsort(scan_mgr->results,
          scan_mgr->count,
          sizeof(WifiScanResult),
          wifi_scan_compare_rssi);
}

int8_t wifi_scan_match_stored(WifiNetcfgManager* mgr)
{
    if (!mgr || mgr->scan_mgr.count == 0 || mgr->storage.count == 0) {
        return -1;
    }

    /* 按RSSI从强到弱匹配 */
    for (uint8_t i = 0; i < mgr->scan_mgr.count; i++) {
        for (uint8_t j = 0; j < mgr->storage.count; j++) {
            if (!mgr->wifi_is_valid[j]) {
                continue;
            }

            if (strcmp(mgr->scan_mgr.results[i].ssid,
                      mgr->storage.credentials[j].ssid) == 0) {
                mgr->retry_ctx.scan_match_index = i;
                SM_LOGI("[WiFi Scan] Matched: %s (RSSI: %d)\n",
                          mgr->scan_mgr.results[i].ssid,
                          mgr->scan_mgr.results[i].rssi);
                return j;
            }
        }
    }

    return -1;
}

/**
 * 从扫描结果中尝试连接下一个匹配的WiFi
 * @param mgr WiFi管理�? * @return 成功返回匹配的存储索引，失败返回-1
 */
/*
 * 统计当前扫描结果中可见的已保存 WiFi 数量。
 * valid_only=true 时，仅统计当前仍允许参与本轮匹配的候选。
 */
static uint8_t wifi_count_visible_saved_ssids(WifiNetcfgManager* mgr, bool valid_only)
{
    if (!mgr || mgr->scan_mgr.count == 0 || mgr->storage.count == 0) {
        return 0;
    }

    uint8_t visible_count = 0;

    for (uint8_t i = 0; i < mgr->storage.count; i++) {
        if (valid_only && (!mgr->wifi_is_valid[i] || mgr->wifi_auth_failed[i])) {
            continue;
        }

        for (uint8_t j = 0; j < mgr->scan_mgr.count; j++) {
            if (strcmp(mgr->storage.credentials[i].ssid,
                       mgr->scan_mgr.results[j].ssid) == 0) {
                visible_count++;
                break;
            }
        }
    }

    return visible_count;
}

/*
 * 自动联网超时按整轮计时。auto_connect_start_time 在启动自动联网时设置，
 * 扫描、认证失败、单次连接失败和内部重扫均不得清零。
 */
static void wifi_netcfg_check_auto_connect_notice_timeout(WifiNetcfgManager* mgr,
                                                           uint32_t now)
{
    uint32_t elapsed;
    bool ssid_seen;

    if (!mgr || mgr->new_wifi_pending || mgr->auto_connect_start_time == 0 ||
        mgr->state == WIFI_STATE_IDLE || mgr->state == WIFI_STATE_NO_CONFIG ||
        mgr->state == WIFI_STATE_BLE_CONFIGURING || mgr->state == WIFI_STATE_CONNECTED ||
        mgr->timeout_event_posted) {
        return;
    }

    elapsed = (uint32_t)(now - mgr->auto_connect_start_time);
    if (elapsed < WIFI_AUTO_CONNECT_NOTICE_TIMEOUT_MS) {
        return;
    }

    SM_LOGI("[WIFI_TIMEOUT_TRACE] auto-connect round 60s HIT: elapsed=%lu, "
           "state=%d, link=%d, ssid_matched=%d\r\n",
           (unsigned long)elapsed, (int)mgr->state,
           mgr->link_connected ? 1 : 0,
           mgr->ssid_matched_in_scan ? 1 : 0);

    if (mgr->link_connected) {
        SM_LOGI("[WIFI_TIMEOUT_TRACE] notice suppressed: L2 connected, "
               "still waiting for IP\r\n");
        mgr->timeout_event_posted = true;
        return;
    }

    ssid_seen = mgr->ssid_matched_in_scan ||
                (wifi_count_visible_saved_ssids(mgr, false) > 0);
    wifi_netcfg_emit_notice(mgr,
                            ssid_seen ? WIFI_NETCFG_NOTICE_WIFI_PWD_ERR
                                      : WIFI_NETCFG_NOTICE_WIFI_SSID_ERR);
    mgr->timeout_event_posted = true;
}

/*
 * 新一轮候选周期开始前，仅恢复运行时 fail policy 相关状态。
 * 不修改共享存储/NV，也不在这里直接触发扫描或连接。
 */
static void wifi_reset_runtime_fail_flags_for_new_cycle(WifiNetcfgManager* mgr)
{
    if (!mgr) {
        return;
    }

    for (uint8_t i = 0; i < mgr->storage.count; i++) {
        if (mgr->wifi_fail_count[i] == 0 &&
            mgr->wifi_is_valid[i] &&
            !mgr->wifi_auth_failed[i]) {
            continue;
        }

        wifi_storage_reset_failed(mgr, i);
        mgr->wifi_auth_failed[i] = false;
    }

    mgr->retry_ctx.scan_match_index = 0;
    mgr->retry_ctx.total_retry_count = 0;
    mgr->retry_ctx.current_wifi_retry = 0;
    mgr->retry_ctx.connect_retry_start_time = 0;
    mgr->retry_ctx.connect_retry_interval = 0;
    mgr->retry_ctx.scan_retry_start_time = 0;
    mgr->retry_ctx.is_first_wifi_in_cycle = true;

    mgr->storage.current_index = -1;
    mgr->pending_wifi_index = -1;
    mgr->pending_channel = 0;
    mgr->ssid_matched_in_scan = false;
}

static int8_t wifi_try_next_matched_wifi(WifiNetcfgManager* mgr)
{
    if (!mgr || mgr->scan_mgr.count == 0 || mgr->storage.count == 0) {
        return -1;
    }

    /* 从当前索引的下一个位置开始搜�?*/
    uint8_t start_idx = mgr->retry_ctx.scan_match_index + 1;

    /* 按RSSI从强到弱匹配，跳过已尝试过的 */
    for (uint8_t i = start_idx; i < mgr->scan_mgr.count; i++) {
        for (uint8_t j = 0; j < mgr->storage.count; j++) {
            if (!mgr->wifi_is_valid[j]) {
                continue;  /* 跳过无效WiFi */
            }

            if (strcmp(mgr->scan_mgr.results[i].ssid,
                      mgr->storage.credentials[j].ssid) == 0) {
                /* 更新扫描匹配索引 */
                mgr->retry_ctx.scan_match_index = i;

                SM_LOGI("[WiFi Connect] Try next matched: %s (scan_idx: %d, RSSI: %d)\n",
                          mgr->scan_mgr.results[i].ssid, i,
                          mgr->scan_mgr.results[i].rssi);
                return j;
            }
        }
    }

    SM_LOGI("[WiFi Connect] No more matched WiFi in scan results\n");
    return -1;
}

/**
 * @brief 处理WiFi认证失败�?-Way握手失败）[优化3]
 *
 * 功能�? * 1. 当检测到4-Way握手失败时，立即标记该WiFi密码错误
 * 2. 立即停止当前连接，不等待超时
 * 3. 立即触发扫描，尝试下一个WiFi
 *
 * 性能提升�? * - 从等�?0秒超�?-> 1-2秒内立即切换
 * - 错误恢复速度提升90%+
 *
 * @param mgr WiFi管理器指�? * @return 0=成功�?1=失败
 */
static int wifi_netcfg_handle_auth_failure_event(WifiNetcfgManager* mgr)
{
    if (!mgr) {
        return -1;
    }

    int8_t idx = mgr->storage.current_index;
    if (idx < 0 || idx >= mgr->storage.count) {
        SM_LOGE("[WiFi Auth] Invalid current index: %d\n", idx);
        return -1;
    }

    SM_LOGE("[WiFi Auth] 4-Way handshake failed (密码错误): %s\n",
            mgr->storage.credentials[idx].ssid);

    /* 运行时标记认证失败（不持久化到NV，重启后可重试） */
    mgr->wifi_auth_failed[idx] = true;
    mgr->wifi_is_valid[idx] = false;
    mgr->wifi_fail_count[idx]++;

    SM_LOGI("[WiFi Auth] Marked WiFi[%d] as auth_failed, fail_count=%d\n",
              idx, mgr->wifi_fail_count[idx]);

    /* 立即停止当前连接，不等待超时 */
    wifi_netcfg_port_sta_stop();
    mgr->storage.current_index = -1;
    mgr->pending_wifi_index = -1;
    wifi_netcfg_reset_connect_progress(mgr);

    /* 等待500ms让WiFi驱动完成资源释放，再触发扫描（由update()调度�?*/
    wifi_netcfg_change_state(mgr, WIFI_STATE_SLEEP_SCANNING);
    mgr->retry_ctx.last_retry_time = os_time_get();
    mgr->retry_ctx.next_scan_interval = 500;  /* 500ms后触发全频段重扫 */
    SM_LOGI("[WiFi Auth] Fast recovery: scheduled rescan in 500ms\n");

    return 0;
}

#if WIFI_ENABLE_DIRECTED_SCAN
/* =====================================================================
 *  定向扫描函数实现（隐藏SSID支持）[优化2]
 * ===================================================================== */

/**
 * @brief 初始化定向扫描上下文
 */
void wifi_directed_scan_ctx_init(WifiDirectedScanContext* ctx)
{
    if (!ctx) return;
    memset(ctx, 0, sizeof(WifiDirectedScanContext));
    ctx->phase = WIFI_SCAN_PHASE_NONE;
    ctx->directed_scan_needed = false;
    ctx->missing_count = 0;
    ctx->current_directed_idx = 0;
}

/**
 * @brief 广播扫描完成后，检查哪些已存储的SSID未出现在结果中（可能是隐藏SSID�? * @return 缺失的SSID数量
 */
uint8_t wifi_check_missing_ssids(WifiNetcfgManager* mgr)
{
    if (!mgr || mgr->storage.count == 0) return 0;

    WifiDirectedScanContext* ctx = &mgr->directed_scan_ctx;
    ctx->missing_count = 0;

    for (uint8_t j = 0; j < mgr->storage.count; j++) {
        if (!mgr->wifi_is_valid[j]) continue;
        if (mgr->wifi_auth_failed[j]) continue;  /* 密码已确认错误，跳过 */

        const char* stored_ssid = mgr->storage.credentials[j].ssid;
        bool found = false;

        for (uint8_t i = 0; i < mgr->scan_mgr.count; i++) {
            if (strcmp(mgr->scan_mgr.results[i].ssid, stored_ssid) == 0) {
                found = true;
                break;
            }
        }

        if (!found && ctx->missing_count < WIFI_MAX_STORED) {
            strlcpy(ctx->missing_ssids[ctx->missing_count],
                    stored_ssid,
                    WIFI_SSID_MAX_LEN);
            ctx->missing_count++;
            SM_LOGI("[Directed Scan] Missing SSID (possible hidden): %s\n", stored_ssid);
        }
    }

    SM_LOGI("[Directed Scan] Total missing SSIDs: %d\n", ctx->missing_count);
    return ctx->missing_count;
}

/**
 * @brief 启动异步定向扫描（针对特定SSID�? * @param ssid 目标SSID
 * @return 0=成功�?1=失败
 */
int wifi_start_directed_scan_async(WifiNetcfgManager* mgr, const char* ssid)
{
    if (!mgr || !ssid) return -1;

    SM_LOGI("[Directed Scan] Starting directed scan for SSID: %s\n", ssid);

    wifi_scan_config_t scan_cfg;
    memset(&scan_cfg, 0, sizeof(wifi_scan_config_t));
    scan_cfg.channel = 0;  /* 全信道定向扫�?*/

    /* 注意：部分平台支持传入SSID做定向扫描，此处通过广播方式兼容 */
    int ret = wifi_netcfg_port_scan_start(false, &scan_cfg);
    if (ret != 0) {
        SM_LOGE("[Directed Scan] Failed to start scan, ret=%d\n", ret);
        return -1;
    }

    mgr->directed_scan_ctx.directed_scan_start_time = os_time_get();
    return 0;
}

/**
 * @brief 调度下一个定向扫描（�?missing_ssids 列表顺序�? * @return 0=成功启动�?1=所有SSID均已扫描完毕
 */
int wifi_start_next_directed_scan(WifiNetcfgManager* mgr)
{
    if (!mgr) return -1;

    WifiDirectedScanContext* ctx = &mgr->directed_scan_ctx;

    if (ctx->current_directed_idx >= ctx->missing_count) {
        SM_LOGI("[Directed Scan] All %d missing SSIDs scanned, finishing\n",
                ctx->missing_count);
        return -1;
    }

    const char* target_ssid = ctx->missing_ssids[ctx->current_directed_idx];
    SM_LOGI("[Directed Scan] Scanning SSID [%d/%d]: %s\n",
            ctx->current_directed_idx + 1,
            ctx->missing_count,
            target_ssid);

    return wifi_start_directed_scan_async(mgr, target_ssid);
}

/**
 * @brief 合并新扫描结果到 scan_mgr（去重，同SSID保留信号更强的）
 */
void wifi_merge_scan_results(WifiNetcfgManager* mgr,
                              const WifiScanResult* new_results,
                              uint8_t new_count)
{
    if (!mgr || !new_results || new_count == 0) return;

    for (uint8_t i = 0; i < new_count; i++) {
        const WifiScanResult* new_r = &new_results[i];

        /* 查找是否已在 scan_mgr �?*/
        bool found = false;
        for (uint8_t j = 0; j < mgr->scan_mgr.count; j++) {
            if (strcmp(mgr->scan_mgr.results[j].ssid, new_r->ssid) == 0) {
                /* 保留信号更强�?*/
                if (new_r->rssi > mgr->scan_mgr.results[j].rssi) {
                    mgr->scan_mgr.results[j] = *new_r;
                    SM_LOGD("[Directed Scan] Updated SSID %s, new RSSI=%d\n",
                            new_r->ssid, new_r->rssi);
                }
                found = true;
                break;
            }
        }

        /* 新SSID，追�?*/
        if (!found && mgr->scan_mgr.count < WIFI_SCAN_MAX_RESULTS) {
            mgr->scan_mgr.results[mgr->scan_mgr.count] = *new_r;
            mgr->scan_mgr.count++;
            SM_LOGI("[Directed Scan] Found hidden SSID: %s (RSSI=%d)\n",
                    new_r->ssid, new_r->rssi);
        }
    }
}

/**
 * @brief 广播扫描完成处理：暂存结果，检查缺失SSID，决定是否发起定向扫�? */
bool wifi_handle_broadcast_scan_done(WifiNetcfgManager* mgr,
                                      WifiScanResult* results,
                                      uint8_t count)
{
    if (!mgr) return false;

    WifiDirectedScanContext* ctx = &mgr->directed_scan_ctx;

    SM_LOGI("[Directed Scan] Broadcast scan done, results=%d\n", count);

    /* 将广播扫描结果存�?scan_mgr */
    uint8_t copy_count = (count > WIFI_SCAN_MAX_RESULTS) ? WIFI_SCAN_MAX_RESULTS : count;
    memcpy(mgr->scan_mgr.results, results, copy_count * sizeof(WifiScanResult));
    mgr->scan_mgr.count = copy_count;
    mgr->scan_mgr.scan_time = os_time_get();

    /* 按RSSI排序 */
    wifi_scan_sort_by_rssi(&mgr->scan_mgr);

    /* 智能限制（优先保留存储WiFi）[优化4] */
    wifi_scan_smart_limit(mgr);

    /* 打印扫描结果 */
    LOG_INFO("WIFI_NETCFG_MANAGER", "[WiFi Scan] ========== WiFi List (sorted by RSSI) ==========\n");
    for (uint8_t i = 0; i < mgr->scan_mgr.count; i++) {
        LOG_INFO("WIFI_NETCFG_MANAGER",
                 "[WiFi Scan] [%2d] SSID: %-32s RSSI: %4d dBm, Channel: %2d, Auth: %d\n",
                 i, mgr->scan_mgr.results[i].ssid,
                 mgr->scan_mgr.results[i].rssi,
                 mgr->scan_mgr.results[i].channel,
                 mgr->scan_mgr.results[i].auth_mode);
    }
    LOG_INFO("WIFI_NETCFG_MANAGER", "[WiFi Scan] ====================================================\n");

    /* 通知外部扫描完成 */
    if (mgr->on_scan_complete) {
        mgr->on_scan_complete(mgr->scan_mgr.count);
    }

    /* 清除驱动层扫描缓�?*/
    wifi_flush_scan_results(STATION_IF);

    /* 备份广播扫描结果（用于后续合并或超时恢复�?*/
    memcpy(ctx->broadcast_results, mgr->scan_mgr.results,
           mgr->scan_mgr.count * sizeof(WifiScanResult));
    ctx->broadcast_count = mgr->scan_mgr.count;

    wifi_storage_load(&mgr->storage);
    if (mgr->storage.count == 0) {
        ctx->directed_scan_needed = false;
        ctx->phase = WIFI_SCAN_PHASE_NONE;
        SM_LOGI("[Directed Scan] No stored WiFi, skip hidden SSID scan\n");
        return false;
    }

    /* 检查缺失SSID（可能是隐藏SSID�?*/
    uint8_t missing = wifi_check_missing_ssids(mgr);

    if (missing > 0) {
        /* 需要定向扫�?*/
        ctx->directed_scan_needed = true;
        ctx->current_directed_idx = 0;
        ctx->phase = WIFI_SCAN_PHASE_DIRECTED;

        SM_LOGI("[Directed Scan] %d hidden SSIDs detected, starting directed scan\n", missing);
        wifi_start_next_directed_scan(mgr);
        return true;   /* 调用方应 return 0，等待定向扫描回�?*/
    }

    /* 不需要定向扫描，直接进入连接逻辑 */
    ctx->directed_scan_needed = false;
    ctx->phase = WIFI_SCAN_PHASE_NONE;

    SM_LOGI("[Directed Scan] No hidden SSIDs, proceed to connect\n");
    return false;  /* 调用方继续执行连接逻辑 */
}

/**
 * @brief 定向扫描完成处理：合并结果，继续下一个或结束扫描
 */
void wifi_handle_directed_scan_done(WifiNetcfgManager* mgr,
                                     WifiScanResult* results,
                                     uint8_t count)
{
    if (!mgr) return;

    WifiDirectedScanContext* ctx = &mgr->directed_scan_ctx;

    SM_LOGI("[Directed Scan] Directed scan done [%d/%d], results=%d\n",
            ctx->current_directed_idx + 1,
            ctx->missing_count,
            count);

    /* 合并定向扫描结果�?scan_mgr */
    wifi_merge_scan_results(mgr, results, count);

    /* 移动到下一个缺失SSID */
    ctx->current_directed_idx++;

    if (ctx->current_directed_idx < ctx->missing_count) {
        /* 还有待扫描的SSID，继�?*/
        SM_LOGI("[Directed Scan] More SSIDs to scan, continue...\n");
        wifi_start_next_directed_scan(mgr);
    } else {
        /* 所有定向扫描完成，重新排序并进入连接逻辑 */
        SM_LOGI("[Directed Scan] All directed scans complete, total results=%d\n",
                mgr->scan_mgr.count);

        ctx->phase = WIFI_SCAN_PHASE_NONE;
        ctx->directed_scan_needed = false;

        /* 重新排序（合并后可能顺序变化�?*/
        wifi_scan_sort_by_rssi(&mgr->scan_mgr);

        SM_LOGI("[Directed Scan] Final scan results=%d, proceeding to connect\n",
                mgr->scan_mgr.count);
    }
}

#endif /* WIFI_ENABLE_DIRECTED_SCAN */

/**
 * @brief 智能限制扫描结果数量 [优化4]
 * 策略：优先保留存储WiFi，剩余位置按RSSI填充
 * @param mgr WiFi管理�? */
static void wifi_scan_smart_limit(WifiNetcfgManager* mgr)
{
    if (!mgr || mgr->scan_mgr.count <= 20) {
        return;  /* 不需要限�?*/
    }

    WifiScanResult limited_results[20];
    uint8_t limited_count = 0;

    /* 第一步：优先保留所有存储WiFi（最�?0个） */
    for (uint8_t i = 0; i < mgr->scan_mgr.count && limited_count < 20; i++) {
        bool is_stored = false;

        /* 检查是否在存储列表�?*/
        for (uint8_t j = 0; j < mgr->storage.count; j++) {
            if (!mgr->wifi_is_valid[j]) {
                continue;
            }

            if (strcmp(mgr->scan_mgr.results[i].ssid,
                      mgr->storage.credentials[j].ssid) == 0) {
                is_stored = true;
                break;
            }
        }

        if (is_stored) {
            memcpy(&limited_results[limited_count],
                   &mgr->scan_mgr.results[i],
                   sizeof(WifiScanResult));
            limited_count++;
        }
    }

    uint8_t stored_count = limited_count;
    SM_LOGD("[WiFi Scan] Smart limit - Stored WiFi preserved: %d\n", stored_count);

    /* 第二步：剩余位置用RSSI最强的非存储WiFi填充 */
    for (uint8_t i = 0; i < mgr->scan_mgr.count && limited_count < 20; i++) {
        bool already_added = false;

        /* 检查是否已经在limited_results中（含第一步加入的全部项） */
        for (uint8_t k = 0; k < limited_count; k++) {
            if (strcmp(mgr->scan_mgr.results[i].ssid,
                      limited_results[k].ssid) == 0) {
                already_added = true;
                break;
            }
        }

        if (!already_added) {
            memcpy(&limited_results[limited_count],
                   &mgr->scan_mgr.results[i],
                   sizeof(WifiScanResult));
            limited_count++;
        }
    }

    SM_LOGI("[WiFi Scan] Smart limit - Total: %d �?%d (Stored: %d, High-RSSI: %d)\n",
              mgr->scan_mgr.count, limited_count, stored_count, limited_count - stored_count);

    /* 第三步：替换原始扫描结果 */
    memcpy(mgr->scan_mgr.results, limited_results,
           limited_count * sizeof(WifiScanResult));
    mgr->scan_mgr.count = limited_count;
}

const WifiScanResult* wifi_scan_get_strongest(const WifiScanManager* scan_mgr)
{
    if (!scan_mgr || scan_mgr->count == 0) {
        return NULL;
    }

    return &scan_mgr->results[0];
}

/* ========================================
 *  管理器核心实�? * ======================================== */

WifiNetcfgManager* wifi_netcfg_manager_create(const WifiNetcfgConfig* config)
{
    WifiNetcfgManager* mgr = (WifiNetcfgManager*)os_zalloc(sizeof(WifiNetcfgManager));
    if (!mgr) {
        SM_LOGE("[WiFi Manager] Create failed\n");
        return NULL;
    }

    /* 设置配置 */
    if (config) {
        memcpy(&mgr->config, config, sizeof(WifiNetcfgConfig));
    } else {
        /* 默认配置 */
        mgr->config.scan_interval_1s = 1000;
        mgr->config.scan_interval_10s = 10000;
        mgr->config.scan_interval_30s = 30000;
        mgr->config.connect_timeout_ms = 15000;
        mgr->config.max_connect_retries = 3;
        mgr->config.max_scan_retries_normal = 10;
        mgr->config.max_scan_retries_extend = 15;
        mgr->config.enable_sleep_scanning = true;
    }

    mgr->state = WIFI_STATE_IDLE;
    mgr->storage.current_index = -1;
    mgr->pending_wifi_index = -1;
    mgr->storage.next_insert_index = 0;  /* 初始化循环插入索�?*/

    /* 初始化运行时信息数组 */
    for (int i = 0; i < WIFI_MAX_STORED; i++) {
        mgr->wifi_rssi[i] = -100;
        mgr->wifi_fail_count[i] = 0;
        mgr->wifi_is_valid[i] = true;
        mgr->wifi_auth_failed[i] = false;  /* 初始化认证失败标�?[优化3] */
        mgr->wifi_last_connect[i] = 0;
    }

    mgr->pending_channel = 0;  /* 初始化临时channel [优化1/5] */

#if WIFI_ENABLE_DIRECTED_SCAN
    wifi_directed_scan_ctx_init(&mgr->directed_scan_ctx);  /* 初始化定向扫描上下文 [优化2] */
#endif

    wifi_netcfg_reset_connect_progress(mgr);
    mgr->mutex = os_mutex_create();

    SM_LOGI("[WiFi Manager] Created\n");
    return mgr;
}

void wifi_netcfg_manager_destroy(WifiNetcfgManager* mgr)
{
    if (!mgr) {
        return;
    }

    if (mgr->mutex) {
        os_mutex_destroy(mgr->mutex);
    }

    os_free(mgr);
    SM_LOGI("[WiFi Manager] Destroyed\n");
}

int wifi_netcfg_manager_init(WifiNetcfgManager* mgr)
{
    if (!mgr) {
        return -1;
    }

    /* 检查重新配网标�?*/
    int need_reconfig = 0;
    bool force_reconfig = false;
#if defined(CONFIG_NV)
    int ret_nv = hal_system_get_config(CUSTOMER_NV_FLAG "NEED_CFG",
                                       &need_reconfig,
                                       sizeof(need_reconfig));
    if (ret_nv > 0 && need_reconfig == 1) {
        SM_LOGI("[WiFi Manager] Init: Reconfig flag detected (NEED_CFG=1)\n");
        force_reconfig = true;
        /* 清除重新配网标志 */
        need_reconfig = 0;
        hal_system_set_config(CUSTOMER_NV_FLAG "NEED_CFG",
                             &need_reconfig,
                             sizeof(need_reconfig));
    }
#endif

    /* 加载存储的WiFi信息（即使需要重新配网，也要加载现有WiFi�?*/
    int ret = wifi_storage_load(&mgr->storage);

    if (ret == 0 && mgr->storage.count > 0) {
        /* 有配网信息，初始化运行时信息 */
        for (int i = 0; i < mgr->storage.count; i++) {
            mgr->wifi_rssi[i] = -100;
            mgr->wifi_fail_count[i] = 0;
            mgr->wifi_is_valid[i] = true;
            mgr->wifi_last_connect[i] = 0;
        }

        /* 如果设置了强制重新配网标志，进入NO_CONFIG状�?*/
        if (force_reconfig) {
            wifi_netcfg_change_state(mgr, WIFI_STATE_NO_CONFIG);
            SM_LOGI("[WiFi Manager] Init: %d WiFi loaded, but forced to NO_CONFIG\n",
                      mgr->storage.count);
            return -1;
        }

        /* 正常启动，进入IDLE状�?*/
        wifi_netcfg_change_state(mgr, WIFI_STATE_IDLE);
        SM_LOGI("[WiFi Manager] Init: %d WiFi credentials loaded\n",
                  mgr->storage.count);
        return 0;
    } else {
        /* 无配网信�?*/
        wifi_netcfg_change_state(mgr, WIFI_STATE_NO_CONFIG);
        SM_LOGI("[WiFi Manager] Init: No WiFi config\n");
        return -1;
    }
}

int wifi_netcfg_dispatch_event(WifiNetcfgManager* mgr, const WifiNetcfgEvent* event)
{
    if (!mgr || !event) {
        return -1;
    }

    switch (event->type) {
        case WIFI_NETCFG_EVENT_START_AUTO_CONNECT:
            return wifi_netcfg_handle_start_auto_connect_event(mgr);
        case WIFI_NETCFG_EVENT_START_BLE_CONFIG:
            return wifi_netcfg_handle_start_ble_config_event(mgr);
        case WIFI_NETCFG_EVENT_SUBMIT_BLE_CONFIG:
            return wifi_netcfg_handle_submit_ble_config_event(mgr,
                                                              event->data.ble_config.ssid,
                                                              event->data.ble_config.password,
                                                              event->data.ble_config.channel);
        case WIFI_NETCFG_EVENT_LINK_CONNECTED:
            return wifi_netcfg_handle_link_connected_event(mgr);
        case WIFI_NETCFG_EVENT_GOT_IP:
            return wifi_netcfg_handle_got_ip_event(mgr);
        case WIFI_NETCFG_EVENT_CONNECT_FAILED:
            return wifi_netcfg_handle_connect_failed_event(mgr);
        case WIFI_NETCFG_EVENT_DISCONNECTED:
            return wifi_netcfg_handle_disconnected_event(mgr);
        case WIFI_NETCFG_EVENT_AUTH_FAILURE:
            return wifi_netcfg_handle_auth_failure_event(mgr);
        case WIFI_NETCFG_EVENT_SCAN_COMPLETE:
            return wifi_netcfg_handle_scan_complete_event(mgr,
                                                          event->data.scan_complete.results,
                                                          event->data.scan_complete.count);
        case WIFI_NETCFG_EVENT_TIMER_TICK:
            wifi_netcfg_process_timer_tick(mgr,
                                           event->data.timer_tick.now ?
                                               event->data.timer_tick.now :
                                               os_time_get());
            return 0;
        case WIFI_NETCFG_EVENT_NONE:
        default:
            break;
    }

    return -1;
}

int wifi_netcfg_start_auto_connect(WifiNetcfgManager* mgr)
{
    WifiNetcfgEvent event = {
        .type = WIFI_NETCFG_EVENT_START_AUTO_CONNECT,
    };

    return wifi_netcfg_dispatch_event(mgr, &event);
}

int wifi_netcfg_start_ble_config(WifiNetcfgManager* mgr)
{
    WifiNetcfgEvent event = {
        .type = WIFI_NETCFG_EVENT_START_BLE_CONFIG,
    };

    return wifi_netcfg_dispatch_event(mgr, &event);
}

int wifi_netcfg_notify_ble_config(WifiNetcfgManager* mgr,
                                  const char* ssid,
                                  const char* password)
{
    return wifi_netcfg_notify_ble_config_with_channel(mgr, ssid, password, 0);
}

int wifi_netcfg_notify_ble_config_with_channel(WifiNetcfgManager* mgr,
                                               const char* ssid,
                                               const char* password,
                                               uint8_t channel)
{
    WifiNetcfgEvent event = {
        .type = WIFI_NETCFG_EVENT_SUBMIT_BLE_CONFIG,
        .data.ble_config = {
            .ssid = ssid,
            .password = password,
            .channel = channel,
        },
    };

    return wifi_netcfg_dispatch_event(mgr, &event);
}

int wifi_netcfg_notify_link_connected(WifiNetcfgManager* mgr)
{
    WifiNetcfgEvent event = {
        .type = WIFI_NETCFG_EVENT_LINK_CONNECTED,
    };

    return wifi_netcfg_dispatch_event(mgr, &event);
}

int wifi_netcfg_notify_connected(WifiNetcfgManager* mgr)
{
    WifiNetcfgEvent event = {
        .type = WIFI_NETCFG_EVENT_GOT_IP,
    };

    return wifi_netcfg_dispatch_event(mgr, &event);
}

int wifi_netcfg_notify_connect_failed(WifiNetcfgManager* mgr)
{
    WifiNetcfgEvent event = {
        .type = WIFI_NETCFG_EVENT_CONNECT_FAILED,
    };

    return wifi_netcfg_dispatch_event(mgr, &event);
}

int wifi_netcfg_notify_disconnected(WifiNetcfgManager* mgr)
{
    WifiNetcfgEvent event = {
        .type = WIFI_NETCFG_EVENT_DISCONNECTED,
    };

    return wifi_netcfg_dispatch_event(mgr, &event);
}

int wifi_netcfg_handle_auth_failure(WifiNetcfgManager* mgr)
{
    WifiNetcfgEvent event = {
        .type = WIFI_NETCFG_EVENT_AUTH_FAILURE,
    };

    return wifi_netcfg_dispatch_event(mgr, &event);
}

int wifi_netcfg_notify_scan_complete(WifiNetcfgManager* mgr,
                                     WifiScanResult* results,
                                     uint8_t count)
{
    WifiNetcfgEvent event = {
        .type = WIFI_NETCFG_EVENT_SCAN_COMPLETE,
        .data.scan_complete = {
            .results = results,
            .count = count,
        },
    };

    return wifi_netcfg_dispatch_event(mgr, &event);
}

void wifi_netcfg_update(WifiNetcfgManager* mgr)
{
    WifiNetcfgEvent event = {
        .type = WIFI_NETCFG_EVENT_TIMER_TICK,
        .data.timer_tick = {
            .now = os_time_get(),
        },
    };

    (void)wifi_netcfg_dispatch_event(mgr, &event);
}

static int wifi_netcfg_handle_start_auto_connect_event(WifiNetcfgManager* mgr)
{
    uint32_t now;

    if (!mgr) {
        return -1;
    }

    if (mgr->storage.count == 0) {
        SM_LOGE("[WiFi Manager] No WiFi to connect\n");
        return -1;
    }

    /* 重置重试上下�?*/
    memset(&mgr->retry_ctx, 0, sizeof(WifiRetryContext));
    mgr->retry_ctx.next_scan_interval = mgr->config.scan_interval_1s;
    mgr->storage.current_index = -1;
    mgr->pending_wifi_index = -1;
    wifi_netcfg_reset_connect_progress(mgr);
    now = os_time_get();
    mgr->auto_connect_start_time = now ? now : 1U;
    mgr->timeout_event_posted = false;

#if WIFI_ENABLE_DIRECTED_SCAN
    /* 重置定向扫描上下文（新连接周期）[优化2] */
    wifi_directed_scan_ctx_init(&mgr->directed_scan_ctx);
#endif

    /* 开始扫�?*/
    wifi_netcfg_change_state(mgr, WIFI_STATE_SCANNING);
    wifi_scan_config_t scan_cfg;
    memset(&scan_cfg, 0, sizeof(wifi_scan_config_t));
    scan_cfg.channel = 0;
    wifi_netcfg_port_scan_start(false, &scan_cfg);
    mgr->last_scan_time = now;

    SM_LOGI("[WiFi Manager] Start auto-connect\n");
    return 0;
}

static int wifi_netcfg_handle_start_ble_config_event(WifiNetcfgManager* mgr)
{
    if (!mgr) {
        return -1;
    }

    wifi_netcfg_change_state(mgr, WIFI_STATE_BLE_CONFIGURING);
    wifi_netcfg_emit_notice(mgr, WIFI_NETCFG_NOTICE_BLE_START);
    return -1;
}

static int wifi_netcfg_handle_submit_ble_config_event(WifiNetcfgManager* mgr,
                                                      const char* ssid,
                                                      const char* password,
                                                      uint8_t channel)
{
    const char* safe_password;

    if (!mgr || !ssid) {
        return -1;
    }

    safe_password = password ? password : "";
    channel = (channel > 0 && channel <= 13) ? channel : 0;

    if (mgr->new_wifi_pending &&
        mgr->state == WIFI_STATE_CONNECTING &&
        mgr->pending_channel == channel &&
        strcmp(mgr->pending_ssid, ssid) == 0 &&
        strcmp(mgr->pending_password, safe_password) == 0) {
        SM_LOGI("[WiFi Manager] Duplicate BLE config ignored while connecting: %s\n",
                ssid);
        return 0;
    }

    SM_LOGI("[WiFi Manager] BLE config received: %s\n", ssid);

    /* 保存待处理的WiFi信息 */
    strlcpy(mgr->pending_ssid, ssid, WIFI_SSID_MAX_LEN);
    strlcpy(mgr->pending_password, safe_password, WIFI_PASSWORD_MAX_LEN);
    mgr->new_wifi_pending = true;
    mgr->pending_wifi_index = -1;
    mgr->timeout_event_posted = false;
    mgr->pending_channel = channel;

    /* 开始连�?*/
    wifi_config_u wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_u));
    strlcpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy(wifi_config.sta.password,
            safe_password,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.channel = mgr->pending_channel;

    if (mgr->pending_channel > 0) {
        SM_LOGI("[WiFi Channel] BLE config prefers channel %d\n",
                mgr->pending_channel);
    }

    wifi_netcfg_start_connect_attempt(mgr, -1, &wifi_config, true, os_time_get());

    return 0;
}

static int wifi_netcfg_handle_link_connected_event(WifiNetcfgManager* mgr)
{
    if (!mgr) {
        return -1;
    }

    if (mgr->state != WIFI_STATE_CONNECTING && mgr->state != WIFI_STATE_CONNECTED) {
        SM_LOGI("[WiFi Manager] Link-connected event ignored in state %d\n", mgr->state);
        return 0;
    }

    mgr->link_connected = true;
    mgr->connect_phase = WIFI_CONNECT_PHASE_WAIT_IP;
    if (mgr->link_connected_time == 0) {
        mgr->link_connected_time = os_time_get();
    }

    SM_LOGI("[WiFi Manager] Link connected, waiting for IP\n");
    return 0;
}

static int wifi_netcfg_handle_got_ip_event(WifiNetcfgManager* mgr)
{
    if (!mgr) {
        return -1;
    }

    mgr->link_connected = true;
    mgr->is_connected = true;
    mgr->has_ip = true;
    mgr->connect_phase = WIFI_CONNECT_PHASE_NONE;
    if (mgr->link_connected_time == 0) {
        mgr->link_connected_time = os_time_get();
    }
    wifi_netcfg_change_state(mgr, WIFI_STATE_CONNECTED);
    mgr->auto_connect_start_time = 0;
    mgr->timeout_event_posted = false;

    /* 重置连接重试上下文（连接成功�?*/
    mgr->retry_ctx.scan_match_index = 0;
    mgr->retry_ctx.connect_retry_start_time = 0;
    mgr->retry_ctx.connect_retry_interval = 0;

    /* 如果是新WiFi，保存到存储 */
    if (mgr->new_wifi_pending) {
        int8_t idx = wifi_storage_add(&mgr->storage,
                                       mgr->pending_ssid,
                                       mgr->pending_password);
        if (idx >= 0) {
            mgr->storage.current_index = idx;

            /* 保存扫描到的channel（BLE配网时从扫描结果获取）[优化1/5] */
            if (mgr->pending_channel > 0 && mgr->pending_channel <= 13) {
                mgr->storage.credentials[idx].channel = mgr->pending_channel;
                SM_LOGI("[WiFi Channel] Saved channel %d for new WiFi: %s\n",
                        mgr->pending_channel, mgr->pending_ssid);
            }

            /* 初始化新WiFi的运行时信息 */
            mgr->wifi_rssi[idx] = -100;
            mgr->wifi_fail_count[idx] = 0;
            mgr->wifi_is_valid[idx] = true;
            mgr->wifi_auth_failed[idx] = false;  /* 重置认证失败标志 [优化3] */
            mgr->wifi_last_connect[idx] = os_time_get();
            wifi_storage_save(&mgr->storage);
            SM_LOGI("[WiFi Manager] Saved new WiFi: %s (index: %d)\n",
                      mgr->pending_ssid, idx);

            if (mgr->on_config_success) {
                mgr->on_config_success(mgr->pending_ssid);
            }
        }
        mgr->new_wifi_pending = false;
        // /* 停止BLE配网 */
        // netcfg_deinit();
        //netcfg_status_inform(PACKET_TYPE_GOT_IP);
    } else {
        /* 正常联网模式：只重置内存中的失败计数，不保存到NV */
        if (mgr->storage.current_index >= 0) {
            int8_t idx = mgr->storage.current_index;

            /* 更新channel到NV（如果扫描到了新的channel）[优化1] */
            if (mgr->pending_channel > 0 && mgr->pending_channel <= 13) {
                if (mgr->storage.credentials[idx].channel != mgr->pending_channel) {
                    mgr->storage.credentials[idx].channel = mgr->pending_channel;
                    wifi_storage_save(&mgr->storage);
                    SM_LOGI("[WiFi Channel] Updated channel for '%s': %d\n",
                            mgr->storage.credentials[idx].ssid,
                            mgr->pending_channel);
                }
            }

            /* 只重置内存中的失败状态，不保存NV，避免系统重启后失败记录仍在 */
            if (mgr->wifi_fail_count[idx] > 0 || !mgr->wifi_is_valid[idx] || mgr->wifi_auth_failed[idx]) {
                wifi_storage_reset_failed(mgr, idx);
                mgr->wifi_auth_failed[idx] = false;  /* 重置认证失败标志 [优化3] */
                SM_LOGI("[WiFi Manager] Reset fail count in memory for: %s (not saved to NV)\n",
                          mgr->storage.credentials[idx].ssid);
            } else {
                /* 正常联网，无需操作 */
                SM_LOGI("[WiFi Manager] Normal reconnect, no operation needed\n");
            }
        }
    }

    /* 重置重试上下�?*/
    memset(&mgr->retry_ctx, 0, sizeof(WifiRetryContext));
    mgr->pending_wifi_index = -1;

    if (mgr->on_connected) {
        const char* ssid = mgr->storage.current_index >= 0 ?
                          mgr->storage.credentials[mgr->storage.current_index].ssid :
                          mgr->pending_ssid;
        mgr->on_connected(ssid);
    }

    SM_LOGI("[WiFi Manager] Connected successfully\n");
    return 0;
}

static int wifi_netcfg_handle_connect_failed_event(WifiNetcfgManager* mgr)
{
    if (!mgr) {
        return -1;
    }

    SM_LOGI( "[WiFi Manager] Connect failed\n");
    wifi_netcfg_reset_connect_progress(mgr);
    mgr->pending_wifi_index = -1;

    /* 如果是BLE配网的WiFi */
    if (mgr->new_wifi_pending) {
        mgr->retry_ctx.current_wifi_retry++;
        if (mgr->retry_ctx.current_wifi_retry >= 2) {
            /* BLE配网失败 */
            SM_LOGE("[WiFi Manager] BLE config failed: %s\n",
                      mgr->pending_ssid);

            if (mgr->on_config_failed && !mgr->timeout_event_posted) {
                mgr->on_config_failed(mgr->pending_ssid);
                mgr->timeout_event_posted = true;
            }
            
            //netcfg_status_inform(PACKET_TYPE_NET_TIMEOUT); todo::
            mgr->new_wifi_pending = false;
            mgr->retry_ctx.current_wifi_retry = 0;

            wifi_netcfg_change_state(mgr, WIFI_STATE_BLE_CONFIGURING);

        } else {
            /* 重试BLE配网的WiFi */
            os_msleep(1000);
            {
                WifiNetcfgEvent event = {
                    .type = WIFI_NETCFG_EVENT_SUBMIT_BLE_CONFIG,
                    .data.ble_config = {
                        .ssid = mgr->pending_ssid,
                        .password = mgr->pending_password,
                        .channel = mgr->pending_channel,
                    },
                };
                wifi_netcfg_dispatch_event(mgr, &event);
            }
        }
        return 0;
    }

    /* 已存储WiFi连接失败 */
    if (mgr->storage.current_index >= 0) {
        wifi_storage_mark_failed(mgr, mgr->storage.current_index);
    }

    mgr->retry_ctx.current_wifi_retry++;

    /* 如果当前WiFi重试次数达到上限，标记为失败，尝试下一�?*/
    if (mgr->retry_ctx.current_wifi_retry >= mgr->config.max_connect_retries) {
        mgr->retry_ctx.current_wifi_retry = 0;
        mgr->storage.current_index = -1;
    }

    /* 重新扫描 */
    wifi_netcfg_change_state(mgr, WIFI_STATE_DISCONNECTED);
    return 0;
}

static int wifi_netcfg_handle_disconnected_event(WifiNetcfgManager* mgr)
{
    int8_t reconnect_index;
    bool start_new_auto_connect_round;
    uint32_t now;

    if (!mgr) {
        return -1;
    }

    start_new_auto_connect_round = (mgr->state == WIFI_STATE_CONNECTED) ||
                                   mgr->has_ip ||
                                   (mgr->auto_connect_start_time == 0);
    reconnect_index = mgr->storage.current_index;
    wifi_netcfg_reset_connect_progress(mgr);
    mgr->pending_wifi_index = -1;
    wifi_netcfg_change_state(mgr, WIFI_STATE_DISCONNECTED);

    if (mgr->on_disconnected) {
        mgr->on_disconnected();
    }

    SM_LOGI( "[WiFi Manager] Disconnected, will reconnect\n");
    wifi_netcfg_port_sta_stop();
    /* 重新扫描连接 */
    mgr->retry_ctx.current_wifi_retry = 0;
    if (reconnect_index >= 0 && reconnect_index < mgr->storage.count) {
        mgr->pending_wifi_index = reconnect_index;
    }
    mgr->storage.current_index = -1;
    now = os_time_get();
    if (start_new_auto_connect_round) {
        mgr->auto_connect_start_time = now ? now : 1U;
        mgr->timeout_event_posted = false;
    }
    mgr->retry_ctx.last_retry_time = now;
    mgr->retry_ctx.next_scan_interval = 500;

    if (mgr->pending_wifi_index >= 0) {
        SM_LOGI("[WiFi Reconnect] Scheduled fast reconnect in 500ms: %s\n",
                mgr->storage.credentials[mgr->pending_wifi_index].ssid);
    } else {
        SM_LOGI("[WiFi Reconnect] Scheduled rescan in 500ms\n");
    }
    return 0;
}

static int wifi_netcfg_handle_scan_complete_event(WifiNetcfgManager* mgr,
                                                  WifiScanResult* results,
                                                  uint8_t count)
{
    if (!mgr || !results) {
        return -1;
    }

    /* BLE配网模式：不处理匹配逻辑，避免匹配到其他WiFi */
    if (mgr->new_wifi_pending) {
        SM_LOGI("[WiFi Manager] Scan result saved but matching ignored in BLE config mode\n");
        return 0;
    }

    /* 只在特定状态下处理匹配和连接逻辑 */
    /* 其他状态的扫描结果只保存不处理（包�?wifi_start_station 触发的扫描） */
    if (mgr->state != WIFI_STATE_SCANNING &&
        mgr->state != WIFI_STATE_SLEEP_SCANNING &&
        mgr->state != WIFI_STATE_DISCONNECTED) {
        SM_LOGI("[WiFi Manager] Scan result saved but matching ignored in state %d\n",
                  mgr->state);
        return 0;
    }

#if WIFI_ENABLE_DIRECTED_SCAN
    /* ---- 定向扫描阶段分发 [优化2] ---- */
    WifiDirectedScanContext* dscan_ctx = &mgr->directed_scan_ctx;

    if (dscan_ctx->phase == WIFI_SCAN_PHASE_DIRECTED) {
        /* 当前处于定向扫描阶段：合并结果，继续或结�?*/
        wifi_handle_directed_scan_done(mgr, results, count);

        /* 如果还有更多定向扫描未完成，等待下次回调 */
        if (dscan_ctx->current_directed_idx < dscan_ctx->missing_count) {
            return 0;
        }
        /* 所有定向扫描完成，继续下方连接逻辑（scan_mgr 已更新） */
        SM_LOGI("[Directed Scan] All done, fall through to connect logic\n");
    } else {
        /* 广播扫描阶段：调用处理函数（排序/智能筛�?打印/检查隐藏SSID�?*/
        dscan_ctx->phase = WIFI_SCAN_PHASE_BROADCAST;
        if (wifi_handle_broadcast_scan_done(mgr, results, count)) {
            return 0;  /* 定向扫描已启动，等待回调 */
        }
        /* 无隐藏SSID，继续连接逻辑 */
        SM_LOGI("[Directed Scan] No hidden SSIDs, proceed to connect logic\n");
    }
    /* ---- 定向扫描阶段分发结束 ---- */

#else
    /* 未启用定向扫描：原有逻辑 */

    /* 排序(RSSI降序) */
    qsort(results, count, sizeof(WifiScanResult), wifi_scan_compare_rssi);

    /* 始终保存扫描结果 */
    mgr->scan_mgr.count = (count > WIFI_SCAN_MAX_RESULTS) ?
                          WIFI_SCAN_MAX_RESULTS : count;
    memcpy(mgr->scan_mgr.results, results,
           mgr->scan_mgr.count * sizeof(WifiScanResult));
    mgr->scan_mgr.scan_time = os_time_get();

    /* 智能限制：优先保留存储WiFi，剩余位置按RSSI填充 [优化4] */
    wifi_scan_smart_limit(mgr);

    SM_LOGI("[WiFi Manager] Scan complete: %d APs\n", mgr->scan_mgr.count);
    LOG_INFO("WIFI_NETCFG_MANAGER", "[WiFi Scan] ========== WiFi List (sorted by RSSI) ==========\n");
    for (uint8_t i = 0; i < mgr->scan_mgr.count; i++) {
        LOG_INFO("WIFI_NETCFG_MANAGER", "[WiFi Scan] [%2d] SSID: %-32s RSSI: %4d dBm, Channel: %2d, Auth: %d\n",
                  i,
                  mgr->scan_mgr.results[i].ssid,
                  mgr->scan_mgr.results[i].rssi,
                  mgr->scan_mgr.results[i].channel,
                  mgr->scan_mgr.results[i].auth_mode);
    }
    LOG_INFO("WIFI_NETCFG_MANAGER", "[WiFi Scan] ====================================================\n");
    if (mgr->on_scan_complete) {
        mgr->on_scan_complete(mgr->scan_mgr.count);
    }

    wifi_flush_scan_results(STATION_IF);

#endif /* WIFI_ENABLE_DIRECTED_SCAN */

    /* 重置扫描匹配索引（新的扫描结果） */
    mgr->retry_ctx.scan_match_index = 0;

    wifi_storage_load(&mgr->storage);
    /* 匹配已存储的WiFi */
    int8_t matched_idx = wifi_scan_match_stored(mgr);

    if (matched_idx >= 0) {
        /* 匹配成功，发起连�?*/
        mgr->storage.current_index = matched_idx;
        mgr->retry_ctx.current_wifi_retry = 0;
        mgr->retry_ctx.total_retry_count = 0;

        /* 重置连接重试上下文（新的扫描周期�?*/
        mgr->retry_ctx.connect_retry_start_time = 0;
        mgr->retry_ctx.connect_retry_interval = 0;

        /* 重置扫描重试计时器（匹配成功�?*/
        mgr->retry_ctx.scan_retry_start_time = 0;

        /* 标记这是本轮循环的第一个WiFi（scan_match_index=0�?*/
        mgr->retry_ctx.is_first_wifi_in_cycle = (mgr->retry_ctx.scan_match_index == 0);

        /* 标记SSID已在扫描结果中匹配到 */
        mgr->ssid_matched_in_scan = true;

        /* 从扫描结果中获取channel并临时保�?[优化1] */
        uint8_t scan_idx = mgr->retry_ctx.scan_match_index;
        if (scan_idx < mgr->scan_mgr.count) {
            mgr->pending_channel = mgr->scan_mgr.results[scan_idx].channel;
            SM_LOGI("[WiFi Channel] Got channel %d from scan result (SSID: %s)\n",
                    mgr->pending_channel,
                    mgr->storage.credentials[matched_idx].ssid);
        } else {
            mgr->pending_channel = 0;
        }

        wifi_config_u wifi_config;
        memset(&wifi_config, 0, sizeof(wifi_config_u));
        strlcpy((char*)wifi_config.sta.ssid,
                mgr->storage.credentials[matched_idx].ssid,
                sizeof(wifi_config.sta.ssid));
        strlcpy(wifi_config.sta.password,
                mgr->storage.credentials[matched_idx].password,
                sizeof(wifi_config.sta.password));

        /* 优先使用扫描到的channel（实时信息优先）[优化1] */
        if (mgr->pending_channel > 0 && mgr->pending_channel <= 13) {
            wifi_config.sta.channel = mgr->pending_channel;
            SM_LOGI("[WiFi Channel] Using scanned channel %d for fast connect\n",
                    mgr->pending_channel);
        } else {
            wifi_config.sta.channel = 0;
            SM_LOGI("[WiFi Channel] No scanned channel, using full-band scan (channel=0)\n");
        }

        wifi_netcfg_start_connect_attempt(mgr, matched_idx, &wifi_config, true, os_time_get());

        SM_LOGI("[WiFi Manager] Connecting to: %s (Channel: %d, first_wifi=%d)\n",
                  mgr->storage.credentials[matched_idx].ssid,
                  wifi_config.sta.channel,
                  mgr->retry_ctx.is_first_wifi_in_cycle);
    } else {
        /* 匹配失败 */
        mgr->ssid_matched_in_scan = false;
        uint32_t now = os_time_get();
        uint8_t visible_saved_count = wifi_count_visible_saved_ssids(mgr, false);
        uint8_t visible_valid_count = wifi_count_visible_saved_ssids(mgr, true);

        /* 清除current_index，避免在SLEEP_SCANNING状态时误判为有等待的WiFi */
        mgr->storage.current_index = -1;

        /*
         * 扫描里还能看到已保存 SSID，但本轮没有 valid 候选可参与匹配，
         * 说明当前已卡在“可见候选被本轮 fail policy 全部淘汰”的状态。
         * 这里先开启新周期，再沿用现有重扫节奏，避免长期误判为无已保存 SSID。
         */
        if (visible_saved_count > 0 && visible_valid_count == 0) {
            SM_LOGI("[WiFi Scan] Saved SSID visible but no valid candidate remains, start new cycle\n");
            wifi_reset_runtime_fail_flags_for_new_cycle(mgr);
            mgr->storage.current_index = -1;
        }

        /* 初始化扫描重试开始时间（第一次匹配失败时�?*/
        if (mgr->retry_ctx.scan_retry_start_time == 0) {
            mgr->retry_ctx.scan_retry_start_time = now;
            SM_LOGI("[WiFi Scan] No match found, start scan retry timer\n");
        }

        /* 计算扫描重试已经经过的时�?*/
        uint32_t scan_elapsed = now - mgr->retry_ctx.scan_retry_start_time;

        SM_LOGI("[WiFi Scan] No match found, scan_elapsed=%d ms\n", scan_elapsed);

        /* 根据扫描重试时间调整扫描间隔和事件上�?*/
        if (scan_elapsed < 30000) {
            /* 0-30秒：立即重试（尽快扫描） */
            mgr->retry_ctx.next_scan_interval = 1000;  /* 1秒间�?*/
            SM_LOGI("[WiFi Scan] 0-30s: use 1s interval\n");
        } else if (scan_elapsed < 60000) {
            /* 30-60秒：10秒间隔扫�?*/
            mgr->retry_ctx.next_scan_interval = 10000;  /* 10秒间�?*/

            SM_LOGI("[WiFi Scan] 30-60s: use 10s interval\n");
        } else {
            /* 60秒后�?0秒间隔扫�?*/
            mgr->retry_ctx.next_scan_interval = 30000;  /* 30秒间�?*/
            SM_LOGI("[WiFi Scan] >60s: use 30s interval\n");
        }

        wifi_netcfg_change_state(mgr, WIFI_STATE_SLEEP_SCANNING);
        mgr->retry_ctx.last_retry_time = now;
    }

    return 0;
}

static void wifi_netcfg_process_timer_tick(WifiNetcfgManager* mgr, uint32_t now)
{
    static uint32_t last_timeout_trace_time;

    if (!mgr) {
        return;
    }

    wifi_netcfg_check_auto_connect_notice_timeout(mgr, now);

    if ((mgr->state == WIFI_STATE_SCANNING ||
         mgr->state == WIFI_STATE_SLEEP_SCANNING ||
         mgr->state == WIFI_STATE_CONNECTING) &&
        (last_timeout_trace_time == 0 ||
         (uint32_t)(now - last_timeout_trace_time) >= 5000U)) {
        SM_LOGI("[WIFI_TIMEOUT_TRACE] timeout tick: now=%lu, state=%d, "
               "round_start=%lu, round_elapsed=%lu, connect_start=%lu, "
               "connect_retry_start=%lu, scan_retry_start=%lu, "
               "link=%d, ssid_matched=%d, first_wifi=%d, timeout_posted=%d\r\n",
               (unsigned long)now, (int)mgr->state,
               (unsigned long)mgr->auto_connect_start_time,
               mgr->auto_connect_start_time ?
                   (unsigned long)(uint32_t)(now - mgr->auto_connect_start_time) : 0UL,
               (unsigned long)mgr->connect_start_time,
               (unsigned long)mgr->retry_ctx.connect_retry_start_time,
               (unsigned long)mgr->retry_ctx.scan_retry_start_time,
               mgr->link_connected ? 1 : 0,
               mgr->ssid_matched_in_scan ? 1 : 0,
               mgr->retry_ctx.is_first_wifi_in_cycle ? 1 : 0,
               mgr->timeout_event_posted ? 1 : 0);
        last_timeout_trace_time = now;
    }

    switch (mgr->state) {
        case WIFI_STATE_SCANNING: {
#if WIFI_ENABLE_DIRECTED_SCAN
            /* 检查定向扫描超时，防止卡死 [优化2] */
            if (!mgr->new_wifi_pending &&
                mgr->directed_scan_ctx.phase == WIFI_SCAN_PHASE_DIRECTED &&
                mgr->directed_scan_ctx.directed_scan_start_time > 0 &&
                (now - mgr->directed_scan_ctx.directed_scan_start_time > WIFI_DIRECTED_SCAN_TIMEOUT_MS)) {
                SM_LOGE("[Directed Scan] Timeout (%d ms)! Fallback to broadcast results (%d APs)\n",
                        WIFI_DIRECTED_SCAN_TIMEOUT_MS,
                        mgr->directed_scan_ctx.broadcast_count);
                /* 恢复广播扫描结果 */
                if (mgr->directed_scan_ctx.broadcast_count > 0) {
                    memcpy(mgr->scan_mgr.results,
                           mgr->directed_scan_ctx.broadcast_results,
                           mgr->directed_scan_ctx.broadcast_count * sizeof(WifiScanResult));
                    mgr->scan_mgr.count = mgr->directed_scan_ctx.broadcast_count;
                }
                /* 重置定向扫描上下�?*/
                wifi_directed_scan_ctx_init(&mgr->directed_scan_ctx);
                /* 进入短等待后�?SLEEP_SCANNING 触发重新扫描 */
                mgr->storage.current_index = -1;
                wifi_netcfg_change_state(mgr, WIFI_STATE_SLEEP_SCANNING);
                mgr->retry_ctx.last_retry_time = now;
                mgr->retry_ctx.next_scan_interval = 100;  /* 100ms后触发全频段扫描 */
            }
#endif
             /* 配网模式：使用原有的超时逻辑 */
             if (mgr->new_wifi_pending) {
                /* BLE配网模式，使用配置的超时时间 */
                if (now - mgr->connect_start_time > mgr->config.connect_timeout_ms) {
                    wifi_netcfg_handle_ble_connect_timeout(mgr, "Scan");
                }
            }
            break;
        }
        case WIFI_STATE_CONNECTING: {
            /* 配网模式：使用原有的超时逻辑 */
            if (mgr->new_wifi_pending) {
                /* BLE配网模式，使用配置的超时时间 */
                if (now - mgr->connect_start_time > mgr->config.connect_timeout_ms) {
                    wifi_netcfg_handle_ble_connect_timeout(mgr, "Connect");
                }
                break;
            }

            /* 初始化连接重试开始时�?第一次连接时) */
            if (mgr->retry_ctx.connect_retry_start_time == 0) {
                mgr->retry_ctx.connect_retry_start_time = now;
                SM_LOGI("[WiFi Connect] Start connect retry timer\n");
            }

            /* 计算连接重试已经经过的时�?*/
            uint32_t connect_elapsed = now - mgr->retry_ctx.connect_retry_start_time;

            /* 确定当前WiFi的超时时�?*/
            uint32_t current_timeout;
            if (mgr->retry_ctx.is_first_wifi_in_cycle) {
                /* 第一个WiFi�?0秒超�?*/
                current_timeout = 30000;
            } else {
                /* 后续WiFi�?0秒连接超�?*/
                current_timeout = 10000;
            }

            /* 检查当前WiFi连接超时 */
            if (now - mgr->connect_start_time > current_timeout) {
                SM_LOGI("[WiFi Connect] Timeout after %d ms while %s (is_first=%d), disconnect\n",
                          current_timeout,
                          mgr->link_connected ? "waiting for IP" : "connecting to AP",
                          mgr->retry_ctx.is_first_wifi_in_cycle);

                /* 立即断开连接 */
                wifi_netcfg_port_sta_stop();
                
                /* 标记当前WiFi失败 */
                if (mgr->storage.current_index >= 0) {
                    wifi_storage_mark_failed(mgr, mgr->storage.current_index);
                }
                mgr->storage.current_index = -1;
                wifi_netcfg_reset_connect_progress(mgr);

                /* 尝试连接扫描结果中下一个匹配的WiFi */
                int8_t next_idx = wifi_try_next_matched_wifi(mgr);

                if (next_idx >= 0) {
                    /* 有下一个可尝试的WiFi */
                    mgr->pending_wifi_index = next_idx;
                    mgr->retry_ctx.is_first_wifi_in_cycle = false;  /* 后续WiFi不再是第一�?*/

                    /* 计算等待时间 */
                    uint32_t wait_time;
                    if (connect_elapsed < 60000) {
                        /* 60秒内�?0秒间�?- 已用�?0�?= 20秒等�?*/
                        wait_time = 20000;
                        SM_LOGI("[WiFi Connect] <60s: wait 20s before next WiFi (30s interval)\n");
                    } else {
                        /* 60秒后�?0秒间�?- 已用�?0�?= 20秒等�?*/
                        wait_time = 20000;
                        SM_LOGI("[WiFi Connect] >60s: wait 20s before next WiFi (30s interval)\n");
                    }

                    /* 进入等待状�?*/
                    wifi_netcfg_change_state(mgr, WIFI_STATE_SLEEP_SCANNING);
                    mgr->retry_ctx.last_retry_time = now;
                    mgr->retry_ctx.next_scan_interval = wait_time;

                    SM_LOGI("[WiFi Connect] Wait %d ms before next WiFi: %s\n",
                              wait_time, mgr->storage.credentials[next_idx].ssid);
                } else {
                    /* 扫描结果中所有WiFi都已尝试，重置并重新开始循�?*/
                    SM_LOGI("[WiFi Connect] All WiFi tried, reset and restart cycle\n");
                    /* 重置所有WiFi的失败标志，给它们新的机�?*/
                    SM_LOGI("[WiFi Connect] Reset runtime fail flags for new cycle\n");
                    wifi_reset_runtime_fail_flags_for_new_cycle(mgr);
                    /* 重置扫描匹配索引，重新开始循�?*/

                    /* 使用30秒间隔等待后重新扫描 */
                    uint32_t rescan_wait_time = 20000;  /* 30秒间�?- 已用10�?= 20�?*/
                    SM_LOGI("[WiFi Connect] Wait %d ms before rescan (30s interval)\n", rescan_wait_time);

                    wifi_netcfg_change_state(mgr, WIFI_STATE_SLEEP_SCANNING);
                    mgr->retry_ctx.last_retry_time = now;
                    mgr->retry_ctx.next_scan_interval = rescan_wait_time;
                }
            }
            break;
        }

        case WIFI_STATE_DISCONNECTED:
        case WIFI_STATE_SLEEP_SCANNING:
            /* 检查是否需要重新扫描或连接重试 */
            if (mgr->retry_ctx.last_retry_time > 0 &&
                now - mgr->retry_ctx.last_retry_time >= mgr->retry_ctx.next_scan_interval) {

                /* 检查是否有等待连接的WiFi（从CONNECTING状态等待后过来的） */
                if (mgr->pending_wifi_index >= 0 &&
                    mgr->pending_wifi_index < mgr->storage.count) {
                    /* 20秒等待结束，连接下一个WiFi */
                    SM_LOGI("[WiFi Connect] Wait done, connect to: %s\n",
                              mgr->storage.credentials[mgr->pending_wifi_index].ssid);

                    wifi_config_u wifi_config;
                    memset(&wifi_config, 0, sizeof(wifi_config_u));
                    strlcpy((char*)wifi_config.sta.ssid,
                            mgr->storage.credentials[mgr->pending_wifi_index].ssid,
                            sizeof(wifi_config.sta.ssid));
                    strlcpy(wifi_config.sta.password,
                            mgr->storage.credentials[mgr->pending_wifi_index].password,
                            sizeof(wifi_config.sta.password));

                    /* 获取存储的channel用于快连 [优化1] */
                    uint8_t stored_channel = mgr->storage.credentials[mgr->pending_wifi_index].channel;
                    if (stored_channel > 0 && stored_channel <= 13) {
                        wifi_config.sta.channel = stored_channel;
                        mgr->pending_channel = stored_channel;  /* 记录到pending，连接成功后可能更新 */
                        SM_LOGI("[WiFi Channel] Using stored channel %d for fast connect\n",
                                stored_channel);
                    } else {
                        wifi_config.sta.channel = 0;
                        mgr->pending_channel = 0;
                        SM_LOGI("[WiFi Channel] No stored channel, using full-band scan (channel=0)\n");
                    }

                    mgr->storage.current_index = mgr->pending_wifi_index;
                    mgr->pending_wifi_index = -1;
                    wifi_netcfg_reset_connect_progress(mgr);
                    mgr->connect_phase = WIFI_CONNECT_PHASE_L2_CONNECTING;
                    wifi_netcfg_change_state(mgr, WIFI_STATE_CONNECTING);
                    wifi_start_station(&wifi_config);
                    mgr->connect_start_time = now;
                    mgr->retry_ctx.last_retry_time = 0;
                } else {
                    /* 等待结束后重新扫描（从扫描匹配失败后过来的） */
                    SM_LOGI("[WiFi Scan] Wait done, trigger rescan\n");
                    mgr->pending_wifi_index = -1;
                    wifi_flush_scan_results(STATION_IF);
#if WIFI_ENABLE_DIRECTED_SCAN
                    /* 重置定向扫描上下文（新一轮扫描开始）[优化2] */
                    wifi_directed_scan_ctx_init(&mgr->directed_scan_ctx);
#endif
                    wifi_netcfg_change_state(mgr, WIFI_STATE_SCANNING);
                    wifi_scan_config_t scan_cfg;
                    memset(&scan_cfg, 0, sizeof(wifi_scan_config_t));
                    scan_cfg.channel = 0;
                    wifi_netcfg_port_sta_stop();
                    wifi_netcfg_port_scan_start(false, &scan_cfg);
                    mgr->last_scan_time = now;
                    mgr->retry_ctx.last_retry_time = 0;
                }
            }
            break;

        default:
            break;
    }
}

/* ========================================
 *  查询接口
 * ======================================== */

WifiNetcfgState wifi_netcfg_get_state(const WifiNetcfgManager* mgr)
{
    return mgr ? mgr->state : WIFI_STATE_ERROR;
}

bool wifi_netcfg_is_connected(const WifiNetcfgManager* mgr)
{
    return mgr ? (mgr->is_connected && mgr->has_ip) : false;
}

const WifiCredential* wifi_netcfg_get_current_wifi(const WifiNetcfgManager* mgr)
{
    if (!mgr || mgr->storage.current_index < 0 ||
        mgr->storage.current_index >= mgr->storage.count) {
        return NULL;
    }

    return &mgr->storage.credentials[mgr->storage.current_index];
}

const WifiStorageManager* wifi_netcfg_get_storage(const WifiNetcfgManager* mgr)
{
    return mgr ? &mgr->storage : NULL;
}

/* ========================================
 *  手动控制接口
 * ======================================== */

int wifi_netcfg_trigger_scan(WifiNetcfgManager* mgr)
{
    if (!mgr) {
        return -1;
    }

    mgr->storage.current_index = -1;
    mgr->pending_wifi_index = -1;
    wifi_netcfg_reset_connect_progress(mgr);
    wifi_netcfg_change_state(mgr, WIFI_STATE_SCANNING);
    wifi_scan_config_t scan_cfg;
    memset(&scan_cfg, 0, sizeof(wifi_scan_config_t));
    scan_cfg.channel = 0;
    wifi_netcfg_port_scan_start(false, &scan_cfg);
    mgr->last_scan_time = os_time_get();
    return 0;
}

int wifi_netcfg_connect_wifi(WifiNetcfgManager* mgr, int8_t index)
{
    if (!mgr || index < 0 || index >= mgr->storage.count) {
        return -1;
    }

    mgr->storage.current_index = index;

    wifi_config_u wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_u));
    strlcpy((char*)wifi_config.sta.ssid,
            mgr->storage.credentials[index].ssid,
            sizeof(wifi_config.sta.ssid));
    strlcpy(wifi_config.sta.password,
            mgr->storage.credentials[index].password,
            sizeof(wifi_config.sta.password));

    wifi_netcfg_start_connect_attempt(mgr, index, &wifi_config, true, os_time_get());

    return 0;
}

int wifi_netcfg_remove_wifi(WifiNetcfgManager* mgr, int8_t index)
{
    if (!mgr) {
        return -1;
    }

    int ret = wifi_storage_remove(&mgr->storage, index);
    if (ret == 0) {
        if (mgr->pending_wifi_index == index) {
            mgr->pending_wifi_index = -1;
        } else if (mgr->pending_wifi_index > index) {
            mgr->pending_wifi_index--;
        }

        wifi_storage_save(&mgr->storage);
    }

    return ret;
}

int wifi_netcfg_clear_all(WifiNetcfgManager* mgr)
{
    if (!mgr) {
        return -1;
    }

    wifi_storage_clear(&mgr->storage);
    wifi_storage_save(&mgr->storage);
    return 0;
}

/* ========================================
 *  回调注册
 * ======================================== */

void wifi_netcfg_register_callbacks(
    WifiNetcfgManager* mgr,
    WifiNetcfgStateChangedCallback on_state_changed,
    WifiNetcfgConnectedCallback on_connected,
    WifiNetcfgDisconnectedCallback on_disconnected,
    WifiNetcfgScanCompleteCallback on_scan_complete,
    WifiNetcfgConfigCallback on_config_success,
    WifiNetcfgConfigCallback on_config_failed)
{
    if (!mgr) {
        return;
    }

    mgr->on_state_changed = on_state_changed;
    mgr->on_connected = on_connected;
    mgr->on_disconnected = on_disconnected;
    mgr->on_scan_complete = on_scan_complete;
    mgr->on_config_success = on_config_success;
    mgr->on_config_failed = on_config_failed;
}

void wifi_netcfg_register_config_timeout_callback(WifiNetcfgManager* mgr,
                                                  WifiNetcfgConfigCallback callback)
{
    if (mgr) {
        mgr->on_config_timeout = callback;
    }
}

void wifi_netcfg_register_notice_callback(WifiNetcfgManager* mgr,
                                          WifiNetcfgNoticeCallback on_notice,
                                          void* user_data)
{
    if (!mgr) {
        return;
    }

    mgr->on_notice = on_notice;
    mgr->callback_user_data = user_data;
}

/* ========================================
 *  调试接口
 * ======================================== */

void wifi_netcfg_print_status(const WifiNetcfgManager* mgr)
{
    if (!mgr) {
        return;
    }

    const char* state_names[] = {
        "IDLE", "NO_CONFIG", "SCANNING", "CONNECTING",
        "CONNECTED", "DISCONNECTED", "BLE_CONFIGURING",
        "SLEEP_SCANNING", "ERROR"
    };

    SM_LOGI("\n========== WiFi Manager Status ==========\n");
    SM_LOGI("State: %s\n", state_names[mgr->state]);
    SM_LOGI("Connected: %s\n", mgr->is_connected ? "Yes" : "No");
    SM_LOGI("Has IP: %s\n", mgr->has_ip ? "Yes" : "No");
    SM_LOGI("Stored WiFi: %d\n", mgr->storage.count);
    SM_LOGI("Current Index: %d\n", mgr->storage.current_index);
    SM_LOGI("Total Retries: %d\n",
              mgr->retry_ctx.total_retry_count);
    SM_LOGI("Current WiFi Retries: %d\n",
              mgr->retry_ctx.current_wifi_retry);
    SM_LOGI("Sleep Mode: %s\n",
              mgr->retry_ctx.in_sleep_mode ? "Yes" : "No");
    SM_LOGI("==========================================\n\n");
}

void wifi_netcfg_print_storage(const WifiNetcfgManager* mgr)
{
    if (!mgr) {
        return;
    }

    SM_LOGI("\n========== Stored WiFi List ==========\n");
    for (uint8_t i = 0; i < mgr->storage.count; i++) {
        const WifiCredential* cred = &mgr->storage.credentials[i];
        SM_LOGI("[%d] SSID: %s\n", i, cred->ssid);
        SM_LOGI("    RSSI: %d, Fails: %d, Valid: %s\n",
                  mgr->wifi_rssi[i],
                  mgr->wifi_fail_count[i],
                  mgr->wifi_is_valid[i] ? "Yes" : "No");
    }
    SM_LOGI("======================================\n\n");
}

/**
 * 打印NV保存的WiFi和扫描后排序的WiFi信息
 * 用于调试和查看当前WiFi状�? */
void wifi_netcfg_print_nv_and_scan(const WifiNetcfgManager* mgr)
{
    if (!mgr) {
        return;
    }

    /* NV存储WiFi列表及其在扫描结果中的排�?*/
    SM_LOGI("\n==== NV WiFi (%d/%d) ====\n", mgr->storage.count, WIFI_MAX_STORED);

    if (mgr->storage.count > 0) {
        SM_LOGI("Cur:%-2d Next:%-2d ScanIdx:%d\n",
                mgr->storage.current_index,
                mgr->storage.next_insert_index,
                mgr->retry_ctx.scan_match_index);

        for (uint8_t i = 0; i < mgr->storage.count; i++) {
            const WifiCredential* cred = &mgr->storage.credentials[i];
            char st = (i == mgr->storage.current_index) ? 'C' :
                      (!mgr->wifi_is_valid[i]) ? 'X' : 'V';

            /* 在扫描结果中查找此SSID */
            int8_t scan_idx = -1;
            int8_t rssi = 0;
            for (uint8_t j = 0; j < mgr->scan_mgr.count; j++) {
                if (strcmp(mgr->scan_mgr.results[j].ssid, cred->ssid) == 0) {
                    scan_idx = j;
                    rssi = mgr->scan_mgr.results[j].rssi;
                    break;
                }
            }

            if (scan_idx >= 0) {
                /* 在扫描结果中找到 */
                SM_LOGI("[%d][%c] %-32s Scan#%-2d R:%-4d F:%d\n",
                        i, st, cred->ssid, scan_idx, rssi, mgr->wifi_fail_count[i]);
            } else {
                /* 未在扫描结果中找�?*/
                SM_LOGI("[%d][%c] %-32s [Not Found] F:%d\n",
                        i, st, cred->ssid, mgr->wifi_fail_count[i]);
            }
        }
    }

    /* 运行时状�?*/
    const char* st[] = {"IDLE", "NOCFG", "SCAN", "CONN", "CTED", "DISC", "BLE", "SLEEP", "ERR"};
    SM_LOGI("==== State:%-6s Con:%d 1st:%d CE:%-6u SE:%-6u ====\n",
            st[mgr->state], mgr->is_connected, mgr->retry_ctx.is_first_wifi_in_cycle,
            mgr->retry_ctx.connect_retry_start_time > 0 ?
                (os_time_get() - mgr->retry_ctx.connect_retry_start_time) : 0,
            mgr->retry_ctx.scan_retry_start_time > 0 ?
                (os_time_get() - mgr->retry_ctx.scan_retry_start_time) : 0);
}

