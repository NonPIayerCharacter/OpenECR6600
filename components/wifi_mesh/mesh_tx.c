#include "mesh_tx.h"
#include "system_wifi.h"
#include "mesh_common.h"
#include "mesh_aes.h"

typedef struct mesh_tx_ctx
{
    unsigned int tx_enable;
    unsigned char own_mac[MESH_MAC_AADR_LEN];
    beacon_data_t beacon_data[MESH_OUI_TPYE_MAX];
    unsigned char data_buff[MESH_OUI_TPYE_MAX][MESH_TLV_MAX_LEN];
}mesh_tx_ctx_t;

static mesh_tx_ctx_t mesh_tx_ctx;

__INLINE mesh_tx_ctx_t *mesh_tx_ctx_get(void)
{
    return &mesh_tx_ctx;
}

int mesh_tx_probe(unsigned char *own_mac, unsigned char *dst,  char *ssid)
{
    unsigned char base_rate[8]={0x82,0x84,0x8b,0x96,0x0c,0x12,0x18,0x24};
    unsigned char extended_rate[8]={0x30,0x48,0x60,0x6c};
    unsigned char buff[512] = {0};
    struct ieee80211_hdr *hdr;
    mesh_tlv_t *tlv;
    int total_len;

    hdr = (struct ieee80211_hdr *)buff;
    if(dst) {
        memcpy(hdr->addr1, dst, 6);
        memcpy(hdr->addr3, dst, 6);
    } else {
        memcpy(hdr->addr1, MESH_BROADCAST_MAC, 6);
        memcpy(hdr->addr3, MESH_BROADCAST_MAC, 6);
    }

    memcpy(hdr->addr2, own_mac, 6);
    hdr->frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
        WLAN_FC_STYPE_PROBE_REQ);
    total_len = sizeof(struct ieee80211_hdr);
    
    tlv = (mesh_tlv_t *)(buff + total_len);
    if (ssid) {
        tlv->eid = WLAN_EID_SSID;
        tlv->len = strlen(ssid);
        memcpy(tlv->val, ssid, tlv->len);
        total_len += tlv->len + 2;

        tlv = (mesh_tlv_t *)(buff + total_len);
    }

    tlv->eid = WLAN_EID_SUPP_RATES;
    tlv->len = 8;
    memcpy(tlv->val, base_rate, tlv->len);
    total_len += tlv->len + 2;

    tlv = (mesh_tlv_t *)(buff + total_len);
    tlv->eid = WLAN_EID_DS_PARAMS;
    tlv->len = 1;
    tlv->val[0] = wifi_rf_get_channel();
    total_len += tlv->len + 2;

    tlv = (mesh_tlv_t *)(buff + total_len);
    tlv->eid = WLAN_EID_EXT_SUPP_RATES;
    tlv->len = 4;
    memcpy(tlv->val, extended_rate, tlv->len);
    total_len += tlv->len + 2;

    return net_packet_send(SOFTAP_IF, (u8 *)buff, total_len, 1, 0);
}

