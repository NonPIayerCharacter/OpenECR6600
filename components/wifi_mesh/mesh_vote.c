#include "mesh_vote.h"
#include "mesh_utils.h"
#include "mesh_aes.h"
#include "mesh_storage.h"
#include "mesh_timer.h"
#include "mesh_config.h"
#include "mesh_calc_rssi.h"
#include "mesh_table.h"
#include "mesh_rx.h"
#include "mesh_tx.h"

#define MESH_VOTE_AGING_PERIOD      (20000)

//#define MESH_IS_HIGH_RSSI(rssi)    ((rssi) > MESH_HIGH_RSSI_THD)
//#define MESH_IS_MIDDLE_RSSI(rssi)  ((rssi) <= MESH_HIGH_RSSI_THD && (rssi) > MESH_LOW_RSSI_THD)
//#define MESH_IS_LOW_RSSI(rssi)     ((rssi) <= MESH_LOW_RSSI_THD)

typedef struct mesh_vote{
    unsigned long long tsf;
    unsigned char major_id[MESH_ID_LEN];
    unsigned char vote_node_mac[MESH_MAC_AADR_LEN];
    signed char vote_node_rssi;
    unsigned char own_node_mac[MESH_MAC_AADR_LEN];
    unsigned char own_node_rssi;
    unsigned char vote_channel;
    unsigned char high_rssi_cnt;
    unsigned char middle_rssi_cnt;
    unsigned char low_rssi_cnt;
}__packed mesh_vote_t;

typedef struct mesh_votelist{
    struct dl_list list;
    long long timestamp;
    unsigned char channel;
    signed char link_rssi;
    mesh_rssi_record_t rssi_record;
    mesh_vote_t   vote;
}__packed mesh_votelist_t;

typedef struct mesh_scorelist{
    struct dl_list list;
    signed char   rssi;
    unsigned short votes;
    float          score;
    unsigned char  vote_node_mac[MESH_MAC_AADR_LEN];;
}__packed mesh_scorelist_t;

typedef struct mesh_vote_ctx
{
    struct dl_list vote_list;
    struct dl_list score_list;
    unsigned int all_votes:31;
    unsigned int is_start:1;
    os_mutex_handle_t vote_mutex;
    mesh_vote_t mesh_vote;
    unsigned char own_mac[MESH_MAC_AADR_LEN];
    unsigned char major_id[MESH_ID_LEN];
    unsigned char to_bssid[MESH_MAC_AADR_LEN];
    mesh_vote_notity_handler_t notity;
}mesh_vote_ctx_t;

static mesh_vote_ctx_t mesh_vote;

void mesh_machine_vote_timeout_cb(void *env, void *arg);

__INLINE mesh_vote_ctx_t *mesh_vote_ctx_get(void)
{
    return &mesh_vote;
}

__INLINE int MESH_IS_HIGH_RSSI(signed char rssi)
{
    return ((rssi) > mesh_config_high_rssi_thd_get());
}

__INLINE int MESH_IS_MIDDLE_RSSI(signed char rssi)
{
    return ((rssi) <= mesh_config_high_rssi_thd_get() && (rssi) > mesh_config_low_rssi_thd_get());
}

__INLINE int MESH_IS_LOW_RSSI(signed char rssi)
{
    return ((rssi) <= mesh_config_low_rssi_thd_get());
}

int mesh_can_bypass_to_root(signed char link_rssi)
{
    return MESH_IS_HIGH_RSSI(link_rssi);
}

void mesh_vote_notity_register_handler(mesh_vote_notity_handler_t handler)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();

    ctx->notity = handler;
}

