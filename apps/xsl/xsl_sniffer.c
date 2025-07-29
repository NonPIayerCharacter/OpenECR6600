#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "c_proto.h"
#include "stddef.h"
#include "system_wifi.h"
#include "oshal.h"
#include "cli.h"

static unsigned char  cmd_buffer_recv[64]  __attribute__((section(".dma.data")));
//static unsigned char  cmd_buffer_sent[256]  __attribute__((section(".dma.data")));
int sniffer_task_handle;
os_timer_handle_t chan_switch_timer;

struct dl_list ap_info_tag;

#define CHAN_RESIDENCE_TIME     100
#define _SSID_MAX_LEN           32
#define _FCTRL_TODS             0x0100
#define _FCTRL_FROMDS           0x0200
#define CPU2HW(ptr)             ((uint32_t)(ptr)) 
#define XSL_DEFAULT_SCAN_TIME   2


void chan_switch_timer_callback(os_timer_handle_t timer)
{
    static uint32_t timer_cnt = 1;
    if(timer_cnt % 10 == 0)
    {
        xsl_probe_response(&ap_info_tag);
    }
    if(timer_cnt > c_proto_global.repeat_count || c_proto_global.probe_switch == 0)
    {
        timer_cnt = 0;
        return;
    }    
    if(c_proto_global.probe_ch == 100)
    {
        if(++(c_proto_global.ch_present) > 13)
        {
            c_proto_global.ch_present = 1;
        }
        wifi_rf_set_channel(c_proto_global.ch_present);     
    }
    timer_cnt++;
    os_timer_start(chan_switch_timer);  
}
void xsl_sniffer_task(void* data)
{
    uint8_t idx = 0;
    uint8_t frame_len = 0;
    xsl_uart_open();
    chan_switch_timer = os_timer_create("chan_switch_timer",CHAN_RESIDENCE_TIME,0,chan_switch_timer_callback,NULL);
    if(chan_switch_timer == NULL)
    {
        return;
    }
    while(1)
    {
        idx = 0;
        frame_len = 0;
        memset(cmd_buffer_recv,0,sizeof(cmd_buffer_recv));
        if(xsl_uart_read(cmd_buffer_recv,1,WAIT_FOREVER) == 0)
        {
            if(cmd_buffer_recv[idx]== 0xaa)
            {
                idx++;
                if(xsl_uart_read(cmd_buffer_recv+idx,3,100))
                {
                    xsl_info("Error:Fail to recv type&len \n");
                    continue;
                }
                else
                {
                    idx += 3;
                    frame_len = *((uint16_t *)(cmd_buffer_recv+2));
                    if(frame_len > 32)
                    {
                        xsl_info("Error:frame too long \n");
                        continue;
                    }
                    if(xsl_uart_read(cmd_buffer_recv+idx,frame_len+1,100))
                    {
                        xsl_info("Error:Fail to recv frame body \n");
                        continue;
                    }
                    else
                    {
                        idx += frame_len+1;
                        for(int i = 0;i<idx;i++)
                        {
                            xsl_info("0x%x ",cmd_buffer_recv[i]);
                        }
                        xsl_uartrx_handle(cmd_buffer_recv,idx);
                    }
                }  
            }
            else
            {
                xsl_info("Error:invalid frame header \n");
            }
        }   
    }
}


void xsl_sniffer_init(void)
{
    dl_list_init(&ap_info_tag);
    xsl_global_init();
    sniffer_task_handle = os_task_create("xsl_sniffer", 6, 1024 ,(task_entry_t)xsl_sniffer_task, NULL);
}

