#ifndef __MESH_STORAGE_H__
#define __MESH_STORAGE_H__

int wifi_mesh_parent_node_write(char *data, unsigned int len);
int wifi_mesh_parent_node_read(char *data, unsigned int len);
int wifi_mesh_parent_node_del(void);
int wifi_mesh_network_cfg_write(char *data, unsigned int len);
int wifi_mesh_network_cfg_read(char *data, unsigned int len);
int wifi_mesh_network_cfg_del(void);
void wifi_mesh_reset_autoconn(void);
int wifi_mesh_cfg_write(char *data, unsigned int len);
int wifi_mesh_cfg_read(char *data, unsigned int len);
int wifi_mesh_cfg_del(void);
#endif