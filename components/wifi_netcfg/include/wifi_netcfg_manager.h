#ifndef WIFI_NETCFG_MANAGER_H
#define WIFI_NETCFG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "system_wifi.h"

/* ========================================
 *  WiFi配网与联网管理器状�? * ======================================== */

typedef enum {
    WIFI_STATE_IDLE,              /* 空闲状�?- 未启�?*/
    WIFI_STATE_NO_CONFIG,         /* 无配网信�?- 触发BLE配网 */
    WIFI_STATE_SCANNING,          /* 扫描�?*/
    WIFI_STATE_CONNECTING,        /* 连接�?*/
    WIFI_STATE_CONNECTED,         /* 已连�?*/
    WIFI_STATE_DISCONNECTED,      /* 断开连接 - 自动重连 */
    WIFI_STATE_BLE_CONFIGURING,   /* BLE配网�?*/
    WIFI_STATE_SLEEP_SCANNING,    /* 休眠状态扫�?*/
    WIFI_STATE_ERROR              /* 错误状�?*/
} WifiNetcfgState;

typedef enum {
    WIFI_NETCFG_EVENT_NONE = 0,
    WIFI_NETCFG_EVENT_START_AUTO_CONNECT,
    WIFI_NETCFG_EVENT_START_BLE_CONFIG,
    WIFI_NETCFG_EVENT_SUBMIT_BLE_CONFIG,
    WIFI_NETCFG_EVENT_LINK_CONNECTED,
    WIFI_NETCFG_EVENT_GOT_IP,
    WIFI_NETCFG_EVENT_CONNECT_FAILED,
    WIFI_NETCFG_EVENT_DISCONNECTED,
    WIFI_NETCFG_EVENT_AUTH_FAILURE,
    WIFI_NETCFG_EVENT_SCAN_COMPLETE,
    WIFI_NETCFG_EVENT_TIMER_TICK,
} WifiNetcfgEventType;

typedef enum {
    WIFI_NETCFG_NOTICE_NONE = 0,
    WIFI_NETCFG_NOTICE_BLE_START,
    WIFI_NETCFG_NOTICE_WIFI_SSID_ERR,
    WIFI_NETCFG_NOTICE_WIFI_PWD_ERR,
} WifiNetcfgNoticeType;

typedef void (*WifiNetcfgStateChangedCallback)(WifiNetcfgState old_state,
                                               WifiNetcfgState new_state);
typedef void (*WifiNetcfgConnectedCallback)(const char* ssid);
typedef void (*WifiNetcfgDisconnectedCallback)(void);
typedef void (*WifiNetcfgScanCompleteCallback)(uint8_t count);
typedef void (*WifiNetcfgConfigCallback)(const char* ssid);
typedef void (*WifiNetcfgNoticeCallback)(void* user_data,
                                         WifiNetcfgNoticeType notice);

/* ========================================
 *  WiFi信息存储结构
 * ======================================== */

#define WIFI_MAX_STORED 5
#define WIFI_PASSWORD_MAX_LEN 64

typedef struct {
    char ssid[WIFI_SSID_MAX_LEN];
    char password[WIFI_PASSWORD_MAX_LEN];
    uint8_t channel;                      /* WiFi信道 (0=未知�?-13=有效信道) [优化1] */
} WifiCredential;

typedef struct {
    WifiCredential credentials[WIFI_MAX_STORED];
    uint8_t count;                /* 当前存储的WiFi数量 */
    int8_t current_index;         /* 当前连接的WiFi索引 */
    uint8_t next_insert_index;    /* 下一个插入位�?循环插入�? */
} WifiStorageManager;

/* ========================================
 *  扫描结果管理
 * ======================================== */

#define WIFI_SCAN_MAX_RESULTS 50  /* 扩大缓冲区以支持智能限制功能 [优化4] */

typedef struct {
    char ssid[WIFI_SSID_MAX_LEN];
    int8_t rssi;
    uint8_t channel;
    uint8_t auth_mode;
} WifiScanResult;

typedef struct {
    WifiScanResult results[WIFI_SCAN_MAX_RESULTS];
    uint8_t count;
    uint32_t scan_time;
} WifiScanManager;