void xsl_sniffer_deinit(void)
{
    if(chan_switch_timer)
    {
        os_timer_delete(chan_switch_timer);
        chan_switch_timer = NULL;
    }
    if(sniffer_task_handle)
    {
        os_task_delete(sniffer_task_handle);
        sniffer_task_handle = 0;
    }
    xsl_uart_close();
}
void sniffer_list_clear(void)
{
    struct xsl_ap_info *ap_info ,*prev;
    dl_list_for_each_safe(ap_info, prev, &ap_info_tag,
                struct xsl_ap_info, list)
    {
        struct xsl_sta_info *sta_info,*prev;
        dl_list_for_each_safe(sta_info, prev, &ap_info->dev_list,
                    struct xsl_sta_info, list)
        {
            os_free(sta_info);
        }
        os_free(ap_info);
    }
    dl_list_init(&ap_info_tag);
}


int xsl_probe_start(uint8_t ch,uint8_t time)
{
    uint8_t scan_chan;
    uint8_t scan_time;
    uint32_t repeat_count;
    if(ch >= 1 && ch <= 13)
    {
        scan_chan = ch;
    }
    else if(ch == XSL_ALL_CH)
    {
        scan_chan = XSL_SCAN_ALL_CHANNEL; //all chan should be scaned;
        xsl_info("scan all channel\n");
    }
    else
    {
        xsl_info("invalid chan param\n");
        return -1;
    }
    if(time == 0)
    {
        scan_time = XSL_DEFAULT_SCAN_TIME; //min
    }
    else
    {
        scan_time = time;
    }
    xsl_monitor_start();

    if(scan_chan != XSL_SCAN_ALL_CHANNEL)
    {
        wifi_rf_set_channel(ch);
    }
    else
    {
        wifi_rf_set_channel(1);
    }
    repeat_count = scan_time * 60 * (1000 / CHAN_RESIDENCE_TIME);
    c_proto_global.repeat_count = repeat_count;
    xsl_info("repeat count = %d,scan_time=%d\n",repeat_count,scan_time);
    os_timer_start(chan_switch_timer);
    return 0;
}
void xsl_probe_stop(void)
{
    xsl_global_init();
    wifi_set_promiscuous(0);
    sniffer_list_clear();
}

static int handle_beacon(void *packet, int size, int8_t rssi)
{
    uint8_t ssid[_SSID_MAX_LEN] = {0}, bssid[ETH_ALEN] = {0};
    struct ieee80211_hdr *hdr;
    int fc, ret, channel;
    struct ieee80211_hdr *mgmt_header = (struct ieee80211_hdr *)packet;
    struct xsl_ap_info *temp_ap_info ,*prev;
    if (mgmt_header == NULL) {
        return -1;
    }

    hdr = (struct ieee80211_hdr *)mgmt_header;
    fc = hdr->frame_control;

    /* just for save ap in aplist for ssid amend */
    if (!ieee80211_is_beacon(fc) && !ieee80211_is_probe_resp(fc)) {
        return -1;
    }

    ret = ieee80211_get_bssid((uint8_t *)mgmt_header, bssid);
    if (ret < 0) {
        return -1;
    } 
    ret = ieee80211_get_ssid((uint8_t *)mgmt_header, size, ssid);
    if (ret < 0) {
        return -1;
    }
    channel = cfg80211_get_bss_channel((uint8_t *)mgmt_header, size);
    rssi = rssi > 0 ? rssi - 256 : rssi;
    dl_list_for_each_safe(temp_ap_info, prev, &ap_info_tag,
            struct xsl_ap_info, list)
    {
        if(MAC_ADDR_CMP(temp_ap_info->bssid, bssid))
        {   
            temp_ap_info->chan_nbr = channel;
            strncpy((char*)temp_ap_info->ssid.array,(char *)ssid,strlen((const char*)ssid));
            temp_ap_info->ssid.length =strlen((const char*)ssid);
            temp_ap_info->rssi = (rssi + temp_ap_info->rssi) / 2;
            return 0;
        }
    }
    struct xsl_ap_info *item = os_malloc(sizeof(struct xsl_ap_info));

    ASSERT_RET(item);

    item->chan_nbr = channel;
    item->rssi = rssi;
    memset(item->ssid.array,0,sizeof(item->ssid.array));
    strncpy((char*)item->ssid.array,(char *)ssid,strlen((const char*)ssid));
    item->ssid.length = strlen((const char*)ssid);
    MAC_ADDR_CPY(item->bssid, bssid);
    dl_list_init(&(item->dev_list));
    dl_list_add_tail(&ap_info_tag,&(item->list));
    return 0;
}

