#include "wifi_sniffer.h"
#include "system_wifi.h"
#include "ak.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "zconfig_ieee80211.h"
#include "awss_aplist.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern bool s_ak_ack_send;
extern int ak_send_ack_start();

const char ak_datevers[] = {__DATE__};
const char ak_timevers[] = {__TIME__};
static ak_head_t ak_head = {{0},{0}};
ak_local_context ak_context;
static ak_config ak_cf;
static TaskHandle_t ak_task_handle = NULL;
static QueueHandle_t ak_event_queue = NULL;
ak_result_t ak_result;
unsigned char cellphone_ip[4] = {0};

static bool is_enter_ak = false;
int ak_current_channel = 0;
int ak_round = 0;
unsigned char total_len_doublecheck = 0;

g_s_p_callback got_ssid_pwd_callback= NULL;

static void airkiss_wifiSnifferCallback(void *buf, int len, wifi_promiscuous_pkt_type_t type);

unsigned char ak_calcrc_1byte(unsigned char abyte)    
{    
    unsigned char i,crc_1byte;     
    crc_1byte=0;                
    for(i = 0; i < 8; i++)    
    {    
        if(((crc_1byte^abyte)&0x01))    
        {    
            crc_1byte^=0x18;     
            crc_1byte>>=1;    
            crc_1byte|=0x80;    
        }          
        else    
        {
            crc_1byte>>=1; 
        }
        abyte>>=1;          
    }   
    return crc_1byte;   
}  

unsigned char ak_calcrc_bytes(unsigned char *p,unsigned int num_of_bytes)  
{  
    unsigned char crc=0;  
    while(num_of_bytes--) 
    {  
        crc=ak_calcrc_1byte(crc^*p++);  
    }  
    return crc;   
} 

static int ak_memcmp(unsigned char *str1, unsigned char *str2, size_t n)
{
    if(str1 == NULL || str2 == NULL || n == 0){
        return -1;
    }
	const unsigned char *p1 = str1, *p2 = str2;

    if (n == 0)
            return 0;

    while (*p1 == *p2) {
            p1++;
            p2++;
            n--;
            if (n == 0)
                return 0;
    }

    return *p1 - *p2;
}

static void resest_ak_data()
{
    memset(&ak_context.data, 0, sizeof(union ak_data));
}

static int ak_get_result(ak_result_t* result)
{
    memcpy(result, &ak_context, sizeof(ak_result_t));
    return 0;
}
static int ak_change_channel(void)
{
    memset(&ak_head, 0, sizeof(ak_head_t));
    resest_ak_data();
    return 0;
}


static int ak_sn_check(uint16_t sn, int type)
{
    static uint16_t last_sn[AK_MAX_PTYPE];
    if(sn != last_sn[type] || sn == 0){
        last_sn[type] = sn;
        return 1;
    } else {
        return 0;
    }
}

static int ak_find_ap_in_aplist(const uint8_t * ap_mac, struct ap_info * t_ap)
{
    uint8_t apmac[6];
    struct ap_info * target_ap = NULL;
    ak_debug("mac:%02x:%02x:%02x:%02x:%02x:%02x \n",ap_mac[0], ap_mac[1], ap_mac[2], ap_mac[3], ap_mac[4], ap_mac[5]);

    memcpy(apmac, ap_mac, 6);
    target_ap = zconfig_get_apinfo(apmac);
    if(target_ap != NULL){
        ak_info("target ap ssid:%s, channel:%d \n",target_ap->ssid,target_ap->channel);
        xTimerStop(ak_cf.channel_timer,0);
        ak_round = 0;
        ak_change_channel();			
        ak_context.ak_state = AK_STATE_SRC_LOCKED;
        memcpy(t_ap,target_ap,sizeof(struct ap_info));
    }

    return 0;
}