void mesh_votelist_update(void *env, void *data, int len, unsigned char channel, unsigned char *bssid, signed char rssi)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();
    mesh_vote_t *mesh_vote;
    mesh_votelist_t *vote_info = NULL, *prev_res = NULL,*temp;
    int add = 0;
    if (len < sizeof(mesh_vote_t)) {
        mesh_e("%s len error", __func__);
        return;
    }

    mesh_vote = (mesh_vote_t *)data;
    if(memcmp(mesh_vote->major_id, ctx->major_id, MESH_ID_LEN)) {
        //mesh_e("%s major_id mismatch", __func__);
        return;
    }

    os_mutex_lock(ctx->vote_mutex, WAIT_FOREVER);
    dl_list_for_each_safe(temp, prev_res, &ctx->vote_list,
        mesh_votelist_t, list) {
        if ((memcmp(mesh_vote->own_node_mac, temp->vote.own_node_mac, MESH_MAC_AADR_LEN) == 0)) {
            vote_info = temp;
            dl_list_del(&temp->list);
            break;
        }
    }

    if (!vote_info) {
        ctx->all_votes++;
        vote_info = (mesh_votelist_t *)os_zalloc(sizeof(mesh_votelist_t));
        if (!vote_info) {
            mesh_e("%s malloc fail", __func__);
            goto BACK;
        }
    }

    vote_info->channel = channel;
    mesh_average_rssi(&vote_info->rssi_record, rssi);
    vote_info->link_rssi = mesh_calc_link_rssi(mesh_vote->vote_node_rssi, vote_info->rssi_record.average, 1, 0);
    vote_info->timestamp = os_time_get();
    memcpy(&vote_info->vote, mesh_vote, sizeof(mesh_vote_t));

    dl_list_for_each_safe(temp, prev_res, &ctx->vote_list, mesh_votelist_t, list) {
        if ((vote_info->link_rssi == temp->link_rssi && vote_info->vote.tsf > temp->vote.tsf)
            || (vote_info->link_rssi > temp->link_rssi)) {
            dl_list_add(temp->list.prev, &vote_info->list);
            add = 1;
            break;
        }
    }
    
    if (!add) {
        dl_list_add_tail(&ctx->vote_list, &vote_info->list);
    }

BACK:
    os_mutex_unlock(ctx->vote_mutex);
}

mesh_scorelist_t *mesh_vote_best_score_update(void)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();
    mesh_scorelist_t *first = NULL;
    mesh_votelist_t *votetemp, *voteprev;
    mesh_scorelist_t *scoreinfo, *scoretemp, *scoreprev;
    int add;

    os_mutex_lock(ctx->vote_mutex, WAIT_FOREVER);
    dl_list_for_each_safe(votetemp, voteprev, &ctx->vote_list,
            mesh_votelist_t, list) {
        scoreinfo = NULL;
        add = 0;
        dl_list_for_each_safe(scoretemp, scoreprev, &ctx->score_list, mesh_scorelist_t, list) {
            if (memcmp(votetemp->vote.vote_node_mac, scoretemp->vote_node_mac, MESH_MAC_AADR_LEN) == 0) {
                if (votetemp->link_rssi > scoretemp->rssi) {
                    scoretemp->rssi = votetemp->link_rssi;
                }

                scoretemp->votes++;
                scoreinfo = scoretemp;
                dl_list_del(&scoretemp->list);
                break;
            } 
        }

        if (!scoreinfo) {
            scoreinfo = (mesh_scorelist_t *)os_zalloc(sizeof(mesh_scorelist_t));
            if (!scoreinfo) {
                mesh_e("%s malloc scoreinfo fail ", __func__);
                goto BACK;
            }

            scoreinfo->votes = 1;
            scoreinfo->rssi = votetemp->link_rssi;
            memcpy(scoreinfo->vote_node_mac, votetemp->vote.vote_node_mac, MESH_MAC_AADR_LEN);
            add = 0;
        } else {
            dl_list_for_each_safe(scoretemp, scoreprev, &ctx->score_list, mesh_scorelist_t, list) {
                if ((scoreinfo->votes > scoretemp->votes ) 
                    || (scoreinfo->score == scoretemp->score && scoreinfo->rssi > scoretemp->rssi)) {
                    dl_list_add(scoretemp->list.prev, &scoreinfo->list);
                    add = 1;
                    break;
                }
            }
        }
        if (!add) {
            dl_list_add_tail(&ctx->score_list, &scoreinfo->list);
        }

    }