typedef struct {
    WifiNetcfgEventType type;
    union {
        struct {
            const char* ssid;
            const char* password;
            uint8_t channel;
        } ble_config;
        struct {
            WifiScanResult* results;
            uint8_t count;
        } scan_complete;
        struct {
            uint32_t now;
        } timer_tick;
    } data;
} WifiNetcfgEvent;

/* ========================================
 *  定向扫描（隐藏SSID支持）[优化2]
 * ======================================== */

/* 启用定向扫描功能开关（1=开启，0=关闭�?*/
#define WIFI_ENABLE_DIRECTED_SCAN       1
/* 单次定向扫描最多同时扫描的SSID数量 */
#define WIFI_MAX_DIRECTED_SCAN_SSID     3
/* 定向扫描等待超时(ms) */
#define WIFI_DIRECTED_SCAN_TIMEOUT_MS   5000

/* 扫描阶段枚举 */
typedef enum {
    WIFI_SCAN_PHASE_NONE = 0,    /* 无扫�?/ 初始状�?*/
    WIFI_SCAN_PHASE_BROADCAST,   /* 广播扫描阶段（全信道�?*/
    WIFI_SCAN_PHASE_DIRECTED,    /* 定向扫描阶段（指定SSID�?*/
} WifiScanPhase;

/* 定向扫描上下�?*/
typedef struct {
    WifiScanPhase   phase;                              /* 当前扫描阶段 */
    bool            directed_scan_needed;               /* 是否需要定向扫�?*/
    uint8_t         missing_count;                      /* 未找到的SSID数量 */
    char            missing_ssids[WIFI_MAX_STORED][WIFI_SSID_MAX_LEN]; /* 未找到的SSID列表 */
    uint8_t         current_directed_idx;               /* 当前定向扫描SSID索引 */
    uint32_t        directed_scan_start_time;           /* 定向扫描开始时间戳 */
    WifiScanResult  broadcast_results[WIFI_SCAN_MAX_RESULTS]; /* 广播扫描原始结果暂存 */
    uint8_t         broadcast_count;                    /* 广播扫描结果数量 */
} WifiDirectedScanContext;

/* ========================================
 *  连接重试策略
 * ======================================== */

typedef struct {
    uint8_t total_retry_count;    /* 总重试次�?*/
    uint8_t current_wifi_retry;   /* 当前WiFi重试次数 */
    uint32_t last_retry_time;     /* 最后重试时�?*/
    uint32_t next_scan_interval;  /* 下次扫描间隔 */
    bool in_sleep_mode;           /* 是否进入休眠扫描模式 */

    /* 连接超时重试相关 */
    uint8_t scan_match_index;     /* 当前尝试的扫描结果索�?按RSSI排序) */
    uint32_t connect_retry_start_time;  /* 开始连接重试的时间�?*/
    uint32_t connect_retry_interval;    /* 当前连接重试间隔(ms) */

    /* 扫描重试时间追踪 */
    uint32_t scan_retry_start_time;     /* 开始扫描重试的时间�?*/
    bool is_first_wifi_in_cycle;        /* 是否是本轮循环的第一个WiFi */
} WifiRetryContext;

typedef enum {
    WIFI_CONNECT_PHASE_NONE = 0,
    WIFI_CONNECT_PHASE_L2_CONNECTING,
    WIFI_CONNECT_PHASE_WAIT_IP,
} WifiConnectPhase;

/* ========================================
 *  WiFi配网管理器配�? * ======================================== */

typedef struct {
    uint32_t scan_interval_1s;        /* 匹配失败<10�? 1�?*/
    uint32_t scan_interval_10s;       /* 匹配失败10-15�? 10�?*/
    uint32_t scan_interval_30s;       /* 匹配失败>15�? 30�?*/
    uint32_t connect_timeout_ms;      /* 连接超时(默认15�? */
    uint8_t max_connect_retries;      /* 单个WiFi最大连接尝试次�?默认3�? */
    uint8_t max_scan_retries_normal;  /* 正常扫描失败上限(10�? */
    uint8_t max_scan_retries_extend;  /* 扩展扫描失败上限(15�? */
    bool enable_sleep_scanning;       /* 启用休眠状态扫�?*/
} WifiNetcfgConfig;

/* ========================================
 *  WiFi配网管理�? * ======================================== */

