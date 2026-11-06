/*******************************************************************************
 * Copyright by Transa Semi.
 *
 * File Name:    
 * File Mark:    
 * Description:  
 * Others:        
 * Version:       V1.0
 * Author:        lixiao
 * Date:          2018-12-12
 * History 1:      
 *     Date: 
 *     Version:
 *     Author: 
 *     Modification:  
 * History 2: 
  ********************************************************************************/

#ifndef _SYSTEM_NETWORK_H
#define _SYSTEM_NETWORK_H

#include "lwip/netifapi.h"
#include "system_wifi_def.h"

#ifdef __cplusplus
    extern "C" {
#endif

#define SYSTEM_NETWORK_SOFTAP_DEFAULT_IP "192.168.4.1"
#define SYSTEM_NETWORK_SOFTAP_DEFAULT_IPMASK "255.255.255.0"

/****************************************************************************
* 	                                        Include files
****************************************************************************/


/****************************************************************************
* 	                                        Macros
****************************************************************************/
#define DHCP_MAX_WAIT_TIMEOUT (3 * 60 * 1000)

bool wifi_get_ip_info(wifi_interface_e if_index, struct ip_info *info);
bool wifi_set_ip_info(wifi_interface_e if_index, struct ip_info *info);
#ifndef CONFIG_CUSTOM_LXW
void wifi_dhcp_open(int vif);
void wifi_dhcp_close(int vif);
bool get_wifi_dhcp_use_status(int vif);
int wifi_station_dhcpc_start(int vif);
int wifi_station_dhcpc_stop(int vif);
int wifi_station_dhcpc_stop2(int vif, bool release_dhcp);
dhcp_status_t wifi_station_dhcpc_status(int vif);
void wifi_softap_dhcps_start(int intf);
void wifi_softap_dhcps_stop(int intf);
dhcp_status_t wifi_softap_dhcps_status(int vif);
#endif
void wifi_ifconfig(char* cmd);
void ping_run(char *cmd);
int iperf_run(char *cmd);
void set_softap_ipconfig(struct ip_info *ipconfig);
void set_softap_dhcps_cfg(void *dhcps_cfg);
void set_sta_ipconfig(struct ip_info *ipconfig);
void clear_sta_ipconfig(void);
void wifi_send_heartbeat_pkg(void);
void discard_dhcp_wait_timer();
void dhcp_wait_timeout_func(os_timer_handle_t timer);


#ifdef __cplusplus
}
#endif
#endif/*_SYSTEM_NETWORK_H*/

