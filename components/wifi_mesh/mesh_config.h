#ifndef __MESH_CONFIG_H__
#define __MESH_CONFIG_H__

int mesh_config_init(void);
int mesh_config_deinit(void);
int mesh_config_max_layer_get(void);
int mesh_config_max_layer_set(char layer);
int mesh_config_high_rssi_thd_get(void);
int mesh_config_low_rssi_thd_get(void);
int mesh_config_move_diff_rssi_get(void);
int mesh_config_vote_timems_get(void);
int mesh_config_linkrssi_loss_get(void);
int mesh_config_beacon_interval_get(void);
void mesh_config_update(void *env, void *data, int len, unsigned char channel, unsigned char *bssid, signed char rssi);
int mesh_config_count_down(void);
mesh_config_t *mesh_config_get(void);
int mesh_config_set_id(    unsigned char *major_id, unsigned char *minor_id);
int mesh_config_bssid_as_major_id(    unsigned char *bssid);
unsigned char * mesh_config_get_major_id(void);
void mesh_config_dump(void);
#endif
