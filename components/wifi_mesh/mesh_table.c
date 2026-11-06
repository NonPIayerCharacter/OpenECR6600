#include "mesh_table.h"
#include "mesh_utils.h"
#include "mesh_aes.h"
#include "mesh_storage.h"
#include "mesh_timer.h"
#include "mesh_config.h"
#include "mesh_calc_rssi.h"

typedef struct mesh_table_ctx
{
    struct dl_list notity_list;
    struct dl_list minor_notity_list;
    struct dl_list ap_list;
    struct dl_list black_list;
    unsigned char own_mac[MESH_MAC_AADR_LEN];
    mesh_network_cfg_t network_cfg;
    mesh_node_t parent_node;
    os_mutex_handle_t table_mutex;
    mesh_table_notity_handler_t notity;
}mesh_table_ctx_t;

static mesh_table_ctx_t mesh_table;

__INLINE mesh_table_ctx_t *mesh_table_get(void)
{
    return &mesh_table;
}

void mesh_table_notity_register_handler(mesh_table_notity_handler_t handler)
{
    mesh_table_ctx_t *ctx = mesh_table_get();

    ctx->notity = handler;
}

static void mesh_table_notity_handler(int event, void *data, int len)
{
    mesh_table_ctx_t *ctx = mesh_table_get();

    if (ctx->notity) {
        ctx->notity(event, data, len);
    }
}

void mesh_table_init(mesh_table_notity_handler_t handler)
{
    mesh_table_ctx_t *ctx = mesh_table_get();

    memset(ctx, 0, sizeof(mesh_table_ctx_t));

    dl_list_init(&ctx->notity_list);
    dl_list_init(&ctx->ap_list);
    dl_list_init(&ctx->black_list);
    ctx->table_mutex = os_mutex_create();

    if(wifi_mesh_network_cfg_read((char *)&ctx->network_cfg, sizeof(ctx->network_cfg))) {
        memset(&ctx->network_cfg, 0, sizeof(ctx->network_cfg));
    }

    ctx->network_cfg.switch_count_down = 0;
    ctx->network_cfg.broadcast_count_down = 0;

    if(wifi_mesh_parent_node_read((char *)&ctx->parent_node, sizeof(mesh_node_t))) {
        memset(&ctx->parent_node, 0, sizeof(ctx->parent_node));
    }

    mesh_table_notity_register_handler(handler);
}
void mesh_table_deinit(void)
{
    mesh_table_ctx_t *ctx = mesh_table_get();

    mesh_table_notitylist_reset();
    mesh_table_aplist_reset();
    os_mutex_destroy(ctx->table_mutex);
}

void mesh_table_refresh(void)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_aplist_t *aptemp,*apprev;
    mesh_node_list_t *nodetemp, *nodeprev;
  
    os_mutex_lock(ctx->table_mutex, WAIT_FOREVER);

    dl_list_for_each_safe(aptemp, apprev, &ctx->ap_list,
    mesh_aplist_t, list) {
        if (os_time_get() > aptemp->timestamp + MESH_TABLE_APLIST_AGING_PERIOD) {
            dl_list_del(&aptemp->list);
            os_free(aptemp);
        }
    }

    dl_list_for_each_safe(nodetemp, nodeprev, &ctx->notity_list,
    mesh_node_list_t, list) {
        if (os_time_get() > nodetemp->node.timestamp + MESH_TABLE_NOTITY_AGING_PERIOD) {
            dl_list_del(&nodetemp->list);
            os_free(nodetemp);
        }
    }

    os_mutex_unlock(ctx->table_mutex);
}

void mesh_table_notity_list_refresh(void)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_node_list_t *nodetemp, *nodeprev;
  
    os_mutex_lock(ctx->table_mutex, WAIT_FOREVER);

    dl_list_for_each_safe(nodetemp, nodeprev, &ctx->notity_list,
    mesh_node_list_t, list) {
        if (os_time_get() > nodetemp->node.timestamp + MESH_TABLE_NOTITY_AGING_PERIOD) {
            dl_list_del(&nodetemp->list);
            os_free(nodetemp);
        }
    }

    os_mutex_unlock(ctx->table_mutex);
}

