// Copyright 2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "lwip/inet.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/mem.h"
#include "apps/dhcpserver/dhcpserver.h"
#include "system_wifi.h"
#include "os.h"
#include "string.h"

#if LWIP_IPV4 && LWIP_DHCPS /* don't build if not configured for use in lwipopts.h */

#define DHCPS_DEBUG_DATA	1

static const u32_t magic_cookie   = 0x63538263;
static struct udp_pcb* pcb_dhcps = NULL;
static ip_addr_t broadcast_dhcps;
static ip_addr_t server_address;
static ip_addr_t client_address;//added
static ip_addr_t client_address_plus;

static struct dhcps_lease dhcps_lease;
static ip_addr_t dhcps_dns_server[2];
static list_node* plist = NULL;
static u8_t offer = 0xFF;
static bool renew = false;
#define DHCPS_LEASE_TIME_DEF	(120)
u32_t dhcps_lease_time = DHCPS_LEASE_TIME_DEF;  //minute
s32_t dhcps_max_lease_time = -((60 * DHCPS_LEASE_TIME_DEF) / DHCPS_COARSE_TIMER_SECS);
static u8_t softap_if_id = SOFTAP_IF; // index of SOFTAP_IF
struct netif* dhcps_netif = NULL;

/******************************************************************************
 * FunctionName : node_insert_to_list
 * Description  : insert the node to the list
 * Parameters   : phead -- the head node of the list
 *                pinsert -- the insert node of the list
 * Returns      : none
*******************************************************************************/
void node_insert_to_list(list_node** phead, list_node* pinsert)
{
	list_node* plist = NULL;
	struct dhcps_pool* pdhcps_pool = NULL;
	struct dhcps_pool* pdhcps_node = NULL;

	if (*phead == NULL) {
		*phead = pinsert;
	} else {
		plist = *phead;
		pdhcps_node = pinsert->pnode;
		pdhcps_pool = plist->pnode;
		#if LWIP_IPV4 && LWIP_IPV6
		if (pdhcps_node->ip.u_addr.ip4.addr < pdhcps_pool->ip.u_addr.ip4.addr)
		#elif LWIP_IPV4
		if (pdhcps_node->ip.addr < pdhcps_pool->ip.addr)
		#endif
		{
			pinsert->pnext = plist;
			*phead = pinsert;
		} else {
			while (plist->pnext != NULL) {
				pdhcps_pool = plist->pnext->pnode;
				#if LWIP_IPV4 && LWIP_IPV6
				if (pdhcps_node->ip.u_addr.ip4.addr < pdhcps_pool->ip.u_addr.ip4.addr)
				#elif LWIP_IPV4
				if (pdhcps_node->ip.addr < pdhcps_pool->ip.addr)
				#endif
				{
					pinsert->pnext = plist->pnext;
					plist->pnext = pinsert;
					break;
				}
				plist = plist->pnext;
			}

			if (plist->pnext == NULL) {
				plist->pnext = pinsert;
			}
		}
	}
}

/******************************************************************************
 * FunctionName : node_delete_from_list
 * Description  : remove the node from list
 * Parameters   : phead -- the head node of the list
 *                pdelete -- the remove node of the list
 * Returns      : none
*******************************************************************************/
void node_remove_from_list(list_node** phead, list_node* pdelete)
{
	list_node* plist = NULL;

	plist = *phead;

	if (plist == NULL) {
		*phead = NULL;
	} else {
		if (plist == pdelete) {
			*phead = plist->pnext;
			pdelete->pnext = NULL;
		} else {
			while (plist != NULL) {
				if (plist->pnext == pdelete) {
					plist->pnext = pdelete->pnext;
					pdelete->pnext = NULL;
				}
				plist = plist->pnext;
			}
		}
	}
}

/******************************************************************************
 * FunctionName : add_msg_type
 * Description  : add TYPE option of DHCP message
 * Parameters   : optptr -- the addr of DHCP message option
 * Returns      : the addr of DHCP message option
*******************************************************************************/
static u8_t* add_msg_type(u8_t* optptr, u8_t type)
{

	*optptr++ = DHCP_OPTION_MSG_TYPE;
	*optptr++ = 1;
	*optptr++ = type;
	return optptr;
}

