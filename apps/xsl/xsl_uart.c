#include "hal_uart.h"
#include "c_proto.h"
#include "stddef.h"
#include "system_wifi.h"
#include "oshal.h"
#include "chip_pinmux.h"

struct c_proto_info c_proto_global;
unsigned char  cmd_buffer_sent[256]  __attribute__((section(".dma.data")));
#define XSL_ACK 			0x00
#define XSL_PROBE_DATE  	0x01
#define XSL_AP_SSID 		0x02
#define XSL_PROBE_TIME  	0x03
#define XSL_PROBE_CHNN  	0x04
#define XSL_PROBE_SWITCH  	0x05

#define XSL_FRAME_HEADER	0xaa
#define XSL_DATE_FIX_LEN	0x14
#define XSL_FRAME_UP		0x0a
#define XSL_FRAME_DOWN		0x09
#define XSL_RESPONSE_LEN    25
#define XSL_DUMMY           0x0
#define XSL_CTRL_FIX_LEN    0x1
#define XSL_CTRL_SWITCH_ON  0x1
#define XSL_CTRL_SWITCH_OFF 0x0
#define XSL_UART_NUM         2
#define XSL_INVALID_CH       0xff

int xsl_uart_open(void)
{
    chip_uart2_pinmux_cfg(1);

    T_DRV_UART_CONFIG config;	
    config.uart_baud_rate = 115200;
    config.uart_data_bits = UART_DATA_BITS_8;
    config.uart_stop_bits = UART_STOP_BITS_1;
    config.uart_parity_bit = UART_PARITY_BIT_NONE;
    config.uart_flow_ctrl = UART_FLOW_CONTROL_DISABLE;
    config.uart_tx_mode = 0x2;
    config.uart_rx_mode = 0x0;
    config.uart_rx_buf_size = 1024;	

    if (hal_uart_open(XSL_UART_NUM, &config) != 0)
    {
        xsl_info("uart init failed\r\n");
        return -1;
    }
    return 0;
}

int xsl_uart_read(uint8_t *buff,uint16_t len,uint32_t delay_ms)
{
    int ret = -1;
    if (hal_uart_read(XSL_UART_NUM, buff, len, delay_ms) == len)
    {
        ret = 0;
    }
    return ret;
}

int xsl_uart_write(uint8_t* buffer,uint16_t len)
{
    int ret = -1;
    if (hal_uart_write(XSL_UART_NUM, buffer, len) == 0)
    {
        ret = 0;
    }
    return ret;
}

int xsl_uart_close(void)
{
    int ret = -1;
    if (hal_uart_close(XSL_UART_NUM) != 0)
    {
        ret = 0;
        xsl_info(" uart2 close fail!\r\n");
    }
    return ret;
}

uint8_t xsl_xor_cal(uint8_t* buff,uint16_t len)
{
    uint8_t ret;
    ASSERT_RET(len);
    if(len == 1)
    {
        return *buff;
    }
    ret = *buff;
    for(int i = 1;i < len-1;i++)
    {
        ret = ret ^ buff[i];
    }
    return ret;
}

void xsl_global_init(void)
{
    c_proto_global.probe_ch = XSL_SCAN_ALL_CHANNEL;
    c_proto_global.probe_switch = XSL_CTRL_SWITCH_OFF;
    c_proto_global.probe_time = 0;
    c_proto_global.ch_present = XSL_INVALID_CH;
    c_proto_global.repeat_count = 0;
}

int xsl_apdata_packed(struct xsl_ap_info *ap_info)
{
    uint16_t len = 0;
    uint16_t idx = 0;
    struct mac_ssid ssid = ap_info->ssid;
    uint8_t * bssid = ap_info->bssid;
    uint16_t *frame_len;
    len = 6 + ssid.length;
    frame_len = (uint16_t*)(cmd_buffer_sent + 2);
    memset(cmd_buffer_sent,0,sizeof(cmd_buffer_sent));

    *(cmd_buffer_sent + 0) = XSL_FRAME_HEADER;
    *(cmd_buffer_sent + 1) = XSL_AP_SSID;
    *frame_len = len;
    idx += 4;
    MAC_ADDR_EXTRACT(cmd_buffer_sent+idx, bssid);
    idx += 6;
    for(int i = 0; i < ssid.length; i++)
    {
        *(cmd_buffer_sent+idx+i) = ssid.array[i];
    }
    idx += ssid.length;
    *(cmd_buffer_sent+idx) = xsl_xor_cal(cmd_buffer_sent,idx+1);  
    idx++;
    return idx;               
}

