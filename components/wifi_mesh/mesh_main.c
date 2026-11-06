#include "system_wifi.h"
#include "oshal.h"
#include "wifi_mesh.h"
#include "mesh_common.h"
#include "mesh_table.h"
#include "mesh_timer.h"
#include "mesh_rx.h"
#include "mesh_tx.h"
#include "mesh_utils.h"
#include "mesh_storage.h"
#include "mesh_net_cfg.h"
#include "mesh_config.h"
#include "mesh_vote.h"

#define  MESH_TASK_STACK         (4096)
#define  MESH_CFG_TASK_STACK         (3072)
#define  MESH_TASK_PRI           (3)

typedef struct wifi_mesh_ctx
{
    mesh_network_cfg_t *network_cfg;
    mesh_network_cfg_t curr_network_cfg;
    mesh_node_t *parent_node;
    mesh_node_t select_node;
    wifi_mesh_cfg_t mesh_cfg;
    mesh_notity_t mesh_notity;
    unsigned char sta_max_cnt;
    unsigned char join_sta_cnt;
    unsigned char cfg_chan2G4_cnt;
    unsigned char find_chan2G4_cnt;
    unsigned char chan2G4_ch_idx;
    unsigned char lock_chan2G4_channel;
    unsigned char *cfg_channel_list;
    unsigned char *find_channel_list;
    unsigned char own_mac[MESH_MAC_AADR_LEN];
    unsigned int machine_state:4;
    unsigned int init_done:1;
    unsigned int start_done:1;
    unsigned int apcfg_exit:1;
    unsigned int network_cfg_send:1;
    unsigned int notity_send:1;
    unsigned int cfg_send:1;
    unsigned int is_ap_start:1;
    unsigned int is_connected:1;
    unsigned int delay_join_timems;
    unsigned int beacon_interval;
    int mesh_task_handle;
    int cfg_task_handle;
    mesh_notity_handler_t notity;
}wifi_mesh_ctx_t;

static wifi_mesh_ctx_t mesh_ctx;
static unsigned char cfg_channel_list[] = {1,2,6,3,4,6,4,5,6,7,8,6,9,10,6,11,12,6,13,14,6};
static unsigned char find_channel_list[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14};

void mesh_machine_state_set(wifi_mesh_ctx_t *ctx, unsigned int mesh_state);
int mesh_machine_state_get(void);
int mesh_machine_handle(wifi_mesh_ctx_t *ctx);
void mesh_machine_join_timeout_cb(void *env, void *arg);
void mesh_connect_done_cb(void *env, void *arg);
int mesh_connect_timeout_next(wifi_mesh_ctx_t *ctx);
void mesh_machine_vote_delay_join(void *env, void *arg);
void mesh_machine_ap_timeout_allow(void *env, void *arg);
void mesh_refresh_timeout_cb(void *env, void *arg);
void mesh_machine_reconnect_delay_join(void *env, void *arg);
void mesh_channel_unlock_cb(void *env, void *arg);
void mesh_softap_channel_sync(void *env, void *arg);
void mesh_cfg_notity_handle(unsigned char *from_bssid, unsigned char *to_bssid, unsigned char *key, unsigned int timestamp);
void mesh_blacklist_refresh_timeout_cb(void *env, void *arg);

__INLINE wifi_mesh_ctx_t *wifi_mesh_get_ctx(void)
{
    return &mesh_ctx;
}

int wifi_mesh_notity_handler_register(mesh_notity_handler_t notity)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();

    if (ctx->init_done) {
        ctx->notity = notity;
        return 0;
    }

    return -1;
}

void wifi_mesh_notity_handle(int mesh_state, void *data)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();
    
    if (ctx->notity) {
        ctx->notity(mesh_state, data);
    }
}

void wifi_mesh_beacon_interval_set(int val)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();

    if (ctx->beacon_interval != val) {
        ctx->beacon_interval = val;
        wifi_set_ap_beacon_interval(ctx->beacon_interval);
    }
}

void mesh_blacklist_refresh_timeout_cb(void *env, void *arg)
{
    mesh_table_blacklist_refresh();
    mesh_timer_set(mesh_blacklist_refresh_timeout_cb, NULL, MESH_NODE_BLACKLIST_REFRESH_PERIOD);
}

void mesh_wifi_sta_config_static_ip(unsigned int ip_prefix)
{
    struct ip_info ipconfig;
    unsigned char ip1, ip2, ip3,ip4;
    unsigned char bssid[MESH_MAC_AADR_LEN];
    unsigned short aid;

    if (fhost_get_sta_aid_bssid(bssid, &aid)) {
        return;
    }

    ip1 = (ip_prefix >> 24) & 0xff;
    ip2 = (ip_prefix >> 16) & 0xff;
    ip3 = (ip_prefix >> 8) & 0xff;
    ip4 = aid + MESH_STA_IP_AID_OFFSET;

    mesh_i("config static ip addr:%d.%d.%d.%d", ip1, ip2, ip3, ip4);
    memset(&ipconfig, 0, sizeof(ipconfig));
#ifdef CONFIG_IPV6
    IP4_ADDR(&ipconfig.ip.u_addr.ip4, ip1, ip2, ip3, ip4);
    IP4_ADDR(&ipconfig.gw.u_addr.ip4, ip1, ip2, ip3, 1);
    IP4_ADDR(&ipconfig.netmask.u_addr.ip4, 255, 255, 255, 0);
    IP4_ADDR(&ipconfig.dns1.u_addr.ip4, 114, 114, 114, 114);
    IP4_ADDR(&ipconfig.dns2.u_addr.ip4, 8, 8, 8, 8);
#else
    IP4_ADDR(&ipconfig.ip, ip1, ip2, ip3, ip4);
    IP4_ADDR(&ipconfig.gw, ip1, ip2, ip3, 1);
    IP4_ADDR(&ipconfig.netmask, 255, 255, 255, 0);
    IP4_ADDR(&ipconfig.dns1, 114, 114, 114, 114);
    IP4_ADDR(&ipconfig.dns2, 8, 8, 8, 8);
#endif

    //wifi_station_dhcpc_stop(STATION_IF);
    set_sta_ipconfig(&ipconfig);
    //wifi_station_dhcpc_start(STATION_IF);
}

int mesh_softap_dhcps_config(wifi_mesh_ctx_t *ctx)
{
    struct ip_info ipconfig;
    unsigned long random_ip = os_random() &0xffff;
    unsigned char ip1 = 10, ip2, ip3;

    random_ip = os_random();
    random_ip = (random_ip & 0x00FFFFFF) | (ip1 << 24);
    if ((random_ip & 0xffffff00) != (ctx->mesh_notity.node_ip & 0xffffff00)) {
        ip2 = (random_ip >> 16)& 0xff;
        ip3 = (random_ip >> 8) & 0xff;
    } else {
        ip2 = ((random_ip >> 16)& 0xff) + 1;
        ip3 = (random_ip >> 8) & 0xff;
    }


    wifi_softap_dhcps_stop(SOFTAP_IF);

    memset(&ipconfig, 0, sizeof(ipconfig));
#ifdef CONFIG_IPV6
    IP4_ADDR(&ipconfig.ip.u_addr.ip4, ip1, ip2, ip3, 1);
    IP4_ADDR(&ipconfig.gw.u_addr.ip4, ip1, ip2, ip3, 1);
    IP4_ADDR(&ipconfig.netmask.u_addr.ip4, 255, 255, 255, 0);
#else
    IP4_ADDR(&ipconfig.ip, ip1, ip2, ip3, 1);
    IP4_ADDR(&ipconfig.gw, ip1, ip2, ip3, 1);
    IP4_ADDR(&ipconfig.netmask, 255, 255, 255, 0);
#endif

#ifdef CONFIG_IPV6
     ctx->mesh_notity.node_ip = ntohl(ip4_addr_get_u32(&ipconfig.ip.u_addr.ip4));
#else
     ctx->mesh_notity.node_ip = ntohl(ip4_addr_get_u32(&ipconfig.ip));
#endif

    os_printf(LM_APP, LL_INFO, "config softap ip addr:%d.%d.%d.1\n", ip1, ip2, ip3);

    set_softap_ipconfig(&ipconfig);
    wifi_softap_dhcps_start(SOFTAP_IF);

    return 0;
}

