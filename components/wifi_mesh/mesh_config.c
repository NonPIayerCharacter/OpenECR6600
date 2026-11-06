#include "mesh_common.h"
#include "mesh_config.h"
#include "mesh_storage.h"

typedef struct mesh_config_ctx {
    unsigned char init_done;
    os_mutex_handle_t config_mutex;
    mesh_config_t config;
} mesh_config_ctx_t;

static mesh_config_ctx_t mesh_config_ctx;

__INLINE mesh_config_ctx_t * mesh_config_ctx_get(void)
{
    return &mesh_config_ctx;
}

int mesh_config_init(void)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();

    memset(ctx, 0, sizeof(mesh_config_ctx_t));

    if(wifi_mesh_cfg_read((char *)&ctx->config, sizeof(mesh_config_t))) {
        ctx->config.max_layer = MESH_LAYER_MAX;
        ctx->config.linkrssi_loss = MESH_LOSS_RSSI;
        ctx->config.high_rssi_thd = MESH_HIGH_RSSI_THD;
        ctx->config.high_rssi_thd = MESH_LOW_RSSI_THD;
        ctx->config.move_diff_rssi = MESH_MOVE_DIFF_RSSI;
        ctx->config.beacon_interval = MESH_DEAULT_BEACON_INTERVAL;
        ctx->config.vote_timems = MESH_VOTE_TIMEOUTMS;
        ctx->config.use_bssid_as_major_id = 1;
        if(!ctx->config.use_bssid_as_major_id) {
            memcpy(ctx->config.major_id,  "\x12\x34\x56\x78\x90\x12", MESH_ID_LEN);
        }
        mesh_i("mesh config use default");
    }

    ctx->config.seqno = 0;
    ctx->config.count_down = 0;
    ctx->config_mutex = os_mutex_create();
    ctx->init_done = 1;
    softap_he_enable_set(0);
    bam_set_agg_enable(1);
    wifi_mesh_monitor_enable_set(1);
    mesh_config_dump();
    return 0;
}

int mesh_config_deinit(void)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();

    if(!ctx->init_done) {
        return -1;
    }

    os_mutex_destroy(ctx->config_mutex);
    memset(ctx, 0, sizeof(mesh_config_ctx_t));
    return 0;
}

int mesh_config_max_layer_get(void)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();
    return ctx->config.max_layer;
}

int mesh_config_max_layer_set(char layer)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();

    ctx->config.max_layer = layer;

    return 0;
}

int mesh_config_high_rssi_thd_get(void)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();
    return ctx->config.high_rssi_thd;
}

int mesh_config_high_rssi_thd_set(char rssi)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();

    ctx->config.high_rssi_thd = rssi;

    return 0;
}
int mesh_config_low_rssi_thd_get(void)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();
    return ctx->config.low_rssi_thd;
}

int mesh_config_low_rssi_thd_set(char rssi)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();

    ctx->config.low_rssi_thd = rssi;

    return 0;
}

int mesh_config_move_diff_rssi_get(void)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();
    return ctx->config.move_diff_rssi;
}

int mesh_config_move_diff_rssi_set(char rssi)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();
    ctx->config.move_diff_rssi = rssi;
    return 0;
}

int mesh_config_vote_timems_get(void)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();
    
    return ctx->config.vote_timems;
}

int mesh_config_vote_timems_set(int timems)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();
    ctx->config.vote_timems = timems;
    return 0;
}

int mesh_config_linkrssi_loss_get(void)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();
    return ctx->config.linkrssi_loss;
}

int mesh_config_linkrssi_loss_set(int loss)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();

    ctx->config.linkrssi_loss = loss;
    return 0;
}

int mesh_config_beacon_interval_set(int val)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();

    ctx->config.beacon_interval = val;
    return 0;
}

int mesh_config_beacon_interval_get(void)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();

    return ctx->config.beacon_interval;
}

void mesh_config_update(void *env, void *data, int len, unsigned char channel, unsigned char *bssid, signed char rssi)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();

    if (!ctx->init_done || len < sizeof(mesh_config_t)) {
        return;
    }

    mesh_config_t *config = (mesh_config_t *)data;
    os_mutex_lock(ctx->config_mutex, WAIT_FOREVER);
    if (memcmp(ctx->config.major_id, config->major_id, MESH_ID_LEN)) {
        goto BACK;
    }

    if (memcmp((char *)&ctx->config + sizeof(short), (char *)config + sizeof(short), sizeof(mesh_config_t) - sizeof(short)) == 0) { /*memcmp ignore count_down*/
        goto BACK;
    }

    if (ctx->config.seqno >= config->seqno) {
        goto BACK;
    }

    if (wifi_mesh_cfg_write((char *)config, sizeof(mesh_config_t))) {
        mesh_e("mesh config save fail!!!");
        goto BACK;
    }

    memcpy(&ctx->config, config, sizeof(mesh_config_t));
    mesh_config_dump();

BACK:
    os_mutex_unlock(ctx->config_mutex);
}

int mesh_config_count_down(void)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();

    if (!ctx->init_done || !ctx->config.count_down) {
        return 0;
    }

    os_mutex_lock(ctx->config_mutex, WAIT_FOREVER);
    ctx->config.count_down--;
    os_mutex_unlock(ctx->config_mutex);

    return ctx->config.count_down;
}

mesh_config_t *mesh_config_get(void)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();
    return &ctx->config;
}

int mesh_config_set_id(    unsigned char *major_id, unsigned char *minor_id)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();

    memcpy(ctx->config.major_id, major_id, MESH_ID_LEN);
    memcpy(ctx->config.minor_id, minor_id, MESH_ID_LEN);

    return 0;
}

int mesh_config_bssid_as_major_id(    unsigned char *bssid)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();

    if(!ctx->config.use_bssid_as_major_id) {
        return -1;
    }

    if(memcmp(ctx->config.major_id, bssid, MESH_ID_LEN) == 0) {
        return 0;
    }

    memcpy(ctx->config.major_id, bssid, MESH_ID_LEN);
    if (wifi_mesh_cfg_write((char *)&ctx->config, sizeof(mesh_config_t))) {
        mesh_e("mesh config save major_id fail!!!");
        return -1;
    }

    return 0;
}

unsigned char * mesh_config_get_major_id(void)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();

    return ctx->config.major_id;
}
void mesh_config_dump(void)
{
    mesh_config_ctx_t *ctx = mesh_config_ctx_get();
    mesh_i("config: mesh id:"MESH_MAC_STR", bssid_as_major_id:%d, seqno:%d vote_timems:%d beacon_interval:%d", MESH_MAC_VALUE(ctx->config.major_id), ctx->config.use_bssid_as_major_id, ctx->config.seqno, ctx->config.vote_timems, ctx->config.beacon_interval);
    mesh_i("config: max_layer:%d linkrssi_loss:%d high_rssi_thd=%d move_diff_rssi:%d", ctx->config.max_layer, ctx->config.linkrssi_loss, ctx->config.high_rssi_thd, ctx->config.move_diff_rssi);
}

