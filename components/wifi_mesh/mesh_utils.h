#ifndef __MESH_UTILS_H__
#define __MESH_UTILS_H__

#include "mesh_common.h"

const unsigned char *mesh_find_ie(unsigned char eid, const unsigned char *ies, int len);
int mesh_get_ssid(unsigned char *beacon_frame, unsigned short frame_len, unsigned char *ssid);
int mesh_get_bss_channel(unsigned char *beacon_frame, unsigned short frame_len);
int mesh_get_bssid(unsigned char *in, unsigned char *mac);
const unsigned char *mesh_find_vendor_ie(const unsigned char *oui, unsigned char oui_type, const unsigned char *ies, int len);
char mesh_average_rssi(mesh_rssi_record_t *record, char new);

void mesh_dump_hex(unsigned char *data, int len, int tab_num);
void dump_mac(char *str, unsigned char *mac);
const char *mesh_state_txt_get(int state);
int8_t mesh_hex2num(char c);

#endif