/******************************************************************************
 * FunctionName : add_offer_options
 * Description  : add OFFER option of DHCP message
 * Parameters   : optptr -- the addr of DHCP message option
 * Returns      : the addr of DHCP message option
*******************************************************************************/
static u8_t* add_offer_options(u8_t* optptr)
{
	ip_addr_t ipadd;

	#if LWIP_IPV4 && LWIP_IPV6
	ipadd.u_addr.ip4.addr = *((u32_t*) &server_address.u_addr.ip4.addr);
	#elif LWIP_IPV4
	ipadd.addr = *((u32_t*) &server_address);
	#endif

#ifdef USE_CLASS_B_NET
	*optptr++ = DHCP_OPTION_SUBNET_MASK;
	*optptr++ = 4;  //length
	*optptr++ = 255;
	*optptr++ = 240;
	*optptr++ = 0;
	*optptr++ = 0;
#else
	*optptr++ = DHCP_OPTION_SUBNET_MASK;
	*optptr++ = 4;
	*optptr++ = 255;
	*optptr++ = 255;
	*optptr++ = 255;
	*optptr++ = 0;
#endif

	*optptr++ = DHCP_OPTION_LEASE_TIME;
	*optptr++ = 4;
	*optptr++ = ((DHCPS_LEASE_TIMER * 60) >> 24) & 0xFF;
	*optptr++ = ((DHCPS_LEASE_TIMER * 60) >> 16) & 0xFF;
	*optptr++ = ((DHCPS_LEASE_TIMER * 60) >> 8) & 0xFF;
	*optptr++ = ((DHCPS_LEASE_TIMER * 60) >> 0) & 0xFF;

	*optptr++ = DHCP_OPTION_SERVER_ID;
	*optptr++ = 4;

	#if LWIP_IPV4 && LWIP_IPV6
	*optptr++ = ip4_addr1(&ipadd.u_addr.ip4);
	*optptr++ = ip4_addr2(&ipadd.u_addr.ip4);
	*optptr++ = ip4_addr3(&ipadd.u_addr.ip4);
	*optptr++ = ip4_addr4(&ipadd.u_addr.ip4);
	#elif LWIP_IPV4
	*optptr++ = ip4_addr1(&ipadd);
	*optptr++ = ip4_addr2(&ipadd);
	*optptr++ = ip4_addr3(&ipadd);
	*optptr++ = ip4_addr4(&ipadd);
	#endif

	if (dhcps_router_enabled(offer)) {
		struct ip_info if_ip;
		bzero(&if_ip, sizeof(struct ip_info));
		wifi_get_ip_info(softap_if_id, &if_ip);

		*optptr++ = DHCP_OPTION_ROUTER;
		*optptr++ = 4;
		#if LWIP_IPV4 && LWIP_IPV6
		*optptr++ = ip4_addr1(&if_ip.gw.u_addr.ip4);
		*optptr++ = ip4_addr2(&if_ip.gw.u_addr.ip4);
		*optptr++ = ip4_addr3(&if_ip.gw.u_addr.ip4);
		*optptr++ = ip4_addr4(&if_ip.gw.u_addr.ip4);
		#elif LWIP_IPV4
		*optptr++ = ip4_addr1(&if_ip.gw);
		*optptr++ = ip4_addr2(&if_ip.gw);
		*optptr++ = ip4_addr3(&if_ip.gw);
		*optptr++ = ip4_addr4(&if_ip.gw);
		#endif
	}

#ifdef USE_DNS
	*optptr++ = DHCP_OPTION_DNS_SERVER;
#if defined ENABLE_LWIP_NAPT || (CONFIG_SPI_REPEATER && CONFIG_SPI_MASTER)
	*optptr++ = 8;

	#if LWIP_IPV4 && LWIP_IPV6
	*optptr++ = ip4_addr1(&dhcps_dns_server[0].u_addr.ip4);
	*optptr++ = ip4_addr2(&dhcps_dns_server[0].u_addr.ip4);
	*optptr++ = ip4_addr3(&dhcps_dns_server[0].u_addr.ip4);
	*optptr++ = ip4_addr4(&dhcps_dns_server[0].u_addr.ip4);
	*optptr++ = ip4_addr1(&dhcps_dns_server[1].u_addr.ip4);
	*optptr++ = ip4_addr2(&dhcps_dns_server[1].u_addr.ip4);
	*optptr++ = ip4_addr3(&dhcps_dns_server[1].u_addr.ip4);
	*optptr++ = ip4_addr4(&dhcps_dns_server[1].u_addr.ip4);
	#elif LWIP_IPV4
	*optptr++ = ip4_addr1(&dhcps_dns_server[0]);
	*optptr++ = ip4_addr2(&dhcps_dns_server[0]);
	*optptr++ = ip4_addr3(&dhcps_dns_server[0]);
	*optptr++ = ip4_addr4(&dhcps_dns_server[0]);
	*optptr++ = ip4_addr1(&dhcps_dns_server[1]);
	*optptr++ = ip4_addr2(&dhcps_dns_server[1]);
	*optptr++ = ip4_addr3(&dhcps_dns_server[1]);
	*optptr++ = ip4_addr4(&dhcps_dns_server[1]);
	#endif
#else
	*optptr++ = 4;

	#if LWIP_IPV4 && LWIP_IPV6
	*optptr++ = ip4_addr1(&ipadd.u_addr.ip4);
	*optptr++ = ip4_addr2(&ipadd.u_addr.ip4);
	*optptr++ = ip4_addr3(&ipadd.u_addr.ip4);
	*optptr++ = ip4_addr4(&ipadd.u_addr.ip4);
	#elif LWIP_IPV4
	*optptr++ = ip4_addr1(&ipadd);
	*optptr++ = ip4_addr2(&ipadd);
	*optptr++ = ip4_addr3(&ipadd);
	*optptr++ = ip4_addr4(&ipadd);
	#endif
#endif	
#endif

#ifdef CLASS_B_NET
	*optptr++ = DHCP_OPTION_BROADCAST_ADDRESS;
	*optptr++ = 4;

	#if LWIP_IPV4 && LWIP_IPV6
	*optptr++ = ip4_addr1(&ipadd.u_addr.ip4);
	#elif LWIP_IPV4
	*optptr++ = ip4_addr1(&ipadd);
	#endif
	*optptr++ = 255;
	*optptr++ = 255;
	*optptr++ = 255;
#else
	*optptr++ = DHCP_OPTION_BROADCAST_ADDRESS;
	*optptr++ = 4;
	#if LWIP_IPV4 && LWIP_IPV6
	*optptr++ = ip4_addr1(&ipadd.u_addr.ip4);
	*optptr++ = ip4_addr2(&ipadd.u_addr.ip4);
	*optptr++ = ip4_addr3(&ipadd.u_addr.ip4);
	#elif LWIP_IPV4
	*optptr++ = ip4_addr1(&ipadd);
	*optptr++ = ip4_addr2(&ipadd);
	*optptr++ = ip4_addr3(&ipadd);
	#endif
	*optptr++ = 255;
#endif

	*optptr++ = DHCP_OPTION_INTERFACE_MTU;
	*optptr++ = 2;
#ifdef CLASS_B_NET
	*optptr++ = 0x05;
	*optptr++ = 0xdc;
#else
	*optptr++ = 0x02;
	*optptr++ = 0x40;
#endif

	*optptr++ = DHCP_OPTION_PERFORM_ROUTER_DISCOVERY;
	*optptr++ = 1;
	*optptr++ = 0x00;

	*optptr++ = 43;
	*optptr++ = 6;

	*optptr++ = 0x01;
	*optptr++ = 4;
	*optptr++ = 0x00;
	*optptr++ = 0x00;
	*optptr++ = 0x00;
	*optptr++ = 0x02;

	return optptr;
}

/******************************************************************************
 * FunctionName : add_end
 * Description  : add end option of DHCP message
 * Parameters   : optptr -- the addr of DHCP message option
 * Returns      : the addr of DHCP message option
*******************************************************************************/
static u8_t* add_end(u8_t* optptr)
{
	*optptr++ = DHCP_OPTION_END;
	return optptr;
}