int ak_filter(const unsigned char *frame, int size, int *type)
{
    int isvalid = 0;
    static bool qos_flag = false;
    struct ap_info  target_ap = {0};
    struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)frame;
    unsigned char mutil_addr[6] = {0x01,0x00,0x5e,0x00,0x00,0x4c};
    unsigned char broad_addr[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    if(size < 24)
        goto invalid;

    int i;
    unsigned char ch;
    for(i=0; i<24; i++)
    {
        ch = *((unsigned char*)frame + i);
        ak_head.dummy[i] = ch;
    }
   
    if((ak_head.dummy[1] & 0x08) != 0x00){
		goto invalid;
    }

    if (26 == ieee80211_hdrlen(hdr->frame_control)){
        qos_flag = true;
    } else {
        qos_flag = false;
    }

    if((ak_head.dummy[1] & 0x02) == 0x02){
        if(qos_flag == true){
            if(0 == ak_memcmp(mutil_addr,&ak_head.dummy[4],6)){
            *type = AK_MULTI_TYPE_FRDS;
            if(ak_context.ak_state < AK_STATE_SRC_LOCKED){
                ak_find_ap_in_aplist(&ak_head.dummy[10],&target_ap);
            }
                
            } else if(0 == ak_memcmp(broad_addr,&ak_head.dummy[4],6)){
                *type = AK_BRODA_TYPE_FRDS;
            } else {
                goto invalid;
            }
        } else {
            if(0 == ak_memcmp(mutil_addr,&ak_head.dummy[4],6)){
                *type = AK_MULTI_TYPE_FRDS;
                if(ak_context.ak_state < AK_STATE_SRC_LOCKED){
                    ak_find_ap_in_aplist(&ak_head.dummy[10],&target_ap);
                }
                    
            } else if(0 == ak_memcmp(broad_addr,&ak_head.dummy[4],6)){
                *type = AK_BRODA_TYPE_FRDS;
            } else {
                goto invalid;
            }
        }
    } else if((ak_head.dummy[1] & 0x01) == 0x01) {
        if(qos_flag == true){
            if(0 == ak_memcmp(mutil_addr,&ak_head.dummy[16],6)){
                if(ak_context.ak_state < AK_STATE_SRC_LOCKED)
                    ak_find_ap_in_aplist(&ak_head.dummy[4],&target_ap);
                *type = AK_MULTI_TYPE_TODS;
            } else if(0 == ak_memcmp(broad_addr,&ak_head.dummy[16],6)){
                *type = AK_BRODA_TYPE_TODS;
            } else {
                goto invalid;
            }
        } else {
            if(0 == ak_memcmp(mutil_addr,&ak_head.dummy[16],6)){
                if(ak_context.ak_state < AK_STATE_SRC_LOCKED)
                    ak_find_ap_in_aplist(&ak_head.dummy[4],&target_ap);
                *type = AK_MULTI_TYPE_TODS;
                } else if(0 == ak_memcmp(broad_addr,&ak_head.dummy[16],6)){
                    *type = AK_BRODA_TYPE_TODS;
                } else {
                goto invalid;
            }
        }
    } 
	else{
		goto invalid;
	}
    int ret = ak_sn_check((ak_head.dummy[22]+ak_head.dummy[23]*256)>>4, *type);
    if(ret == 0){
        goto invalid;
    }
    isvalid = 1;

invalid:
    return isvalid;
}

int ak_diakover_filter(const unsigned char *frame, int size)
{
    int isvalid = 0;
    if(size < 50)
        goto invalid;

    isvalid = 1;
invalid:
    return isvalid;
}

static void ak_record_move_ones(void *base_addr, int record_num)
{
    int i; 
    unsigned short *record_base = base_addr;

    for(i = 0; i < record_num; i++)
    {
        if(record_base[i+1] != record_base[i])
            record_base[i] = record_base[i+1];
    }
}

static void ak_int_record_move_ones(void *base_addr, int record_num)
{
    int i; 
    unsigned int *record_base = base_addr;

    for(i = 0; i < record_num; i++)
    {
        if(record_base[i+1] != record_base[i])
            record_base[i] = record_base[i+1];
    }
}

static void ak_add_seq_data(const unsigned char *data, int seq)
{
    if(seq < ak_context.need_seq)
    {
        if((seq*4 + 4) <= AK_USR_DATA_BUFF_MAX_SIZE)
        {
            if((ak_context.seq_success_map & (1 << seq)) == 0) 
            {
                memcpy(ak_context.usr_data + seq*4, data, 4);
                ak_context.seq_success_map |= (1 << seq);
            }
        }
    }
}
static void swap(uint16_t *p1,uint16_t *p2){
    uint16_t temp = *p1;
    *p1 = *p2;
    *p2 = temp;
}

static bool append_prefix(void *base_addr, unsigned short length, int type)
{
    int i; 
    unsigned short *record_base = base_addr;
    record_base[AK_MAX_GUIDE_RECORD] = length;
    for(i = 0; i < 4; i++){
        if(length == record_base[i])
            goto check;           
    }
    for(i = 3; i >= 0; i--)
    {
        if(record_base[i] == 0)
            swap(&record_base[i+1],&record_base[i]);
        else if(record_base[i+1] < record_base[i])
            swap(&record_base[i],&record_base[i+1]);
        else{
            break;
        }
    }
    
check:
    ak_debug("type ----------- %d length %d\n",type,length );
    for(i = 0; i <= 4; i++)
    {
        ak_debug("%d ",record_base[i]);
    }
    ak_debug("\n");

    for(i = 0; i < 4; i++)
    {
        if(record_base[i] == 0){
            return false;
        }
    }
   
    return true;
}