int mesh_softap_dhcps_reconfig(wifi_mesh_ctx_t *ctx)
{
    struct ip_info info;
    unsigned int node_ip;

    wifi_get_ip_info(STATION_IF, &info);
#ifdef CONFIG_IPV6
    node_ip = ntohl(ip4_addr_get_u32(&info.ip.u_addr.ip4));
#else
    node_ip = ntohl(ip4_addr_get_u32(&info.ip));
#endif

    if ((node_ip & 0xffffff00) == (ctx->mesh_notity.node_ip & 0xffffff00)) {
        mesh_softap_dhcps_config(ctx);
    }

    return 0;
    
}
int mesh_node_broadcast_data_compare(mesh_notity_t *old, mesh_notity_t *new)
{
#if 1
    if (memcmp(old, new, sizeof(mesh_notity_t))) {
        return -1;
    }

    return 0;
#else
    if (memcmp(old->major_id, new->major_id, MESH_ID_LEN) || memcmp(old->minor_id, new->minor_id, MESH_ID_LEN)
        || memcmp(old->parent, new->parent, MESH_ID_LEN)) {
        return -1;
    }

    if (old->join_status != new->join_status || old->node_layer != new->node_layer 
        || old->node_status != new->node_status || old->node_ip != new->node_ip) {
        return -1;
    }

    if (ABS(old->link_rssi -new->link_rssi) > 1) {
        return -1;
    }

    return 0;
#endif
}
int mesh_node_broadcast_data_get(wifi_mesh_ctx_t *ctx, mesh_notity_t *notity)
{
    struct ip_info info;
    unsigned int  node_ip;
    mesh_node_list_t *node_info;

    node_info = mesh_notitylist_search_by_mac(ctx->select_node.node_mac);
    if (node_info) {
        if(mesh_machine_state_get() == MESH_CONNETED_STATE && memcmp(ctx->select_node.node_mac, ctx->network_cfg->to_bssid,MESH_MAC_AADR_LEN)) {
            if ((node_info->node.notity.node_ip & 0xffffff00) != (ctx->select_node.notity.node_ip & 0xffffff00)) {
                mesh_table_notitylist_node_dump(node_info);
                mesh_d("ip change:node_ip:%08x,notity_ip:%08x",node_info->node.notity.node_ip, ctx->select_node.notity.node_ip);
                mesh_wifi_sta_config_static_ip(node_info->node.notity.node_ip);
            }
        }
        memcpy(&ctx->select_node, &node_info->node, sizeof(ctx->select_node));
    }
    
    wifi_get_ip_info(SOFTAP_IF, &info);
    
#ifdef CONFIG_IPV6
        node_ip = ntohl(ip4_addr_get_u32(&info.ip.u_addr.ip4));
#else
        node_ip = ntohl(ip4_addr_get_u32(&info.ip));
#endif

    memset(notity, 0, sizeof(mesh_notity_t));
    
    memcpy(notity->major_id, mesh_config_get_major_id(), MESH_ID_LEN);
    if (memcmp(ctx->select_node.node_mac, ctx->network_cfg->to_bssid, MESH_MAC_AADR_LEN)) {
        memcpy(notity->minor_id, ctx->own_mac, MESH_MAC_AADR_LEN);
    } else {
        memcpy(notity->minor_id, ctx->select_node.notity.minor_id, MESH_MAC_AADR_LEN);
    }

    memcpy(notity->parent, ctx->select_node.node_mac, MESH_MAC_AADR_LEN);
    notity->join_status = ctx->join_sta_cnt | ((ctx->join_sta_cnt == ctx->sta_max_cnt) ? MESH_NODE_DISABLE_JOIN : MESH_NODE_ENABLE_JOIN);
    notity->node_status = mesh_machine_state_get();
    notity->node_ip = node_ip;
    notity->link_rssi = ctx->select_node.link_rssi;
    notity->node_layer = ctx->select_node.notity.node_layer + 1;
    if (notity->node_layer >= mesh_config_max_layer_get()) {
        notity->join_status |= MESH_NODE_DISABLE_JOIN;
    } else {
        notity->join_status |= MESH_NODE_ENABLE_JOIN;
    }

    if (MESH_NODE_IS_CONNECT_AP(ctx->select_node.notity.join_status) && ctx->is_connected) {
        notity->join_status |= MESH_NODE_CONNECT_AP; 
    } else {
        notity->join_status &= !MESH_NODE_CONNECT_AP; 
    }

    return 0;
}

void mesh_node_broadcast_timeout_cb(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = (wifi_mesh_ctx_t *)env;
    mesh_notity_t notity;
    mesh_network_cfg_t *network_cfg;
    int update = 0;

    network_cfg = mesh_table_network_cfg_get();
    if (network_cfg->switch_count_down > 0) {
        ctx->network_cfg_send = 1;
        network_cfg->switch_count_down--;
        if (network_cfg->switch_count_down == 0) {
            mesh_i("5s time is up change to find state");
            wifi_stop_station();
            mesh_machine_state_set(ctx, MESH_FIND_STATE);
        }
        update = 1;
    }

    if (network_cfg->broadcast_count_down > 0) {
        ctx->network_cfg_send = 1;
        network_cfg->broadcast_count_down --;
        update = 1;
        mesh_tx_beacon_update_by_type(MESH_OUI_TPYE_NETWORKCFG, (unsigned char *)mesh_table_network_cfg_get(), sizeof(mesh_network_cfg_t) - (WIFI_PWD_MAX_LEN - mesh_table_network_cfg_get()->passwd_len), 0);
    } else {
        if (ctx->network_cfg_send) {
            mesh_i("stop send network cfg");
            ctx->network_cfg_send = 0;
            update = 1;
            mesh_tx_beacon_update_by_type(MESH_OUI_TPYE_NETWORKCFG, 0, 0, 0);
        }
    }

    if (mesh_config_count_down()) {
        if (mesh_machine_state_get() == MESH_CONNETED_STATE && ctx->beacon_interval != mesh_config_beacon_interval_get()) {
            wifi_mesh_beacon_interval_set(mesh_config_beacon_interval_get());
        }
        ctx->cfg_send = 1;
        update = 1;
        mesh_tx_beacon_update_by_type(MESH_OUI_TPYE_CFG, (unsigned char *)mesh_config_get(), sizeof(mesh_config_t), 0);
    } else {
        if (ctx->cfg_send) {
            mesh_i("stop send mesh cfg");
            ctx->cfg_send = 0;
            update = 1;
            mesh_tx_beacon_update_by_type(MESH_OUI_TPYE_CFG, 0, 0, 0);
        }
    }

    if (network_cfg->switch_count_down || mesh_machine_state_get() == MESH_FIND_STATE || mesh_machine_state_get() == MESH_CFG_STATE ) {
        if (ctx->notity_send) {
            ctx->notity_send = 0;
            update = 1;
            mesh_tx_beacon_update_by_type(MESH_OUI_TPYE_NOTITY, 0, 0, 0);
        }
    } else {
        mesh_node_broadcast_data_get(ctx, &notity);
        if (mesh_node_broadcast_data_compare(&ctx->mesh_notity, &notity)) {
             memcpy(&ctx->mesh_notity, &notity, sizeof(mesh_notity_t));
            ctx->notity_send = 1;
            update = 1;
            mesh_tx_beacon_update_by_type(MESH_OUI_TPYE_NOTITY, (unsigned char *)&ctx->mesh_notity, sizeof(mesh_notity_t), 0);
        }
    }

    if (update) {
        mesh_tx_beacon_update();
    }

    mesh_timer_set(mesh_node_broadcast_timeout_cb, 0, MESH_NODE_BROADCAST_PERIOD);
}

