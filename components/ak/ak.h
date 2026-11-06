#ifndef _AK_
#define _AK_

#ifdef __cplusplus
extern "C" {
#endif

#include "wifi_sniffer.h"
#include "system_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "cli.h"

#define AK_PASSWORD_MAX_LEN    64
#define AK_SSID_MAX_LEN        32
#define AK_MAX_CHANNELS 13
#define AK_CHANNEL_TIME_FREQ 200
#define AK_CHANNEL_LOCK_TIME 3000
#define ZC_MAX_SSID_LEN     (32 + 1)
#define ZC_MAX_PASSWD_LEN   (64 + 1)
#define AK_USR_DATA_BUFF_MAX_SIZE    (AK_PASSWORD_MAX_LEN + 1 + AK_SSID_MAX_LEN)
#define AK_MAX_MAGIC_CODE_RECORD    4
#define AK_MAX_PREFIX_CODE_RECORD    4
#define AK_MAX_SEQ_CODE_RECORD    6

#define AK_MAX_PTYPE           4
#define AK_MULTI_TYPE_TODS     0
#define AK_MULTI_TYPE_FRDS     1
#define AK_BRODA_TYPE_TODS     2
#define AK_BRODA_TYPE_FRDS     3
#define AK_MAX_GUIDE_RECORD    4

#define ak_info(...) os_printf(LM_WIFI, LL_INFO, __VA_ARGS__)
#define ak_debug(...) os_printf(LM_WIFI, LL_DBG, __VA_ARGS__)
#define ak_error(...) os_printf(LM_WIFI, LL_ERR, __VA_ARGS__)

typedef void (*ak_callback_t)(short status, void *pdata);
typedef void (*g_s_p_callback)(void *ssid, void *pwd);

typedef struct
{
	char pwd[64+1]; 				
	char ssid[32+1];			
	unsigned char pwd_length;	
	unsigned char ssid_length;
	unsigned char random;		
	unsigned char reserved; 
} ak_result_t;

typedef enum {
	AK_STATUS_CONTINUE = 0, 
	AK_STATUS_LOCK_CHANNEL,
	AK_STATUS_GOT_SSID_PSWD,
	AK_STATUS_TIMEOUT,
	AK_STATUS_STOP,
} airkiss_status_t;

typedef struct ak_head
{
	unsigned char dummyap[26];
	unsigned char dummy[32];
} ak_head_t;

typedef enum
{
	AK_STATE_STOPED = 0,
	AK_STATE_IDLE,
	AK_STATE_SRC_LOCKED,
	AK_STATE_MAGIC_CODE_COMPLETE,
	AK_STATE_PREFIX_CODE_COMPLETE,
	AK_STATE_COMPLETE
} AK_STATE;

typedef struct
{
	unsigned short	length_record[AK_MAX_PTYPE][AK_MAX_GUIDE_RECORD + 1];
	unsigned int 	sn_record[AK_MAX_PTYPE][AK_MAX_GUIDE_RECORD + 1];
}ak_guide_code_record;

typedef struct
{
	unsigned short record[AK_MAX_PTYPE][AK_MAX_MAGIC_CODE_RECORD + 1];
	unsigned int   sn_record[AK_MAX_PTYPE][AK_MAX_MAGIC_CODE_RECORD + 1];
}ak_magic_code_record;

typedef struct
{
	unsigned short record[AK_MAX_PTYPE][AK_MAX_PREFIX_CODE_RECORD + 1];
	unsigned int   sn_record[AK_MAX_PTYPE][AK_MAX_PREFIX_CODE_RECORD + 1];
}ak_prefix_code_record;

typedef struct
{
	unsigned short record[AK_MAX_PTYPE][AK_MAX_SEQ_CODE_RECORD + 1];
	unsigned int   sn_record[AK_MAX_PTYPE][AK_MAX_SEQ_CODE_RECORD + 1];
}ak_seq_code_record;
	
union ak_data{
	ak_guide_code_record guide_code;
	ak_magic_code_record magic_code;
	ak_prefix_code_record prefix_code;
	ak_seq_code_record	seq_code;
};

typedef struct
{
	char pwd[ZC_MAX_PASSWD_LEN];						
	char ssid[ZC_MAX_SSID_LEN];
	unsigned char pswd_len;
	unsigned char ssid_len;
	unsigned char random_num;
	unsigned char ssid_crc;
	unsigned char usr_data[AK_USR_DATA_BUFF_MAX_SIZE];
	AK_STATE ak_state;
	unsigned char src_mac[6];
	unsigned char need_seq;
	unsigned char base_len[AK_MAX_PTYPE];
	unsigned char total_len;
	unsigned char pswd_lencrc;
	unsigned char recv_len;
	unsigned short seq_success_map;
	unsigned short seq_success_map_cmp;
	union ak_data data;
}ak_local_context;

typedef struct _ak_config
{
	ak_callback_t callback;
	unsigned int timeout_s;
	TimerHandle_t channel_timer;
	TimerHandle_t task_timer;
}ak_config;

int ak_start(void);
int ak_stop(void);
int ak_event_handler_register(short event, void* func, int reserver);
int ak_is_already_started();

#ifdef __cplusplus
}
#endif

#endif //_AK_