static void clear_record(void *base_addr)
{
    int i; 
    unsigned short *record_base = base_addr;
    for(i = 0; i < 4; i++)
    {
        record_base[i] = 0;
    }
}

static int ak_get_base_len(const uint8_t* frame, unsigned short length, int type)
{
    struct ap_info target_ap = {0};
    if(length > 60 && length < 93 && ak_context.base_len[type] == 0){
        ak_context.data.guide_code.length_record[type][AK_MAX_GUIDE_RECORD] = length;
		ak_context.data.guide_code.sn_record[type][AK_MAX_GUIDE_RECORD] = (frame[22]+frame[23]*256)>>4;
    } else {
        return 0;
    }
    bool ret = append_prefix(ak_context.data.guide_code.length_record[type],length, type);
	ak_int_record_move_ones(ak_context.data.guide_code.sn_record[type],AK_MAX_GUIDE_RECORD);
    if(ret == true){
	if(!((ak_context.data.guide_code.sn_record[type][1] - ak_context.data.guide_code.sn_record[type][0] == 1) &&
        (ak_context.data.guide_code.sn_record[type][2] - ak_context.data.guide_code.sn_record[type][1] == 1) &&
        (ak_context.data.guide_code.sn_record[type][3] - ak_context.data.guide_code.sn_record[type][2] == 1)))
		ret = false;
    }
	
    if(ret == true){
        if((ak_context.data.guide_code.length_record[type][1] - ak_context.data.guide_code.length_record[type][0] == 1) &&
        (ak_context.data.guide_code.length_record[type][2] - ak_context.data.guide_code.length_record[type][1] == 1) &&
        (ak_context.data.guide_code.length_record[type][3] - ak_context.data.guide_code.length_record[type][2] == 1))
        {
            ak_context.base_len[type] = ak_context.data.guide_code.length_record[type][0] - 1;
            ak_info("type:%d, base_len:%d\n", type, ak_context.base_len[type]);
            if(type == AK_MULTI_TYPE_FRDS || type == AK_BRODA_TYPE_FRDS){
                ak_find_ap_in_aplist(&frame[10],&target_ap);
            } else{
                ak_find_ap_in_aplist(&frame[4],&target_ap);
            }
            if(target_ap.ssid[0] == 0){
                clear_record(ak_context.data.guide_code.length_record[type]);
				ak_context.base_len[type] = 0;
                return 0; 
            }
            ak_context.ssid_len = strlen(target_ap.ssid);
            memcpy(ak_context.ssid, target_ap.ssid, ak_context.ssid_len);
			return 1;
        }
        clear_record(ak_context.data.guide_code.length_record[type]);
        return 0; 
    } 

	return 0;
}

static void ak_recv_diakover(const uint8_t* frame, unsigned short length, int type)
{
    int success = 0;
    success = ak_get_base_len(frame,length,type);
    if(success)
    {
        ak_context.ak_state = AK_STATE_SRC_LOCKED;
        resest_ak_data();
    }
}


static void ak_process_magic_code(int type, unsigned short length)
{
    if(ak_context.base_len[type] == 0){
        return;
    }

    ak_context.data.magic_code.record[type][AK_MAX_MAGIC_CODE_RECORD] = length - ak_context.base_len[type];
	
    ak_record_move_ones(ak_context.data.magic_code.record[type], AK_MAX_MAGIC_CODE_RECORD);
	ak_int_record_move_ones(ak_context.data.magic_code.sn_record[type],AK_MAX_MAGIC_CODE_RECORD);
	
	if(!((ak_context.data.magic_code.sn_record[type][1] - ak_context.data.magic_code.sn_record[type][0] == 1) &&
		(ak_context.data.magic_code.sn_record[type][2] - ak_context.data.magic_code.sn_record[type][1] == 1) &&
		(ak_context.data.magic_code.sn_record[type][3] - ak_context.data.magic_code.sn_record[type][2] == 1))){
		return;
	}

    if(((ak_context.data.magic_code.record[type][0]&0x01f0)==0x0000)&&
        ((ak_context.data.magic_code.record[type][1]&0x01f0)==0x0010)&&
            ((ak_context.data.magic_code.record[type][2]&0x01f0)==0x0020)&&
            ((ak_context.data.magic_code.record[type][3]&0x01f0)==0x0030))
    {
            ak_context.total_len = ((ak_context.data.magic_code.record[type][0] & 0x000F) << 4) + (ak_context.data.magic_code.record[type][1] & 0x000F);
            if(ak_context.total_len > 128) 
                ak_context.total_len -= 128;
            ak_context.ssid_crc = ((ak_context.data.magic_code.record[type][2] & 0x000F) << 4) + (ak_context.data.magic_code.record[type][3] & 0x000F);
            
            resest_ak_data();
			ak_info("recv_magic_code: len: %d, crc:%x\n", ak_context.total_len, ak_context.ssid_crc);

			if(total_len_doublecheck == ak_context.total_len)
			{
				ak_context.ak_state = AK_STATE_MAGIC_CODE_COMPLETE;
				total_len_doublecheck = 0;
			}
			else
			{
				total_len_doublecheck = ak_context.total_len;
			}
    }else {
        ak_debug("%s-%d: 0x%x  0x%x  0x%x  0x%x \n", __func__, __LINE__, (ak_context.data.magic_code.record[type][0]&0x01f0), (ak_context.data.magic_code.record[type][1]&0x01f0), (ak_context.data.magic_code.record[type][2]&0x01f0), (ak_context.data.magic_code.record[type][3]&0x01f0));
    }
}