typedef struct WifiNetcfgManager {
    WifiNetcfgState state;
    WifiNetcfgState previous_state;
    WifiNetcfgConfig config;

    /* WiFi存储 */
    WifiStorageManager storage;

    /* WiFi运行时信息（不持久化到NV�?*/
    int8_t wifi_rssi[WIFI_MAX_STORED];              /* 信号强度 */
    uint8_t wifi_fail_count[WIFI_MAX_STORED];       /* 连接失败次数 */
    bool wifi_is_valid[WIFI_MAX_STORED];            /* 是否有效 */
    bool wifi_auth_failed[WIFI_MAX_STORED];         /* 认证失败标志�?-Way握手失败）[优化3] */
    uint32_t wifi_last_connect[WIFI_MAX_STORED];    /* 最后连接时�?*/

    /* 扫描管理 */
    WifiScanManager scan_mgr;

    /* 定向扫描上下文（隐藏SSID支持）[优化2] */
#if WIFI_ENABLE_DIRECTED_SCAN
    WifiDirectedScanContext directed_scan_ctx;
#endif

    /* 重试上下�?*/
    WifiRetryContext retry_ctx;

    /* 状态标�?*/
    bool is_connected;
    bool has_ip;
    bool link_connected;          /* Connected to AP but not necessarily got IP */
    WifiConnectPhase connect_phase;
    int8_t pending_wifi_index;    /* Deferred WiFi target while waiting in SLEEP_SCANNING */
    bool ble_config_pending;      /* BLE配网待处�?*/
    bool new_wifi_pending;        /* 新WiFi信息待保�?*/
    bool ssid_matched_in_scan;    /* 当前连接的SSID是否在扫描结果中匹配�?*/
    bool timeout_event_posted;    /* 30秒超时事件是否已上报 */
    
    /* 时间�?*/
    uint32_t state_enter_time;
    uint32_t last_scan_time;
    uint32_t connect_start_time;
    uint32_t link_connected_time;
    uint32_t auto_connect_start_time; /* 本轮自动联网起点，跨扫描/重连状态保持 */

    /* 临时配网信息 */
    char pending_ssid[WIFI_SSID_MAX_LEN];
    char pending_password[WIFI_PASSWORD_MAX_LEN];
    uint8_t pending_channel;          /* 临时保存扫描到的channel [优化1/5] */

    /* 回调函数 */
    WifiNetcfgStateChangedCallback on_state_changed;
    WifiNetcfgConnectedCallback on_connected;
    WifiNetcfgDisconnectedCallback on_disconnected;
    WifiNetcfgScanCompleteCallback on_scan_complete;
    WifiNetcfgConfigCallback on_config_success;
    WifiNetcfgConfigCallback on_config_failed;
    WifiNetcfgConfigCallback on_config_timeout;
    WifiNetcfgNoticeCallback on_notice;
    void* callback_user_data;

    /* 线程同步 */
    void* mutex;                  /* 互斥�?*/
} WifiNetcfgManager;

/* ========================================
 *  API 函数
 * ======================================== */

/**
 * 创建WiFi配网管理�? */
WifiNetcfgManager* wifi_netcfg_manager_create(const WifiNetcfgConfig* config);

/**
 * 销毁管理器
 */
void wifi_netcfg_manager_destroy(WifiNetcfgManager* mgr);

/**
 * 初始化管理器(加载存储的WiFi信息)
 * @return 0=有配网信息，-1=无配网信�?需要BLE配网)
 */
int wifi_netcfg_manager_init(WifiNetcfgManager* mgr);

/*
 * Unified event entry for the internal WiFi netcfg state machine.
 * Legacy start/notify APIs remain as compatibility wrappers on top.
 */
int wifi_netcfg_dispatch_event(WifiNetcfgManager* mgr, const WifiNetcfgEvent* event);

/**
 * 启动自动联网流程(有配网信�?
 */
int wifi_netcfg_start_auto_connect(WifiNetcfgManager* mgr);

/**
 * 启动BLE配网流程(无配网信�?
 */
int wifi_netcfg_start_ble_config(WifiNetcfgManager* mgr);

/**
 * 通知BLE配网信息下发
 * @param ssid WiFi SSID
 * @param password WiFi密码
 */
