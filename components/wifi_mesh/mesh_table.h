#ifndef  __MESH_TABLE_H__
#define  __MESH_TABLE_H__

#include "mesh_common.h"

#define MESH_TABLE_APLIST_AGING_PERIOD    (10000)
#define MESH_TABLE_NOTITY_AGING_PERIOD    (30000)

typedef void (* mesh_table_notity_handler_t)(int event, void *data, int len);

typedef enum MESH_TABLE_EVENT_ID {
    MESH_TABLE_APLIST_UPATE,
    MESH_TABLE_APLIST_ADD,
    MESH_TABLE_APLIST_DEL,
    MESH_TABLE_VOTELIST_UPATE,
    MESH_TABLE_VOTELIST_ADD,
    MESH_TABLE_VOTELIST_DEL,
    MESH_TABLE_NOTITYLIST_UPATE,
    MESH_TABLE_NOTITYLIST_ADD,
    MESH_TABLE_NOTITYLIST_DEL,
    MESH_TABLE_NETWOTK_CFG_UPATE,
    MESH_TABLE_NETWOTK_CFG_ADD,
    MESH_TABLE_NETWOTK_CFG_DEL,
}MESH_TABLE_EVENT_ID_E;

void mesh_table_init(mesh_table_notity_handler_t handler);
void mesh_table_deinit(void);
void mesh_table_refresh(void);
void mesh_table_vote_node_get(void);
void mesh_table_aplist_update(void *env, void *buf, int len, signed char rssi);
mesh_aplist_t * mesh_aplist_bssid_search(unsigned char *bssid);
void mesh_table_aplist_dump(void);
void mesh_table_aplist_reset(void);
void mesh_table_notitylist_update(void *env, void *data, int len, unsigned char channel, unsigned char *bssid, signed char rssi);
mesh_node_list_t * mesh_notitylist_best_node_get(void);
int mesh_notitylist_set_allow(unsigned char *mac, unsigned char allow);
mesh_node_list_t * mesh_notitylist_search_by_mac(unsigned char *bssid);
void mesh_table_notitylist_reset(void);
void mesh_table_notity_list_refresh(void);
void mesh_table_notitylist_node_dump(mesh_node_list_t *temp);
void mesh_table_notitylist_dump(int cnt);
void mesh_table_network_cfg_update(void *env, void *data, int len, unsigned char channel, unsigned char *bssid, signed char rssi);
mesh_network_cfg_t *mesh_table_network_cfg_get(void);
mesh_node_t *mesh_table_parent_node_get(void);
int mesh_table_parent_node_set(mesh_node_t *node);
void mesh_table_blacklist_refresh(void);
int mesh_table_blacklist_add(unsigned char *mac, unsigned del_timems);
int mesh_table_blacklist_del(unsigned char *mac);
void mesh_table_network_cfg_dump(void);

#endif