static void ak_process_prefix_code(int type, unsigned short length)
{
    if(ak_context.base_len[type] == 0){
        return;
    }
    ak_context.data.prefix_code.record[type][AK_MAX_PREFIX_CODE_RECORD] = length - ak_context.base_len[type];
	
    ak_record_move_ones(ak_context.data.prefix_code.record[type], AK_MAX_PREFIX_CODE_RECORD );
	ak_int_record_move_ones(ak_context.data.prefix_code.sn_record[type],AK_MAX_PREFIX_CODE_RECORD);

	if(!((ak_context.data.prefix_code.sn_record[type][1] - ak_context.data.prefix_code.sn_record[type][0] == 1) &&
		(ak_context.data.prefix_code.sn_record[type][2] - ak_context.data.prefix_code.sn_record[type][1] == 1) &&
		(ak_context.data.prefix_code.sn_record[type][3] - ak_context.data.prefix_code.sn_record[type][2] == 1))){
		return;
	}

    if((ak_context.data.prefix_code.record[type][0]&0x01f0)==0x0040&&
        (ak_context.data.prefix_code.record[type][1]&0x01f0)==0x0050&&
            (ak_context.data.prefix_code.record[type][2]&0x01f0)==0x0060&&
            (ak_context.data.prefix_code.record[type][3]&0x01f0)==0x0070)
    {
        ak_context.pswd_len = ((ak_context.data.prefix_code.record[type][0] & 0x000F) << 4) + (ak_context.data.prefix_code.record[type][1] & 0x000F);
        if(ak_context.pswd_len > AK_PASSWORD_MAX_LEN)
            ak_context.pswd_len = 0;
        ak_context.pswd_lencrc = ((ak_context.data.prefix_code.record[type][2] & 0x000F) << 4) + (ak_context.data.prefix_code.record[type][3] & 0x000F);
        ak_debug("prefix_code: len: %d, crc:%x\n", ak_context.pswd_len, ak_context.pswd_lencrc);
        if(ak_calcrc_1byte(ak_context.pswd_len)==ak_context.pswd_lencrc)
        {
            ak_context.ak_state = AK_STATE_PREFIX_CODE_COMPLETE;
        }
        else
        {
            resest_ak_data();
            return;
        }

        ak_context.need_seq = (ak_context.total_len + 3)/4; 
        ak_context.seq_success_map_cmp = (1 << ak_context.need_seq) - 1;
            
        resest_ak_data();
        ak_info("prefix: len:%d, crc:%x\n", ak_context.pswd_len, ak_context.pswd_lencrc);
	}
	ak_debug("%s-%d: 0x%x  0x%x  0x%x  0x%x \n", __func__, __LINE__, (ak_context.data.prefix_code.record[type][0]&0x01f0), (ak_context.data.prefix_code.record[type][1]&0x01f0), (ak_context.data.prefix_code.record[type][2]&0x01f0), (ak_context.data.prefix_code.record[type][3]&0x01f0));
}