int wifi_netcfg_notify_ble_config(WifiNetcfgManager* mgr,
                                   const char* ssid,
                                   const char* password);
int wifi_netcfg_notify_ble_config_with_channel(WifiNetcfgManager* mgr,
                                               const char* ssid,
                                               const char* password,
                                               uint8_t channel);

/**
 * 通知WiFi连接成功(获得IP)
 */
int wifi_netcfg_notify_link_connected(WifiNetcfgManager* mgr);
int wifi_netcfg_notify_connected(WifiNetcfgManager* mgr);

/**
 * 通知WiFi连接失败
 */
int wifi_netcfg_notify_connect_failed(WifiNetcfgManager* mgr);

/**
 * 通知WiFi断开连接
 */
int wifi_netcfg_notify_disconnected(WifiNetcfgManager* mgr);

/**
 * 处理WiFi认证失败�?-Way握手失败）[优化3]
 * 功能：检测到密码错误时，立即切换到下一个WiFi�?-2秒）�? *       不等�?0秒超时，错误恢复速度提升90%+
 */
int wifi_netcfg_handle_auth_failure(WifiNetcfgManager* mgr);

/**
 * 通知扫描完成
 * @param results 扫描结果数组
 * @param count 结果数量
 */
int wifi_netcfg_notify_scan_complete(WifiNetcfgManager* mgr,
                                      WifiScanResult* results,
                                      uint8_t count);

/**
 * 更新管理器状�?需要定期调�?
 * 功能: 检查超时、执行重试、调度扫描等
 */
void wifi_netcfg_update(WifiNetcfgManager* mgr);

/**
 * 获取当前状�? */
WifiNetcfgState wifi_netcfg_get_state(const WifiNetcfgManager* mgr);

/**
 * 检查是否已连接
 */
bool wifi_netcfg_is_connected(const WifiNetcfgManager* mgr);

/**
 * 获取当前连接的WiFi信息
 */
const WifiCredential* wifi_netcfg_get_current_wifi(const WifiNetcfgManager* mgr);

/**
 * 获取所有存储的WiFi信息
 */
const WifiStorageManager* wifi_netcfg_get_storage(const WifiNetcfgManager* mgr);

/**
 * 手动触发扫描
 */
int wifi_netcfg_trigger_scan(WifiNetcfgManager* mgr);

/**
 * 手动触发连接指定WiFi
 */
int wifi_netcfg_connect_wifi(WifiNetcfgManager* mgr, int8_t index);

/**
 * 删除指定WiFi信息
 */
int wifi_netcfg_remove_wifi(WifiNetcfgManager* mgr, int8_t index);

/**
 * 清除所有WiFi信息
 */
int wifi_netcfg_clear_all(WifiNetcfgManager* mgr);

/**
 * 注册回调函数
 */
void wifi_netcfg_register_callbacks(
    WifiNetcfgManager* mgr,
    WifiNetcfgStateChangedCallback on_state_changed,
    WifiNetcfgConnectedCallback on_connected,
    WifiNetcfgDisconnectedCallback on_disconnected,
    WifiNetcfgScanCompleteCallback on_scan_complete,
    WifiNetcfgConfigCallback on_config_success,
    WifiNetcfgConfigCallback on_config_failed
);
void wifi_netcfg_register_notice_callback(WifiNetcfgManager* mgr,
                                          WifiNetcfgNoticeCallback on_notice,
                                          void* user_data);

/* Optional first-timeout policy. The pending attempt is cleared before this
 * callback; the application must stop STA and choose whether to reconnect.
 * NULL retains the existing BLE connection retry policy. */
void wifi_netcfg_register_config_timeout_callback(WifiNetcfgManager* mgr,
                                                  WifiNetcfgConfigCallback callback);

/**
 * 打印管理器状�?调试�?
 */
void wifi_netcfg_print_status(const WifiNetcfgManager* mgr);

/**
 * 打印存储的WiFi列表
 */
void wifi_netcfg_print_storage(const WifiNetcfgManager* mgr);