int mesh_tx_beacon_data(unsigned char *bssid, beacon_data_t *vote, beacon_data_t *notity, 
                beacon_data_t *network_cfg, beacon_data_t *mesh_cfg)
{
    unsigned char *buff;
    int total_len = 0, offset = 0;
    mesh_vendor_ie_t *tlv;
    int result;

    total_len = (MESH_TLV_MAX_LEN + 2) * MESH_OUI_TPYE_MAX;
    buff = os_malloc(total_len);
    if (!buff) {
        mesh_e("%s malloc fail",__func__);
        return -1;
    }

    tlv = (mesh_vendor_ie_t *)(buff + offset);
    if (vote->is_send && vote->data && vote->len) {
        tlv->eid = MESH_EID_VENDOR_SPECIFIC;
        memcpy(tlv->oui, MESH_OUI, MESH_OUI_LEN); 
        tlv->oui_type = MESH_OUI_TPYE_VOTE;
        result = mesh_aes_enc_by_bssid(bssid, vote->data, vote->len, (buff + offset + sizeof(mesh_vendor_ie_t)), MESH_TLV_MAX_LEN);
        if (result < 0) {
            os_free(buff);
            return -2;
        }

        tlv->len = result + MESH_VENDOR_OUI_LEN;
        offset += tlv->len + 2;
    }

    tlv = (mesh_vendor_ie_t *)(buff + offset);
    if (notity->is_send && notity->data && notity->len) {
        tlv->eid = MESH_EID_VENDOR_SPECIFIC;
        memcpy(tlv->oui, MESH_OUI, MESH_OUI_LEN); 
        tlv->oui_type = MESH_OUI_TPYE_NOTITY;
        result = mesh_aes_enc_by_bssid(bssid, notity->data, notity->len, (buff + offset + sizeof(mesh_vendor_ie_t)), MESH_TLV_MAX_LEN);
        if (result < 0) {
            os_free(buff);
            return -2;
        }

        tlv->len = result + MESH_VENDOR_OUI_LEN;
        offset += tlv->len + 2;
    }

    tlv = (mesh_vendor_ie_t *)(buff + offset);
    if (network_cfg->is_send && network_cfg->data && network_cfg->len) {
        tlv->eid = MESH_EID_VENDOR_SPECIFIC;
        memcpy(tlv->oui, MESH_OUI, MESH_OUI_LEN); 
        tlv->oui_type = MESH_OUI_TPYE_NETWORKCFG;
        result = mesh_aes_enc_by_bssid(bssid, network_cfg->data, network_cfg->len, (buff + offset + sizeof(mesh_vendor_ie_t)), MESH_TLV_MAX_LEN);
        if (result < 0) {
            os_free(buff);
            return -2;
        }
        tlv->len = result + MESH_VENDOR_OUI_LEN;
        offset += tlv->len + 2;
    }

    tlv = (mesh_vendor_ie_t *)(buff + offset);
    if (mesh_cfg->is_send && mesh_cfg->data && mesh_cfg->len) {
        tlv->eid = MESH_EID_VENDOR_SPECIFIC;
        memcpy(tlv->oui, MESH_OUI, MESH_OUI_LEN); 
        tlv->oui_type = MESH_OUI_TPYE_CFG;
        result = mesh_aes_enc_by_bssid(bssid, mesh_cfg->data, mesh_cfg->len, (buff + offset + sizeof(mesh_vendor_ie_t)), MESH_TLV_MAX_LEN);
        if (result < 0) {
            os_free(buff);
            return -2;
        }
        tlv->len = result + MESH_VENDOR_OUI_LEN;
        offset += tlv->len + 2;
    }

    //mesh_d("%s :%d", __func__, offset);
    result = wifi_set_ap_vendor_elements(buff, offset);
    if (!result) {
        os_free(buff);
        return -3;
    }

    os_free(buff);

    return 0;
}

int mesh_tx_beacon_update(void)
{
    mesh_tx_ctx_t *ctx = mesh_tx_ctx_get();

    if(!ctx->tx_enable) {
        return -1;
    }

    //mesh_i("tx_beacon: %d %d %d %d",  ctx->beacon_data[0].is_send, ctx->beacon_data[1].is_send, ctx->beacon_data[2].is_send, ctx->beacon_data[3].is_send);
    return mesh_tx_beacon_data(ctx->own_mac, &ctx->beacon_data[0], &ctx->beacon_data[1], &ctx->beacon_data[2], &ctx->beacon_data[3]);
}

int mesh_tx_beacon_update_by_type(int type, unsigned char *data, unsigned int len, int immediate)
{
    mesh_tx_ctx_t *ctx = mesh_tx_ctx_get();
    beacon_data_t *beacon_data;

    if(type > MESH_OUI_TPYE_MAX || len > MESH_TLV_MAX_LEN) {
        mesh_e("%s error type:%d len:%d", type, len);
        return -1;
    }

    beacon_data = &ctx->beacon_data[type];
    if(data && len) {
        memcpy(beacon_data->data, data, len);
        beacon_data->len = len;
        beacon_data->is_send = 1;
    } else{
        beacon_data->is_send = 0;
    }

    if(!immediate) {
        return 0;
    }

    return mesh_tx_beacon_update();
}

void mesh_tx_beacon_update_enable(int enable)
{
    mesh_tx_ctx_t *ctx = mesh_tx_ctx_get();

    if(enable){
        ctx->tx_enable = 1;
    } else {
        ctx->tx_enable = 0;
    }
}

void mesh_tx_init(unsigned char *own_mac)
{
    mesh_tx_ctx_t *ctx = mesh_tx_ctx_get();
    int i;

    memset(ctx, 0, sizeof(mesh_tx_ctx_t));
    memcpy(ctx->own_mac, own_mac, MESH_MAC_AADR_LEN);
    for(i = 0; i < MESH_OUI_TPYE_MAX; i++) {
        ctx->beacon_data[i].data = &ctx->data_buff[i][0];
    }
}