static int ak_pkt_seq_type(int type) 
{
    if(((ak_context.data.seq_code.record[type][0]&0x180)==0x80) &&
        ((ak_context.data.seq_code.record[type][1]&0x180)==0x80) && 
        ((ak_context.data.seq_code.record[type][2]&0x0100)==0x0100) && 
        ((ak_context.data.seq_code.record[type][3]&0x0100)==0x0100) && 
        ((ak_context.data.seq_code.record[type][4]&0x0100)==0x0100) && 
        ((ak_context.data.seq_code.record[type][5]&0x0100)==0x0100) && 
        ((ak_context.data.seq_code.record[type][1]&0x7F) <= ((ak_context.total_len>>2)+1)))
    {
        return 0;
    } else if (((ak_context.data.seq_code.record[type][0]&0x180)==0x80) &&
        ((ak_context.data.seq_code.record[type][1]&0x7f)==(ak_context.need_seq - 1)) && 
        ((ak_context.data.seq_code.record[type][2]&0x0100)==0x0100)){
        return 1;
    } else {
        return 2;
    }
}

static void ak_process_sequence(int type,unsigned short length)
{
    if(ak_context.base_len[type] == 0){
        return;
    }
    ak_context.data.seq_code.record[type][AK_MAX_SEQ_CODE_RECORD] = length - ak_context.base_len[type];
	
    ak_record_move_ones(ak_context.data.seq_code.record[type], AK_MAX_SEQ_CODE_RECORD);
	ak_int_record_move_ones(ak_context.data.seq_code.sn_record[type],AK_MAX_SEQ_CODE_RECORD);
	if(!((ak_context.data.seq_code.sn_record[type][1] - ak_context.data.seq_code.sn_record[type][0] == 1) &&
		(ak_context.data.seq_code.sn_record[type][2] - ak_context.data.seq_code.sn_record[type][1] == 1) &&
		(ak_context.data.seq_code.sn_record[type][3] - ak_context.data.seq_code.sn_record[type][2] == 1) &&
		(ak_context.data.seq_code.sn_record[type][4] - ak_context.data.seq_code.sn_record[type][3] == 1) &&
		(ak_context.data.seq_code.sn_record[type][5] - ak_context.data.seq_code.sn_record[type][4] == 1))){
		return;
	}
    int pkt_type = ak_pkt_seq_type(type);
    if(pkt_type != 2)
    {
        unsigned char tempBuffer[AK_MAX_PTYPE][6] = {0};
        tempBuffer[type][0]=ak_context.data.seq_code.record[type][0]&0x7F;
        tempBuffer[type][1]=ak_context.data.seq_code.record[type][1]&0x7F;
        tempBuffer[type][2]=ak_context.data.seq_code.record[type][2]&0xFF;
        tempBuffer[type][3]=ak_context.data.seq_code.record[type][3]&0xFF;
        tempBuffer[type][4]=ak_context.data.seq_code.record[type][4]&0xFF;
        tempBuffer[type][5]=ak_context.data.seq_code.record[type][5]&0xFF;

		ak_info("process_seq: %02x %d %d %02x %02x %02x %02x %02x %02x \n",
                tempBuffer[type][0], tempBuffer[type][1],
                type, ak_context.seq_success_map_cmp, ak_context.seq_success_map,
                tempBuffer[type][2], tempBuffer[type][3], tempBuffer[type][4], tempBuffer[type][5]);
        if(pkt_type == 0 && tempBuffer[type][0] ==  (ak_calcrc_bytes(&tempBuffer[type][1],5)&0x7F))
        {
            int cur_seq = tempBuffer[type][1];
            ak_add_seq_data(&tempBuffer[type][2], cur_seq);
			resest_ak_data();
        } else if(pkt_type == 1 && tempBuffer[type][0] == (ak_calcrc_bytes(&tempBuffer[type][1],ak_context.total_len%4 + 1)&0x7F)){
            int cur_seq = tempBuffer[type][1];
            ak_add_seq_data(&tempBuffer[type][2], cur_seq);

            resest_ak_data();
        } else
        {
        }
        if(ak_context.seq_success_map_cmp == ak_context.seq_success_map)
        {
            ak_context.random_num = ak_context.usr_data[ak_context.pswd_len];
            memcpy(ak_context.pwd, ak_context.usr_data, ak_context.pswd_len);
            ak_context.usr_data[ak_context.pswd_len + 1] = 0;
            ak_context.ak_state = AK_STATE_COMPLETE;
        }
    } 
}