BACK:
    first = dl_list_first(&ctx->score_list, mesh_scorelist_t, list);
    os_mutex_unlock(ctx->vote_mutex);

    return first;
}

void mesh_vote_best_score_reset(void)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();
    mesh_scorelist_t *scoretemp, *prev;

    os_mutex_lock(ctx->vote_mutex, WAIT_FOREVER);
    
    dl_list_for_each_safe(scoretemp, prev, &ctx->score_list,
        mesh_scorelist_t, list) {
            dl_list_del(&scoretemp->list);
            os_free(scoretemp);
    }
    os_mutex_unlock(ctx->vote_mutex);
}
mesh_scorelist_t *mesh_vote_best_score_get(void)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();
    mesh_scorelist_t *first = NULL;

    os_mutex_lock(ctx->vote_mutex, WAIT_FOREVER);
    first = dl_list_first(&ctx->score_list, mesh_scorelist_t, list);
    os_mutex_unlock(ctx->vote_mutex);

    return first;
}

mesh_votelist_t *mesh_vote_best_rssi_get(void)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();
    mesh_votelist_t *first = NULL;

    os_mutex_lock(ctx->vote_mutex, WAIT_FOREVER);
    first = dl_list_first(&ctx->vote_list, mesh_votelist_t, list);
    os_mutex_unlock(ctx->vote_mutex);

    return first;
}

unsigned char mesh_vote_channel_get(void)
{
    mesh_votelist_t *votelist;
    unsigned char channel = 0;
    votelist = mesh_vote_best_rssi_get();
    if (votelist) {
        mesh_i("mesh_vote_channel_get vote_node_mac:"MESH_MAC_STR" own_node_mac:"MESH_MAC_STR" link rssi = %d, vote rssi = %d %d %d ch:%d %d",
        MESH_MAC_VALUE(votelist->vote.vote_node_mac),  MESH_MAC_VALUE(votelist->vote.own_node_mac), 
        votelist->link_rssi, votelist->vote.vote_node_rssi, votelist->vote.high_rssi_cnt, votelist->vote.middle_rssi_cnt, votelist->channel, votelist->vote.vote_channel);
        channel = votelist->vote.vote_channel;
    }

    return channel;
}

mesh_votelist_t *mesh_votelist_get_by_bssid(unsigned char *bssid)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();
    mesh_votelist_t *temp = NULL;

    os_mutex_lock(ctx->vote_mutex, WAIT_FOREVER);
    dl_list_for_each(temp, &ctx->vote_list,
        mesh_votelist_t, list) {
        if (memcmp(temp->vote.own_node_mac, bssid, MESH_MAC_AADR_LEN) == 0) {
            os_mutex_unlock(ctx->vote_mutex);
            return temp;
        } 
    }
    os_mutex_unlock(ctx->vote_mutex);
    return NULL;
}

int mesh_votelist_cnt_get(void)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();

    return dl_list_len(&ctx->vote_list);
}

void mesh_votelist_reset(void)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();
    mesh_votelist_t *vote_info, *prev;

    os_mutex_lock(ctx->vote_mutex, WAIT_FOREVER);
    dl_list_for_each_safe(vote_info, prev, &ctx->vote_list,
        mesh_votelist_t, list) {
            dl_list_del(&vote_info->list);
            os_free(vote_info);
    }
    ctx->all_votes = 0;
    os_mutex_unlock(ctx->vote_mutex);
}

void mesh_votelist_dump(void)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();
    mesh_votelist_t *temp;

    mesh_i("%s all_votes:%d", __func__,ctx->all_votes);
    os_mutex_lock(ctx->vote_mutex, WAIT_FOREVER);
    dl_list_for_each(temp, &ctx->vote_list,
        mesh_votelist_t, list) {
        mesh_i("vote_node_mac:"MESH_MAC_STR" own_node_mac:"MESH_MAC_STR" link rssi = %d, vote rssi = %d %d %d ch:%d %d",
               MESH_MAC_VALUE(temp->vote.vote_node_mac),  MESH_MAC_VALUE(temp->vote.own_node_mac), 
               temp->link_rssi, temp->vote.vote_node_rssi, temp->vote.high_rssi_cnt, temp->vote.middle_rssi_cnt, temp->channel, temp->vote.vote_channel);
    }
    os_mutex_unlock(ctx->vote_mutex);
}