void xsl_stadata_packed(struct xsl_ap_info *ap_info,struct xsl_sta_info *sta_info)
{
    uint8_t *sta_mac = sta_info->sta_mac;
    uint8_t *ap_mac = ap_info->bssid;
    uint8_t ch = ap_info->chan_nbr;
    uint8_t rssi = 100 + sta_info->rssi;
    uint32_t frameup_delt = sta_info->frame_up_total - sta_info->frame_up_last;
    uint32_t framedown_delt = sta_info->frame_down_total - sta_info->frame_down_last;
    float frame_up_rate = (float)(frameup_delt)*8.0/1024.0;
    float frame_down_rate = (float)(framedown_delt)*8.0/1024.0;
    sta_info->frame_up_last = sta_info->frame_up_total;
    sta_info->frame_down_last = sta_info->frame_up_total;
    memset(cmd_buffer_sent,0,sizeof(cmd_buffer_sent));
    *(cmd_buffer_sent + 0) = XSL_FRAME_HEADER;
    *(cmd_buffer_sent + 1) = XSL_PROBE_DATE;
    *(cmd_buffer_sent + 2) = XSL_DATE_FIX_LEN;
    *(cmd_buffer_sent + 3) = XSL_DUMMY;
    *(cmd_buffer_sent + 4) = XSL_FRAME_UP;
    MAC_ADDR_EXTRACT(cmd_buffer_sent+5, &sta_mac);
    MAC_ADDR_EXTRACT(cmd_buffer_sent+11, &ap_mac);
    *(cmd_buffer_sent + 17) = ch;
    *(cmd_buffer_sent + 18) = rssi;
    *(cmd_buffer_sent + 19) = XSL_DUMMY;
    *((float*)(cmd_buffer_sent + 20)) = frame_up_rate;
    *(cmd_buffer_sent+24) = xsl_xor_cal(cmd_buffer_sent,XSL_DATE_FIX_LEN);
    memcpy(cmd_buffer_sent+XSL_RESPONSE_LEN,cmd_buffer_sent,XSL_RESPONSE_LEN);
    *(cmd_buffer_sent + 29) = XSL_FRAME_DOWN;
    *((float*)(cmd_buffer_sent + 45)) = frame_down_rate;
    *(cmd_buffer_sent+49) = xsl_xor_cal(cmd_buffer_sent+XSL_RESPONSE_LEN,XSL_RESPONSE_LEN);
}

void xsl_probe_response(const struct dl_list *const ap_list)
{
    struct xsl_ap_info* temp_ap_info ,*prev;
    struct xsl_sta_info *sta_info;
    uint8_t ap_frame_len;
    dl_list_for_each_safe(temp_ap_info, prev, ap_list,
            struct xsl_ap_info, list)
    {
        ap_frame_len = xsl_apdata_packed(temp_ap_info);
        xsl_uart_write(cmd_buffer_sent,ap_frame_len);
        struct xsl_sta_info *prev;
        dl_list_for_each_safe(sta_info, prev, &temp_ap_info->dev_list,
                    struct xsl_sta_info, list)
        {
            xsl_stadata_packed(temp_ap_info,sta_info);
            xsl_uart_write(cmd_buffer_sent,XSL_RESPONSE_LEN*2);
        }

    }
}

int xsl_uartrx_handle(uint8_t* buff,uint16_t len)
{
    uint8_t cal_in_frame;
    uint8_t cal_value;
    uint8_t type;
    ASSERT_RET(len>1);
    cal_in_frame = buff[len-1];
    cal_value = xsl_xor_cal(buff,len);
    if(cal_in_frame != cal_value||buff[0]!=XSL_FRAME_HEADER)
    {
        xsl_info("Error:Cal Failed\n");
        return -1;
    }
		
    type = buff[1];
	
    switch (type)
    {
    case XSL_PROBE_TIME:
        if(buff[2] != XSL_CTRL_FIX_LEN)
            return -1;
        c_proto_global.probe_time = buff[4];
        break;
    case XSL_PROBE_CHNN:
        if(buff[2] != XSL_CTRL_FIX_LEN)
            return -1;
        if(buff[4] == 100)
        {
            c_proto_global.probe_ch = XSL_ALL_CH;
            c_proto_global.ch_present = 1;
        }
        else if(buff[4] >= 1 && buff[4] <= 13)
        {
            c_proto_global.probe_ch = buff[4];
            c_proto_global.ch_present = buff[4];
        }
        else
        {
            xsl_info("invalid ch param\n");
            return -1;
        }
        break;
        case XSL_PROBE_SWITCH:
        if(buff[2] != XSL_CTRL_FIX_LEN)
            return -1;
        if(buff[4] == XSL_CTRL_SWITCH_ON)
        {
            if(c_proto_global.probe_switch == XSL_CTRL_SWITCH_OFF)
            {
                xsl_probe_start(c_proto_global.probe_ch,c_proto_global.probe_time);
                c_proto_global.probe_switch = XSL_CTRL_SWITCH_ON;
            }
        }
        else if(buff[4] == XSL_CTRL_SWITCH_OFF)
        {
            if(c_proto_global.probe_switch == XSL_CTRL_SWITCH_ON)
            {
                xsl_probe_stop();
            }
        }
        break;
    default:
        break;
    }
    return 0;
}