int ak_recv(const uint8_t * frame, int length)
{
    int type = 0;
    if(!ak_filter(frame, length, &type))
        return AK_STATUS_CONTINUE;

    uint8_t *temp = (uint8_t *)frame;
    ak_debug("%s:state:%d, type:%d, len:%d, seq:%d\n", __func__, ak_context.ak_state, type,length,(temp[22]+temp[23]*256)>>4);
    switch(ak_context.ak_state)
    {
        case AK_STATE_IDLE:
            if(ak_diakover_filter(frame, length)) {
                ak_recv_diakover(frame, length, type);
                if(ak_context.ak_state == AK_STATE_SRC_LOCKED) {
                    ak_info("lock channel \n");
                    return AK_STATUS_LOCK_CHANNEL;
                }
            }
            break;
        case AK_STATE_SRC_LOCKED:
            ak_get_base_len(frame, length, type);
			ak_context.data.magic_code.sn_record[type][AK_MAX_MAGIC_CODE_RECORD] = (frame[22]+frame[23]*256)>>4;
            ak_process_magic_code(type,length);
            break;
        case AK_STATE_MAGIC_CODE_COMPLETE:
            ak_get_base_len(frame, length, type);
			ak_context.data.prefix_code.sn_record[type][AK_MAX_PREFIX_CODE_RECORD] = (frame[22]+frame[23]*256)>>4;
            ak_process_prefix_code(type,length);
            break;    
        case AK_STATE_PREFIX_CODE_COMPLETE:
            ak_get_base_len(frame, length, type);
            ak_context.data.seq_code.sn_record[type][AK_MAX_SEQ_CODE_RECORD] = (frame[22]+frame[23]*256)>>4;
            ak_process_sequence(type,length);
            if(ak_context.ak_state == AK_STATE_COMPLETE) {
                ak_info("AK_STATE_COMPLETE \n");
                return AK_STATUS_GOT_SSID_PSWD;
            }
            break;        
        default:
            ak_context.ak_state = AK_STATE_IDLE;
            break;
    }
    return AK_STATUS_CONTINUE;
}

static int ak_process_data(void *packet, int size)
{
    int ret;
    int msg;
    ret = ak_recv((const uint8_t *)packet, size);
    if(ret == AK_STATUS_CONTINUE)
    {
    }
    else if(ret == AK_STATUS_LOCK_CHANNEL)
    {
        xTimerStop(ak_cf.channel_timer,0);
        msg = AK_STATUS_LOCK_CHANNEL;
        ak_round = 0;
        xQueueSend(ak_event_queue, &msg, portMAX_DELAY);
    }
    else if(ret == AK_STATUS_GOT_SSID_PSWD)
    {
        ak_debug("%s:AK_STATUS_GOT_SSID_PSWD \n", __func__);
        xTimerStop(ak_cf.task_timer,0);
        ak_get_result(&ak_result);
        msg = AK_STATUS_GOT_SSID_PSWD;
        xQueueSend(ak_event_queue, &msg, portMAX_DELAY);
    }

    return ret;
}


int ak_process_manager(void *packet, int size, int8_t rssi)
{
    uint8_t ssid[ZC_MAX_SSID_LEN] = {0}, bssid[ETH_ALEN] = {0};
    uint8_t auth, pairwise_cipher, group_cipher;
    struct ieee80211_hdr *hdr;
    int fc, ret, channel;
    struct ieee80211_hdr *mgmt_header = (struct ieee80211_hdr *)packet;

    if (mgmt_header == NULL) {
        return -1;
    }

    hdr = (struct ieee80211_hdr *)mgmt_header;
    fc = hdr->frame_control;

    
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

    cfg80211_get_cipher_info((uint8_t *)mgmt_header, size, &auth,
                             &pairwise_cipher, &group_cipher);
    
    awss_save_apinfo(ssid, bssid, channel, auth,
                     pairwise_cipher, group_cipher, rssi);

    return -1;
}

static void airkiss_wifiSnifferCallback(void *buf, int len, wifi_promiscuous_pkt_type_t type)
{
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf; 
    if(type == WIFI_PKT_MGMT){
        ak_process_manager((char *)pkt->payload, len, pkt->rx_ctrl.rssi);
    }
    else if(type == WIFI_PKT_DATA){
        if (AK_STATUS_GOT_SSID_PSWD==ak_process_data((char *)pkt->payload, len)){
        }
    }
    
}

