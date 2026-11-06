#ifndef __HAL_SNTP_H__
#define __HAL_SNTP_H__

#define SNTPINTVMIN (15)
#define SNTPINTVMAX (1073741)

#define SNTP_SERVER_DNS_NAME_MAX_LEN 46
#define HAL_SNTP_SETTING_SAVE 0x01
#define HAL_SNTP_SYNC_NTP 0x02
#define HAL_SNTP_SETTING_SERVER 0x04

typedef struct {
    unsigned char isDst;
    unsigned int sntpPeriod;
    float timeZone;
} sntp_ctrl_cfg_t;

typedef void (*hal_sntp_get_servertime_callback)(void);
void hal_sntp_sync_func_register(hal_sntp_get_servertime_callback func);
const sntp_ctrl_cfg_t *hal_sntp_config_get(void);
int hal_sntp_timezone_sec(float timezone);
unsigned int hal_sntp_dst_sec(void);
/* ntp server update time */
int hal_sntp_set_system_time(unsigned int ntpTime);
int hal_sntp_timezone_set(float tz, int action);
float hal_sntp_timezone_get(void);
int hal_sntp_period_set(unsigned int periodMs, int action);
unsigned int hal_sntp_periodms_get(void);
int hal_sntp_set_servername(unsigned char idx, const char *server, int action);
const char *hal_sntp_get_servername(unsigned char idx);
void hal_sntp_init(int action);
void hal_sntp_stop_async(void);
int hal_sntp_set_utc_dst(unsigned char dst, int action);

#endif