/******************************************************************************
 * FunctionName : create_msg
 * Description  : create response message
 * Parameters   : m -- DHCP message info
 * Returns      : none
*******************************************************************************/
static void create_msg(struct dhcps_msg* m)
{
	ip_addr_t client;
	#if LWIP_IPV4 && LWIP_IPV6
	client.u_addr.ip4.addr = *((uint32_t*) &client_address.u_addr.ip4.addr);
	#elif LWIP_IPV4
	client.addr = *((uint32_t*) &client_address);
	#endif

	m->op = DHCP_REPLY;
	m->htype = DHCP_HTYPE_ETHERNET;
	m->hlen = 6;
	m->hops = 0;
	m->secs = 0;
	m->flags = htons(BOOTP_BROADCAST);
	#if LWIP_IPV4 && LWIP_IPV6
	os_memcpy((char*) m->yiaddr, (char*) &client.u_addr.ip4.addr, sizeof(m->yiaddr));
	#elif LWIP_IPV4
	os_memcpy((char*) m->yiaddr, (char*) &client.addr, sizeof(m->yiaddr));
	#endif

	os_memset((char*) m->ciaddr, 0, sizeof(m->ciaddr));
	os_memset((char*) m->siaddr, 0, sizeof(m->siaddr));
	os_memset((char*) m->giaddr, 0, sizeof(m->giaddr));
	os_memset((char*) m->sname, 0, sizeof(m->sname));
	os_memset((char*) m->file, 0, sizeof(m->file));

	os_memset((char*) m->options, 0, sizeof(m->options));

	u32_t magic_cookie1 = magic_cookie;
	os_memcpy((char*) m->options, &magic_cookie1, sizeof(magic_cookie1));
}

/******************************************************************************
 * FunctionName : send_offer
 * Description  : DHCP message OFFER Response
 * Parameters   : m -- DHCP message info
 * Returns      : none
*******************************************************************************/
static void send_offer(struct dhcps_msg* m)
{
	u8_t* end;
	struct pbuf* p, *q;
	u8_t* data;
	u16_t cnt = 0;
	u16_t i;
	err_t SendOffer_err_t __attribute__((unused)) ;
	create_msg(m);

	end = add_msg_type(&m->options[4], DHCPOFFER);
	end = add_offer_options(end);
	end = add_end(end);

	p = pbuf_alloc(PBUF_TRANSPORT, sizeof(struct dhcps_msg), PBUF_RAM);

	LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("udhcp: send_offer>>p->ref = %d\n", p->ref));

	if (p != NULL) {
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: send_offer>>pbuf_alloc succeed\n"));
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: send_offer>>p->tot_len = %d\n", p->tot_len));
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: send_offer>>p->len = %d\n", p->len));

		q = p;

		while (q != NULL) {
			data = (u8_t*)q->payload;

			for (i = 0; i < q->len; i++) {
				data[i] = ((u8_t*) m)[cnt++];
#if DHCPS_DEBUG_DATA
				LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("%02x ", data[i]));
				if ((i + 1) % 16 == 0) {
					LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("\n"));
				}
#endif
			}

			q = q->next;
		}
	} else {
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: send_offer>>pbuf_alloc failed\n"));
		return;
	}
	SendOffer_err_t = udp_sendto(pcb_dhcps, p, &broadcast_dhcps, DHCPS_CLIENT_PORT);
	LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: send_offer>>udp_sendto result %x\n", SendOffer_err_t));

	if (p->ref != 0) {
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("udhcp: send_offer>>free pbuf\n"));
		pbuf_free(p);
	}
}

/******************************************************************************
 * FunctionName : send_nak
 * Description  : DHCP message NACK Response
 * Parameters   : m -- DHCP message info
 * Returns      : none
*******************************************************************************/
static void send_nak(struct dhcps_msg* m)
{
	u8_t* end;
	struct pbuf* p, *q;
	u8_t* data;
	u16_t cnt = 0;
	u16_t i;
	err_t SendNak_err_t __attribute__((unused));
	create_msg(m);

	end = add_msg_type(&m->options[4], DHCPNAK);
	end = add_end(end);

	p = pbuf_alloc(PBUF_TRANSPORT, sizeof(struct dhcps_msg), PBUF_RAM);
	LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("udhcp: send_nak>>p->ref = %d\n", p->ref));

	if (p != NULL) {
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: send_nak>>pbuf_alloc succeed\n"));
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: send_nak>>p->tot_len = %d\n", p->tot_len));
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: send_nak>>p->len = %d\n", p->len));
		q = p;

		while (q != NULL) {
			data = (u8_t*)q->payload;

			for (i = 0; i < q->len; i++) {
				data[i] = ((u8_t*) m)[cnt++];
#if DHCPS_DEBUG_DATA
				LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("%02x ", data[i]));
				if ((i + 1) % 16 == 0) {
					LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("\n"));
				}
#endif
			}
			q = q->next;
		}
	} else {
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: send_nak>>pbuf_alloc failed\n"));
		return;
	}
	SendNak_err_t = udp_sendto(pcb_dhcps, p, &broadcast_dhcps, DHCPS_CLIENT_PORT);
	LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: send_nak>>udp_sendto result %x\n", SendNak_err_t));

	if (p->ref != 0) {
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("udhcp: send_nak>>free pbuf\n"));
		pbuf_free(p);
	}
}