mesh_aplist_t * mesh_aplist_bssid_search(unsigned char *bssid)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_aplist_t *temp;
    unsigned char *select_bssid;

    if (bssid) {
        select_bssid = bssid;
    } else {
        select_bssid = ctx->network_cfg.to_bssid;
    }

    dl_list_for_each(temp, &ctx->ap_list,
        mesh_aplist_t, list) {
        if (memcmp(select_bssid, temp->node.bssid, MESH_MAC_AADR_LEN) == 0) {
            return temp;
        }
    }

    return NULL;
}

static void __mesh_table_aplist_update(void *env, unsigned long long tsf, unsigned char *ssid, unsigned char *bssid, signed char rssi, unsigned char channel)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_aplist_t *ap_info,*temp;
    mesh_aplist_t *prev_res = NULL;

    os_mutex_lock(ctx->table_mutex, WAIT_FOREVER);
    dl_list_for_each(temp, &ctx->ap_list,
        mesh_aplist_t, list) {
        if (memcmp(bssid, temp->node.bssid, MESH_MAC_AADR_LEN) == 0) {
            prev_res = temp;
            break;
        }
    }

    if (!prev_res) {
        ap_info = os_zalloc(sizeof(mesh_aplist_t));
        if (!ap_info) {
            os_mutex_unlock(ctx->table_mutex);
            mesh_e("%s l:%d malloc fail", __func__, __LINE__);
            return ;
        }
        strcpy((char *)ap_info->node.ssid, (char *)ssid);
        memcpy(ap_info->node.bssid, bssid, MESH_MAC_AADR_LEN);
        ap_info->node.rssi = rssi;
        ap_info->node.channel = channel;
        ap_info->node.tsf = tsf;
        dl_list_add(&ctx->ap_list, &ap_info->list);
    } else {
        prev_res->node.rssi = rssi;
        prev_res->node.channel = channel;
        prev_res->node.tsf = tsf;
    }
    os_mutex_unlock(ctx->table_mutex);
}

void mesh_table_aplist_update(void *env, void *buf, int len, signed char rssi)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)buf;
    unsigned char ssid[33] = {0};
    unsigned char channel;
    unsigned long long *tsf;
    mesh_notity_t ap_node;
    int result, fc;

    fc = le_to_host16(mgmt->frame_control);
    if (WLAN_FC_GET_STYPE(fc) != WLAN_FC_STYPE_BEACON && WLAN_FC_GET_STYPE(fc) != WLAN_FC_STYPE_PROBE_RESP){
        return;
    }

    if (memcmp(mgmt->bssid, ctx->network_cfg.to_bssid, MESH_ID_LEN)) {
        return;
    }

    if (WLAN_FC_GET_STYPE(mgmt->frame_control) == WLAN_FC_STYPE_BEACON) {
        tsf = (unsigned long long *)mgmt->u.beacon.timestamp;
    } else {
        tsf = (unsigned long long *)mgmt->u.probe_resp.timestamp;
    }

    channel = mesh_get_bss_channel((unsigned char *)mgmt, len);
    if(channel < 1 || channel > 14) {
        mesh_e("mesh_table_aplist_update channel  error:%d  bssid:"MESH_MAC_STR" ",channel , MESH_MAC_VALUE(mgmt->bssid));
        return;
    }
    result = mesh_get_ssid((unsigned char *)mgmt, len, ssid);
    if (result < 0) {
        return ;
    }

    memset(&ap_node, 0, sizeof(mesh_notity_t));
    memcpy(ap_node.major_id, mesh_config_get_major_id(), MESH_ID_LEN);
    ap_node.link_rssi = rssi;
    ap_node.node_layer = 0;
    ap_node.node_status = MESH_CONNETED_STATE;
    ap_node.join_status    = MESH_NODE_CONNECT_AP;
    mesh_table_notitylist_update(env, &ap_node, sizeof(ap_node), channel, mgmt->bssid, rssi);

    __mesh_table_aplist_update(env, *tsf, ssid, mgmt->bssid, rssi, channel);
}

void mesh_table_aplist_dump(void)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_aplist_t *temp;

    mesh_i("%s :%d", __func__, dl_list_len(&ctx->ap_list));
    os_mutex_lock(ctx->table_mutex, WAIT_FOREVER);
    dl_list_for_each(temp, &ctx->ap_list,
        mesh_aplist_t, list) {
        mesh_i("ssid:%s bssid:"MESH_MAC_STR" ch:%d rssi:%d", temp->node.ssid, MESH_MAC_VALUE(temp->node.bssid), temp->node.channel,temp->node.rssi);
    }
    os_mutex_unlock(ctx->table_mutex);
}