void airkiss_task(void* parameter)
{
    int event;
    
    memset(&ak_context , 0, sizeof(ak_local_context));
    ak_context.ak_state = AK_STATE_IDLE;
    ak_event_queue = xQueueCreate(3, sizeof(int));
    
    wifi_promiscuous_filter_t filter = {0};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA | WIFI_PROMIS_FILTER_MASK_MGMT;
    wifi_set_promiscuous_filter(&filter);
    wifi_set_promiscuous_rx_cb(airkiss_wifiSnifferCallback);
    wifi_set_promiscuous(true);
    awss_init_ieee80211_aplist();
    xTimerStart(ak_cf.channel_timer, 0);
    xTimerStart(ak_cf.task_timer, 0 );
    while(1){
        if (pdPASS == xQueueReceive(ak_event_queue, &event, (portTickType)portMAX_DELAY)){
            switch (event)
            {
                case AK_STATUS_LOCK_CHANNEL:
                    if(ak_cf.callback != NULL){
                        ak_cf.callback(AK_STATUS_LOCK_CHANNEL,&ak_current_channel);
                    }
					wifi_rf_set_channel(ak_current_channel);
                    memset(&filter, 0, sizeof(airkiss_wifiSnifferCallback));
                    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
                    wifi_set_promiscuous_filter(&filter);
                    break;
                case AK_STATUS_GOT_SSID_PSWD:
                    if(ak_cf.callback != NULL){
                                ak_info("%s:AK_STATUS_GOT_SSID_PSWD \n", __func__);
                        ak_cf.callback(AK_STATUS_GOT_SSID_PSWD,&ak_result);
                    }
                    break;
                case AK_STATUS_TIMEOUT:
                    ak_context.ak_state = AK_STATE_STOPED;
                    memset(&ak_head, 0, sizeof(ak_head_t));
                    resest_ak_data();
                    if(ak_cf.callback != NULL){
                        ak_cf.callback(AK_STATUS_TIMEOUT,NULL);
                    }
                    goto stop;
                case AK_STATUS_STOP:
                    ak_context.ak_state = AK_STATE_STOPED;
                    memset(&ak_head, 0, sizeof(ak_head_t));
                    resest_ak_data();
                    if(ak_cf.callback != NULL){
                        ak_cf.callback(AK_STATUS_STOP,NULL);
                    }
                    goto stop;
            }
        }
    }
stop:
    if(ak_event_queue) {
        vQueueDelete(ak_event_queue);
    }   
    ak_event_queue = NULL;
	is_enter_ak = false;
    vTaskDelete(NULL);
}

bool airkiss_channel_is_valid(int channel)
{
    return zconfig_check_ap_channel(channel);
}

void airkiss_channel_switch_func(void* param)
{
    uint8_t chn = wifi_rf_get_channel();
    chn++;
    if(chn > AK_MAX_CHANNELS) {
        chn = 1;
    }
    while(ak_round >= 20 && airkiss_channel_is_valid(chn) == false){
        chn++;
    }
    ak_round++;
    ak_current_channel = chn;
    ak_change_channel();
    wifi_rf_set_channel(chn);
    if(chn == 1 || chn == 6 || chn == 11){
        xTimerChangePeriod(ak_cf.channel_timer, pdMS_TO_TICKS(500), 0);
    }else {
        xTimerChangePeriod(ak_cf.channel_timer, pdMS_TO_TICKS(200), 0);
    }
}

void airkiss_timeout_func(void* param)
{
    int msg;

    ak_info("%s:timeout:%d s \n", __func__, ak_cf.timeout_s);
    if(ak_context.ak_state < AK_STATE_COMPLETE && ak_context.ak_state != AK_STATE_STOPED) {
        xTimerDelete(ak_cf.channel_timer,0);
        xTimerDelete(ak_cf.task_timer,0);
        msg = AK_STATUS_TIMEOUT;
        if(ak_event_queue) {
            xQueueSend(ak_event_queue, &msg, portMAX_DELAY);
        }
        wifi_set_promiscuous(false);
    } else {
		return;
    }
}

void ak_callback(short status, void *pdata)
{
    if(status == AK_STATUS_GOT_SSID_PSWD) 
    {
        memcpy(&ak_result,pdata,sizeof(ak_result_t));
        ak_stop();
    } else if(status == AK_STATUS_STOP){
        if(ak_result.ssid[0] != 0 && ak_result.ssid_length != 0) {
				got_ssid_pwd_callback(ak_result.ssid,ak_result.pwd);
        }
		
		memset(&ak_result, 0 ,sizeof(ak_result_t));
    } else if(status == AK_STATUS_TIMEOUT){
        memset(&ak_result, 0 ,sizeof(ak_result_t));
        ak_stop();
    }
	return;
}

void handler_got_ssid_passwd(void* ssid, void* password)
{
	int ret = 0;
	wifi_config_u conf;

    ak_info("AP_SSID:%s\n", ssid);
    ak_info("AP_PASSWORD:%s\n", password);

    memset(&conf, 0, sizeof(wifi_config_u));
    strcpy((char *)conf.sta.ssid, ssid);
    strcpy(conf.sta.password, password);
    ret |= wifi_stop_station();
    ret |= wifi_start_station(&conf);
    if(ret != 0){
        os_printf(LM_CMD, LL_ERR,"CONNECT AP ERROR %d\n",ret);
    }
	else
	{
		struct ip_info if_ip;
        while(1){
			if(STA_STATUS_CONNECTED!= wifi_get_sta_status())
			{
				vTaskDelay(pdMS_TO_TICKS(100));
				continue;
			}
			wifi_get_ip_info(STATION_IF,&if_ip);
			if(ip_addr_isany_val(if_ip.ip))
			{
				vTaskDelay(pdMS_TO_TICKS(100));
				continue;
			}
		break;
		}
		ak_send_ack_start();
	}

	os_printf(LM_CMD, LL_INFO,"AK STOP!\n");
}