static void sta_info_insert(void *packet, int size, int8_t rssi,struct xsl_ap_info* ap_info,uint8_t hit_sa,uint8_t hit_da)
{
    struct ieee80211_hdr *hdr;
    uint16_t fc;
    uint8_t *sa, *da;
    hdr = (struct ieee80211_hdr *)packet;
    fc = hdr->frame_control;
    sa = ieee80211_get_SA(hdr);
    da = ieee80211_get_DA(hdr);
    if(hit_sa == 0)
    {
        if(!MAC_ADDR_GROUP(sa) && ((fc & _FCTRL_TODS) && !(fc & _FCTRL_FROMDS)))
        {
            struct xsl_sta_info *item_sa = os_malloc(sizeof(struct xsl_sta_info));
            ASSERT(item_sa);
            MAC_ADDR_CPY(item_sa->sta_mac,sa);
            item_sa->frame_up_total =size ;
            item_sa->frame_down_total = 0;
            item_sa->frame_down_last = 0;
            item_sa->frame_up_last = 0;
            item_sa->rssi = rssi;
            dl_list_add_tail(&(ap_info->dev_list),&(item_sa->list));
        }
    }
    if(hit_da == 0)
    {
        if(!MAC_ADDR_GROUP(da) && ((fc & _FCTRL_TODS) && !(fc & _FCTRL_FROMDS)))
        {
            struct xsl_sta_info *item_da = os_malloc(sizeof(struct xsl_sta_info));
            ASSERT(item_da);
            MAC_ADDR_CPY(item_da->sta_mac,da);
            item_da->frame_down_total = size;
            item_da->frame_up_total = 0;
            item_da->frame_down_last = 0;
            item_da->frame_up_last =0;
            item_da->rssi = rssi;
            dl_list_add_tail(&(ap_info->dev_list),&(item_da->list));   
        }
    }
}

void handle_data(void *packet, int size, int8_t rssi)
{
    struct ieee80211_hdr *hdr;
    uint16_t fc;
    uint8_t *bssid, *sa, *da;
    struct xsl_ap_info *temp_ap_info, *prev;
    struct xsl_sta_info *temp_sta_info ;
    hdr = (struct ieee80211_hdr *)packet;

    if (hdr == NULL) {
        return;
    }
    fc = hdr->frame_control;
    bssid = ieee80211_get_BSSID(hdr);
    sa = ieee80211_get_SA(hdr);
    da = ieee80211_get_DA(hdr);
    if((fc & _FCTRL_TODS) && (fc & _FCTRL_FROMDS))
        return;
    if(!memcmp(bssid,sa,ETH_ALEN)||!memcmp(bssid,da,ETH_ALEN)||!memcmp(sa,da,ETH_ALEN))
        return;
    dl_list_for_each_safe(temp_ap_info, prev, &ap_info_tag,
            struct xsl_ap_info, list)
    {
        if(MAC_ADDR_CMP(temp_ap_info->bssid, bssid))
        {
            uint8_t hit_sa = 0;
            uint8_t hit_da = 0;
            struct xsl_sta_info *prev;
            dl_list_for_each_safe(temp_sta_info, prev, &temp_ap_info->dev_list,
                    struct xsl_sta_info, list)
            {
                if(MAC_ADDR_CMP(temp_sta_info->sta_mac, sa)||MAC_ADDR_CMP(temp_sta_info->sta_mac, da))
                {
                    if(MAC_ADDR_CMP(temp_sta_info->sta_mac, sa))
                    {   
                        hit_sa = 1;
                        if((fc & _FCTRL_TODS) && !(fc & _FCTRL_FROMDS))
                        {
                            if(temp_sta_info->rssi == 0xff)
                                temp_sta_info->rssi = rssi;
                            else
                                temp_sta_info->rssi = (temp_sta_info->rssi + rssi)/2;
                            temp_sta_info->frame_up_total += size;
                        }      
                    }
                    else if(MAC_ADDR_CMP(temp_sta_info->sta_mac, da))
                    {
                        hit_da = 1;
                        if((fc & _FCTRL_TODS) && !(fc & _FCTRL_FROMDS))
                            temp_sta_info->frame_down_total += size;   
                    }
                }
            }
            sta_info_insert(packet,size,rssi,temp_ap_info,hit_sa,hit_da);
            return;
        }
    }
    struct xsl_ap_info *item = os_malloc(sizeof(struct xsl_ap_info));
    ASSERT(item);
    item->chan_nbr = c_proto_global.ch_present;
    item->rssi = 0xff;
    memset(item->ssid.array,0,sizeof(item->ssid.array));

    MAC_ADDR_CPY(item->bssid, bssid);
    dl_list_init(&(item->dev_list));
    dl_list_add_tail(&ap_info_tag,&(item->list));
    sta_info_insert(packet,size,rssi,item,0,0);
}