void mesh_table_aplist_reset(void)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_aplist_t *temp, *prev;

    os_mutex_lock(ctx->table_mutex, WAIT_FOREVER);
    dl_list_for_each_safe(temp, prev, &ctx->ap_list,
        mesh_aplist_t, list) {
        dl_list_del(&temp->list);
        os_free(temp);
    }
    os_mutex_unlock(ctx->table_mutex);
}

void mesh_table_notitylist_update(void *env, void *data, int len, unsigned char channel, unsigned char *bssid, signed char rssi)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_notity_t *mesh_notity;
    mesh_node_list_t *node_info, *temp, *prev_temp, *prev_res = NULL;
    struct dl_list *notity_list;
    int add = 0;

    if (len < sizeof(mesh_notity_t)) {
        return;
    }

    mesh_notity = (mesh_notity_t *)data;
    if(memcmp(mesh_notity->major_id, mesh_config_get_major_id(), MESH_ID_LEN)) {
        //mesh_e("%s major_id mismatch", __func__);
        return;
    }

    /*if (memcmp(mesh_notity->minor_id, ctx->parent_node.notity.minor_id, MESH_ID_LEN) == 0) {
        notity_list = &ctx->minor_notity_list;
    } else */{
        notity_list = &ctx->notity_list;
    }

    os_mutex_lock(ctx->table_mutex, WAIT_FOREVER);
    dl_list_for_each_safe(temp, prev_temp, notity_list,
        mesh_node_list_t, list) {
        if (memcmp(bssid, temp->node.node_mac, MESH_MAC_AADR_LEN) == 0) {
            prev_res = temp;
            dl_list_del(&prev_res->list);
            break;
        }
    }

    if (prev_res) {
        node_info = prev_res;
    } else {
        node_info = (mesh_node_list_t *)os_zalloc(sizeof(mesh_node_list_t));
        if (!node_info) {
            os_mutex_unlock(ctx->table_mutex);
            mesh_e("%s malloc fail", __func__);
            return;
        }
    }
    
    memcpy(&node_info->node.notity, mesh_notity, sizeof(mesh_notity_t));
    node_info->node.timestamp = os_time_get();
    memcpy(node_info->node.node_mac, bssid, MESH_MAC_AADR_LEN);
    node_info->node.channel = channel;
    mesh_average_rssi(&node_info->node.rssi_record, rssi);
    node_info->node.link_rssi = mesh_calc_link_rssi(mesh_notity->link_rssi, node_info->node.rssi_record.average, mesh_notity->node_layer, mesh_notity->join_status&0x80);

    dl_list_for_each(temp, notity_list, mesh_node_list_t, list) {
        if (((node_info->node.link_rssi == temp->node.link_rssi) && (node_info->node.notity.node_layer < temp->node.notity.node_layer))
            || (node_info->node.link_rssi > temp->node.link_rssi)) {
            dl_list_add(temp->list.prev, &node_info->list);
            add = 1;
            break;
        }
    }

    if (!add) {
        dl_list_add_tail(notity_list, &node_info->list);
    }
    os_mutex_unlock(ctx->table_mutex);
}

mesh_node_list_t * mesh_notitylist_search_by_mac(unsigned char *mac)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_node_list_t *temp = NULL;

    os_mutex_lock(ctx->table_mutex, WAIT_FOREVER);
    dl_list_for_each(temp, &ctx->notity_list,
        mesh_node_list_t, list) {
        if (memcmp(mac, temp->node.node_mac, MESH_MAC_AADR_LEN) == 0) {
			 os_mutex_unlock(ctx->table_mutex);
            return temp;
        }
    }
    os_mutex_unlock(ctx->table_mutex);

    return NULL;
}

void mesh_table_notitylist_reset(void)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_node_list_t *nodetemp, *nodeprev;

    os_mutex_lock(ctx->table_mutex, WAIT_FOREVER);
    dl_list_for_each_safe(nodetemp, nodeprev, &ctx->notity_list,
    mesh_node_list_t, list) {
        dl_list_del(&nodetemp->list);
        os_free(nodetemp);
    }
    os_mutex_unlock(ctx->table_mutex);
}

