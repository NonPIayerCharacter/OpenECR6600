#include "system_wifi.h"
#include "mesh_common.h"
#include "mesh_rx.h"
#include "mesh_utils.h"
#include "mesh_aes.h"

#define MESH_RX_HANDLERS_NUM  (5)

typedef struct {
    void *arg;
    mesh_rx_frame_handler_t cb;
}mesh_rx_cb_t;

typedef struct {
    void *arg;
    unsigned char oui[MESH_OUI_LEN];
    unsigned char oui_type;
    mesh_rx_vendor_data_handler_t cb;
}mesh_rx_ie_cb_t;

static mesh_rx_cb_t  mesh_rx_handlers[MESH_RX_HANDLERS_NUM];
static mesh_rx_ie_cb_t  mesh_rx_vendor_handlers[MESH_RX_HANDLERS_NUM];

void mesh_rx_handlers_process(void *frame, int len, signed char rssi)
{
    unsigned short ieoffset = offsetof(struct ieee80211_mgmt, u.beacon.variable);//same as u.probe_resp.variable
    struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)frame;
    unsigned short ie_len = len -ieoffset;
    const unsigned char *ies = (unsigned char *)frame + ieoffset;
    const unsigned char *ie_pos;
    unsigned char *data_pos;
    int idx, data_len;
    unsigned char channel;
    int fc;

    for (idx = 0; idx < MESH_RX_HANDLERS_NUM; idx++) {
        if (mesh_rx_handlers[idx].cb) {
            mesh_rx_handlers[idx].cb(mesh_rx_handlers[idx].arg, frame, len, rssi);
        }
    }

    fc = le_to_host16(mgmt->frame_control); 
    if ( WLAN_FC_GET_STYPE(fc) != WLAN_FC_STYPE_BEACON && WLAN_FC_GET_STYPE(fc) != WLAN_FC_STYPE_PROBE_RESP){
        return;
    }

    channel = mesh_get_bss_channel((unsigned char *)mgmt, len);
    if(channel < 1 || channel > 14) {
        mesh_e("mesh_get_bss_channel  error:%d  bssid:"MESH_MAC_STR"\n",channel , MESH_MAC_VALUE(mgmt->bssid));
        return;
    }

    for (idx = 0; idx < MESH_RX_HANDLERS_NUM; idx++)
    {
        if (mesh_rx_vendor_handlers[idx].cb) {
            ie_pos = mesh_find_vendor_ie(mesh_rx_vendor_handlers[idx].oui, mesh_rx_vendor_handlers[idx].oui_type, ies, ie_len);
            if(ie_pos) {
                data_pos = (unsigned char *)ie_pos + sizeof(mesh_vendor_ie_t);
                data_len = ((mesh_vendor_ie_t *)ie_pos)->len - MESH_VENDOR_OUI_LEN;
                data_len = mesh_aes_dec_by_bssid(mgmt->bssid, data_pos, data_len, data_pos, data_len);
                if(data_len > 0) {
                    mesh_rx_vendor_handlers[idx].cb(mesh_rx_vendor_handlers[idx].arg, data_pos, data_len, channel, mgmt->bssid, rssi);
                }
            }
        }
    }
}


int mesh_rx_frame_register_handler(mesh_rx_frame_handler_t handler, void *arg)
{
    int idx;

    for (idx = 0; idx < MESH_RX_HANDLERS_NUM; idx++) {
        if (!mesh_rx_handlers[idx].cb) {
            mesh_rx_handlers[idx].cb = handler;
            mesh_rx_handlers[idx].arg = arg;
            return 0;
        }
    }

    return -1;
}
int mesh_rx_frame_unregister_handler(mesh_rx_frame_handler_t handler)
{
    int idx;

    for (idx = 0; idx < MESH_RX_HANDLERS_NUM; idx++) {
        if (mesh_rx_handlers[idx].cb == handler) {
            mesh_rx_handlers[idx].cb = NULL;
            mesh_rx_handlers[idx].arg = NULL;
            return 0;
        }
    }

    return -1;
}
int mesh_rx_vendor_data_register_handler(unsigned char *oui, unsigned char oui_type, mesh_rx_vendor_data_handler_t handler, void *env)
{
    int idx;

    for (idx = 0; idx < MESH_RX_HANDLERS_NUM; idx++) {
        if (!mesh_rx_vendor_handlers[idx].cb) {
            mesh_rx_vendor_handlers[idx].arg = env;
            memcpy(mesh_rx_vendor_handlers[idx].oui, oui, MESH_OUI_LEN);
            mesh_rx_vendor_handlers[idx].oui_type = oui_type;
            mesh_rx_vendor_handlers[idx].cb = handler;
            return 0;
        }
    }

    return 0;
}

int mesh_rx_vendor_data_unregister_handler(mesh_rx_vendor_data_handler_t handler)
{
    int idx;

    for (idx = 0; idx < MESH_RX_HANDLERS_NUM; idx++) {
        if (mesh_rx_vendor_handlers[idx].cb == handler) {
            memset(&mesh_rx_vendor_handlers[idx], 0, sizeof(mesh_rx_ie_cb_t));
            return 0;
        }
    }

    return -1;
}

static void mesh_rx_cb(void *buf, int len, wifi_promiscuous_pkt_type_t type)
{
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf; 
    struct ieee80211_mgmt *mgmt;
    int fc;

    if (type != WIFI_PKT_MGMT || len < IEEE80211_HDRLEN) {
        return;
    }

    mgmt = (struct ieee80211_mgmt *)pkt->payload;
    fc = le_to_host16(mgmt->frame_control);

    if ((WLAN_FC_GET_TYPE(fc) != WLAN_FC_TYPE_MGMT) ){
        return;
    }

    if ((WLAN_FC_GET_STYPE(fc) != WLAN_FC_STYPE_PROBE_RESP) && (WLAN_FC_GET_STYPE(fc) != WLAN_FC_STYPE_BEACON)
        && (WLAN_FC_GET_STYPE(fc) != WLAN_FC_STYPE_AUTH)){
        return ;
    }

    mesh_rx_handlers_process(pkt->payload, len, pkt->rx_ctrl.rssi);
}

int mesh_rx_init(void)
{
    memset(&mesh_rx_handlers, 0, sizeof(mesh_rx_handlers));
    memset(&mesh_rx_vendor_handlers, 0, sizeof(mesh_rx_vendor_handlers));

    return 0;
}

int mesh_rx_deinit(void)
{
    return wifi_sniffer_stop();
}

int mesh_rx_start(void)
{
    wifi_promiscuous_filter_t filter;

    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_BEACON_PROBE_RESP;
    return wifi_sniffer_start(mesh_rx_cb, &filter);
}