void xsl_frame_process(void *buf, int len, wifi_promiscuous_pkt_type_t type)
{

    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf; 

    if (type == WIFI_PKT_MGMT) {
        handle_beacon((char *)pkt->payload, len, pkt->rx_ctrl.rssi);
    } else if (type == WIFI_PKT_DATA) {
        handle_data((char *)pkt->payload, len, pkt->rx_ctrl.rssi);
    }
}

void xsl_monitor_start(void)
{
    wifi_promiscuous_filter_t filter = {0};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA | WIFI_PROMIS_FILTER_MASK_MGMT;
    wifi_set_promiscuous_filter(&filter);
    wifi_set_promiscuous_rx_cb(xsl_frame_process);
    wifi_set_promiscuous(true);
}
#if 1
int frame_dump(cmd_tbl_t *t, int argc, char *argv[])
{
    struct xsl_ap_info *temp_ap_info,*prev;
    struct xsl_sta_info *temp_sta_info;
    dl_list_for_each_safe(temp_ap_info, prev, &ap_info_tag,
            struct xsl_ap_info, list)
    {
        struct xsl_sta_info *prev;
        xsl_info("bssid:%02x:%02x:%02x:%02x:%02x:%02x\n",MAC2STR((uint8_t *)temp_ap_info->bssid));   
        xsl_info("ssid= %s,len = %d,chan:%d,rssi:%d\n",temp_ap_info->ssid.array,temp_ap_info->ssid.length,temp_ap_info->chan_nbr,temp_ap_info->rssi); 
        dl_list_for_each_safe(temp_sta_info, prev, &temp_ap_info->dev_list,
                struct xsl_sta_info, list)
        {
            xsl_info("%02x:%02x:%02x:%02x:%02x:%02x\n",MAC2STR((uint8_t *)temp_sta_info->sta_mac));
            xsl_info("frame_up = %d,frame dowm = %d\n",temp_sta_info->frame_up_total,temp_sta_info->frame_down_total); 
            xsl_info("rssi = %d",temp_sta_info->rssi);
            xsl_info("\n");
        }
        xsl_info("=======\n");

    }
    return CMD_RET_SUCCESS;
}
CLI_CMD(xsl_debug, frame_dump, "", "");
int xsl_sniffer_stop(cmd_tbl_t *t, int argc, char *argv[])
{
    xsl_sniffer_deinit();

    return CMD_RET_SUCCESS;
}
CLI_CMD(xsl_sniffer_stop, xsl_sniffer_stop, "", "");


int xsl_sniffer_start(cmd_tbl_t *t, int argc, char *argv[])
{
    xsl_sniffer_init();

    return CMD_RET_SUCCESS;
}
CLI_CMD(xsl_sniffer_start, xsl_sniffer_start, "", "");
#endif