/******************************************************************************
 * FunctionName : send_ack
 * Description  : DHCP message ACK Response
 * Parameters   : m -- DHCP message info
 * Returns      : none
*******************************************************************************/
static void send_ack(struct dhcps_msg* m, struct dhcps_pool* pool)
{
	u8_t* end;
	struct pbuf* p, *q;
	u8_t* data;
	u16_t cnt = 0;
	u16_t i;
	err_t SendAck_err_t;
	create_msg(m);

	end = add_msg_type(&m->options[4], DHCPACK);
	end = add_offer_options(end);
	end = add_end(end);

	p = pbuf_alloc(PBUF_TRANSPORT, sizeof(struct dhcps_msg), PBUF_RAM);
	LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("udhcp: send_ack>>p->ref = %d\n", p->ref));

	if (p != NULL) {
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: send_ack>>pbuf_alloc succeed\n"));
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: send_ack>>p->tot_len = %d\n", p->tot_len));
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: send_ack>>p->len = %d\n", p->len));

		q = p;

		while (q != NULL) {
			data = (u8_t*)q->payload;

			for (i = 0; i < q->len; i++) {
				data[i] = ((u8_t*) m)[cnt++];
#if DHCPS_DEBUG_DATA
				LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("%02x ", data[i]));
				if ((i + 1) % 16 == 0) {
					LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("\n"));
				}
#endif
			}
			q = q->next;
		}
	} else {
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: send_ack>>pbuf_alloc failed\n"));
		return;
	}


	if (pool == NULL)
	{
		SendAck_err_t = udp_sendto(pcb_dhcps, p, &broadcast_dhcps, DHCPS_CLIENT_PORT);
	}
	else
	{
		IP_SET_TYPE(&(pool->ip), IPADDR_TYPE_V4);
		SendAck_err_t = udp_sendto(pcb_dhcps, p, &(pool->ip), DHCPS_CLIENT_PORT);
	}
	
	LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: send_ack>>udp_sendto result %x\n", SendAck_err_t));

	if (p->ref != 0) {
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("udhcp: send_ack>>free pbuf\n"));
		pbuf_free(p);
	}

	extern void wifi_handle_ap_staipassigned_event(int vif, struct dhcps_msg* m);
	if(SendAck_err_t == ERR_OK) {
		wifi_handle_ap_staipassigned_event(softap_if_id, m);
	}
}

/******************************************************************************
 * FunctionName : parse_options
 * Description  : parse DHCP message options
 * Parameters   : optptr -- DHCP message option info
 *                len -- DHCP message option length
 * Returns      : none
*******************************************************************************/
static u8_t parse_options(u8_t* optptr, s16_t len)
{
	ip_addr_t client;
	bool is_dhcp_parse_end = false;
	struct dhcps_state s;

	#if LWIP_IPV4 && LWIP_IPV6
	client.u_addr.ip4.addr = *((uint32_t*) &client_address.u_addr.ip4.addr);
	#elif LWIP_IPV4
	client.addr = *((uint32_t*) &client_address);
	#endif

	u8_t* end = optptr + len;
	u16_t type = 0;

	s.state = DHCPS_STATE_IDLE;

	while (optptr < end) {
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: (s16_t)*optptr = %d\n", (s16_t)*optptr));

		switch ((s16_t) *optptr) {

			case DHCP_OPTION_MSG_TYPE:	//53
				type = *(optptr + 2);
				break;

			case DHCP_OPTION_REQ_IPADDR://50
				#if LWIP_IPV4 && LWIP_IPV6
				if (os_memcmp((char*) &client.u_addr.ip4.addr, (char*) optptr + 2, 4) == 0)
				#elif LWIP_IPV4
				if (os_memcmp((char*) &client.addr, (char*) optptr + 2, 4) == 0)
				#endif
				{
					LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: DHCP_OPTION_REQ_IPADDR = 0 ok\n"));
					s.state = DHCPS_STATE_ACK;
				} else {
					LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: DHCP_OPTION_REQ_IPADDR != 0 err\n"));
					s.state = DHCPS_STATE_NAK;
				}

				break;

				case DHCP_OPTION_END: {
					is_dhcp_parse_end = true;
				}
				break;
		}

		if (is_dhcp_parse_end) {
			break;
		}

		optptr += optptr[1] + 2;
	}

	switch (type) {

		case DHCPDISCOVER://1
			s.state = DHCPS_STATE_OFFER;
			LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: DHCPD_STATE_OFFER\n"));
			break;

		case DHCPREQUEST://3
			if (!(s.state == DHCPS_STATE_ACK || s.state == DHCPS_STATE_NAK)) {
				if (renew == true) {
					s.state = DHCPS_STATE_ACK;
				} else {
					s.state = DHCPS_STATE_NAK;
				}

				LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: DHCPD_STATE_NAK\n"));
			}

			break;

		case DHCPDECLINE://4
			s.state = DHCPS_STATE_IDLE;
			LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: DHCPD_STATE_IDLE\n"));
			break;

		case DHCPRELEASE://7
			s.state = DHCPS_STATE_RELEASE;
			LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: DHCPD_STATE_IDLE\n"));
			break;
	}
	LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: return s.state = %d\n", s.state));

	return s.state;
}

