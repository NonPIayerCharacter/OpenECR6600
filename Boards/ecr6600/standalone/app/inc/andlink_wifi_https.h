/**
* @file andlink_wifi_https.h
* @author 小王同学
* @brief andlink_wifi_https module is used to 
* @version 0.1
* @date 2023-05-04
*
*/
 
#ifndef __ANDLINK_WIFI_HTTPS_H__
#define __ANDLINK_WIFI_HTTPS_H__
#include "system_def.h"

#ifdef __cplusplus
extern "C" {
#endif
 
/***********************************************************
*************************micro define***********************
***********************************************************/
typedef enum {
    HTTPS_REQUEST_NUM_GET_GATEWAY_ADDRESS = 0,
    HTTPS_REQUEST_NUM_DEVICE_AUTH,
    HTTPS_REQUEST_NUM_DEVICE_ONLINE_REQUEST,
    HTTPS_REQUEST_NUM_TERMINAL_DATA_REPORT,
} HTTPS_REQUEST_E;
 
/***********************************************************
***********************typedef define***********************
***********************************************************/
 
 
/***********************************************************
***********************variable define**********************
***********************************************************/
 
 
/***********************************************************
***********************function define**********************
***********************************************************/
void andlink_https_start(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /*__ANDLINK_WIFI_HTTPS_H__*/
 