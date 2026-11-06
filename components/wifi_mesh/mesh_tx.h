#ifndef __MESH_TX_H__
#define __MESH_TX_H__

typedef struct beacon_data {
    unsigned char is_send;
    unsigned char len;
    unsigned char *data;
}beacon_data_t;

int mesh_tx_probe(unsigned char *own_mac, unsigned char *dst,  char *ssid);
int mesh_tx_beacon_data(unsigned char *bssid, beacon_data_t *vote, beacon_data_t *notity, 
                beacon_data_t *network_cfg, beacon_data_t *mesh_cfg);
void mesh_tx_beacon_update_enable(int enable);
void mesh_tx_init(unsigned char *own_mac);
int mesh_tx_beacon_update_by_type(int type, unsigned char *data, unsigned int len, int immediate);
int mesh_tx_beacon_update(void);


#endif