void mesh_reconnect_timeout_cb(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = (wifi_mesh_ctx_t *)env;
    mesh_node_list_t *node_info;

    if (mesh_machine_state_get() != MESH_CONNETED_STATE) { 
        return;
    }
    
    if (ctx->select_node.notity.node_layer == 1) {
        mesh_i("%s root node lost...", __func__);
        node_info = mesh_notitylist_best_node_get();
        if (node_info) {
            mesh_i("best bssid:"MESH_MAC_STR" link rssi:%d layer:%d own link_rssi:%d ", MESH_MAC_VALUE(node_info->node.node_mac),
                node_info->node.notity.link_rssi, node_info->node.notity.node_layer, ctx->mesh_notity.link_rssi);
            if (ctx->mesh_notity.link_rssi >= node_info->node.notity.link_rssi) {
                node_info = mesh_notitylist_search_by_mac(ctx->network_cfg->to_bssid);
                if (node_info) {
                    memset(&ctx->select_node, 0, sizeof(ctx->select_node));
                    memcpy(&ctx->select_node, &node_info->node, sizeof(ctx->select_node));
                    mesh_machine_state_set(ctx, MESH_JOIN_STATE);
                } else {
                    ctx->delay_join_timems = MESH_ROOT_LOST_DELAY_JOIN_TIMEMS;
                    mesh_timer_set(mesh_machine_reconnect_delay_join, ctx, ctx->delay_join_timems);
                }
            } else {
                ctx->delay_join_timems = MESH_ROOT_LOST_DELAY_JOIN_TIMEMS;
                //mesh_notitylist_set_allow(ctx->network_cfg->to_bssid, 1);
                //mesh_timer_set(mesh_machine_ap_timeout_allow, ctx, MESH_DELAY_AP_ALLOW_TIMEMS);
                mesh_table_blacklist_add(ctx->network_cfg->to_bssid, MESH_DELAY_AP_ALLOW_TIMEMS);
                mesh_timer_set(mesh_machine_reconnect_delay_join, ctx, ctx->delay_join_timems);
            }
        } else {
            mesh_machine_state_set(ctx, MESH_JOIN_STATE);
        }
    }else {
        mesh_connect_timeout_next(ctx);
        mesh_machine_state_set(ctx, MESH_JOIN_STATE);
    }
}

void mesh_table_event_cb(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();
    mesh_network_cfg_t *network_cfg =  mesh_table_network_cfg_get();//&mesh_table->table_event_info.network_cfg;
    mesh_i("cb state:%d switch:%d countdown:%d tsf:%d pwd_len:%d pwd:%s fbssid:"MESH_MAC_STR" tbssid:"MESH_MAC_STR"", mesh_machine_state_get(),
            network_cfg->switch_count_down,network_cfg->broadcast_count_down, network_cfg->timestamp,
           network_cfg->passwd_len,network_cfg->passwd, MESH_MAC_VALUE(network_cfg->from_bssid), MESH_MAC_VALUE(network_cfg->to_bssid));

    if (mesh_machine_state_get() != MESH_CFG_STATE) {
        wifi_stop_station();
        mesh_machine_state_set(ctx, MESH_INIT_STATE);
    }
    
    if(network_cfg->switch_count_down == 0) {
        mesh_machine_state_set(ctx, MESH_FIND_STATE);
    }

    mesh_timer_clear(mesh_node_broadcast_timeout_cb);
    mesh_timer_set(mesh_node_broadcast_timeout_cb, 0, MESH_NODE_BROADCAST_PERIOD);
}

void mesh_wifi_event_sta_connected(wifi_mesh_ctx_t *ctx, system_event_t *event)
{
    mesh_node_list_t *node_info;

    if (memcmp(ctx->select_node.node_mac, ctx->network_cfg->to_bssid, MESH_MAC_AADR_LEN)) {
        //mesh_table_notitylist_dump();
        node_info = mesh_notitylist_search_by_mac(ctx->select_node.node_mac);
        if (node_info) {
            mesh_wifi_sta_config_static_ip(node_info->node.notity.node_ip);
        } else {
            mesh_e("%s config ip fail\r\n", __func__);
        }
    }
}

void mesh_wifi_event_cb(wifi_mesh_ctx_t *ctx, system_event_t *event)
{
    mesh_i("%s %d", __func__, event->event_id);
    if (!ctx->init_done) {
        return ;
    }

    switch(event->event_id){
        case SYSTEM_EVENT_AP_STACONNECTED:
            ctx->join_sta_cnt++;
            if (mesh_machine_state_get() == MESH_CFG_STATE) {
                mesh_timer_clear(mesh_channel_unlock_cb);
                mesh_timer_set(mesh_channel_unlock_cb, NULL, MESH_SOFTAP_UNLOCK_CHANNEL_TIMEMS);
            } else if (mesh_machine_state_get() == MESH_CONNETED_STATE) {
                mesh_timer_clear(mesh_node_broadcast_timeout_cb);
                mesh_timer_set(mesh_node_broadcast_timeout_cb, 0, 0);
            }
            break;
        case SYSTEM_EVENT_AP_STADISCONNECTED:
            ctx->join_sta_cnt--;
            if (mesh_machine_state_get() == MESH_CFG_STATE && ctx->join_sta_cnt == 0) {
                mesh_timer_clear(mesh_channel_unlock_cb);
                mesh_timer_set(mesh_channel_unlock_cb, NULL, 0);
            } else if (mesh_machine_state_get() == MESH_CONNETED_STATE) {
                mesh_timer_clear(mesh_node_broadcast_timeout_cb);
                mesh_timer_set(mesh_node_broadcast_timeout_cb, 0, 0);
            }
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            mesh_wifi_event_sta_connected(ctx, event);
            mesh_timer_clear(mesh_softap_channel_sync);
            mesh_timer_set(mesh_softap_channel_sync, ctx, 0);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            ctx->is_connected = 0;
            mesh_timer_clear(mesh_reconnect_timeout_cb);
            mesh_timer_set(mesh_reconnect_timeout_cb, ctx, MESH_MOVE_CHECK_PERIOD);
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            ctx->is_connected = 1;
            mesh_timer_clear(mesh_reconnect_timeout_cb);
            mesh_timer_clear(mesh_connect_done_cb);
            mesh_timer_set(mesh_connect_done_cb, ctx, 0);
            break;
        case SYSTEM_EVENT_AP_START:
            ctx->is_ap_start = 1;
            mesh_softap_dhcps_config(ctx);
            mesh_tx_beacon_update_enable(1);
            break;
        case SYSTEM_EVENT_AP_STOP:
            ctx->is_ap_start = 0;
            mesh_tx_beacon_update_enable(0);
            break;
        default:
            break;
    }
}

void mesh_machine_event_cb(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = (wifi_mesh_ctx_t *)env;
    mesh_event_t *mesh_event = (mesh_event_t *)arg;

    mesh_e("%s event %d %p", __func__, mesh_event->event_id, mesh_event);
    switch(mesh_event->event_id){
        case MESH_TABLE_EVENT:
            mesh_table_event_cb(ctx, (mesh_table_event_t *)&mesh_event->event_info);
            break;
        case MESH_SYSTEM_EVENT:
            mesh_wifi_event_cb(ctx, (system_event_t *)&mesh_event->event_info);
            break;
        default:
            mesh_e("%s unsupport event %d", __func__, mesh_event->event_id);
            break;
    }

    os_free(mesh_event);
}
void mesh_table_notity_handle(int event, void *data, int len)
{
    mesh_d("%s %d state:%d", __func__, event, mesh_machine_state_get());
    if (event == MESH_TABLE_NETWOTK_CFG_ADD || event == MESH_TABLE_NETWOTK_CFG_UPATE) {
        mesh_vote_mid_bssid_update(mesh_config_get_major_id(), mesh_table_network_cfg_get()->to_bssid);
        mesh_timer_set(mesh_table_event_cb, 0, 0);
    }
}

int mesh_machine_find_channel(wifi_mesh_ctx_t *ctx)
{
    mesh_node_list_t *node_info;

    /*if (ctx->lock_chan2G4_channel) {
        wifi_rf_set_channel(ctx->lock_chan2G4_channel);
        wifi_set_ap_csa(ctx->lock_chan2G4_channel, 0, 1);
        return ctx->lock_chan2G4_channel;
    }*/
    ctx->lock_chan2G4_channel = 0;

    node_info = mesh_notitylist_best_node_get();
    if (node_info) {
        ctx->lock_chan2G4_channel = node_info->node.channel;
    } else {
        ctx->lock_chan2G4_channel = mesh_vote_channel_get();
    }

    if (ctx->lock_chan2G4_channel) {
        wifi_rf_set_channel(ctx->lock_chan2G4_channel);
        mesh_d("%s lock ch:%d softap:%d", __func__, ctx->lock_chan2G4_channel ,  wifi_freq_to_channel(fhost_get_softap_freq()));
        mesh_table_notitylist_dump(1);
        mesh_votelist_dump();
        if (ctx->lock_chan2G4_channel != wifi_freq_to_channel(fhost_get_softap_freq())) {
            wifi_set_ap_csa(ctx->lock_chan2G4_channel, 0, 1);
        }
    }

    return ctx->lock_chan2G4_channel;
}

