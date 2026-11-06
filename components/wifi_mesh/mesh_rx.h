#ifndef __MESH_RX_H__
#define __MESH_RX_H__

typedef void (* mesh_rx_frame_handler_t)(void *env, void *buf, int len, signed char rssi);
typedef void (* mesh_rx_vendor_data_handler_t)(void *env, void *data, int len, unsigned char channel, unsigned char *bssid, signed char rssi);

int mesh_rx_init(void);
int mesh_rx_frame_register_handler(mesh_rx_frame_handler_t handler, void *env);
int mesh_rx_frame_unregister_handler(mesh_rx_frame_handler_t handler);
int mesh_rx_vendor_data_register_handler(unsigned char *oui, unsigned char out_type, mesh_rx_vendor_data_handler_t handler, void *env);
int mesh_rx_vendor_data_unregister_handler(mesh_rx_vendor_data_handler_t handler);
int mesh_rx_deinit(void);
int mesh_rx_start(void);
#endif