#ifndef __MESH_VOTE_H__
#define __MESH_VOTE_H__

#include "mesh_common.h"

typedef enum MESH_ELECTION_EVENT_ID {
    MESH_ELECTION_EARLY_TERM_FAST_JOIN,
    MESH_ELECTION_COMPLETE_ROOT_AP_JOIN,
    MESH_ELECTION_DELAYED_OPTIMAL_JOIN,
    MESH_ELECTION_FREECHOICE_OPTIMAL_JOIN,
    MESH_ELECTION_MAX,
}MESH_ELECTION_EVENT_ID_E;


typedef void (* mesh_vote_notity_handler_t)(int event, void *data, int len);


void mesh_vote_init(unsigned char *own_mac, unsigned char *major_id, unsigned char *to_bssid, mesh_vote_notity_handler_t handler);
void mesh_vote_deinit(void);
int mesh_vote_handle(void);
int mesh_vote_mid_bssid_update(unsigned char *major_id, unsigned char *to_bssid);
unsigned char mesh_vote_channel_get(void);
void mesh_vote_best_score_reset();
void mesh_votelist_refresh();
void mesh_votelist_reset(void);
int mesh_can_bypass_to_root(signed char link_rssi);

void mesh_votelist_dump(void);
void mesh_scorelist_dump(void);

#endif