int __mesh_machine_find_handle(wifi_mesh_ctx_t *ctx)
{
    mesh_node_list_t *node_info;

    node_info = mesh_notitylist_best_node_get();
    if (!node_info) {
        mesh_machine_state_set(ctx, MESH_VOTE_STATE);
    } else {
        if (memcmp(node_info->node.node_mac, ctx->network_cfg->to_bssid, MESH_MAC_AADR_LEN) == 0){
            if (mesh_can_bypass_to_root(node_info->node.link_rssi)) {
                memcpy(&ctx->select_node, &node_info->node, sizeof(ctx->select_node));
                mesh_machine_state_set(ctx, MESH_FAST_ROOT_STATE);
            } else {
                mesh_machine_state_set(ctx, MESH_VOTE_STATE);
            }
        } else {
            memcpy(&ctx->select_node, &node_info->node, sizeof(ctx->select_node));
            mesh_machine_state_set(ctx, MESH_JOIN_STATE);
        }
    }

    return 0;
}

void mesh_machine_find_timout_cb(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = (wifi_mesh_ctx_t *)env;

    if (mesh_machine_state_get() != MESH_FIND_STATE) {
        return;
    }

    if (__mesh_machine_find_handle(ctx)){
        mesh_timer_set(mesh_machine_find_timout_cb, ctx, MESH_FIND_WAIT_TIMEMS);
    }
}

void mesh_machine_channel_switch_cb(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = (wifi_mesh_ctx_t *)env;
    unsigned char *channel_list;
    unsigned char chan2G4_cnt;
    unsigned int switch_time = MESH_CHANNEL_SWITCH_TIMEMS;

    if (mesh_machine_state_get() == MESH_CFG_STATE) {
        channel_list = ctx->cfg_channel_list;
        chan2G4_cnt = ctx->cfg_chan2G4_cnt;
    } else if(mesh_machine_state_get() == MESH_FIND_STATE) {
        channel_list = ctx->find_channel_list;
        chan2G4_cnt = ctx->find_chan2G4_cnt;
    } else{
        return;
    }

    if (mesh_machine_state_get() == MESH_CFG_STATE && ctx->lock_chan2G4_channel) {
        mesh_timer_set(mesh_machine_channel_switch_cb, ctx, MESH_CHANNEL_SWITCH_TIMEMS);
        return;
    }

    ctx->chan2G4_ch_idx++;
    if (ctx->chan2G4_ch_idx >= chan2G4_cnt) {
        ctx->chan2G4_ch_idx = 0;
    }

    wifi_rf_set_channel(channel_list[ctx->chan2G4_ch_idx]);

    if (mesh_machine_state_get() == MESH_FIND_STATE) {
        if (mesh_machine_find_channel(ctx)) {
            mesh_timer_clear(mesh_machine_find_timout_cb);
            mesh_timer_set(mesh_machine_find_timout_cb, ctx, MESH_FIND_WAIT_TIMEMS);
            return ;
        }
    }

    /*if(channel_list[ctx->chan2G4_ch_idx] == MESH_SOFTAP_DEFAULT_CHANNEL) {
        switch_time = 500;
    }*/

    mesh_timer_set(mesh_machine_channel_switch_cb, ctx, switch_time);
}

void mesh_machine_probe_send_cb(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = (wifi_mesh_ctx_t *)env;
    //mesh_e("%s %d", __func__, __LINE__);
    if (mesh_machine_state_get() == MESH_CFG_STATE || mesh_machine_state_get() == MESH_FIND_STATE) {
        //mesh_tx_probe(ctx->own_mac, NULL, MESH_SOFTAP_SSID);
        mesh_timer_set(mesh_machine_probe_send_cb, ctx, MESH_PROBE_SEND_TIMEMS);
    }
}
static void wifi_mesh_cfg_task(void *arg)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();

    while (!wifi_is_ready()) {
        os_msleep(10);
    }

    ctx->apcfg_exit = 0;
    apnet_cfg_init();
    apnet_cfg_notity_register_handler(mesh_cfg_notity_handle);
    apnet_service_main(NULL);
    ctx->apcfg_exit = 1;
    while(1) {
        os_msleep(100);
    }
}

int mesh_softap_start(wifi_mesh_ctx_t *ctx)
{
    wifi_config_u config={0};
    int result;

    if (ctx->is_ap_start) {
        return 0;
    }
#if 0
    memcpy(config.ap.ssid, MESH_SOFTAP_SSID, sizeof(MESH_SOFTAP_SSID));
    memcpy(config.ap.password, MESH_SOFTAP_PWD, sizeof(MESH_SOFTAP_PWD));
#else
    snprintf((char *)config.ap.ssid, sizeof(config.ap.ssid), "DEVICE_%02x%02x%02x%02x", ctx->own_mac[2], ctx->own_mac[3], ctx->own_mac[4], ctx->own_mac[5]);
    snprintf((char *)config.ap.password, sizeof(config.ap.password), "eswin_%02x%02x%02x%02x", ctx->own_mac[2], ctx->own_mac[3], ctx->own_mac[4], ctx->own_mac[5]);
#endif
    config.ap.channel = MESH_SOFTAP_DEFAULT_CHANNEL;
    //config.ap.hidden_ssid = 1;
    config.ap.authmode = AUTH_WPA_WPA2_PSK;
    config.ap.max_connect = get_macsw_sta_max_cnt();

    wifi_stop_softap();
    result = wifi_start_softap(&config);
    if (result) {
        mesh_e("%s start softap fail\r\n",__func__);
        return -1;
    }

    while (!ctx->is_ap_start) {
        os_msleep(10);
    }

    if (!ctx->cfg_task_handle) {
        ctx->cfg_task_handle = os_task_create("mesh_cfg", MESH_TASK_PRI - 1, MESH_CFG_TASK_STACK, (task_entry_t)wifi_mesh_cfg_task, ctx);
    }

    wifi_mesh_beacon_interval_set(MESH_DEAULT_BEACON_INTERVAL);

    return 0;
}
int mesh_machine_cfg_handle(wifi_mesh_ctx_t *ctx)
{
   if (!ctx->cfg_task_handle) {
        ctx->cfg_task_handle = os_task_create("mesh_cfg", MESH_TASK_PRI, MESH_TASK_STACK, (task_entry_t)apnet_service_main, ctx);
    }

    ctx->chan2G4_ch_idx = 0;
    ctx->lock_chan2G4_channel = 0;
    wifi_rf_set_channel(ctx->cfg_channel_list[ctx->chan2G4_ch_idx]);
    mesh_timer_clear(mesh_machine_probe_send_cb);
    mesh_machine_probe_send_cb(ctx, NULL);
    mesh_timer_set(mesh_machine_channel_switch_cb, ctx, MESH_CHANNEL_SWITCH_TIMEMS);

    return 0;
}

int mesh_machine_find_scan(wifi_mesh_ctx_t *ctx)
{
    int timeout;

    ctx->chan2G4_ch_idx = 0;
    ctx->lock_chan2G4_channel = 0;
    timeout = MESH_CHANNEL_SWITCH_TIMEMS;

    mesh_timer_set(mesh_machine_channel_switch_cb, ctx, timeout);
    wifi_rf_set_channel(ctx->find_channel_list[ctx->chan2G4_ch_idx]);
    mesh_machine_probe_send_cb(ctx, NULL);

    return 0;
}

int mesh_machine_find_handle(wifi_mesh_ctx_t *ctx)
{
    mesh_timer_clear(mesh_machine_channel_switch_cb);
    mesh_timer_clear(mesh_machine_probe_send_cb);
    mesh_timer_clear(mesh_machine_find_timout_cb);

    memcpy(ctx->network_cfg, mesh_table_network_cfg_get(), sizeof(mesh_network_cfg_t));
    mesh_table_notitylist_reset();
    mesh_vote_best_score_reset();
    mesh_votelist_reset();

    wifi_mesh_beacon_interval_set(MESH_DEAULT_BEACON_INTERVAL);

    if (!mesh_machine_find_channel(ctx)) {
        mesh_machine_find_scan(ctx);
    } else {
        mesh_timer_set(mesh_machine_find_timout_cb, ctx, MESH_FIND_WAIT_TIMEMS);
    }

    return 0;
}

