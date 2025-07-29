#ifndef __C_PROTO_H__
#define __C_PROTO_H__
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "zconfig_ieee80211.h"
#include "system_wifi.h"
#include "utils/list.h"

#define ASSERT(cond)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            os_printf(0,4,"E %s L:%d\n", __func__, __LINE__);          \
            return;                                                 \
        }                                                                                \
    } while(0)
#define ASSERT_RET(cond)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            os_printf(0,4,"E %s L:%d\n", __func__, __LINE__);          \
            return -1;                                                 \
        }                                                                                \
    } while(0)
#define MAC_ADDR_EXTRACT(mac_addr, addr_ptr)                        \
    *(((uint8_t*)(mac_addr)) + 0) = *(((uint8_t*)(addr_ptr)) + 0);  \
    *(((uint8_t*)(mac_addr)) + 1) = *(((uint8_t*)(addr_ptr)) + 1);  \
    *(((uint8_t*)(mac_addr)) + 2) = *(((uint8_t*)(addr_ptr)) + 2);  \
    *(((uint8_t*)(mac_addr)) + 3) = *(((uint8_t*)(addr_ptr)) + 3);  \
    *(((uint8_t*)(mac_addr)) + 4) = *(((uint8_t*)(addr_ptr)) + 4);  \
    *(((uint8_t*)(mac_addr)) + 5) = *(((uint8_t*)(addr_ptr)) + 5)

#define MAC_ADDR_CPY(addr1_ptr, addr2_ptr)                                              \
    *(((uint16_t*)(addr1_ptr)) + 0) = *(((uint16_t*)(addr2_ptr)) + 0);                  \
    *(((uint16_t*)(addr1_ptr)) + 1) = *(((uint16_t*)(addr2_ptr)) + 1);                  \
    *(((uint16_t*)(addr1_ptr)) + 2) = *(((uint16_t*)(addr2_ptr)) + 2)

#define MAC_ADDR_CMP(addr1_ptr, addr2_ptr)                                              \
    ((*(((uint16_t*)(addr1_ptr)) + 0) == *(((uint16_t*)(addr2_ptr)) + 0)) &&            \
     (*(((uint16_t*)(addr1_ptr)) + 1) == *(((uint16_t*)(addr2_ptr)) + 1)) &&            \
     (*(((uint16_t*)(addr1_ptr)) + 2) == *(((uint16_t*)(addr2_ptr)) + 2)))  

#define MAC_ADDR_GROUP(mac_addr_ptr) ((*((uint8_t *)(mac_addr_ptr))) & 1)
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define xsl_info(...) os_printf(0, 4, __VA_ARGS__)
#define XSL_SCAN_ALL_CHANNEL    0xff
#define XSL_ALL_CH              100
struct mac_ssid
{
    /// Actual length of the SSID.
    uint8_t length;
    /// Array containing the SSID name.
    uint8_t array[32];
};

struct xsl_ap_info
{
	struct dl_list list; 
	struct dl_list dev_list;
	uint8_t bssid[ETH_ALEN];       
	struct mac_ssid ssid;
    uint8_t chan_nbr;
    int8_t rssi;
};
struct xsl_sta_info
{
	struct dl_list list;
    uint8_t sta_mac[ETH_ALEN]; 
	uint32_t frame_up_total;  
	uint32_t frame_down_total;
    uint32_t frame_up_last;
    uint32_t frame_down_last;
	char rssi;			          
};

struct c_proto_info
{
    uint8_t probe_ch;
    uint8_t probe_time;
    uint8_t probe_switch;
    uint8_t ch_present;
    uint32_t repeat_count;
};

extern struct dl_list ap_info_tag;
extern struct c_proto_info c_proto_global;
void xsl_global_init(void);
int xsl_uartrx_handle(uint8_t* buff,uint16_t len);
int xsl_uart_open(void);
int xsl_uart_read(uint8_t *buff,uint16_t len,uint32_t delay_ms);
int xsl_uart_write(uint8_t* buffer,uint16_t len);
int xsl_uart_close(void);
void xsl_sniffer_init(void);
void xsl_sniffer_deinit(void);
void xsl_monitor_start(void);
int xsl_probe_start(uint8_t ch,uint8_t time);
void xsl_probe_stop(void);
void xsl_frame_process(void *buf, int len, wifi_promiscuous_pkt_type_t type);
int xsl_apdata_packed(struct xsl_ap_info *ap_info);
void xsl_stadata_packed(struct xsl_ap_info *ap_info,struct xsl_sta_info *sta_info);
void xsl_probe_response(const struct dl_list *const ap_list);
// void transport_frame_send(struct date_response* list_head);
#endif