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

#ifndef __PSM_MODE_CTRL_H__
#define __PSM_MODE_CTRL_H__

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "uart.h"
#include "gpio.h"
#include "pmu_reg.h"
#include "aon_reg.h"
#include "pd_core_reg.h"
#include "cli.h"
#include "rtc.h"

#define AGC_RAM_DS	((0x1) << (30))
#define AGC_RAM_ISO	((0x1) << (29))

#define WAKEUP_FALL_STATUS(a,b) (((a) >> ((b) + 6)) & 0x1)
#define WAKEUP_RISE_STATUS(a,b) (((a) >> ((b) + 1)) & 0x1)
#define WAKEUP_RTC_STATUS(a) (((a) >> 11) & 0x1)
#define WAKE_IO_NUM  5

enum SLEEP_STATUS{
   //sleep if close
   SLEEP_OFF =0,
   //close clock partly
   CLOCK_SLEEP,
   OFFLINE_SLEEP,
   //close rf and clock partly
   MODEM_SLEEP,
   //cpu and other clock gated,rf is close 
   WFI_SLEEP,
   //close cpu rf and clock,when weaked up system can quickly resume
   LIGHT_SLEEP,
   //close cpu rf and clock,when weaked up system will reset
   DEEP_SLEEP,
   //max status,should't in this status 
   SLEEP_MAX,
   
};

enum PSM_CTRL_INTF{
	PSM_SLEEP_IN = 0,
	PSM_SLEEP_OUT,
	PSM_SLEEP_MAX,
};

typedef struct _T_WAKE_IO_ISR_CALLBACK
{
	int pin;
	void (* callback)(void *);
	void *data;
}T_WAKE_ISR_CALLBACK;

typedef signed int (*psm_func_cb)(void);

bool psm_wake_trigger_level(unsigned char mode);
void psm_wake_clear_trigger_level();
void psm_set_powerup_pcu_int_status();
unsigned int psm_get_powerup_pcu_int_status();
void psm_set_aon_gpio(unsigned int pin , unsigned int type);
bool psm_gpio_wakeup_config(unsigned int pin, unsigned int enable);
void psm_soc_sleep_distribute(unsigned int mode);
void psm_set_wakeup_mode();
void psm_enter_sleep(unsigned int mode);
#endif