int mesh_comomon_connect(wifi_mesh_ctx_t *ctx, mesh_timeout_handler handler, unsigned int time_ms)
{
    wifi_config_u config = {0};
    mesh_aplist_t *ap_list;

    memcpy(config.sta.bssid, ctx->select_node.node_mac, MESH_MAC_AADR_LEN);
#if 0
    if (ctx->select_node.notity.node_layer == 0) {
        ap_list = mesh_aplist_bssid_search(config.sta.bssid);
        if(ap_list) {
            strcpy((char *)config.sta.ssid, (char *)ap_list->node.ssid);
        }
        memcpy(config.sta.password, ctx->network_cfg->passwd, ctx->network_cfg->passwd_len);
    } else {
        memcpy(config.sta.ssid, MESH_SOFTAP_SSID, sizeof(MESH_SOFTAP_SSID));
        memcpy(config.sta.password, MESH_SOFTAP_PWD, sizeof(MESH_SOFTAP_PWD));
    }
#else
    if (ctx->select_node.notity.node_layer == 0) {
        ap_list = mesh_aplist_bssid_search(config.sta.bssid);
        if(ap_list) {
            strcpy((char *)config.sta.ssid, (char *)ap_list->node.ssid);
        } else {
            mesh_w("can't find ssid!!!");
            mesh_table_aplist_dump();
        }
        memcpy(config.sta.password, ctx->network_cfg->passwd, ctx->network_cfg->passwd_len);
    } else {
        snprintf((char *)config.ap.ssid, sizeof(config.ap.ssid), "DEVICE_%02x%02x%02x%02x", config.sta.bssid[2], config.sta.bssid[3], config.sta.bssid[4], config.sta.bssid[5]);
        snprintf((char *)config.ap.password, sizeof(config.ap.password), "eswin_%02x%02x%02x%02x", config.sta.bssid[2], config.sta.bssid[3], config.sta.bssid[4], config.sta.bssid[5]);
    }

#endif

    config.sta.channel = ctx->select_node.channel;

    mesh_i("connect ssid:%s bssid:"MESH_MAC_STR" link_rssi:%d layer:%d rssi:%d status:%d ip:%08x", 
               config.sta.ssid, MESH_MAC_VALUE(config.sta.bssid), ctx->select_node.link_rssi, ctx->select_node.notity.node_layer, ctx->select_node.notity.link_rssi, ctx->select_node.notity.node_status, ctx->select_node.notity.node_ip);
    config.sta.channel = ctx->select_node.channel;

    wifi_stop_station();
    if (wifi_start_station(&config)) {
        handler(ctx, NULL);
    } else {
        mesh_timer_set(handler, ctx, time_ms);
    }

    return 0;
}

int mesh_connect_timeout_next(wifi_mesh_ctx_t *ctx)
{
    mesh_node_list_t *node_info;

    mesh_i("timeout will connect next ...");
    wifi_stop_station();
    //mesh_notitylist_set_allow(ctx->select_node.node_mac, 1);
    mesh_table_blacklist_add(ctx->select_node.node_mac, MESH_NODE_BLACKLIST_KEEP_TIMEMS);
    //mesh_table_notitylist_dump();
    node_info = mesh_notitylist_best_node_get();
    if (node_info) {
        memcpy(&ctx->select_node, &node_info->node, sizeof(ctx->select_node));
   } else {
        mesh_table_blacklist_del((unsigned char *)MESH_BROADCAST_MAC);
        node_info = mesh_notitylist_best_node_get();
        if (node_info) {
            memcpy(&ctx->select_node, &node_info->node, sizeof(ctx->select_node));
        }
   }

    return 0;
}

void mesh_machine_fast_root_timeout_cb(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = (wifi_mesh_ctx_t *)env;

    if (mesh_machine_state_get() != MESH_FAST_ROOT_STATE) {
        return;
    }

    if (wifi_get_status(STATION_IF) == STA_STATUS_CONNECTED) {
        mesh_machine_state_set(ctx, MESH_CONNETED_STATE);
    } else {
        mesh_connect_timeout_next(ctx);
        mesh_machine_state_set(ctx, MESH_JOIN_STATE);
    }
}

int mesh_machine_fast_root_handle(wifi_mesh_ctx_t *ctx)
{
    return mesh_comomon_connect(ctx, mesh_machine_fast_root_timeout_cb, MESH_JOIN_TIMEOUTMS);
}

void mesh_machine_join_timeout_cb(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = (wifi_mesh_ctx_t *)env;

    if (mesh_machine_state_get() != MESH_JOIN_STATE && mesh_machine_state_get() != MESH_MOVE_STATE) {
        return;
    }

    if (wifi_get_status(STATION_IF) == STA_STATUS_CONNECTED) {
        mesh_machine_state_set(ctx, MESH_CONNETED_STATE);
    } else {
        mesh_connect_timeout_next(ctx);
        mesh_comomon_connect(ctx, mesh_machine_join_timeout_cb, MESH_JOIN_TIMEOUTMS);
    }
}

int mesh_machine_join_handle(wifi_mesh_ctx_t *ctx)
{
    return mesh_comomon_connect(ctx, mesh_machine_join_timeout_cb, MESH_JOIN_TIMEOUTMS);
}

void mesh_machine_vote_delay_join(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = (wifi_mesh_ctx_t *)env;
    mesh_node_list_t *node_info;

    if (mesh_machine_state_get() != MESH_VOTE_STATE) {
        return;
    }

    mesh_table_notitylist_dump(15);
    node_info = mesh_notitylist_best_node_get();
    if (node_info) {
        memcpy(&ctx->select_node, &node_info->node, sizeof(ctx->select_node));
        mesh_machine_state_set(ctx, MESH_JOIN_STATE);
        return;
   }

   mesh_timer_set(mesh_machine_vote_delay_join, ctx, ctx->delay_join_timems);
}

void mesh_machine_reconnect_delay_join(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = (wifi_mesh_ctx_t *)env;

    if (mesh_machine_state_get() != MESH_CONNETED_STATE) {
        return;
    }

    mesh_connect_timeout_next(ctx);
    mesh_machine_state_set(ctx, MESH_JOIN_STATE);
}

int mesh_machine_vote_handle(wifi_mesh_ctx_t *ctx)
{
    return mesh_vote_handle();
}

void mesh_machine_elected_timeout_cb(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = (wifi_mesh_ctx_t *)env;

    if (mesh_machine_state_get() != MESH_ELECTED_ROOT_STATE) {
        return;
    }

    if (wifi_get_status(STATION_IF) == STA_STATUS_CONNECTED) {
        mesh_machine_state_set(ctx, MESH_CONNETED_STATE);
    } else {
        mesh_connect_timeout_next(ctx);
        mesh_machine_state_set(ctx, MESH_JOIN_STATE);
    }
}

int mesh_machine_elected_handle(wifi_mesh_ctx_t *ctx)
{
    return mesh_comomon_connect(ctx, mesh_machine_elected_timeout_cb, MESH_JOIN_TIMEOUTMS);
}

void mesh_machine_move_timeout_cb(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = (wifi_mesh_ctx_t *)env;
    mesh_node_list_t *best_node;
    mesh_node_list_t *cur_node;

    if (mesh_machine_state_get() != MESH_CONNETED_STATE) {
        return;
    }

    /*if (){
        mesh_timer_set(mesh_machine_move_timeout_cb, ctx, MESH_MOVE_CHECK_PERIOD);
        return;
    }*/

    best_node = mesh_notitylist_best_node_get();
    if (best_node) {
        if(memcmp(ctx->select_node.node_mac, best_node->node.node_mac, MESH_MAC_AADR_LEN)) {
            if (/*(ctx->select_node.notity.node_layer >= best_node->node.notity.node_layer) 
                && */(ctx->select_node.link_rssi + mesh_config_move_diff_rssi_get() < best_node->node.link_rssi)) {
                mesh_i("move layer:%d link rssi:%d -->bssid:"MESH_MAC_STR" layer:%d link rssi:%d", ctx->select_node.notity.node_layer, ctx->select_node.link_rssi,
                        MESH_MAC_VALUE(best_node->node.node_mac), best_node->node.notity.node_layer, best_node->node.link_rssi);
                memcpy(&ctx->select_node, &best_node->node, sizeof(ctx->select_node));
                mesh_machine_state_set(ctx, MESH_MOVE_STATE);
            } else {
                cur_node = mesh_notitylist_search_by_mac(ctx->select_node.node_mac);
                if(cur_node) {
                    memcpy(&ctx->select_node, &cur_node->node, sizeof(ctx->select_node));
                }
            }
        } else {
            memcpy(&ctx->select_node, &best_node->node, sizeof(ctx->select_node));
        }
    }

    mesh_timer_set(mesh_machine_move_timeout_cb, ctx, MESH_MOVE_CHECK_PERIOD);
}