/******************************************************************************
 * FunctionName : parse_msg
 * Description  : parse DHCP message from netif
 * Parameters   : m -- DHCP message info
 *                len -- DHCP message length
 * Returns      : DHCP message type
*******************************************************************************/
#if LWIP_IPV4 && LWIP_IPV6
static s16_t parse_msg(struct dhcps_msg* m, struct dhcps_pool** pool, u16_t len)
{
	u32_t lease_timer = (dhcps_lease_time * 60) / DHCPS_COARSE_TIMER_SECS;
	*pool = NULL;

	if (memcmp((char*)m->options,
	&magic_cookie,
	sizeof(magic_cookie)) == 0) {
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: len = %d\n", len));

		ip_addr_t addr_tmp;

		struct dhcps_pool* pdhcps_pool = NULL;
		list_node* pnode = NULL;
		list_node* pback_node = NULL;
		ip_addr_t first_address;
		bool flag = false;

		first_address.u_addr.ip4.addr = dhcps_lease.start_ip.u_addr.ip4.addr;
		client_address.u_addr.ip4.addr = client_address_plus.u_addr.ip4.addr;
		renew = false;

		if (plist != NULL) {
			for (pback_node = plist; pback_node != NULL; pback_node = pback_node->pnext) {
				pdhcps_pool = pback_node->pnode;

				if (memcmp(pdhcps_pool->mac, m->chaddr, sizeof(pdhcps_pool->mac)) == 0) {
					if (memcmp(&pdhcps_pool->ip.u_addr.ip4.addr, m->ciaddr, sizeof(pdhcps_pool->ip.u_addr.ip4.addr)) == 0)
					{
						renew = true;
						*pool = pdhcps_pool;
					}

					client_address.u_addr.ip4.addr = pdhcps_pool->ip.u_addr.ip4.addr;
					pdhcps_pool->lease_timer = lease_timer;
					pnode = pback_node;
					goto POOL_CHECK;
				}
				else if (pdhcps_pool->ip.u_addr.ip4.addr == client_address_plus.u_addr.ip4.addr) 
				{
					addr_tmp.u_addr.ip4.addr = htonl(client_address_plus.u_addr.ip4.addr);
					addr_tmp.u_addr.ip4.addr++;
					client_address_plus.u_addr.ip4.addr = htonl(addr_tmp.u_addr.ip4.addr);
					client_address.u_addr.ip4.addr = client_address_plus.u_addr.ip4.addr;
				}

				if (flag == false) { // search the fisrt unused ip
					if (first_address.u_addr.ip4.addr < pdhcps_pool->ip.u_addr.ip4.addr)
					{
						flag = true;
					}
					else 
					{
						addr_tmp.u_addr.ip4.addr = htonl(first_address.u_addr.ip4.addr);
						addr_tmp.u_addr.ip4.addr++;
						first_address.u_addr.ip4.addr = htonl(addr_tmp.u_addr.ip4.addr);
					}
				}
			}
		} else {
			client_address.u_addr.ip4.addr = dhcps_lease.start_ip.u_addr.ip4.addr;
		}

		if (client_address_plus.u_addr.ip4.addr > dhcps_lease.end_ip.u_addr.ip4.addr) {
			client_address.u_addr.ip4.addr = first_address.u_addr.ip4.addr;
		}

		if (client_address.u_addr.ip4.addr > dhcps_lease.end_ip.u_addr.ip4.addr) {
			client_address_plus.u_addr.ip4.addr = dhcps_lease.start_ip.u_addr.ip4.addr;
			pdhcps_pool = NULL;
			pnode = NULL;
		} else {
			pdhcps_pool = (struct dhcps_pool*)mem_malloc(sizeof(struct dhcps_pool));
			pdhcps_pool->ip.u_addr.ip4.addr = client_address.u_addr.ip4.addr;
			os_memcpy(pdhcps_pool->mac, m->chaddr, sizeof(pdhcps_pool->mac));
			pdhcps_pool->lease_timer = lease_timer;
			pnode = (list_node*)mem_malloc(sizeof(list_node));
			pnode->pnode = pdhcps_pool;
			pnode->pnext = NULL;
			node_insert_to_list(&plist, pnode);

			if (client_address.u_addr.ip4.addr == dhcps_lease.end_ip.u_addr.ip4.addr) {
				client_address_plus.u_addr.ip4.addr = dhcps_lease.start_ip.u_addr.ip4.addr;
			} else {
				addr_tmp.u_addr.ip4.addr = htonl(client_address.u_addr.ip4.addr);
				addr_tmp.u_addr.ip4.addr++;
				client_address_plus.u_addr.ip4.addr = htonl(addr_tmp.u_addr.ip4.addr);
			}
		}

		POOL_CHECK:

		if ((client_address.u_addr.ip4.addr > dhcps_lease.end_ip.u_addr.ip4.addr) ){//|| (ip_addr_isany(&client_address))) {
			if (pnode != NULL) {
				node_remove_from_list(&plist, pnode);
				mem_free(pnode);
				pnode = NULL;
			}

			if (pdhcps_pool != NULL) {
				mem_free(pdhcps_pool);
				pdhcps_pool = NULL;
			}

			return 4;
		}

		s16_t ret = parse_options(&m->options[4], len);

		if (ret == DHCPS_STATE_RELEASE) {
#if 0
			if (pnode != NULL) {
				node_remove_from_list(&plist, pnode);
				mem_free(pnode);
				pnode = NULL;
			}

			if (pdhcps_pool != NULL) {
				mem_free(pdhcps_pool);
				pdhcps_pool = NULL;
			}
#endif

			os_memset(&client_address, 0x0, sizeof(client_address));
		}
#if 0
		if (wifi_softap_set_station_info(m->chaddr, &client_address) == false) {
			return 0;
		}
#endif
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: xid changed\n"));
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: client_address.addr = %x\n", client_address.u_addr.ip4.addr));
		return ret;
	}

	return 0;
}

#elif LWIP_IPV4

