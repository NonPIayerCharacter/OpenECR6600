#ifndef CMD_STATIC_IP_H
#define CMD_STATIC_IP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STATIC_IP_ADDR_LEN 16

typedef struct {
    char ip[STATIC_IP_ADDR_LEN];
    char netmask[STATIC_IP_ADDR_LEN];
    char gateway[STATIC_IP_ADDR_LEN];
    char dns1[STATIC_IP_ADDR_LEN];
    char dns2[STATIC_IP_ADDR_LEN];
} static_ip_config_t;

typedef struct {
    bool configured;
    bool dhcp_stopped;
    int vif_index;
    static_ip_config_t config;
} static_ip_status_t;

void static_ip_get_defaults(static_ip_config_t *cfg);
int static_ip_set(int vif, const static_ip_config_t *cfg);
int static_ip_get(int vif, static_ip_config_t *cfg);
int static_ip_restore_dhcp(int vif);
int static_ip_get_status(int vif, static_ip_status_t *status);

#ifdef __cplusplus
}
#endif

#endif