mesh_node_list_t * mesh_notitylist_best_node_get(void)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_node_list_t *temp;
    int find = 0;

    os_mutex_lock(ctx->table_mutex, WAIT_FOREVER);
    dl_list_for_each(temp, &ctx->notity_list,
        mesh_node_list_t, list) {
        if (!temp->node.disallow && temp->node.notity.node_status == MESH_CONNETED_STATE
                && MESH_NODE_IS_ENABLE_JOIN(temp->node.notity.join_status)
                && MESH_NODE_IS_CONNECT_AP(temp->node.notity.join_status)) {
            find = 1;
            break;
        }
    }
    os_mutex_unlock(ctx->table_mutex);

    return find ? temp:NULL;
}

int mesh_notitylist_set_allow(unsigned char *mac, unsigned char disallow)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_node_list_t *temp;

    os_mutex_lock(ctx->table_mutex, WAIT_FOREVER);
    dl_list_for_each(temp, &ctx->notity_list,
        mesh_node_list_t, list) {
        if (memcmp(mac, temp->node.node_mac, MESH_MAC_AADR_LEN) == 0) {
            temp->node.disallow = disallow;
            break;
        }
    }
    os_mutex_unlock(ctx->table_mutex);

    return -1;
}

void mesh_table_notitylist_node_dump(mesh_node_list_t *temp)
{
    mesh_table_ctx_t *ctx = mesh_table_get();

    os_mutex_lock(ctx->table_mutex, WAIT_FOREVER);
    mesh_i("tsf:0x%08x, bssid:"MESH_MAC_STR" parent:"MESH_MAC_STR" link_rssi:%d layer:%d rssi:%d status:%d disallow:%d ip:%08x join[cnt:%d dis:%d co:%d]",
        (int)temp->node.timestamp, MESH_MAC_VALUE(temp->node.node_mac), MESH_MAC_VALUE(temp->node.notity.parent),
        temp->node.link_rssi,temp->node.notity.node_layer,temp->node.notity.link_rssi, temp->node.notity.node_status, temp->node.disallow,temp->node.notity.node_ip,
        temp->node.notity.join_status & MESH_MODE_NODE_JOIN_CNT_MASK, (temp->node.notity.join_status & MESH_NODE_DISABLE_JOIN) >> 7, MESH_NODE_IS_CONNECT_AP(temp->node.notity.join_status));
    os_mutex_unlock(ctx->table_mutex);
}

void mesh_table_notitylist_dump(int cnt)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_node_list_t *temp;
    int idx = 0;

    mesh_i("%s :%d", __func__,dl_list_len(&ctx->notity_list));
    os_mutex_lock(ctx->table_mutex, WAIT_FOREVER);
    dl_list_for_each(temp, &ctx->notity_list,
        mesh_node_list_t, list) {
        mesh_table_notitylist_node_dump(temp);
        idx++;
        if (idx > cnt) {
            break;
        }
    }
    os_mutex_unlock(ctx->table_mutex);
}

void mesh_table_network_cfg_update(void *env, void *data, int len, unsigned char channel, unsigned char *bssid, signed char rssi)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_network_cfg_t *network_cfg;
    unsigned char passwd[WIFI_PWD_MAX_LEN];
    int event;

    if (len < (sizeof(mesh_network_cfg_t) - WIFI_PWD_MAX_LEN)) {
        mesh_i("%s len small\r\n", __func__);
        return;
    }