void mesh_own_info_dump(void)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();
 
    mesh_i("own information:join sta max cnt:%d cfg bssid:"MESH_MAC_STR" major_id:"MESH_MAC_STR"", ctx->sta_max_cnt, MESH_MAC_VALUE(ctx->network_cfg->to_bssid), MESH_MAC_VALUE(mesh_config_get_major_id()));
    mesh_i("own:"MESH_MAC_STR" link rssi:%d layer:%d status:%d ip:0x%08x join[cnt:%d dis:%d]", MESH_MAC_VALUE(ctx->own_mac), ctx->mesh_notity.link_rssi, ctx->mesh_notity.node_layer, ctx->mesh_notity.node_status, ctx->mesh_notity.node_ip,
            ctx->mesh_notity.join_status & MESH_MODE_NODE_JOIN_CNT_MASK, (ctx->mesh_notity.join_status & MESH_NODE_DISABLE_JOIN) >> 7);

    mesh_i("parent:"MESH_MAC_STR" link rssi:%d layer:%d status:%d ip:0x%08x join[cnt:%d dis:%d]",MESH_MAC_VALUE(ctx->select_node.node_mac), ctx->select_node.notity.link_rssi, ctx->select_node.notity.node_layer,  ctx->select_node.notity.node_status, ctx->select_node.notity.node_ip,
            ctx->select_node.notity.join_status & MESH_MODE_NODE_JOIN_CNT_MASK, (ctx->select_node.notity.join_status & MESH_NODE_DISABLE_JOIN) >> 7);
}

void mesh_softap_channel_sync(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = (wifi_mesh_ctx_t *)env;
    int sta_freq, softap_freq;

    sta_freq = fhost_get_sta_freq();
    softap_freq = fhost_get_softap_freq();

    if(sta_freq < 0|| softap_freq < 0) {
        mesh_e("%s sta_freq=%d softap:%d", __func__, sta_freq, softap_freq);
        return ;
    }

    if(sta_freq != softap_freq) {
        mesh_i("%s csa %d -> %d", __func__, softap_freq, sta_freq);
        wifi_set_ap_csa(sta_freq, 10, 1);

        ctx->lock_chan2G4_channel = wifi_freq_to_channel(sta_freq);
        wifi_rf_set_channel(ctx->lock_chan2G4_channel);
    }

    return ;
}

int mesh_machine_done_handle(wifi_mesh_ctx_t *ctx)
{
    mesh_notity_t notity;

    mesh_table_parent_node_set(&ctx->select_node);
    mesh_timer_clear(mesh_node_broadcast_timeout_cb);
    mesh_node_broadcast_timeout_cb(ctx, NULL);
    mesh_own_info_dump();
    mesh_timer_set(mesh_machine_move_timeout_cb, ctx, MESH_MOVE_CHECK_PERIOD);
    mesh_node_broadcast_data_get(ctx, &notity);
    wifi_mesh_beacon_interval_set(mesh_config_beacon_interval_get());

    return 0;
}

int mesh_machine_move_handle(wifi_mesh_ctx_t *ctx)
{
    mesh_comomon_connect(ctx, mesh_machine_join_timeout_cb, MESH_JOIN_TIMEOUTMS);
    return 0;
}

void mesh_connect_done_cb(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = (wifi_mesh_ctx_t *)env;

    if (mesh_machine_state_get() != MESH_JOIN_STATE && mesh_machine_state_get() != MESH_ELECTED_ROOT_STATE
        && mesh_machine_state_get() != MESH_FAST_ROOT_STATE && mesh_machine_state_get() != MESH_CONNETED_STATE) {
        return;
    }

    mesh_timer_clear(mesh_machine_fast_root_timeout_cb);
    mesh_timer_clear(mesh_machine_elected_timeout_cb);
    mesh_timer_clear(mesh_machine_join_timeout_cb);

    //mesh_softap_channel_sync(ctx);
    mesh_softap_dhcps_reconfig(ctx);

    mesh_machine_state_set(ctx, MESH_CONNETED_STATE);

}

int mesh_machine_handle(wifi_mesh_ctx_t *ctx)
{
    switch(ctx->machine_state) {
        case MESH_INIT_STATE:
            break;
        case MESH_CFG_STATE:
            mesh_machine_cfg_handle(ctx);
            break;
        case MESH_FIND_STATE:
            mesh_machine_find_handle(ctx);
            break;
        case MESH_FAST_ROOT_STATE:
            mesh_machine_fast_root_handle(ctx);
            break;
        case MESH_VOTE_STATE:
            mesh_machine_vote_handle(ctx);
            break;
        case MESH_ELECTED_ROOT_STATE:
            mesh_machine_elected_handle(ctx);
            break;
        case MESH_JOIN_STATE:
            mesh_machine_join_handle(ctx);
            break;
        case MESH_MOVE_STATE:
            mesh_machine_move_handle(ctx);
            break;
        case MESH_CONNETED_STATE:
            mesh_machine_done_handle(ctx);
            break;
        default:
            break;
    }

    return 0;
}

void mesh_machine_state_set(wifi_mesh_ctx_t *ctx, unsigned int mesh_state)
{
    if (ctx->machine_state != mesh_state) {
        mesh_i("mesh state %s->%s", mesh_state_txt_get(ctx->machine_state), mesh_state_txt_get(mesh_state));
        ctx->machine_state = mesh_state;
        mesh_machine_handle(ctx);
    }
}
int mesh_machine_state_get(void)
{
    return mesh_ctx.machine_state;
}

void mesh_refresh_timeout_cb(void *env, void *arg)
{
    if (mesh_machine_state_get() == MESH_CONNETED_STATE) {
        mesh_table_refresh();
        mesh_votelist_refresh();
    } else {
        mesh_table_notity_list_refresh();
    }

    mesh_timer_set(mesh_refresh_timeout_cb, NULL, MESH_TABLE_REFRESH_PERIOD);
}
void mesh_machine_state_init(wifi_mesh_ctx_t *ctx)
{
    int chan2G4_cnt;

    chan2G4_cnt = wifi_get_chan2G4_cnt();
    mesh_d("chan2G4_cnt =%d",chan2G4_cnt);
    if (chan2G4_cnt == 14) {
        chan2G4_cnt = sizeof(cfg_channel_list);
    } else if(chan2G4_cnt == 13) {
        chan2G4_cnt = sizeof(cfg_channel_list) - 2;
    } else {
        chan2G4_cnt = sizeof(cfg_channel_list) - 5;
    }

    ctx->cfg_chan2G4_cnt = chan2G4_cnt;//wifi_get_chan2G4_cnt();
    ctx->find_chan2G4_cnt = wifi_get_chan2G4_cnt();
    ctx->cfg_channel_list = cfg_channel_list;
    ctx->find_channel_list = find_channel_list;

    ctx->sta_max_cnt = get_macsw_sta_max_cnt() -1;
    ctx->chan2G4_ch_idx = 0;

    if(memcmp(ctx->network_cfg->to_bssid, MESH_ZERO_MAC, MESH_MAC_AADR_LEN) == 0) {
        //network cfg
        mesh_machine_state_set(ctx, MESH_CFG_STATE);
    } else {
        mesh_machine_state_set(ctx, MESH_FIND_STATE);
    }

     mesh_timer_set(mesh_blacklist_refresh_timeout_cb, NULL, MESH_NODE_BLACKLIST_REFRESH_PERIOD);
     mesh_refresh_timeout_cb(NULL, NULL);
}

sys_err_t system_event_cb(void *ctx, system_event_t *event)
{
    mesh_wifi_event_cb(ctx, event);
    return 0;
}