static s16_t parse_msg(struct dhcps_msg* m, struct dhcps_pool** pool, u16_t len)
{
	u32_t lease_timer = (dhcps_lease_time * 60) / DHCPS_COARSE_TIMER_SECS;
	*pool = NULL;

	if (memcmp((char*)m->options,
	&magic_cookie,
	sizeof(magic_cookie)) == 0) {
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: len = %d\n", len));

		ip_addr_t addr_tmp;

		struct dhcps_pool* pdhcps_pool = NULL;
		list_node* pnode = NULL;
		list_node* pback_node = NULL;
		ip_addr_t first_address;
		bool flag = false;

		first_address.addr = dhcps_lease.start_ip.addr;
		client_address.addr = client_address_plus.addr;
		renew = false;

		if (plist != NULL) {
			for (pback_node = plist; pback_node != NULL; pback_node = pback_node->pnext) {
				pdhcps_pool = pback_node->pnode;

				if (memcmp(pdhcps_pool->mac, m->chaddr, sizeof(pdhcps_pool->mac)) == 0) {
					if (memcmp(&pdhcps_pool->ip.addr, m->ciaddr, sizeof(pdhcps_pool->ip.addr)) == 0)
					{
						renew = true;
						*pool = pdhcps_pool;
					}

					client_address.addr = pdhcps_pool->ip.addr;
					pdhcps_pool->lease_timer = lease_timer;
					pnode = pback_node;
					goto POOL_CHECK;
				}
				else if (pdhcps_pool->ip.addr == client_address_plus.addr) 
				{
					addr_tmp.addr = htonl(client_address_plus.addr);
					addr_tmp.addr++;
					client_address_plus.addr = htonl(addr_tmp.addr);
					client_address.addr = client_address_plus.addr;
				}

				if (flag == false) { // search the fisrt unused ip
					if (first_address.addr < pdhcps_pool->ip.addr)
					{
						flag = true;
					}
					else 
					{
						addr_tmp.addr = htonl(first_address.addr);
						addr_tmp.addr++;
						first_address.addr = htonl(addr_tmp.addr);
					}
				}
			}
		} else {
			client_address.addr = dhcps_lease.start_ip.addr;
		}

		if (client_address_plus.addr > dhcps_lease.end_ip.addr) {
			client_address.addr = first_address.addr;
		}

		if (client_address.addr > dhcps_lease.end_ip.addr) {
			client_address_plus.addr = dhcps_lease.start_ip.addr;
			pdhcps_pool = NULL;
			pnode = NULL;
		} else {
			pdhcps_pool = (struct dhcps_pool*)mem_malloc(sizeof(struct dhcps_pool));
			pdhcps_pool->ip.addr = client_address.addr;
			os_memcpy(pdhcps_pool->mac, m->chaddr, sizeof(pdhcps_pool->mac));
			pdhcps_pool->lease_timer = lease_timer;
			pnode = (list_node*)mem_malloc(sizeof(list_node));
			pnode->pnode = pdhcps_pool;
			pnode->pnext = NULL;
			node_insert_to_list(&plist, pnode);

			if (client_address.addr == dhcps_lease.end_ip.addr) {
				client_address_plus.addr = dhcps_lease.start_ip.addr;
			} else {
				addr_tmp.addr = htonl(client_address.addr);
				addr_tmp.addr++;
				client_address_plus.addr = htonl(addr_tmp.addr);
			}
		}

POOL_CHECK:

		if ((client_address.addr > dhcps_lease.end_ip.addr) ){//|| (ip_addr_isany(&client_address))) {
			if (pnode != NULL) {
				node_remove_from_list(&plist, pnode);
				mem_free(pnode);
				pnode = NULL;
			}

			if (pdhcps_pool != NULL) {
				mem_free(pdhcps_pool);
				pdhcps_pool = NULL;
			}

			return 4;
		}

		s16_t ret = parse_options(&m->options[4], len);

		if (ret == DHCPS_STATE_RELEASE) {
#if 0
			if (pnode != NULL) {
				node_remove_from_list(&plist, pnode);
				mem_free(pnode);
				pnode = NULL;
			}

			if (pdhcps_pool != NULL) {
				mem_free(pdhcps_pool);
				pdhcps_pool = NULL;
			}
#endif


			os_memset(&client_address, 0x0, sizeof(client_address));
		}
#if 0
		if (wifi_softap_set_station_info(m->chaddr, &client_address) == false) {
			return 0;
		}
#endif
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: xid changed\n"));
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: client_address.addr = %x\n", client_address.addr));
		return ret;
	}

	return 0;
}
#endif
/******************************************************************************
 * FunctionName : handle_dhcp
 * Description  : If an incoming DHCP message is in response to us, then trigger the state machine
 * Parameters   : arg -- arg user supplied argument (udp_pcb.recv_arg)
 * 				  pcb -- the udp_pcb which received data
 * 			      p -- the packet buffer that was received
 * 				  addr -- the remote IP address from which the packet was received
 * 				  port -- the remote port from which the packet was received
 * Returns      : none
*******************************************************************************/
static void handle_dhcp(void* arg,
                        struct udp_pcb* pcb,
                        struct pbuf* p,
                        const ip_addr_t* addr,
                        u16_t port)
{
	struct dhcps_msg* pmsg_dhcps = NULL;
	struct dhcps_pool* pool = NULL;
	s16_t tlen;
	u16_t i;
	u16_t dhcps_msg_cnt = 0;
	u8_t* p_dhcps_msg = NULL;
	u8_t* data;

	LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: handle_dhcp-> receive a packet\n"));

	if (p == NULL) {
		return;
	}

	pmsg_dhcps = (struct dhcps_msg*)mem_malloc(sizeof(struct dhcps_msg));

	if (NULL == pmsg_dhcps) {
		pbuf_free(p);
		return;
	}

	p_dhcps_msg = (u8_t*)pmsg_dhcps;
	tlen = p->tot_len;
	data = p->payload;

	LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: handle_dhcp-> p->tot_len = %d\n", tlen));
	LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: handle_dhcp-> p->len = %d\n", p->len));

	for (i = 0; i < p->len; i++) {
		p_dhcps_msg[dhcps_msg_cnt++] = data[i];
#if DHCPS_DEBUG_DATA
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("%02x ", data[i]));
		if ((i + 1) % 16 == 0) {
			LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("\n"));
		}
#endif
	}

	if (p->next != NULL) {
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: handle_dhcp-> p->next != NULL\n"));
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: handle_dhcp-> p->next->tot_len = %d\n", p->next->tot_len));
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: handle_dhcp-> p->next->len = %d\n", p->next->len));

		data = p->next->payload;

		for (i = 0; i < p->next->len; i++) {
			p_dhcps_msg[dhcps_msg_cnt++] = data[i];
#if DHCPS_DEBUG_DATA
			LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("%02x ", data[i]));
			if ((i + 1) % 16 == 0) {
				LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("\n"));
			}
#endif
		}
	}
	LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: handle_dhcp-> parse_msg(p)\n"));

	switch (parse_msg(pmsg_dhcps, &pool, tlen - 240)) {

		case DHCPS_STATE_OFFER:
			LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: handle_dhcp-> DHCPD_STATE_OFFER\n"));
			send_offer(pmsg_dhcps);
			break;

		case DHCPS_STATE_ACK://3
			LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: handle_dhcp-> DHCPD_STATE_ACK\n"));
			send_ack(pmsg_dhcps, pool);
			break;

		case DHCPS_STATE_NAK://4
			LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: handle_dhcp-> DHCPD_STATE_NAK\n"));
			send_nak(pmsg_dhcps);
			break;

		default :
			break;
	}
	LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps: handle_dhcp-> pbuf_free(p)\n"));

	pbuf_free(p);
	mem_free(pmsg_dhcps);
	pmsg_dhcps = NULL;
}

