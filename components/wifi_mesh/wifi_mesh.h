#ifndef __WIFI_MESH_H__
#define __WIFI_MESH_H__

#define MESH_SSID_LEN      (32+1)
#define MESH_PASSWD_LEN    (64+1)
#define MESH_BSSID_LEN     (6)
#define MESH_MAJOR_ID_LEN  (6)

typedef void (* mesh_notity_handler_t)(int status, void *data);

typedef struct wifi_ap_cfg {
    uint8_t passwd[MESH_PASSWD_LEN];
    uint8_t max_connection;
}wifi_softap_cfg_t;

typedef struct wifi_router_cfg {
    unsigned char channel;
    unsigned int ssid[MESH_SSID_LEN];
    unsigned char passwd[MESH_PASSWD_LEN];
    unsigned int bssid[MESH_BSSID_LEN];
}wifi_router_cfg_t;

typedef struct wifi_mesh_cfg{
    unsigned int multi_mesh_enable:1;
    unsigned int mesh_max_layer:3;
    float vote_percentage;
    signed char rssi_thd;
    wifi_softap_cfg_t softap_cfg;
}wifi_mesh_cfg_t;

int wifi_mesh_init(void);
int wifi_mesh_deinit(void);
int wifi_mesh_config_set(wifi_mesh_cfg_t *config);
int wifi_mesh_config_get(wifi_mesh_cfg_t *config);
int wifi_mesh_notity_handler_register(mesh_notity_handler_t notity);
int wifi_mesh_start(void);
int wifi_mesh_stop(void);
#endif
