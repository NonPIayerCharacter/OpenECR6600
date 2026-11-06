#include "ewss.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <oshal.h>
#include <system_lwip.h>
#include <system_wifi.h>

#define EWSS_ID_SSID        "EWSS"
#define SSID_TAG_LEN        (sizeof(normal_tag) + strlen(EWSS_ID_SSID))
#define DS_TAG_LEN          (sizeof(normal_tag) + 1)
#define TIMESTAMP_LEN       8
#define BEACON_INTERVAL_LEN 2
#define CAPABILITIES_LEN    2
#define EWSS_ID_TAG_LEN     (sizeof(struct ieee80211_vendor_ie))

#define TAG_NUMBER_SSID            0
#define TAG_NUMBER_DS              3
#define TAG_NUMBER_VENDOR_SPECIFIC 221
#define OUI_ESWIN                  0x94, 0x47, 0xb0
#define OUI_ESWIN_WORD             0x9447b0
#define ESWIN_OUI_TYPE_EWSS        0xf8

#define MAC_FCTRL_TYPE_MASK             0x000C
#define MAC_FCTRL_SUBT_MASK             0x00F0
#define MAC_FCTRL_MGT_T                 0x0000
#define MAC_FCTRL_PROBEREQ_ST           0x0040
#define MAC_FCTRL_PROBERSP_ST           0x0050
#define MAC_FCTRL_PROBEREQ              (MAC_FCTRL_MGT_T     | MAC_FCTRL_PROBEREQ_ST)
#define MAC_FCTRL_PROBERSP              (MAC_FCTRL_MGT_T     | MAC_FCTRL_PROBERSP_ST)

#define EWSS_TICK_TIMER_PERIOD 50
#define EWSS_CHANNEL_TIME      150
#define EWSS_MSG_QUEUE_SIZE    10
#define ENROLLEE_RX_BUF_SIZE   2048

#ifndef __PACKED
#define __PACKED __attribute__ ((__packed__))
#endif

#define MAC_ADDR_LEN 6

/// Structure of a Data/Management frame MAC header
typedef struct
{
    /// Frame control
    uint16_t fctl;
    /// Duration/ID
    uint16_t durid;
    /// Address 1
    uint8_t addr1[MAC_ADDR_LEN];
    /// Address 2
    uint8_t addr2[MAC_ADDR_LEN];
    /// Address 3
    uint8_t addr3[MAC_ADDR_LEN];
    /// Sequence control
    uint16_t seq;
} __PACKED mac_hdr;

typedef struct {
    uint8_t id;
    uint8_t length;
    uint8_t data[];
} __PACKED normal_tag;

struct ieee80211_vendor_ie {
    uint8_t element_id;
    uint8_t len;
    uint8_t oui[3];
    uint8_t oui_type;
} __PACKED;

struct ewss_ctx {
    ewss_role role;
    void *request_payload;
    size_t request_payload_len;
    void *enrollee_recv_buf;
    size_t enrollee_recv_len;
    uint8_t enrollee_channel_hint;
    ewss_recv_cb recv_cb;
    void *recv_cb_arg;
    uint32_t flags;
    os_timer_handle_t tick_timer;
    os_queue_handle_t msg_queue;
    uint8_t stop_requested;
    uint8_t current_channel_ticks;
    uint8_t current_channel_idx;
    uint8_t local_addr[MAC_ADDR_LEN];
    uint8_t ap_bssid[MAC_ADDR_LEN];
    size_t msg_pending;
} ewss_ctx;

typedef enum {
    TIMER_TICK,
    PROBE_REQUEST,
    PROBE_RESPONSE,
    DISABLE_REQUEST,
} ewss_msg_id;

typedef struct {
    ewss_msg_id id;
    void *payload;
    size_t payload_len;
    union {
        uint8_t src_addr[MAC_ADDR_LEN];
    };
} ewss_msg;

#define EWSS_LOG_A(fmt, ...) printf("[EWSS] " fmt, ##__VA_ARGS__)
#ifdef EWSS_DEBUG
#define EWSS_LOG_D(fmt, ...) printf("[EWSS] " fmt, ##__VA_ARGS__)
#else
#define EWSS_LOG_D(fmt, ...) do { } while (0)
#endif

#define ewss_assert(expr) \
    if (!(expr)) { \
        EWSS_LOG_A("Assertion @ %s:%d failed\r\n", __func__, __LINE__); \
        while (1) ; \
    }