/******************************************************************************
 * FunctionName : wifi_softap_init_dhcps_lease
 * Description  : init ip lease from start to end for station
 * Parameters   : ip -- The current ip addr
 * Returns      : none
*******************************************************************************/
static void wifi_softap_init_dhcps_lease(u32_t ip)
{
	u32_t softap_ip = 0, local_ip = 0;
	u32_t start_ip = 0;
	u32_t end_ip = 0;

	if (dhcps_lease.enable == true) {
		softap_ip = htonl(ip);
		#if LWIP_IPV4 && LWIP_IPV6
		start_ip = htonl(dhcps_lease.start_ip.u_addr.ip4.addr);
		end_ip = htonl(dhcps_lease.end_ip.u_addr.ip4.addr);
		#elif LWIP_IPV4
		start_ip = htonl(dhcps_lease.start_ip.addr);
		end_ip = htonl(dhcps_lease.end_ip.addr);
		#endif

		/*config ip information can't contain local ip*/
		if ((start_ip <= softap_ip) && (softap_ip <= end_ip)) {
			dhcps_lease.enable = false;
		} else {
			/*config ip information must be in the same segment as the local ip*/
			softap_ip >>= 8;

			if (((start_ip >> 8 != softap_ip) || (end_ip >> 8 != softap_ip))
			|| (end_ip - start_ip > DHCPS_MAX_LEASE)) {
				dhcps_lease.enable = false;
			}
		}
	}

	if (dhcps_lease.enable == false) {
		local_ip = softap_ip = htonl(ip);
		softap_ip &= 0xFFFFFF00;
		local_ip &= 0xFF;

		if (local_ip >= 0x80) {
			local_ip -= DHCPS_MAX_LEASE;
		} else {
			local_ip ++;
		}

		bzero(&dhcps_lease, sizeof(dhcps_lease));
		#if LWIP_IPV4 && LWIP_IPV6
		dhcps_lease.start_ip.u_addr.ip4.addr = softap_ip | local_ip;
		dhcps_lease.end_ip.u_addr.ip4.addr = softap_ip | (local_ip + DHCPS_MAX_LEASE - 1);
		dhcps_lease.start_ip.u_addr.ip4.addr = htonl(dhcps_lease.start_ip.u_addr.ip4.addr);
		dhcps_lease.end_ip.u_addr.ip4.addr = htonl(dhcps_lease.end_ip.u_addr.ip4.addr);
		#elif LWIP_IPV4
		dhcps_lease.start_ip.addr = softap_ip | local_ip;
		dhcps_lease.end_ip.addr = softap_ip | (local_ip + DHCPS_MAX_LEASE - 1);
		dhcps_lease.start_ip.addr = htonl(dhcps_lease.start_ip.addr);
		dhcps_lease.end_ip.addr = htonl(dhcps_lease.end_ip.addr);
		#endif
	}

}

/******************************************************************************
 * FunctionName : dhcps_start
 * Description  : start dhcp server function
 * Parameters   : netif -- The current netif addr
 *              : info  -- The current ip info
 * Returns      : none
*******************************************************************************/
void dhcps_start(struct netif *netif, struct ip_info* info)
{
	if (netif->dhcps_pcb != NULL) {
		udp_remove(netif->dhcps_pcb);
	}

	pcb_dhcps = udp_new();

	if (pcb_dhcps == NULL || info == NULL) {
		LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps_start(): could not obtain pcb\n"));
        return;
	}
    dhcps_netif = netif;
    //softap_if_id = netif->num;
    pcb_dhcps->netif_idx = netif->num+1;
	netif->dhcps_pcb = pcb_dhcps;
	
	#if LWIP_IPV4 && LWIP_IPV6
	IP4_ADDR(&broadcast_dhcps.u_addr.ip4, 255, 255, 255, 255);

	server_address.u_addr.ip4.addr = info->ip.u_addr.ip4.addr;
    dhcps_dns_server[0].u_addr.ip4.addr  = info->dns1.u_addr.ip4.addr;
    dhcps_dns_server[1].u_addr.ip4.addr  = info->dns2.u_addr.ip4.addr;

	wifi_softap_init_dhcps_lease(server_address.u_addr.ip4.addr);
	client_address_plus.u_addr.ip4.addr = dhcps_lease.start_ip.u_addr.ip4.addr;
	#elif LWIP_IPV4
	IP4_ADDR(&broadcast_dhcps, 255, 255, 255, 255);
	
	server_address = info->ip;
	dhcps_dns_server[0]  = info->dns1;
	dhcps_dns_server[1]  = info->dns2;

	wifi_softap_init_dhcps_lease(server_address.addr);
	client_address_plus.addr = dhcps_lease.start_ip.addr;
	#endif
    udp_bind(pcb_dhcps, &netif->ip_addr, DHCPS_SERVER_PORT);
	udp_recv(pcb_dhcps, handle_dhcp, NULL);
	LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps:dhcps_start->udp_recv function Set a receive callback handle_dhcp for UDP_PCB pcb_dhcps\n"));
}

/******************************************************************************
 * FunctionName : dhcps_stop
 * Description  : stop dhcp server function
 * Parameters   : netif -- The current netif addr
 * Returns      : none
*******************************************************************************/
void dhcps_stop(void)
{
	struct netif* apnetif = dhcps_netif;

    if (apnetif == NULL) {
        LWIP_DEBUGF(DHCPS_DEBUG | LWIP_DBG_TRACE, ("dhcps_stop: apnetif == NULL\n"));
        return;
    }
    
    dhcps_netif = NULL;
    softap_if_id = SOFTAP_IF;
	if (apnetif->dhcps_pcb != NULL) {
        udp_disconnect(apnetif->dhcps_pcb);
		udp_remove(apnetif->dhcps_pcb);
		apnetif->dhcps_pcb = NULL;
	}

	list_node* pnode = NULL;
	list_node* pback_node = NULL;
	pnode = plist;

	while (pnode != NULL) {
		pback_node = pnode;
		pnode = pback_node->pnext;
		node_remove_from_list(&plist, pback_node);
		mem_free(pback_node->pnode);
		pback_node->pnode = NULL;
		mem_free(pback_node);
		pback_node = NULL;
	}
	plist = NULL;
}