void mesh_scorelist_dump(void)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();
    mesh_scorelist_t *temp;

    mesh_i("%s :%d", __func__,dl_list_len(&ctx->score_list));
    os_mutex_lock(ctx->vote_mutex, WAIT_FOREVER);
    dl_list_for_each(temp, &ctx->score_list,
        mesh_scorelist_t, list) {
        mesh_i("mac:"MESH_MAC_STR" link rssi = %d, votes: %d, score: %d", MESH_MAC_VALUE(temp->vote_node_mac), temp->rssi, temp->votes, temp->score);
    }
    os_mutex_unlock(ctx->vote_mutex);
}

void mesh_votelist_rssi_level_get(unsigned char *low_rssi_cnt, unsigned char *middle_rssi_cnt, unsigned char *high_rssi_cnt)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();
    mesh_votelist_t *temp;

    *low_rssi_cnt = 0;
    *middle_rssi_cnt = 0;
    *high_rssi_cnt = 0;

    os_mutex_lock(ctx->vote_mutex, WAIT_FOREVER);
    dl_list_for_each(temp, &ctx->vote_list,
        mesh_votelist_t, list) {
        if (MESH_IS_HIGH_RSSI(temp->vote.own_node_rssi)) {
            (*high_rssi_cnt)++;
        } else if (MESH_IS_HIGH_RSSI(temp->vote.own_node_rssi)) {
            (*middle_rssi_cnt)++;
        } else {
            (*low_rssi_cnt)++;
        }
    }
    os_mutex_unlock(ctx->vote_mutex);
}
void mesh_votelist_refresh(void)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();
    mesh_votelist_t *votetemp, *voteprev;

    os_mutex_lock(ctx->vote_mutex, WAIT_FOREVER);
    dl_list_for_each_safe(votetemp, voteprev, &ctx->vote_list,
        mesh_votelist_t, list) {
        if (os_time_get() > votetemp->timestamp + MESH_VOTE_AGING_PERIOD) {
            dl_list_del(&votetemp->list);
            os_free(votetemp);
        }
    }

    os_mutex_unlock(ctx->vote_mutex);
}

int mesh_machine_vote_insert_ap(mesh_vote_ctx_t *ctx)
{
    mesh_aplist_t *ap_info;
    mesh_vote_t   vote;

    memset(&vote, 0 ,sizeof(mesh_vote_t));

    ap_info = mesh_aplist_bssid_search(NULL);
    if (ap_info) {
        memcpy(vote.major_id, ctx->major_id, MESH_ID_LEN); ///*ap_info->node.bssid,*/
        memcpy(vote.own_node_mac, ctx->own_mac, MESH_MAC_AADR_LEN);
        memcpy(vote.vote_node_mac, ctx->own_mac, MESH_MAC_AADR_LEN);
        vote.own_node_rssi = ap_info->node.rssi;
        vote.vote_node_rssi = ap_info->node.rssi;
        vote.tsf = ap_info->node.tsf;
        vote.vote_channel = ap_info->node.channel;

        mesh_votelist_update(ctx, &vote, sizeof(vote), ap_info->node.channel, ap_info->node.bssid, ap_info->node.rssi);
    }

    return 0;
}