static int ak_is_already_started_flag = 0;
int ak_is_already_started()
{
    return ak_is_already_started_flag;
}

int ak_start()
{
    portBASE_TYPE result;
    ak_info("%s \n", __func__);
    if (-1 ==  ak_event_handler_register(1, handler_got_ssid_passwd, 0)) {
        ak_error("ak_start error \n");
        return CMD_RET_FAILURE;
    }

    if(ak_context.ak_state == AK_STATE_STOPED && is_enter_ak == false){
        wifi_stop_station();
        memset(&ak_context , 0, sizeof(ak_local_context));
        memset(&ak_cf , 0, sizeof(ak_config));
        ak_context.ak_state = AK_STATE_IDLE;
		is_enter_ak = true;
        ak_cf.callback = ak_callback;
        if(ak_cf.timeout_s == 0){
            ak_cf.timeout_s = 120;
        }
		
		s_ak_ack_send = false;
		vTaskDelay(pdMS_TO_TICKS(200));

        ak_cf.channel_timer = xTimerCreate("ChannelTimer", pdMS_TO_TICKS(AK_CHANNEL_TIME_FREQ), pdTRUE, NULL, (TimerCallbackFunction_t)airkiss_channel_switch_func);
        ak_cf.task_timer = xTimerCreate("TaskTimer",pdMS_TO_TICKS(ak_cf.timeout_s*1000), pdFALSE, NULL, (TimerCallbackFunction_t)airkiss_timeout_func);
	    result = xTaskCreate(airkiss_task, (const char *)"airkiss_task", 1024, NULL, 7, &ak_task_handle);

        if(result != pdTRUE || ak_cf.channel_timer == NULL || ak_cf.task_timer == NULL) {
            if(ak_task_handle) {vTaskDelete(ak_task_handle);}
            if(ak_cf.channel_timer) {xTimerDelete(ak_cf.channel_timer,0);}
            if(ak_cf.task_timer) {xTimerDelete(ak_cf.task_timer,0);}
			is_enter_ak = false;
			ak_error("ak error! \n");
        }
        ak_is_already_started_flag = 1;
        return 0;
    } else {
        ak_error("ak is already started\n");
        return -1;
    }
}

int ak_stop(void)
{
    int msg;

    ak_info("%s \n", __func__);
    if(ak_context.ak_state != AK_STATE_STOPED)
    {
        wifi_set_promiscuous(false);

        if(ak_cf.channel_timer) 
        {
            ak_round = 0;
            xTimerStop(ak_cf.channel_timer,0);
            xTimerDelete(ak_cf.channel_timer,0);
        }
        if(ak_cf.task_timer) 
        {
            xTimerStop(ak_cf.task_timer,0);
            xTimerDelete(ak_cf.task_timer,0);
        }
        msg =  AK_STATUS_STOP;
        if(ak_event_queue) {
            xQueueSend(ak_event_queue, &msg, portMAX_DELAY);
        }
        awss_deinit_ieee80211_aplist();
        ak_context.ak_state = AK_STATE_STOPED;
        
        vTaskDelay(pdMS_TO_TICKS(100));
        ak_is_already_started_flag = 0;
        return 0;
    }else {
        ak_error("ak is already stopped\n");
        return -1;
    }
}

static int cmd_ak_start(int argc, char *argv[])
{
	if (-1 ==  ak_event_handler_register(1, handler_got_ssid_passwd, 0)) {
	    ak_error("ak_event_handler_register error \n");
		return CMD_RET_FAILURE;
	}

	ak_start();

	return CMD_RET_SUCCESS;
}

static int cmd_ak_stop(int argc, char *argv[])
{
	ak_stop();

	return CMD_RET_SUCCESS;
}

static int cmdak_show_version(int argc, char *argv[])
{
	printf("ak_vers %s %s\n",ak_datevers,ak_timevers);
	return CMD_RET_SUCCESS;
}

CLI_CMD(ak_start, cmd_ak_start, "ak_start", "ak_start");
CLI_CMD(ak_stop, cmd_ak_stop, "ak_stop", "ak_stop");
CLI_CMD(akversion, cmdak_show_version, "displays version", "displays version");
