/*******************************************************************************
 * Copyright by eswin 
 *
 * File Name:  
 * File Mark:    
 * Description:  
 * Others:        
 * Version:       v0.1
 * Author:        
 * Date:          
 * History 0:      
 *     Date: 
 *     Version:
 *     Author: 
 *     Modification:  
  ********************************************************************************/

#ifndef __PSM_USER_H__
#define __PSM_USER_H__

#include "psm_mode_ctrl.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include <stdlib.h>

#include "task.h"

#ifdef CONFIG_PSM_SWITCH_LOWPOWER
typedef enum {
	CONNECT_OP_NONE,
	CONNECT_BY_BLE,
	CONNECT_BY_SC,
	CONNECT_BY_AP,
}CONNECT_OP_STATE;

#define SF_STAT_LOW_POWER          0   // idle status,use to external config network
#define SF_STAT_UNPROVISION        1   // smart config status
#define SF_STAT_AP_STA_UNCFG       2   // ap WIFI config status
#define SF_STAT_AP_STA_DISC        3   // ap WIFI already config,station disconnect
#define SF_STAT_AP_STA_CONN        4   // ap station mode,station connect
#define SF_STAT_STA_DISC           5   // only station mode,disconnect
#define SF_STAT_STA_CONN           6   // station mode connect
#define SF_STAT_CLOUD_CONN         7   // cloud connect
#define SF_STAT_AP_CLOUD_CONN      8   // cloud connect and ap start
#define SF_STAT_REG_FAIL           9   // register fail
#define SF_STAT_OFFLINE            10   // offline
#define SF_STAT_MQTT_ONLINE        11
#define SF_STAT_MQTT_OFFLINE       12
#define SF_STAT_UNPROVISION_AP_STA_UNCFG		13 //smart-cfg and ap-cfg concurrent config status
#define SF_STAT_BT_ACTIVED         14
#define SF_STAT_OTA_UPDATE         15


/**
 * @brief parameters of recharge for single_fire_netconfig 
 * 
 * @param[in]   1.param_updated(false/true):if the recharge_parameters has been updated
 * 		    2.volt_full(mV):full voltage
 *			3.volt_low(mV):lowest voltage
 *			4.time_recharge_len(ms):time_length of recharge from volt_low to volt_full
 *			5.time_consum_len(ms):time_length of consume from volt_full to volt_low
 */
typedef struct psm_recharge_time_param
{
	bool param_updated;
	unsigned int volt_full;
	unsigned int volt_low;
	unsigned int time_recharge_len;
	unsigned int time_consum_len;
}RECHARGE_PARAMS;

/**
 * @brief the type of voltage_detection for single_fire_netconfig
*/
typedef enum
{
	ADC_DETECT = 0,
	GPIO_DETECT,
}DETEC_TYPE;

/**
 * @brief init recharge_param
 *
 * @param[in]	param ,init all recharge params 
 *				
 * @return  none
 */
bool psm_start_connect(CONNECT_OP_STATE status);
bool psm_stop_connect(CONNECT_OP_STATE status);
char psm_active_status_op(char isSet, char value);
void psm_recharge_param_init(RECHARGE_PARAMS param);
unsigned int psm_adc_recharge_time_cal();
unsigned int psm_gpio_recharge_time_cal(unsigned int time_len);
unsigned int psm_adc_volt_get();

/**
 * @brief calculate recharge_time_length for single_fire_netconfig
 *
 * @param[in]	volt_detec_type ADC_DETECT:0  GPIO_DETECT:1 
 *				consum_time_len(ms):time_length of consume from volt_full to volt_curr
 *
 * @return  time_recharge_need(ms)
 */
 
int psm_recharge_time_cal(DETEC_TYPE volt_detec_type, unsigned int consum_time_len);

bool psm_set_lp_mode(unsigned int sleep_mode);
bool psm_start_connect(CONNECT_OP_STATE status);
int psm_charge_time(DETEC_TYPE volt_detec_type,int time_ms);
int psm_set_smart_config_mode(unsigned char active_code);
int psm_get_smart_config_mode();
int psm_get_mqtt_client_id();
int psm_set_mqtt_client_id(int id);
int psm_sf_event_cb(void (* cb)(unsigned char));
int psm_event_set(unsigned char stat);
int psm_msdelay(unsigned int delay_ms);
#endif	//endif  CONFIG_PSM_SWITCH_LOWPOWER

void psm_set_lowpower(void);
void psm_set_normal(void);
bool psm_wakeup_gpio_conf(unsigned int gpio_n, unsigned int enable);
bool psm_wakeup_irq_register(int pin, void (* callback)(void *), void * data);
bool psm_set_deep_sleep(unsigned int sleep_time);
bool psm_set_sleep_mode(unsigned int sleep_mode, unsigned char listen_interval);
bool psm_switch_rf_state(bool rf_state);
#endif