int __mesh_machine_vote_handle(mesh_vote_ctx_t *ctx, mesh_vote_t *mesh_vote, signed char *vote_channel)
{
    mesh_aplist_t *ap_info;
    mesh_votelist_t *vote_info;

    mesh_machine_vote_insert_ap(ctx);
    vote_info = mesh_vote_best_rssi_get();
    if (!vote_info) {
        return -1;
    }

    memcpy(mesh_vote->vote_node_mac, vote_info->vote.vote_node_mac, MESH_MAC_AADR_LEN);
    mesh_vote->vote_node_rssi = vote_info->vote.vote_node_rssi;
    mesh_vote->tsf = vote_info->vote.tsf;
    mesh_vote->vote_channel = vote_info->vote.vote_channel;
    memcpy(mesh_vote->own_node_mac, ctx->own_mac, MESH_MAC_AADR_LEN);
    ap_info = mesh_aplist_bssid_search(NULL);
    if (ap_info) {
         mesh_vote->own_node_rssi = ap_info->node.rssi;
    } else {
        mesh_vote->own_node_rssi = MESH_RSSI_MIN_THD;
    }

    *vote_channel = vote_info->vote.vote_channel;

    mesh_votelist_rssi_level_get(&mesh_vote->low_rssi_cnt, &mesh_vote->middle_rssi_cnt, &mesh_vote->high_rssi_cnt);
    memcpy(mesh_vote->major_id, ctx->major_id, MESH_MAC_AADR_LEN);

    return 0;
}

void mesh_machine_vote_update_cb(void *env, void *arg)
{
    mesh_vote_ctx_t *ctx = (mesh_vote_ctx_t *)arg;
    mesh_vote_t  mesh_vote;
    mesh_node_list_t *node_info;
    signed char vote_channel;
    int result;

    if (!ctx->is_start) {
        return;
    }

    node_info = mesh_notitylist_best_node_get();
    if (node_info) {
        if (MESH_IS_HIGH_RSSI(node_info->node.link_rssi)) {
            mesh_timer_clear(mesh_machine_vote_timeout_cb);
            ctx->notity(MESH_ELECTION_EARLY_TERM_FAST_JOIN, &node_info->node, sizeof(mesh_node_t));
            ctx->is_start = 0;
            return;
        }
   }

    result = __mesh_machine_vote_handle(ctx, &mesh_vote, &vote_channel);
    if(!result) {
        mesh_votelist_update(ctx, &mesh_vote, sizeof(ctx->mesh_vote), vote_channel, ctx->own_mac, MESH_HIGH_RSSI_THD);
        if (memcmp(&ctx->mesh_vote, &mesh_vote, sizeof(mesh_vote_t))) {
            memcpy(&ctx->mesh_vote, &mesh_vote, sizeof(mesh_vote_t));
            mesh_tx_beacon_update_by_type(MESH_OUI_TPYE_VOTE, (unsigned char *)&ctx->mesh_vote, sizeof(mesh_vote_t), 1);
        }
    }

    mesh_timer_set(mesh_machine_vote_update_cb, ctx, MESH_VOTE_UPDATE_PERIOD);
}