#define MAC_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define ARRAY_6_LIST(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

static const uint8_t broadcast_addr[MAC_ADDR_LEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const struct ieee80211_vendor_ie ewss_id_tag = {
    .element_id = TAG_NUMBER_VENDOR_SPECIFIC,
    .len = 4,
    .oui = {OUI_ESWIN},
    .oui_type = ESWIN_OUI_TYPE_EWSS,
};

/*
 * This copyright notice applies to the following two functions.
 *
 * Copyright (C) 2015-2018 Alibaba Group Holding Limited
 */
static const uint8_t *cfg80211_find_ie(uint8_t eid, const uint8_t *ies, int len)
{
    while (len > 2 && ies[0] != eid) {
        len -= ies[1] + 2;
            ies += ies[1] + 2;
    }
    if (len < 2)
        return NULL;
    if (len < 2 + ies[1])
        return NULL;
    return ies;
}

static const uint8_t *cfg80211_find_vendor_ie(uint32_t oui, uint8_t oui_type, const uint8_t *ies, int len)
{
    struct ieee80211_vendor_ie *ie;
    const uint8_t *pos = ies, *end = ies + len;
    int ie_oui;

    while (pos < end) {
        pos = cfg80211_find_ie(TAG_NUMBER_VENDOR_SPECIFIC, pos,
                               end - pos);
        if (!pos)
            return NULL;

        ie = (struct ieee80211_vendor_ie *)pos;

        if (ie->len < 4)
            goto cont;

        ie_oui = ie->oui[0] << 16 | ie->oui[1] << 8 | ie->oui[2];
        if (ie_oui == oui && ie->oui_type == oui_type)
            return pos;
cont:
        pos += 2 + ie->len;
    }
    return NULL;
}

static void cleanup(struct ewss_ctx *ctx)
{
    if (ctx->role == ROLE_REGISTRAR) {
        wifi_set_accept_other_bssid_probe_req(0);
        wifi_mgmt_rx_hook_cb_register(NULL, NULL);
    }
    ctx->role = ROLE_NONE;

    if (ctx->tick_timer) {
        os_timer_stop(ctx->tick_timer);
        os_timer_delete(ctx->tick_timer);
    }
    if (ctx->msg_queue) {
        ewss_msg msg;
        while (!os_queue_receive(ctx->msg_queue, (char *)&msg, sizeof(msg), 0)) {
            os_free(msg.payload);
        }
        os_queue_destroy(ctx->msg_queue);
    }
    os_free(ctx->request_payload);
    os_free(ctx->enrollee_recv_buf);

    memset(ctx, 0, sizeof(*ctx));
}

static void tick_timer(os_timer_handle_t timer)
{
    struct ewss_ctx *ctx = os_timer_get_arg(timer);
    if (ctx->stop_requested) {
        return;
    }
    ewss_msg msg = { TIMER_TICK };
    os_queue_send(ctx->msg_queue, (char *)&msg, sizeof(msg), 0);
}

static int tx_pkt(const uint8_t *dst_addr, const void *data, size_t data_len, bool is_probe_req, bool monitor)
{
    struct ewss_ctx *ctx = &ewss_ctx;
    size_t pkt_len = sizeof(mac_hdr);
    void *buf;
    int if_idx = STATION_IF;

    if (monitor) {
        if_idx = MONITOR_IF;
    }
    if (is_probe_req) {
        pkt_len += SSID_TAG_LEN;
    } else {
        pkt_len += TIMESTAMP_LEN + BEACON_INTERVAL_LEN + CAPABILITIES_LEN + SSID_TAG_LEN + DS_TAG_LEN;
    }
    pkt_len += EWSS_ID_TAG_LEN + data_len;

    buf = os_zalloc(pkt_len);
    if (buf == NULL) {
        return -1;
    }
    mac_hdr *hdr = (mac_hdr *)buf;
    uint8_t *p = (uint8_t *)(hdr + 1);

    memcpy(hdr->addr1, dst_addr, MAC_ADDR_LEN);
    memcpy(hdr->addr2, ctx->local_addr, MAC_ADDR_LEN);
    if (is_probe_req) {
        hdr->fctl = MAC_FCTRL_PROBEREQ;
        memcpy(hdr->addr3, broadcast_addr, MAC_ADDR_LEN);
    } else {
        hdr->fctl = MAC_FCTRL_PROBERSP;
        memcpy(hdr->addr3, ctx->ap_bssid, MAC_ADDR_LEN);
        // Timestamp: 1
        p[0] = 1;
        p += TIMESTAMP_LEN;
        // Beacon Interval: 0.1024s
        p[0] = 100;
        p += BEACON_INTERVAL_LEN;
        // Capabilities: 0x0021
        p[0] = 0x21;
        p[1] = 0x00;
        p += CAPABILITIES_LEN;
    }
    // SSID
    normal_tag *tag = (normal_tag *)p;
    tag->id = TAG_NUMBER_SSID;
    tag->length = strlen(EWSS_ID_SSID);
    strcpy((char *)tag->data, EWSS_ID_SSID);
    p += SSID_TAG_LEN;
    if (!is_probe_req) {
        // DS
        tag = (normal_tag *)p;
        tag->id = TAG_NUMBER_DS;
        tag->length = 1;
        tag->data[0] = wifi_rf_get_channel();
        p += DS_TAG_LEN;
    }
    memcpy(p, &ewss_id_tag, EWSS_ID_TAG_LEN);
    p += EWSS_ID_TAG_LEN;
    memcpy(p, data, data_len);
    p += data_len;
    ewss_assert(p == (uint8_t *)buf + pkt_len);

    int ret;
    if ((ret = net_packet_send(if_idx, buf, pkt_len, 1, 0))) {
        EWSS_LOG_D("tx_pkt: net_packet_send ret %d\r\n", ret);
        os_free(buf);
        return -1;
    }
    os_free(buf);

    return 0;
}

int ewss_tx_response(const uint8_t *dst_addr, const void *data, size_t data_len)
{
    struct ewss_ctx *ctx = &ewss_ctx;
    if (ctx->role != ROLE_REGISTRAR) {
        return -1;
    }

    // Override dst_addr
    if (ctx->flags & EWSS_FLAG_BCAST_PROBE_RESPONSE) {
        dst_addr = broadcast_addr;
    }
    // TX Probe Response
    return tx_pkt(dst_addr, data, data_len, false, false);
}

static void tick_handler(struct ewss_ctx *ctx)
{
    const uint8_t channel_list[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};

    tx_pkt(broadcast_addr, ctx->request_payload, ctx->request_payload_len, true, true);
    if (++ctx->current_channel_ticks == EWSS_CHANNEL_TIME / EWSS_TICK_TIMER_PERIOD) {
        ctx->current_channel_ticks = 0;
        uint8_t next_channel_idx = (ctx->current_channel_idx + 1) % (sizeof(channel_list) / sizeof(channel_list[0]));
        EWSS_LOG_D("Switching to channel %d\r\n", channel_list[next_channel_idx]);
        wifi_rf_set_channel(channel_list[next_channel_idx]);
        ctx->current_channel_idx = next_channel_idx;
    }
}

static void enrollee_sniffer_cb(void *buf, int len, wifi_promiscuous_pkt_type_t type)
{
    struct ewss_ctx *ctx = &ewss_ctx;
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    const uint8_t *data = pkt->payload;
    const size_t fixed_len = sizeof(mac_hdr) + TIMESTAMP_LEN + BEACON_INTERVAL_LEN + CAPABILITIES_LEN;
    const size_t min_len = fixed_len + SSID_TAG_LEN + DS_TAG_LEN + EWSS_ID_TAG_LEN;
    if (len < min_len) {
        return;
    }
    mac_hdr *hdr = (mac_hdr *)data;
    bool accept_bcast = (ctx->flags & EWSS_FLAG_BCAST_PROBE_RESPONSE) != 0;
    if ((hdr->fctl & (MAC_FCTRL_TYPE_MASK | MAC_FCTRL_SUBT_MASK)) != MAC_FCTRL_PROBERSP ||
            ctx->stop_requested ||
            !((accept_bcast && !memcmp(hdr->addr1, broadcast_addr, MAC_ADDR_LEN)) ||
                !memcmp(hdr->addr1, ctx->local_addr, MAC_ADDR_LEN))) {
        return;
    }

    // The ID tag should be at exact offset
    const uint8_t *id = cfg80211_find_vendor_ie(OUI_ESWIN_WORD, ESWIN_OUI_TYPE_EWSS, data + min_len - EWSS_ID_TAG_LEN, EWSS_ID_TAG_LEN);

    bool send = false;
    size_t copy_len = len - min_len;
    if (id && copy_len <= ENROLLEE_RX_BUF_SIZE) {
        unsigned long psw = system_irq_save();
        if (ctx->msg_pending == 0) {
            ++ctx->msg_pending;
            send = true;
        }
        system_irq_restore(psw);
    }
    if (send) {
        ewss_msg msg = {PROBE_RESPONSE};
        normal_tag *ds = (normal_tag *)cfg80211_find_ie(TAG_NUMBER_DS, data + fixed_len, len - fixed_len);
        memcpy(ctx->enrollee_recv_buf, id + EWSS_ID_TAG_LEN, copy_len);
        ctx->enrollee_recv_len = copy_len;
        ctx->enrollee_channel_hint = 0;
        if (ds) {
            ctx->enrollee_channel_hint = ds->data[0];
        }
        memcpy(msg.src_addr, hdr->addr2, MAC_ADDR_LEN);
        memcpy(ctx->ap_bssid, hdr->addr3, MAC_ADDR_LEN);
        int ret = os_queue_send(ctx->msg_queue, (char *)&msg, sizeof(msg), 0);
        if (ret) {
            unsigned long psw = system_irq_save();
            --ctx->msg_pending;
            system_irq_restore(psw);
        }
    }
}

static void probe_response_handler(struct ewss_ctx *ctx, const uint8_t *src_addr)
{
    if (ctx->recv_cb) {
        ctx->recv_cb(src_addr, ctx->enrollee_recv_buf, ctx->enrollee_recv_len, ctx->recv_cb_arg);
    }
    unsigned long psw = system_irq_save();
    --ctx->msg_pending;
    system_irq_restore(psw);
}

static void probe_request_handler(struct ewss_ctx *ctx, const uint8_t *data, size_t len, const uint8_t *src_addr)
{
    if (ctx->recv_cb) {
        ctx->recv_cb(src_addr, data, len, ctx->recv_cb_arg);
    }
}

static int registrar_rx_cb(const void *data, size_t len, void *arg)
{
    struct ewss_ctx *ctx = (struct ewss_ctx *)arg;
    const size_t min_len = sizeof(mac_hdr) + SSID_TAG_LEN + EWSS_ID_TAG_LEN;
    if (len < min_len) {
        return 0;
    }
    mac_hdr *hdr = (mac_hdr *)data;
    if ((hdr->fctl & (MAC_FCTRL_TYPE_MASK | MAC_FCTRL_SUBT_MASK)) != MAC_FCTRL_PROBEREQ) {
        return 0;
    }

    if (ctx->stop_requested) {
        return 0;
    }

    const uint8_t *expected_id = data + sizeof(mac_hdr) + SSID_TAG_LEN;
    size_t left_len = len - sizeof(mac_hdr) - SSID_TAG_LEN;
    const uint8_t *id = cfg80211_find_vendor_ie(OUI_ESWIN_WORD, ESWIN_OUI_TYPE_EWSS, expected_id, left_len);
    if (id == expected_id) {
        left_len -= EWSS_ID_TAG_LEN;
        ewss_msg msg = {PROBE_REQUEST};
        uint8_t *cpy;
        if (left_len) {
            if ((cpy = os_malloc(left_len)) == NULL) {
                return 1;
            }
            memcpy(cpy, id + EWSS_ID_TAG_LEN, left_len);
            msg.payload = cpy;
            msg.payload_len = left_len;
        }
        memcpy(msg.src_addr, hdr->addr2, MAC_ADDR_LEN);
        os_queue_send(ctx->msg_queue, (char *)&msg, sizeof(msg), WAIT_FOREVER);
        return 1;
    }
    return 0;
}

static void ewss_task(void *arg)
{
    struct ewss_ctx *ctx = (struct ewss_ctx *)arg;
    ewss_msg msg;

    if (ctx->role == ROLE_ENROLLEE) {
        wifi_promiscuous_filter_t filter = {0};
        filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;

        wifi_stop_station();
        wifi_stop_softap();
        wifi_sniffer_set_reply_ack(1);
        wifi_set_promiscuous_filter(&filter);
        wifi_set_promiscuous_rx_cb(enrollee_sniffer_cb);
        wifi_set_promiscuous(true);
        wifi_rf_set_channel(1);
    } else {
        wifi_set_accept_other_bssid_probe_req(1);
        wifi_mgmt_rx_hook_cb_register(registrar_rx_cb, ctx);
    }

    while (1) {
        if (os_queue_receive(ctx->msg_queue, (char *)&msg, sizeof(msg), WAIT_FOREVER)) {
            continue;
        }
        switch (msg.id) {
        case TIMER_TICK:
            tick_handler(ctx);
            break;
        case PROBE_REQUEST:
            probe_request_handler(ctx, msg.payload, msg.payload_len, msg.src_addr);
            break;
        case PROBE_RESPONSE:
            probe_response_handler(ctx, msg.src_addr);
            break;
        case DISABLE_REQUEST:
            ctx->stop_requested = 1;
            break;
        }
        os_free(msg.payload);

        if (ctx->stop_requested) {
            cleanup(ctx);
            os_task_delete(0);
        }
    }
}

int ewss_enable(ewss_role role, ewss_recv_cb cb, void *cb_arg, uint32_t flags)
{
    struct ewss_ctx *ctx = &ewss_ctx;

    if (ctx->role != ROLE_NONE || !(role == ROLE_ENROLLEE || role == ROLE_REGISTRAR)) {
        return -1;
    }

    ewss_assert(ctx->request_payload == NULL);

    if (role == ROLE_REGISTRAR) {
        wifi_info_t ap_info;
        if (!(wifi_get_sta_status() == STA_STATUS_CONNECTED)) {
            return -1;
        }
        if (wifi_get_wifi_info(&ap_info)) {
            return -1;
        }
        memcpy(ctx->ap_bssid, ap_info.bssid, MAC_ADDR_LEN);
    }
    if (wifi_get_mac_addr(STATION_IF, ctx->local_addr)) {
        return -1;
    }
    if (role == ROLE_ENROLLEE) {
        ctx->local_addr[0] ^= 0x1 << 2;
    }
    ctx->role = role;
    ctx->recv_cb = cb;
    ctx->recv_cb_arg = cb_arg;
    ctx->flags = flags;
    ctx->msg_queue = os_queue_create("ewss-q", EWSS_MSG_QUEUE_SIZE, sizeof(ewss_msg), 0);
    if (ctx->msg_queue == NULL) {
        cleanup(ctx);
        return -1;
    }
    if (role == ROLE_ENROLLEE) {
        ctx->enrollee_recv_buf = os_malloc(ENROLLEE_RX_BUF_SIZE);
        ctx->tick_timer = os_timer_create("ewss-tick", EWSS_TICK_TIMER_PERIOD, 1, tick_timer, ctx);
        if (!ctx->enrollee_recv_buf || !ctx->tick_timer) {
            cleanup(ctx);
            return -1;
        }
        os_timer_start(ctx->tick_timer);
    }

    if (os_task_create("ewss", 4, 4096, ewss_task, ctx) == -1) {
        cleanup(ctx);
        return -1;
    }
    return 0;
}

int ewss_set_request_payload(const void *buf, size_t buf_len)
{
    struct ewss_ctx *ctx = &ewss_ctx;

    if (ctx->role != ROLE_ENROLLEE) {
        return -1;
    }
    os_free(ctx->request_payload);
    ctx->request_payload = NULL;
    if (buf_len == 0) {
        ctx->request_payload_len = 0;
        return 0;
    }
    if ((ctx->request_payload = os_malloc(buf_len))) {
        memcpy(ctx->request_payload, buf, buf_len);
        ctx->request_payload_len = buf_len;
    } else {
        return -1;
    }
    return 0;
}

int ewss_disable(void)
{
    struct ewss_ctx *ctx = &ewss_ctx;

    if (ctx->role == ROLE_NONE) {
        return -1;
    }
    if (!ctx->stop_requested) {
        if (ctx->role == ROLE_ENROLLEE) {
            wifi_sniffer_stop();
        }
        ctx->stop_requested = 1;
        ewss_msg msg = { DISABLE_REQUEST };
        os_queue_send(ctx->msg_queue, (char *)&msg, sizeof(msg), WAIT_FOREVER);
    }
    return 0;
}

ewss_role ewss_get_current_role(void)
{
    struct ewss_ctx *ctx = &ewss_ctx;

    return ctx->role;
}

int ewss_get_announced_ap_info(struct ewss_ap_info *info)
{
    struct ewss_ctx *ctx = &ewss_ctx;

    if (ctx->role != ROLE_ENROLLEE) {
        return -1;
    }
    info->channel = ctx->enrollee_channel_hint;
    memcpy(info->bssid, ctx->ap_bssid, MAC_ADDR_LEN);
    return 0;
}

#ifdef CONFIG_EWSS_CLI
#include <cli.h>

#define PASSWORD_SSID_SEPARATOR '\xff'
#define SSID_PASSWORD_BUF_LEN (63 + 1 + 32 + 1)
static char demo_ssid_password_buf[SSID_PASSWORD_BUF_LEN];

static void default_cb(const uint8_t *src_addr, const void *data, size_t data_len, void *cb_arg)
{
    if (ewss_get_current_role() == ROLE_ENROLLEE) {
        char dup[SSID_PASSWORD_BUF_LEN];
        EWSS_LOG_A("RX PROBE_RESPONSE from " MAC_FMT ", len %zu\r\n", ARRAY_6_LIST(src_addr), data_len);
        if (data_len > 0 && data_len < sizeof(dup)) {
            memset(dup, 0, sizeof(dup));
            memcpy(dup, data, data_len);
            char *pos;
            if (!(pos = strchr(dup, PASSWORD_SSID_SEPARATOR))) {
                return;
            }
            const char *password, *ssid;
            *pos = '\0';
            password = dup;
            ssid = pos + 1;

            ewss_disable();

            wifi_config_u sta_config;
            struct ewss_ap_info info;
            ewss_get_announced_ap_info(&info);
            memset(&sta_config, 0, sizeof(sta_config));
            strcpy((char *)sta_config.sta.ssid, ssid);
            strcpy(sta_config.sta.password, password);
            sta_config.sta.channel = info.channel;
            memcpy(sta_config.sta.bssid, info.bssid, MAC_ADDR_LEN);
            EWSS_LOG_A("Connecting %s(" MAC_FMT ") with password %s channel %d\r\n", ssid, ARRAY_6_LIST(info.bssid), password, info.channel);
            wifi_start_station(&sta_config);
        }
    } else {
        EWSS_LOG_A("RX PROBE_REQUEST from " MAC_FMT ", len %zu\r\n", ARRAY_6_LIST(src_addr), data_len);
        ewss_tx_response(src_addr, demo_ssid_password_buf, strlen(demo_ssid_password_buf));
    }
}

int ewss_cli_handler(int argc, char *argv[])
{
    if (!(argc >= 2)) {
        return CMD_RET_USAGE;
    }
    uint32_t flags = 0;

    if (!strcmp(argv[0], "ewss_b")) {
        flags |= EWSS_FLAG_BCAST_PROBE_RESPONSE;
    }

    if (!strcmp(argv[1], "stop")) {
        return ewss_disable() == 0 ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
    } else if (!strcmp(argv[1], "start_enrollee")) {
        int ret = ewss_enable(ROLE_ENROLLEE, default_cb, NULL, flags);
        if (ret == 0) {
            uint8_t demo_request_payload[64];
            for (int i = 0; i < sizeof(demo_request_payload); ++i) {
                demo_request_payload[i] = i;
            }
            ewss_set_request_payload(demo_request_payload, sizeof(demo_request_payload));
        } else {
            return CMD_RET_FAILURE;
        }
    } else if (!strcmp(argv[1], "start_registrar")) {
        if (!(argc >= 3)) {
            return CMD_RET_USAGE;
        }
        const char *ssid = argv[2];
        const char *password = "";
        if (argc >= 4) {
            password = argv[3];
        }
        snprintf(demo_ssid_password_buf, sizeof(demo_ssid_password_buf), "%s%c%s", password, PASSWORD_SSID_SEPARATOR, ssid);
        if (ewss_enable(ROLE_REGISTRAR, default_cb, NULL, flags)) {
            return CMD_RET_FAILURE;
        }
    } else {
        return CMD_RET_USAGE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD(ewss, ewss_cli_handler, "ewss zeroconfig", "ewss <start_enrollee|start_registrar|stop> [args]");
CLI_CMD(ewss_b, ewss_cli_handler, "ewss zeroconfig", "ewss <start_enrollee|start_registrar|stop> [args]");
#endif
