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
#ifdef CONFIG_CUSTOM_HEF
#define PSM_PREVENT_SLEEP_T  (300)
#endif

void psm_set_lowpower(void);
void psm_set_normal(void);
bool psm_wakeup_gpio_conf(unsigned int gpio_n, unsigned int enable);
bool psm_set_deep_sleep(unsigned int sleep_time);
bool psm_set_sleep_mode(unsigned int sleep_mode, unsigned char listen_interval);
bool psm_switch_rf_state(bool rf_state);
void psm_axuadc_ldo_open();
#ifdef CONFIG_CUSTOM_HEF
bool  psm_set_prevent_sleep_time(unsigned int prev_time/*unit:ms*/);
#endif

#if defined(CONFIG_AI_VLLM) || defined(CONFIG_AI_COZE) || defined(CONFIG_AI_QZT)
void psm_enter_deep_sleep_no_rtc();
#endif

#endif
