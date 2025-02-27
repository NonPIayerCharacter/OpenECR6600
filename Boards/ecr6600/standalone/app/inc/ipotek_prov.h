/**
* @file ipotek_prov.h
* @author 小王同学
* @brief ipotek_prov module is used to 
* @version 0.1
* @date 2023-04-27
*
*/
 
#ifndef __IPOTEK_PROV_H__
#define __IPOTEK_PROV_H__
#include "system_def.h"
 
#ifdef __cplusplus
extern "C" {
#endif
 
/***********************************************************
*************************micro define***********************
***********************************************************/
#define ANDLINK_DEVICE_TYPE                 "590559"    
#define ANDLINK_PRODUCT_TOKEN               "PAKlBX4AbHA2PJAO"    
#define ANDLINK_TOKEN                       "op2HK7Dx1VDcEIkP"
#define ANDLINK_VENDOR_ID                   "0775"
#define ANDLINK_TUI                         "11100069"
// #define DEVICE_AP_SSID                      "CMQLINK-590488-"
#define DEVICE_AP_SSID                      "CMQLINK-"

#define DEFAULT_USER_KEY                    "CMCC-0775-590559"
#define FRIME_VERSION                       "1.0"  
#define SOFTWARE_VERSION                    "1.0"

#define ANDLINK_SAVE_INFO_STR               "andlink_wifi"
#define DEVICE_SAVE_INFO_STR                "device_info"
#define DEVICE_OTA_INFO_STR                 "ota_info"

#define DEFAULT_OTA_CHECK_URL               "https://fota.komect.com/fota/chkUpgrade"
 
/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef enum {
    WIFI_AUTH_OPEN = 0,        /**< authenticate mode : open */
    WIFI_AUTH_WEP,             /**< authenticate mode : WEP */
    WIFI_AUTH_WPA_PSK,         /**< authenticate mode : WPA_PSK */
    WIFI_AUTH_WPA2_PSK,        /**< authenticate mode : WPA2_PSK */
    WIFI_AUTH_WPA_WPA2_PSK,    /**< authenticate mode : WPA_WPA2_PSK */
    WIFI_AUTH_WPA2_ENTERPRISE, /**< authenticate mode : WPA2_ENTERPRISE */
    WIFI_AUTH_WPA3_PSK,        /**< authenticate mode : WPA3_PSK */
    WIFI_AUTH_WPA2_WPA3_PSK,   /**< authenticate mode : WPA2_WPA3_PSK */
    WIFI_AUTH_MAX
} WIFI_AUTH_MODE_T;

typedef struct {
    /***********************************设备配网时需要存储的信息*************************************************/
    char ssid[32];
    char password[32];
    unsigned char channel;
    WIFI_AUTH_MODE_T auth_mode;
    char user_key[33];
    char gw_address[50];
    char gw_address2[50];
    /**********************************************************************************************************/

    /***********************************设备进行注册请求获得的信息*************************************************/
    char deviceId[33];
    char deviceToken[17];
    char dmToken[64];
    char gwToken[17];
    unsigned int userId;
    /**********************************************************************************************************/

    /***********************************设备进行上线请求获得的信息*************************************************/
    char deviceManageUrl[65];                    //终端设备管理平台 URL 由平台返回
    unsigned short httpRetry[4];                 //http重试策略
    char mqttClientId[33];
    unsigned short mqttKeepalive;                 //mqtt心跳周期             
    char mqttPassword[17];
    unsigned short mqttRetry[4];                 //mqtt重试策略
    char mqttUrl[33];
    char mqttUser[33];
    unsigned char userBind;
    /**********************************************************************************************************/

    /******************************************设备OTA信息***************************************************/
    unsigned int ota_status;
    unsigned char ota_need_report;
    /**********************************************************************************************************/
    unsigned char is_bound;
} USER_PROV_INFO_T;

typedef struct {
    char deviceMac[13];      // 业务唯一标识：设备可根据实际业务需求上报 SN 值或真实 MAC 值(填写 MAC 时，全大写不带冒号)
    char productToken[17];   // 为设备在开发者门户注册的产品类型 Token，平台会检查该值的合法性，非法则不允许注册
    char deviceType[7];      // 设备在开发者门户注册的产品类型码
    unsigned int timestamp;  // 时间戳，单位毫秒
    char protocolVersion[5]; // 协议版本号，本协议版本默认填写：V3.2
    char cmei[13];           // 设备唯一标识
    unsigned char authMode;  // 0 代表类型认证,1 代表设备认证
    char authId[17];         // 智能设备安全认证唯一标识     选填
    char mac[13];            // 设备真实 MAC，全大写不带冒号
    char sn[13];             // 设备真实 SN
    char reserve[17];        // 厂商特殊标记字段    选填
    char manuDate[17];       // 设备生产日期,格式为:年-月
    char OS[17];             // 操作系统，android 9.0、Linux 2.6.18 等需包含操作系统版本号
    char type[17];           // 芯片类型,Main/WiFi/Zigbee/BLE 等，必填
    char factory[17];        // 芯片厂商
    char model[17];          // 芯片型号
} ANDLINK_AUTH_INFO_T;

/***********************************************************
***********************variable define**********************
***********************************************************/
 
 
/***********************************************************
***********************function define**********************
***********************************************************/
sys_err_t ipotek_prov_data_init(void); 
sys_err_t ipotek_prov_data_save(USER_PROV_INFO_T *data);
sys_err_t prov_get_ssid(char *ssid);
sys_err_t prov_get_password(char *password);
sys_err_t prov_get_wifi_channel(unsigned char *channel);
sys_err_t prov_get_gw_address2(char *gw_address2);
sys_err_t prov_get_user_key(char *user_key);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /*__IPOTEK_PROV_H__*/
 