#if 0
    network_cfg = (mesh_network_cfg_t *)data;
    memset(passwd, 0, sizeof(passwd));
    memcpy(passwd, network_cfg->passwd, network_cfg->passwd_len);
    if (memcmp(network_cfg, &ctx->network_cfg, sizeof(mesh_network_cfg_t) - WIFI_PWD_MAX_LEN) == 0
        && memcmp(passwd, ctx->network_cfg.passwd, WIFI_PWD_MAX_LEN) == 0) {
        //mesh_i("%s network_cfg same\r\n", __func__);
        return;
    }
    if (memcmp(ctx->major_id, MESH_ZERO_MAC, MESH_ID_LEN) == 0) {
        // frist cfg ssid pwd;
        if (memcmp(network_cfg->from_bssid, MESH_ZERO_MAC, MESH_ID_LEN) == 0
            || (memcmp(network_cfg->from_bssid, MESH_BROADCAST_MAC, MESH_ID_LEN) == 0)) {
            if(wifi_mesh_network_cfg_write((char *)network_cfg, sizeof(mesh_network_cfg_t))) {
                memset(&ctx->network_cfg, 0, sizeof(ctx->network_cfg));
                mesh_i("%s write cfg to flash fail\r\n", __func__);
                return;
            }else {
                mesh_i("%s %d %d\r\n", __func__,network_cfg->timestamp, network_cfg->broadcast_count_down, network_cfg->switch_count_down);
            }

            memset(&ctx->network_cfg, 0, sizeof(ctx->network_cfg));
            memcpy(&ctx->network_cfg, network_cfg, sizeof(ctx->network_cfg) -WIFI_PWD_MAX_LEN + network_cfg->passwd_len);
            memcpy(ctx->major_id, &ctx->network_cfg.to_bssid, MESH_ID_LEN);
            mesh_table_notity_handler(MESH_TABLE_NETWOTK_CFG_ADD, network_cfg, sizeof(ctx->network_cfg));
        }
    } else {
        if((network_cfg->timestamp > ctx->network_cfg.timestamp) && ((memcmp(network_cfg->from_bssid, MESH_BROADCAST_MAC, MESH_ID_LEN) == 0)
            || memcmp(network_cfg->from_bssid, ctx->major_id, MESH_ID_LEN) == 0)) {
                if(wifi_mesh_network_cfg_write((char *)network_cfg, sizeof(mesh_network_cfg_t))) {
                    memset(&ctx->network_cfg, 0, sizeof(ctx->network_cfg));
                    mesh_e("%s write cfg to flash fail\r\n", __func__);
                    return;
                } else {
                    mesh_i("%s %d %d\r\n", __func__,network_cfg->timestamp, network_cfg->broadcast_count_down, network_cfg->switch_count_down);
                }

                memset(&ctx->network_cfg, 0, sizeof(ctx->network_cfg));
                memcpy(&ctx->network_cfg, network_cfg, sizeof(ctx->network_cfg) -WIFI_PWD_MAX_LEN + network_cfg->passwd_len);

                memcpy(ctx->major_id, &ctx->network_cfg.to_bssid, MESH_ID_LEN);
                mesh_table_notity_handler(MESH_TABLE_NETWOTK_CFG_UPATE, network_cfg, sizeof(ctx->network_cfg));
        }
    }
#else
    network_cfg = (mesh_network_cfg_t *)data;
    if(network_cfg->timestamp <= ctx->network_cfg.timestamp) {
        //mesh_i("%s network_cfg old\r\n", __func__);
        return;
    }

    memset(passwd, 0, sizeof(passwd));
    memcpy(passwd, network_cfg->passwd, network_cfg->passwd_len);
    if ((memcmp(network_cfg,  &ctx->network_cfg, sizeof(mesh_network_cfg_t) - WIFI_PWD_MAX_LEN) == 0)
        && (memcmp(passwd, ctx->network_cfg.passwd, WIFI_PWD_MAX_LEN) == 0)) {
        //mesh_i("%s network_cfg same\r\n", __func__);
        return;
    }

    if((memcmp(ctx->network_cfg.from_bssid, MESH_ZERO_MAC, MESH_ID_LEN) == 0)
        && (memcmp(ctx->network_cfg.to_bssid, MESH_ZERO_MAC, MESH_ID_LEN) == 0)) {
        event = MESH_TABLE_NETWOTK_CFG_ADD;
    }else if ( (memcmp(network_cfg->from_bssid, ctx->network_cfg.to_bssid, MESH_ID_LEN) == 0)
        || (memcmp(network_cfg->to_bssid, ctx->network_cfg.to_bssid, MESH_ID_LEN) == 0)
        || (memcmp(network_cfg->from_bssid, MESH_BROADCAST_MAC, MESH_ID_LEN) == 0)){
        event = MESH_TABLE_NETWOTK_CFG_UPATE;
    } else {
        return ;
    }

     if(wifi_mesh_network_cfg_write((char *)network_cfg, sizeof(mesh_network_cfg_t))) {
         memset(&ctx->network_cfg, 0, sizeof(ctx->network_cfg));
         mesh_i("%s write cfg to flash fail\r\n", __func__);
         return;
     }else {
         mesh_i("%s %d %d %d\r\n", __func__,network_cfg->timestamp, network_cfg->broadcast_count_down, network_cfg->switch_count_down);
     }

     memset(&ctx->network_cfg, 0, sizeof(ctx->network_cfg));
     memcpy(&ctx->network_cfg, network_cfg, sizeof(ctx->network_cfg) -WIFI_PWD_MAX_LEN + network_cfg->passwd_len);
     mesh_config_bssid_as_major_id(ctx->network_cfg.to_bssid);
     mesh_table_notity_handler(event, network_cfg, sizeof(ctx->network_cfg));