void mesh_machine_vote_timeout_cb(void *env, void *arg)
{
    mesh_vote_ctx_t *ctx = (mesh_vote_ctx_t *)arg;
    mesh_scorelist_t *scorelist;
    mesh_votelist_t *vote_info;
    mesh_node_list_t *node_list;
    int delay_timems = 0;
    mesh_node_t select_node;

    if (!ctx->is_start) {
        return;
    }

    mesh_votelist_dump();
    mesh_tx_beacon_update_by_type(MESH_OUI_TPYE_VOTE, 0, 0, 1);
    mesh_vote_best_score_update();
    mesh_scorelist_dump();
    mesh_timer_clear(mesh_machine_vote_update_cb);
    scorelist = mesh_vote_best_score_get();
    if (scorelist) {
        mesh_i("%s %d rssi:%d score:"MESH_MAC_STR" own:"MESH_MAC_STR"",__func__, __LINE__, scorelist->rssi, MESH_MAC_VALUE(scorelist->vote_node_mac),  MESH_MAC_VALUE(ctx->own_mac));
        if (memcmp(scorelist->vote_node_mac, ctx->own_mac, MESH_MAC_AADR_LEN) == 0) {
            memset(&select_node, 0, sizeof(select_node));
            node_list = mesh_notitylist_search_by_mac(ctx->to_bssid);
            if (node_list) {
                mesh_table_notitylist_node_dump(node_list);
                memcpy(&select_node, &node_list->node, sizeof(mesh_node_t));
            } else {
                select_node.notity.node_layer = 0;
                select_node.link_rssi = scorelist->rssi;
                memcpy(&select_node.node_mac, ctx->to_bssid, MESH_MAC_AADR_LEN);
            }

            if(ctx->notity) {
                ctx->notity(MESH_ELECTION_COMPLETE_ROOT_AP_JOIN, &select_node, sizeof(mesh_node_t));
                ctx->is_start = 0;
            }
            return;
        } else {
            vote_info = mesh_votelist_get_by_bssid(scorelist->vote_node_mac);
            if (vote_info) {
                 mesh_i("%s %d %d %d rssi:%d",__func__, __LINE__, vote_info->vote.high_rssi_cnt,vote_info->vote.middle_rssi_cnt, scorelist->rssi);
                 if (MESH_IS_HIGH_RSSI(scorelist->rssi)) {
                     delay_timems = MESH_DELAY_JOIN_TIMEMS;
                 } else if(MESH_IS_MIDDLE_RSSI(scorelist->rssi)) {
                     delay_timems = MESH_DELAY_JOIN_TIMEMS + (vote_info->vote.high_rssi_cnt / 5) * 100;
                 } else {
                     delay_timems = MESH_DELAY_JOIN_TIMEMS + (vote_info->vote.high_rssi_cnt + (vote_info->vote.middle_rssi_cnt/5)) / 5 * 100;
                 }
            } else {
                mesh_i("%s %d ",__func__, __LINE__);
                delay_timems = os_random() % 30 * 100;
            }

            if(ctx->notity) {
                ctx->notity(MESH_ELECTION_DELAYED_OPTIMAL_JOIN, &delay_timems, sizeof(delay_timems));
                ctx->is_start = 0;
            }
        }
    } else {
        mesh_i("%s %d",__func__, __LINE__);
        if(ctx->notity) {
            ctx->notity(MESH_ELECTION_FREECHOICE_OPTIMAL_JOIN, &delay_timems, sizeof(delay_timems));
            ctx->is_start = 0;
        }
    }
}

int mesh_vote_handle(void)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();

    ctx->is_start = 0;
    mesh_vote_best_score_reset();
    mesh_votelist_reset();
    mesh_timer_clear(mesh_machine_vote_update_cb);
    mesh_timer_clear(mesh_machine_vote_timeout_cb);
    ctx->is_start = 1;
    mesh_timer_set(mesh_machine_vote_update_cb, ctx, MESH_VOTE_UPDATE_PERIOD);
    mesh_timer_set(mesh_machine_vote_timeout_cb, ctx, mesh_config_vote_timems_get());

    return 0;
}

int mesh_vote_mid_bssid_update(unsigned char *major_id, unsigned char *to_bssid)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();

    memcpy(ctx->major_id, major_id, MESH_ID_LEN);
    memcpy(ctx->to_bssid, to_bssid, MESH_MAC_AADR_LEN);
    wifi_mesh_bssid_set(to_bssid);

    return 0;
}

void mesh_vote_init(unsigned char *own_mac, unsigned char *major_id, unsigned char *to_bssid, mesh_vote_notity_handler_t handler)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();

    memset(ctx, 0, sizeof(mesh_vote_ctx_t));

    dl_list_init(&ctx->vote_list);
    dl_list_init(&ctx->score_list);
    ctx->vote_mutex = os_mutex_create();
    memcpy(ctx->own_mac, own_mac, MESH_MAC_AADR_LEN);
    mesh_vote_mid_bssid_update(major_id,  to_bssid);
    mesh_vote_notity_register_handler(handler);
    mesh_rx_vendor_data_register_handler((unsigned char *)MESH_OUI, MESH_OUI_TPYE_VOTE, mesh_votelist_update, ctx);
}

void mesh_vote_deinit(void)
{
    mesh_vote_ctx_t *ctx = mesh_vote_ctx_get();

    mesh_votelist_reset();
    mesh_vote_best_score_reset();
    os_mutex_destroy(ctx->vote_mutex);
}

