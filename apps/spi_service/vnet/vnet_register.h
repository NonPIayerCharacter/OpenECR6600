#ifndef __VNET_REG__H__
#define __VNET_REG__H__

#include "system_wifi_def.h"

#define VNET_WIFI_LINK_DOWN 0
#define VNET_WIFI_LINK_UP   1

/* VNETINFO VERSION SHOULD BE 0xA0 0xA1... */
#define VNET_WIFI_INFO_VER  0xA0

#define VNET_WIFI_SET_STA_LINKUP(X) ((X) |= BIT0)
#define VNET_WIFI_ISSET_STA_LINKUP(X) (((X) & BIT0) != 0)
#define VNET_WIFI_UNSET_STA_LINKUP(X) ((X) &= ~BIT0)
#define VNET_WIFI_SET_AP_LINKUP(X) ((X) |= BIT1)
#define VNET_WIFI_ISSET_AP_LINKUP(X) (((X) & BIT1) != 0)
#define VNET_WIFI_UNSET_AP_LINKUP(X) ((X) &= ~BIT1)

typedef struct {
    unsigned int status;
    unsigned int intFlag;
    unsigned int intMask;
    unsigned int intClr;
    unsigned int ipAddr;
    unsigned int ipMask;
    unsigned int stamac[2];
    unsigned int gw[4];
    unsigned int dns[4];
    unsigned int fwVersion;
    unsigned int powerOff;
    unsigned int apAddr;
    unsigned int apMask;
    unsigned int apgw;
    unsigned int apmac[2];
    unsigned int reserve[41];
} vnet_reg_t;

void vnet_reg_wifilink_status(wifi_interface_e iface, int link);
vnet_reg_t *vnet_reg_get_addr(void);
void vnet_reg_get_info(void);

#endif
