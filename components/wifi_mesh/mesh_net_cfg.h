#ifndef __MESH_NET_CFG_H__
#define __MESH_NET_CFG_H__

typedef void (* mesh_cfg_notity_handler_t)(unsigned char *from_bssid, unsigned char *to_bssid, unsigned char *key, unsigned int timestamp);

int apnet_service_main(void *param);
int apnet_cfg_notity_register_handler(mesh_cfg_notity_handler_t handler);
void apnet_cfg_deinit(void);
void apnet_cfg_init(void);

#endif