/**
 * 打印NV保存的WiFi和扫描后排序的WiFi信息（完整报告）
 * 显示内容�? * 1. NV存储的WiFi列表（包括状态、失败次数等�? * 2. 扫描结果列表（按RSSI排序，最强信号优先）
 * 3. 匹配分析（NV存储与扫描结果的匹配关系�? * 4. 运行时状态（当前连接状态、计时器等）
 */
void wifi_netcfg_print_nv_and_scan(const WifiNetcfgManager* mgr);

/* ========================================
 *  WiFi存储管理(持久�?
 * ======================================== */

/**
 * 从NV存储加载WiFi信息
 */
int wifi_storage_load(WifiStorageManager* storage);

/**
 * 保存WiFi信息到NV存储
 */
int wifi_storage_save(const WifiStorageManager* storage);

/**
 * 添加新WiFi(自动管理5个WiFi上限)
 */
int wifi_storage_add(WifiStorageManager* storage,
                      const char* ssid,
                      const char* password);

/**
 * 更新WiFi信息(RSSI、连接时间等)
 */
int wifi_storage_update(WifiNetcfgManager* mgr,
                         int8_t index,
                         int8_t rssi);

/**
 * 标记WiFi连接失败
 */
int wifi_storage_mark_failed(WifiNetcfgManager* mgr, int8_t index);

/**
 * 重置WiFi连接失败计数
 */
int wifi_storage_reset_failed(WifiNetcfgManager* mgr, int8_t index);

/**
 * 删除指定WiFi
 */
int wifi_storage_remove(WifiStorageManager* storage, int8_t index);

/**
 * 查找WiFi(根据SSID)
 */
int8_t wifi_storage_find(const WifiStorageManager* storage, const char* ssid);

/**
 * 清除所有WiFi
 */
void wifi_storage_clear(WifiStorageManager* storage);

/* ========================================
 *  扫描结果管理
 * ======================================== */

/**
 * 扫描结果排序(按RSSI降序)
 */
void wifi_scan_sort_by_rssi(WifiScanManager* scan_mgr);

/**
 * 匹配扫描结果与已存储WiFi
 * @return 匹配到的WiFi索引�?1表示无匹�? */
int8_t wifi_scan_match_stored(WifiNetcfgManager* mgr);

/**
 * 获取扫描结果中信号最强的WiFi
 */
const WifiScanResult* wifi_scan_get_strongest(const WifiScanManager* scan_mgr);

/* ========================================
 *  定向扫描API（隐藏SSID支持）[优化2]
 * ======================================== */

#if WIFI_ENABLE_DIRECTED_SCAN

/**
 * 初始化定向扫描上下文
 */
void wifi_directed_scan_ctx_init(WifiDirectedScanContext* ctx);

/**
 * 检查广播扫描后哪些存储的SSID未在结果中出现（可能是隐藏SSID�? * @return 缺失的SSID数量
 */
uint8_t wifi_check_missing_ssids(WifiNetcfgManager* mgr);

/**
 * 启动异步定向扫描（指定SSID�? * @param ssid 要扫描的SSID
 * @return 0=成功�?1=失败
 */
int wifi_start_directed_scan_async(WifiNetcfgManager* mgr, const char* ssid);

/**
 * 调度下一个定向扫描（顺序扫描每个缺失的SSID�? * @return 0=还有待扫描的SSID�?1=全部扫描完毕
 */
int wifi_start_next_directed_scan(WifiNetcfgManager* mgr);

/**
 * 合并广播扫描结果与定向扫描结果（去重�? */
void wifi_merge_scan_results(WifiNetcfgManager* mgr,
                              const WifiScanResult* new_results,
                              uint8_t new_count);

/**
 * 广播扫描完成后的处理（检查是否需要定向扫描）
 * @return true=已启动定向扫描（调用方应立即return），false=无需定向扫描（调用方继续连接逻辑�? */
bool wifi_handle_broadcast_scan_done(WifiNetcfgManager* mgr,
                                      WifiScanResult* results,
                                      uint8_t count);

/**
 * 定向扫描完成后的处理（合并结果，调度下一次或结束�? */
void wifi_handle_directed_scan_done(WifiNetcfgManager* mgr,
                                     WifiScanResult* results,
                                     uint8_t count);

#endif /* WIFI_ENABLE_DIRECTED_SCAN */

#endif /* WIFI_NETCFG_MANAGER_H */