void mesh_cfg_notity_handle(unsigned char *from_bssid, unsigned char *to_bssid, unsigned char *key, unsigned int timestamp)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();
    mesh_network_cfg_t network_cfg;

    memset(&network_cfg, 0, sizeof(mesh_network_cfg_t));

    memcpy(network_cfg.from_bssid, from_bssid, MESH_MAC_AADR_LEN);
    memcpy(network_cfg.to_bssid, to_bssid, MESH_MAC_AADR_LEN);
    network_cfg.passwd_len = strlen((char *)key);
    if (network_cfg.passwd_len > WIFI_PWD_MAX_LEN) {
        mesh_e("%s error network_cfg passwd_len to long\r\n", __func__, network_cfg.passwd_len);
        return;
    }

    memcpy(network_cfg.passwd, key, network_cfg.passwd_len);
    network_cfg.timestamp = timestamp;
    network_cfg.broadcast_count_down = MESH_NETWORK_CFG_COUNTDOWN_TIMMS;
    if (mesh_machine_state_get() == MESH_CFG_STATE) {
        network_cfg.switch_count_down = 0;
    } else {
        network_cfg.switch_count_down = MESH_NETWORK_CFG_STATEWSWITCH_TIMMS;
    }

    mesh_table_network_cfg_update(ctx, &network_cfg, sizeof(mesh_network_cfg_t), 1, network_cfg.to_bssid, -40);
}

void mesh_channel_unlock_cb(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();

    if (mesh_machine_state_get() != MESH_CFG_STATE) {
        return;
    }

    ctx->lock_chan2G4_channel = 0;
    mesh_i("cfg unlock channel");
}

void mesh_rx_auth_handle(void *env, void *buf, int len, signed char rssi)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();
    struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)buf;
    int fc = le_to_host16(mgmt->frame_control);

    if (mesh_machine_state_get() != MESH_CFG_STATE) {
        return;
    }

    if (WLAN_FC_GET_STYPE(fc) != WLAN_FC_STYPE_AUTH) {
        return;
    }

    if ((memcmp(mgmt->da, ctx->own_mac, MESH_MAC_AADR_LEN) == 0) 
        && (memcmp(mgmt->bssid, ctx->own_mac, MESH_MAC_AADR_LEN) == 0)) {
        if (!ctx->lock_chan2G4_channel) {
            mesh_i("find sta join softap lock channel");
            ctx->lock_chan2G4_channel = MESH_SOFTAP_DEFAULT_CHANNEL;
            wifi_rf_set_channel(ctx->lock_chan2G4_channel);
            mesh_timer_clear(mesh_channel_unlock_cb);
            mesh_timer_set(mesh_channel_unlock_cb, NULL, MESH_AUTH_UNLOCK_CHANNEL_TIMEMS);
        }
    }
}

static void wifi_mesh_process_task(void *arg)
{
    long long  timeout;
    wifi_mesh_ctx_t *ctx = (wifi_mesh_ctx_t *)arg;

    while (!wifi_is_ready()) {
        os_msleep(10);
    }

    mesh_softap_start(ctx);
    mesh_rx_start();
    mesh_machine_state_init(ctx);

    while(1) {
        timeout = mesh_timer_schedule(ctx);
        if (timeout) {
            mesh_timer_for_signal(timeout);
        }
    }
}

void mesh_vote_notity_handle(int event, void *data, int len)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();
    mesh_node_t *mesh_node;
    int delay_timems = 10000;

    mesh_i("%s %d event:%d",__func__, __LINE__, event);

    if(event == MESH_ELECTION_EARLY_TERM_FAST_JOIN) {
        mesh_node = (mesh_node_t *)data;
        memcpy(&ctx->select_node, mesh_node, sizeof(ctx->select_node));
        mesh_machine_state_set(ctx, MESH_JOIN_STATE);
        return;
    } else if(event == MESH_ELECTION_COMPLETE_ROOT_AP_JOIN) {
        mesh_node = (mesh_node_t *)data;
        if (mesh_node->channel == 0) {
            mesh_node->channel = ctx->lock_chan2G4_channel;
        }
        memcpy(&ctx->select_node, data, sizeof(ctx->select_node));
        mesh_machine_state_set(ctx, MESH_ELECTED_ROOT_STATE);
    } else if(event == MESH_ELECTION_DELAYED_OPTIMAL_JOIN) {
        mesh_table_blacklist_add(ctx->network_cfg->to_bssid, MESH_DELAY_AP_ALLOW_TIMEMS);
        delay_timems = *(int *)data;
        mesh_e("%s delay_timems:%d", __func__, delay_timems);
        ctx->delay_join_timems = delay_timems;
        mesh_timer_set(mesh_machine_vote_delay_join, ctx, delay_timems);
    } else if(event == MESH_ELECTION_FREECHOICE_OPTIMAL_JOIN){
        mesh_connect_timeout_next(ctx);
        mesh_machine_state_set(ctx, MESH_JOIN_STATE);
    } else {
        mesh_e("%s error:%d", __func__, event);
    }
}

int wifi_mesh_init(void)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();

    if (ctx->init_done) {
        return -1;
    }

    memset(ctx, 0, sizeof(wifi_mesh_ctx_t));
    hal_system_get_ap_mac(ctx->own_mac);
    mesh_config_init();
    mesh_timer_init();
    mesh_table_init(mesh_table_notity_handle);
    ctx->network_cfg = &ctx->curr_network_cfg; //mesh_table_network_cfg_get();
    memcpy(ctx->network_cfg, mesh_table_network_cfg_get(), sizeof(mesh_network_cfg_t));
    ctx->parent_node = mesh_table_parent_node_get();
    mesh_tx_init(ctx->own_mac);
    mesh_rx_init();
    mesh_rx_frame_register_handler(mesh_table_aplist_update, ctx);
    mesh_rx_vendor_data_register_handler((unsigned char *)MESH_OUI, MESH_OUI_TPYE_NOTITY, mesh_table_notitylist_update, ctx);
    mesh_rx_vendor_data_register_handler((unsigned char *)MESH_OUI, MESH_OUI_TPYE_NETWORKCFG, mesh_table_network_cfg_update, ctx);
    mesh_rx_vendor_data_register_handler((unsigned char *)MESH_OUI, MESH_OUI_TPYE_CFG, mesh_config_update, ctx);
    mesh_rx_frame_register_handler(mesh_rx_auth_handle, ctx);
    sys_event_loop_clear_cb(system_event_cb);
    sys_event_loop_set_cb(system_event_cb, ctx);
    wifi_mesh_reset_autoconn();
    mesh_vote_init(ctx->own_mac, mesh_config_get_major_id(), mesh_table_network_cfg_get()->to_bssid, mesh_vote_notity_handle);

    ctx->init_done = 1;

    return 0;
}

int wifi_mesh_deinit(void)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();

    if (!ctx->init_done) {
        return -1;
    }

    mesh_rx_deinit();
    mesh_timer_deinit();
    mesh_table_deinit();
    mesh_config_deinit();
    mesh_vote_deinit();
    ctx->init_done = 0;

    return 0;
}

int wifi_mesh_config_set(wifi_mesh_cfg_t *config)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();

    if (!config) {
        return -1;
    }

    memcpy(&ctx->mesh_cfg, config, sizeof(wifi_mesh_cfg_t));
    return 0;
}

int wifi_mesh_config_get(wifi_mesh_cfg_t *config)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();

    if (!config) {
        return -1;
    }

    memcpy(config, &ctx->mesh_cfg, sizeof(wifi_mesh_cfg_t));
    return 0;
}

int wifi_mesh_start(void)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();

    if (ctx->start_done) {
        return -1;
    }

    wifi_stop_station();
    ctx->mesh_task_handle = os_task_create("wifi_mesh", MESH_TASK_PRI, MESH_TASK_STACK, (task_entry_t)wifi_mesh_process_task, ctx);
    if (!ctx->mesh_task_handle) {
        mesh_e("%s %d", __func__, __LINE__);
        return -1;
    }

    ctx->start_done = 1;

    return 0;
}

int wifi_mesh_stop(void)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();

    if (!ctx->init_done || !ctx->start_done) {
        return -1;
    }

    sys_event_loop_clear_cb(system_event_cb);
    wifi_stop_station();
    wifi_stop_softap();
    if (ctx->mesh_task_handle) {
        os_task_delete(ctx->mesh_task_handle);
        ctx->mesh_task_handle = 0;
    }

    if (ctx->cfg_task_handle) {
        apnet_cfg_deinit();
        while(!ctx->apcfg_exit) {
            os_msleep(5);
        }
        os_task_delete(ctx->cfg_task_handle);
        ctx->cfg_task_handle = 0;
    }

    ctx->start_done = 0;

    return 0;
}

int wifi_mesh_at_start(void)
{
    wifi_mesh_init();
    wifi_mesh_start();

    return 0;
}
int wifi_mesh_at_stop(void)
{
    wifi_mesh_stop();
    wifi_mesh_deinit();

    return 0;
}
int wifi_mesh_at_del(void)
{
    wifi_mesh_network_cfg_del();
    wifi_mesh_parent_node_del();
    wifi_mesh_cfg_del();

    return 0;
}