bool wifi_softap_set_dhcps_lease(struct dhcps_lease* please)
{
	    struct ip_info info;
	    u32_t softap_ip = 0;
	    u32_t start_ip = 0;
	    u32_t end_ip = 0;
        
	    if (please == NULL || wifi_softap_dhcps_status(softap_if_id) == TCPIP_DHCP_STARTED) 
			return false;
	    if (please->enable) {
		        bzero(&info, sizeof(struct ip_info));
		        wifi_get_ip_info(softap_if_id, &info);
				#if LWIP_IPV4 && LWIP_IPV6
				softap_ip = htonl(info.ip.u_addr.ip4.addr);
		        start_ip = htonl(please->start_ip.u_addr.ip4.addr);
		        end_ip = htonl(please->end_ip.u_addr.ip4.addr);
				#elif LWIP_IPV4
		        softap_ip = htonl(info.ip.addr);
		        start_ip = htonl(please->start_ip.addr);
		        end_ip = htonl(please->end_ip.addr);
				#endif

		        /*config ip information can't contain local ip*/
		        if ((start_ip <= softap_ip) && (softap_ip <= end_ip)) {
				return false;
		        }

		        /*config ip information must be in the same segment as the local ip*/
		        softap_ip >>= 8;

		        if ((start_ip >> 8 != softap_ip)
		                || (end_ip >> 8 != softap_ip)) {
				return false;
		        }

		        if (end_ip - start_ip > DHCPS_MAX_LEASE) {
				return false;
		        }
		        bzero(&dhcps_lease, sizeof(dhcps_lease));
				#if LWIP_IPV4 && LWIP_IPV6
		        dhcps_lease.start_ip.u_addr.ip4.addr = please->start_ip.u_addr.ip4.addr;
		        dhcps_lease.end_ip.u_addr.ip4.addr = please->end_ip.u_addr.ip4.addr;
				#elif LWIP_IPV4
		        dhcps_lease.start_ip.addr = please->start_ip.addr;
		        dhcps_lease.end_ip.addr = please->end_ip.addr;
				#endif
	    }

	    dhcps_lease.enable = please->enable;
	    return true;
}

/******************************************************************************
 * FunctionName : wifi_softap_get_dhcps_lease
 * Description  : get the lease information of DHCP server
 * Parameters   : please -- Additional argument to get the lease information,
 * 							Little-Endian.
 * Returns      : true or false
*******************************************************************************/
bool wifi_softap_get_dhcps_lease(struct dhcps_lease* please)
{
	if (NULL == please) {
		return false;
	}

	if (dhcps_lease.enable == false) {
		if (wifi_softap_dhcps_status(softap_if_id) == TCPIP_DHCP_STOPPED) {
			return false;
		}
	}
	#if LWIP_IPV4 && LWIP_IPV6
	please->start_ip.u_addr.ip4.addr = dhcps_lease.start_ip.u_addr.ip4.addr;
	please->end_ip.u_addr.ip4.addr = dhcps_lease.end_ip.u_addr.ip4.addr;
	#elif LWIP_IPV4
	please->start_ip.addr = dhcps_lease.start_ip.addr;
	please->end_ip.addr = dhcps_lease.end_ip.addr;
	#endif
	
	return true;
}

/******************************************************************************
 * FunctionName : kill_oldest_dhcps_pool
 * Description  : remove the oldest node from list
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
static void kill_oldest_dhcps_pool(void)
{
	list_node* pre = NULL, *p = NULL;
	list_node* minpre = NULL, *minp = NULL;
	struct dhcps_pool* pdhcps_pool = NULL, *pmin_pool = NULL;
	pre = plist;
	p = pre->pnext;
	minpre = pre;
	minp = p;

	while (p != NULL) {
		pdhcps_pool = p->pnode;
		pmin_pool = minp->pnode;

		if (pdhcps_pool->lease_timer < pmin_pool->lease_timer) {
			minp = p;
			minpre = pre;
		}

		pre = p;
		p = p->pnext;
	}

	minpre->pnext = minp->pnext;
	mem_free(minp->pnode);
	minp->pnode = NULL;
	mem_free(minp);
	minp = NULL;
}

/******************************************************************************
 * FunctionName : dhcps_coarse_tmr
 * Description  : the lease time count
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void dhcps_coarse_tmr(void)
{
	u8_t num_dhcps_pool = 0;
	list_node* pback_node = NULL;
	list_node* pnode = NULL;
	struct dhcps_pool* pdhcps_pool = NULL;
	pnode = plist;

	while (pnode != NULL) {
		pdhcps_pool = pnode->pnode;
		pdhcps_pool->lease_timer --;

		if (pdhcps_pool->lease_timer == dhcps_max_lease_time) {
			pback_node = pnode;
			pnode = pback_node->pnext;
			node_remove_from_list(&plist, pback_node);
			mem_free(pback_node->pnode);
			pback_node->pnode = NULL;
			mem_free(pback_node);
			pback_node = NULL;
		} else {
			pnode = pnode ->pnext;
			num_dhcps_pool ++;
		}
	}

	if (num_dhcps_pool > MAX_STATION_NUM) {
		kill_oldest_dhcps_pool();
	}
}

bool wifi_softap_set_dhcps_offer_option(u8_t level, void* optarg)
{
	bool offer_flag = true;

	if (optarg == NULL && wifi_softap_dhcps_status(softap_if_id) == TCPIP_DHCP_STOPPED) {
		return false;
	}

	if (level <= OFFER_START || level >= OFFER_END) {
		return false;
	}

	switch (level) {
		case OFFER_ROUTER:
			offer = (*(u8_t*)optarg) & 0x01;
			offer_flag = true;
			break;

		default :
			offer_flag = false;
			break;
	}

	return offer_flag;
}

bool wifi_softap_set_dhcps_lease_time(u32_t minute)
{
	if (wifi_softap_dhcps_status(softap_if_id) == TCPIP_DHCP_STARTED) {
		return false;
	}

	if (minute == 0) {
		return false;
	}

	dhcps_lease_time = minute;
	return true;
}

bool wifi_softap_reset_dhcps_lease_time(void)
{
	if (wifi_softap_dhcps_status(softap_if_id) == TCPIP_DHCP_STARTED) {
		return false;
	}

	dhcps_lease_time = DHCPS_LEASE_TIME_DEF;
	return true;
}

u32_t wifi_softap_get_dhcps_lease_time(void) // minute
{
	return dhcps_lease_time;
}

// Called by hostapd_ctrl_iface_sta in ctrl_iface_ap.c
unsigned int wifi_get_sta_ip_from_mac(unsigned char *mac)
{
	list_node *node;
	struct dhcps_pool *dhcps;
	unsigned int ip_addr = 0;

	if (plist)
	{
		for(node=plist; node!=NULL; node = node->pnext)
		{
			dhcps = node->pnode;
			if (memcmp(dhcps->mac, mac, sizeof(dhcps->mac)) == 0)
			{
				#if LWIP_IPV4 && LWIP_IPV6
				ip_addr = dhcps->ip.u_addr.ip4.addr;
				#elif LWIP_IPV4
				ip_addr = dhcps->ip.addr;
				#endif
				break;
			}
		}
	}

	return ip_addr;
}

#endif