#endif
}

mesh_network_cfg_t *mesh_table_network_cfg_get(void)
{
    mesh_table_ctx_t *ctx = mesh_table_get();

    return &ctx->network_cfg;
}
mesh_node_t *mesh_table_parent_node_get(void)
{
    mesh_table_ctx_t *ctx = mesh_table_get();

    return &ctx->parent_node;
}

int mesh_table_parent_node_set(mesh_node_t *node)
{
    mesh_table_ctx_t *ctx = mesh_table_get();

    if (memcmp(node , &ctx->parent_node, sizeof(mesh_node_t)) == 0) {
        mesh_e("%s unchange!!!", __func__);
        return -1;
    }

    memcpy(&ctx->parent_node, node, sizeof(mesh_node_t));
    if(wifi_mesh_parent_node_write((char *)&ctx->parent_node, sizeof(mesh_node_t))) {
        memset(&ctx->parent_node, 0, sizeof(ctx->parent_node));
    }

    return 0;
}

void mesh_table_blacklist_refresh(void)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_blacklist_t *temp, *prev;

    os_mutex_lock(ctx->table_mutex, WAIT_FOREVER);
    dl_list_for_each_safe(temp, prev, &ctx->black_list,
        mesh_blacklist_t, list) {
        if (temp->timestamp < os_time_get()) {
            mesh_notitylist_set_allow(temp->mac, 0);
            dl_list_del(&temp->list);
            os_free(temp);
        }
    }
    os_mutex_unlock(ctx->table_mutex);
}

int mesh_table_blacklist_add(unsigned char *mac, unsigned del_timems)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_blacklist_t *temp, *prev = NULL;

    if(!mac || memcmp(mac, MESH_ZERO_MAC, MESH_MAC_AADR_LEN) == 0) {
        return 0;
    }

    os_mutex_lock(ctx->table_mutex, WAIT_FOREVER);
    dl_list_for_each(temp, &ctx->black_list,
        mesh_blacklist_t, list) {
        if (memcmp(mac, temp->mac, MESH_MAC_AADR_LEN) == 0) {
            temp->timestamp = os_time_get() + del_timems;
            prev = temp;
            break;
        }
    }

    if (!prev) {
        prev = (mesh_blacklist_t *)os_zalloc(sizeof(mesh_blacklist_t));
        if (!prev) {
            os_mutex_unlock(ctx->table_mutex);
            mesh_e("%s l:%d malloc error", __func__, __LINE__);
            return -1;
        }
        memcpy(prev->mac, mac, MESH_MAC_AADR_LEN);
        prev->timestamp = os_time_get() + del_timems;
        dl_list_add_tail(&ctx->black_list, &prev->list);
    }

    mesh_notitylist_set_allow(mac, 1);

    os_mutex_unlock(ctx->table_mutex);

    return 0;
}


int mesh_table_blacklist_del(unsigned char *mac)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_blacklist_t *temp, *prev;

    os_mutex_lock(ctx->table_mutex, WAIT_FOREVER);
    dl_list_for_each_safe(temp, prev, &ctx->black_list,
        mesh_blacklist_t, list) {
        if(memcmp(mac, MESH_BROADCAST_MAC, MESH_MAC_AADR_LEN) == 0) {
            mesh_notitylist_set_allow(temp->mac, 0);
            dl_list_del(&temp->list);
            os_free(temp);
        } else {
            if (memcmp(mac, temp->mac, MESH_MAC_AADR_LEN) == 0) {
                mesh_notitylist_set_allow(temp->mac, 0);
                dl_list_del(&temp->list);
                os_free(temp);
                break;
            }
        }
    }

    os_mutex_unlock(ctx->table_mutex);
    return 0;
}

void mesh_table_network_cfg_dump(void)
{
    mesh_table_ctx_t *ctx = mesh_table_get();
    mesh_i("netweork cfg:broadcastdown:%d switch_down:%d " MESH_MAC_STR"", ctx->network_cfg.broadcast_count_down,ctx->network_cfg.switch_count_down, MESH_MAC_VALUE(ctx->network_cfg.to_bssid));
}




