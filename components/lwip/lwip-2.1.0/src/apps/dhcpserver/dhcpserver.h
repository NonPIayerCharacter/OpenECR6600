#ifndef __DHCPS_H__
#define __DHCPS_H__

#define USE_DNS

#include "lwip/ip_addr.h"
#include "system_wifi_def.h"
#include "system_network.h"

typedef struct dhcps_state {
	s16_t state;
} dhcps_state;


typedef struct dhcps_msg {
	u8_t op, htype, hlen, hops;
	u8_t xid[4];
	u16_t secs, flags;
	u8_t ciaddr[4];
	u8_t yiaddr[4];
	u8_t siaddr[4];
	u8_t giaddr[4];
	u8_t chaddr[16];
	u8_t sname[64];
	u8_t file[128];
	u8_t options[312];
} dhcps_msg;

#ifndef LWIP_OPEN_SRC
struct dhcps_lease {
#define DHCP_SERVER_MASK_INIT 0x1
#define DHCP_SERVER_MASK_IP   0x2
#define DHCP_SERVER_MASK_TIME 0x4
#define DHCP_SERVER_MASK_DNS  0x8
    unsigned int mask;
    ip_addr_t start_ip;
    ip_addr_t end_ip;
    unsigned int dhcps_lease;
    ip_addr_t dnsserver[DNS_MAX_SERVERS];
};

enum dhcps_offer_option {
	OFFER_START = 0x00,
	OFFER_ROUTER = 0x01,
	OFFER_END
};
#endif

struct dhcps_pool {
	ip_addr_t ip;
	u8_t mac[6];
	char hostname[WIFI_SOFTAP_STA_HOSTNAME_MAX_LEN];
	s32_t lease_timer;
};

typedef struct _list_node {
	void* pnode;
	struct _list_node* pnext;
} list_node;

#define DHCPS_COARSE_TIMER_SECS  1
#define DHCPS_MAX_LEASE 0x64
#define BOOTP_BROADCAST 0x8000

#define DHCP_REPLY          2
#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HLEN_ETHERNET  6
#define DHCP_MSG_LEN      236

#define DHCPS_SERVER_PORT  67
#define DHCPS_CLIENT_PORT  68

#define DHCPDISCOVER  1
#define DHCPOFFER     2
#define DHCPREQUEST   3
#define DHCPDECLINE   4
#define DHCPACK       5
#define DHCPNAK       6
#define DHCPRELEASE   7

#define DHCP_OPTION_SUBNET_MASK   1
#define DHCP_OPTION_ROUTER        3
#define DHCP_OPTION_DNS_SERVER    6
#define DHCP_OPTION_HOSTNAME      12
#define DHCP_OPTION_REQ_IPADDR   50
#define DHCP_OPTION_LEASE_TIME   51
#define DHCP_OPTION_MSG_TYPE     53
#define DHCP_OPTION_SERVER_ID    54
#define DHCP_OPTION_INTERFACE_MTU 26
#define DHCP_OPTION_PERFORM_ROUTER_DISCOVERY 31
#define DHCP_OPTION_BROADCAST_ADDRESS 28
#define DHCP_OPTION_REQ_LIST     55
#define DHCP_OPTION_END         255

#define MAX_STATION_NUM      8

#define DHCPS_STATE_OFFER 1
#define DHCPS_STATE_DECLINE 2
#define DHCPS_STATE_ACK 3
#define DHCPS_STATE_NAK 4
#define DHCPS_STATE_IDLE 5
#define DHCPS_STATE_RELEASE 6

#define   dhcps_router_enabled(offer)	((offer & OFFER_ROUTER) != 0)

void dhcps_start(struct netif *netif);
void dhcps_stop(void);

void dhcps_coarse_tmr(void);

int dhcps_lease_info_set(struct dhcps_lease* please);
int dhcps_lease_info_get(struct dhcps_lease *lease);
void dhcps_get_sta_hostname(const u8_t *mac, char *hostname, u16_t hostname_len);

#endif