#include "cli.h"
int mesh_atstart_cmd( int argc, char *argv[])
{
    wifi_mesh_at_start();
    return 0;
}
CLI_CMD(mesh_atstart, mesh_atstart_cmd, "mesh_atstart", "mesh_atstart");
int mesh_atstop_cmd(int argc, char *argv[])
{
    wifi_mesh_at_stop();
    return 0;
}
CLI_CMD(mesh_atstop, mesh_atstop_cmd, "mesh_atstop", "mesh_atstop");
int mesh_atdel_cmd(int argc, char *argv[])
{
    wifi_mesh_at_del();
    return 0;
}
CLI_CMD(mesh_atdel, mesh_atdel_cmd, "mesh_atdel", "mesh_atdel");

int mesh_start_cmd(int argc, char *argv[])
{
    wifi_mesh_init();
    wifi_mesh_start();
    return 0;
}
CLI_CMD(mesh_start, mesh_start_cmd, "mesh_start", "mesh_start");

int mesh_net_cfg_cmd(int argc, char *argv[])
{
    unsigned char fssid[MESH_MAC_AADR_LEN];
    unsigned char tssid[MESH_MAC_AADR_LEN];
    unsigned char passwd[WIFI_PWD_MAX_LEN] = {0};
    int passwd_len = 0;
    char *bssid;
    int i;
    static unsigned int tt = 0;

    if (argc < 2 || argc > 5)
    {
        return CMD_RET_USAGE;
    }

    bssid = argv[1];
    for (i = 0; i < 6; i++)
    {
        fssid[i] = mesh_hex2num(bssid[i*3]) * 0x10 + mesh_hex2num(bssid[i*3+1]);
    }

    bssid = argv[2];
    for (i = 0; i < 6; i++)
    {
        tssid[i] = mesh_hex2num(bssid[i*3]) * 0x10 + mesh_hex2num(bssid[i*3+1]);
    }

    if (argc == 4) {
        passwd_len = strlen(argv[3]);
        if (passwd_len > WIFI_PWD_MAX_LEN) {
            return CMD_RET_USAGE;
        }
        memcpy((char *)passwd, argv[3], passwd_len);
    }
    tt++;
    mesh_d("cli cfg pwdlen:%d pwd:%s",passwd_len, passwd);
    dump_mac("cfg from bssid:", fssid);
    dump_mac("cfg to bssid:", tssid);
    mesh_cfg_notity_handle(fssid, tssid, passwd, tt);
    return 0;
}
CLI_CMD(mesh_net_cfg, mesh_net_cfg_cmd, "mesh_net_cfg", "mesh_net_cfg");

int mesh_dump_cmd(int argc, char *argv[])
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();

    if(!ctx->init_done) {
        mesh_d("must mesh_start !!!");
        return 0;
    }

    if (argc == 1) {
        mesh_votelist_dump();
        mesh_table_notitylist_dump(100);
        mesh_scorelist_dump();
        mesh_own_info_dump();
        mesh_table_network_cfg_dump();
        return 0;
    }

    if(!strcmp("aplist", argv[1])) {
        mesh_table_aplist_dump();
    } else if(!strcmp("votelist", argv[1])) {
        mesh_votelist_dump();
    } else if(!strcmp("notitylist", argv[1])) {
        mesh_table_notitylist_dump(100);
    } else if(!strcmp("scorelist", argv[1])) {
        mesh_scorelist_dump();
    } else if(!strcmp("votes", argv[1])) {
        mesh_votelist_dump();
        mesh_scorelist_dump();
    } else if(!strcmp("own", argv[1])) {
        mesh_own_info_dump();
    } else if(!strcmp("build", argv[1])){
        mesh_i("mesh bulid time%s %s", __DATE__, __TIME__);
    }

    return 0;
}
CLI_CMD(mesh_dump, mesh_dump_cmd, "mesh_dump", "mesh_dump");

int mesh_del_cmd( int argc, char *argv[])
{
    wifi_mesh_network_cfg_del();
    wifi_mesh_parent_node_del();
    wifi_mesh_cfg_del();
    return 0;
}

CLI_CMD(mesh_del, mesh_del_cmd, "mesh_del", "mesh_del");

int mesh_sta_cmd(int argc, char *argv[])
{
    struct softap_sta_info info[CFG_STA_MAX] = {0};
    uint32_t num, i;
    int result;

    result = wifi_softap_get_sta_list((struct softap_sta_info *)&info, &num);
    if (!result) {
        for (i = 0; i < num; i++){
            mesh_i("[%d]:"MESH_MAC_STR"", i,MESH_MAC_VALUE(info[i].mac_addr));
        }
    } else {
        mesh_d("no sta");
    }
    return 0;
}
CLI_CMD(mesh_sta, mesh_sta_cmd, "mesh_sta", "mesh_sta");

static unsigned char select_tssid[MESH_MAC_AADR_LEN];
void mesh_select_node_cb(void *env, void *arg)
{
    wifi_mesh_ctx_t *ctx = wifi_mesh_get_ctx();
    mesh_node_list_t *node_info;

    node_info = mesh_notitylist_search_by_mac(select_tssid);
    if (node_info) {
        mesh_d("select cmd to:"MESH_MAC_STR"", MESH_MAC_VALUE(select_tssid));
        memset(&ctx->select_node, 0, sizeof(ctx->select_node));
        memcpy(&ctx->select_node, &node_info->node, sizeof(ctx->select_node));
        mesh_machine_state_set(ctx, MESH_JOIN_STATE);
    }
}

int mesh_select_cmd(int argc, char *argv[])
{
    char *bssid;
    int i;

    if (argc != 2) {
        return -1;
    }

    bssid = argv[1];
    for (i = 0; i < 6; i++)
    {
        select_tssid[i] = mesh_hex2num(bssid[i*3]) * 0x10 + mesh_hex2num(bssid[i*3+1]);
    }

    mesh_timer_set(mesh_select_node_cb, NULL, 0);

    return 0;
}
CLI_CMD(mesh_select, mesh_select_cmd, "mesh_select", "mesh_select");

int mesh_cfg_cmd(int argc, char *argv[])
{
    mesh_config_t *config = mesh_config_get();
    mesh_config_t new_config;

    if (argc == 1) {
        mesh_config_dump();
        return 0;
    }

    memcpy(&new_config, config, sizeof(mesh_config_t));
    if (strcmp(argv[1], "beacon_interval") == 0) {
        new_config.beacon_interval = atoi(argv[2]);
    } else {
        return 0;
    }

    new_config.seqno++;
    new_config.count_down = 15;
    mesh_config_update(0, &new_config, sizeof(mesh_config_t), 0, 0, 0);
    return 0;
}
CLI_CMD(mesh_cfg, mesh_cfg_cmd, "mesh_cfg", "mesh_cfg");

int mesh_cfg_major_id_cmd(int argc, char *argv[])
{
    mesh_config_t *config = mesh_config_get();
    int mode = 0 ,i ;
    char *bssid;

    if(argc < 2) {
        return -1;
    }

    mode = atoi(argv[1]);
    if(mode == 0) {
        if(argc < 3) {
            return -1;
        }
        bssid = argv[2];
        for (i = 0; i < 6; i++)
        {
            config->major_id[i] = (unsigned char)(mesh_hex2num(bssid[i*3]) * 0x10 + mesh_hex2num(bssid[i*3+1]));
        }
    }
    mesh_i("config: mesh id:"MESH_MAC_STR", bssid_as_major_id:%d", MESH_MAC_VALUE(config->major_id), config->use_bssid_as_major_id);
    if (wifi_mesh_cfg_write((char *)config, sizeof(mesh_config_t))) {
           mesh_e("mesh config save fail!!!");
    }

    return 0;
}
CLI_CMD(mesh_cfg_id, mesh_cfg_major_id_cmd, "mesh_cfg", "mesh_cfg");

int mesh_rx_cmd(int argc, char *argv[])
{
    if (argc == 2) {
        if (atoi(argv[1])) {
            mesh_rx_start();
        } else{
            mesh_rx_deinit();
        }
    }
    return 0;
}
CLI_CMD(mesh_rx, mesh_rx_cmd, "mesh_cfg", "mesh_cfg");


