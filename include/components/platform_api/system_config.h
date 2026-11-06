/**
 * @file system_config.h
 * @brief This is a brief description
 * @details This is the detail description
 * @author liuyafeng
 * @date 2021-6-17
 * @version V0.1
 * @par Copyright by http://eswin.com/.
 * @par History 1:
 *      Date:
 *      Version:
 *      Author:
 *      Modification:
 *
 * @par History 2:
 */

#ifndef _SYSTEM_CONFIG_H
#define _SYSTEM_CONFIG_H

/*--------------------------------------------------------------------------
* 	                                        	Include files
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
* 	                                        	Macros
--------------------------------------------------------------------------*/
/** Description of the macro */
typedef enum { MAC_SAVE_TO_AMT = 0, MAC_SAVE_TO_OTP = 1, MAC_SAVE_TO_EFUSE = 2, MAC_SAVE_MAX } MAC_SAVE_LACATION;

typedef enum { MAC_STA = 0, MAC_AP = 1, MAC_BLE = 2, MAC_MAX } MAC_TYPE;

// mac
#define STA_MAC                              "mac"
#define AP_MAC                               "ap_mac"
#define BLE_MAC                              "ble_mac"

#define MAC_ADDR_IN_OTP                      0

// amt-nv
#define AMT_NV_FLAG                          "amt"
#define AMT_TX_POWER                         AMT_NV_FLAG "txPower"
#define AMT_TX_GAIN_FLAG                     AMT_NV_FLAG "txGainFlag"
#define AMT_FINISH_FLAG                      AMT_NV_FLAG "FinishFlag"
#define AMT_NV_MAC_PRIMARY_IF                AMT_NV_FLAG "MACPrimaryIF"

#define AMT_ADC_BAT_CONVERT_PARAM            AMT_NV_FLAG "adcConvertParam"
#define AMT_NV_VOICE_DEVICE_ID               AMT_NV_FLAG "VoiceDeviceId"
#define AMT_NV_VOICE_PRODUCT_KEY             AMT_NV_FLAG "VoiceProductKey"
#define AMT_NV_VOICE_AUTH_KEY                AMT_NV_FLAG "VoiceAuthKey"

#define AMT_NV_VOICE_QTH_PRODUCT_KEY         AMT_NV_FLAG "QTHProductKey"
#define AMT_NV_VOICE_QTH_PRODUCT_SECRET      AMT_NV_FLAG "QTHProductSecret"
#define AMT_NV_VOICE_QTH_DEVICE_KEY          AMT_NV_FLAG "QTHDeviceKey"
#define AMT_NV_VOICE_QTH_ACCESS_SECRET       AMT_NV_FLAG "QTHAccessSecret"
#define AMT_NV_VOICE_QTH_AUTH_KEY            AMT_NV_FLAG "QTHAuthKey"
// customer
#define CUSTOMER_NV_FLAG                     "cus"
#define CUSTOMER_NV_WIFI_STA_SSID            CUSTOMER_NV_FLAG "StaSSID"
#define CUSTOMER_NV_WIFI_STA_PWD             CUSTOMER_NV_FLAG "StaPW"
#define CUSTOMER_NV_WIFI_STA_BSSID           CUSTOMER_NV_FLAG "StaBSSID"
#define CUSTOMER_NV_WIFI_STA_PMK             CUSTOMER_NV_FLAG "PMK"
#define CUSTOMER_NV_WIFI_STA_CHANNEL         CUSTOMER_NV_FLAG "StaChannel"
#define CUSTOMER_NV_WIFI_AUTO_CONN           CUSTOMER_NV_FLAG "StaAutoConn"
#define CUSTOMER_NV_WIFI_OP_MODE             CUSTOMER_NV_FLAG "OpMode"

#define CUSTOMER_NV_WIFI_PCI_EN              CUSTOMER_NV_FLAG "pci_en"
#define CUSTOMER_NV_WIFI_LISTEN_INTERVAL     CUSTOMER_NV_FLAG "ListenInterval"
#define CUSTOMER_NV_WIFI_INTERVALSECOND      CUSTOMER_NV_FLAG "Intervalsecond"
#define CUSTOMER_NV_WIFI_REPEATCOUNT         CUSTOMER_NV_FLAG "RepeatCount"
#define CUSTOMER_NV_WIFI_SCAN_MODE           CUSTOMER_NV_FLAG "ScanMode"
#define CUSTOMER_NV_WIFI_JAP_TIMEOUT         CUSTOMER_NV_FLAG "JapTimeout"
#define CUSTOMER_NV_WIFI_PMF                 CUSTOMER_NV_FLAG "PMF"
#define CUSTOMER_NV_WIFI_STA_WPA3_PTK        CUSTOMER_NV_FLAG "StaWpa3Ptk"

#define CUSTOMER_NV_WIFI_AP_SSID             CUSTOMER_NV_FLAG "ApSSID"
#define CUSTOMER_NV_WIFI_AP_PWD              CUSTOMER_NV_FLAG "ApPW"
#define CUSTOMER_NV_WIFI_AP_CHANNEL          CUSTOMER_NV_FLAG "ApChannel"
#define CUSTOMER_NV_WIFI_AP_AUTH             CUSTOMER_NV_FLAG "ApAuth"
#define CUSTOMER_NV_WIFI_MAX_CON             CUSTOMER_NV_FLAG "MaxCon"
#define CUSTOMER_NV_WIFI_HIDDEN_SSID         CUSTOMER_NV_FLAG "HiddenSSID"

#define CUSTOMER_NV_WIFI_COUNTRYCODE         CUSTOMER_NV_FLAG "CountryCode"
#define CUSTOMER_NV_WIFI_WPAS_PMKINFO        CUSTOMER_NV_FLAG "wpas_pmk_info"
#define CUSTOMER_NV_WIFI_WIRELESSMODE        CUSTOMER_NV_FLAG "WirelessMode"
#define CUSTOMER_NV_WIFI_WIRELESSMODE_STA    CUSTOMER_NV_FLAG "WirelessModeSTA"
#define CUSTOMER_NV_WIFI_WIRELESSMODE_SOFTAP CUSTOMER_NV_FLAG "WirelessModeSoftap"

#define CUSTOMER_NV_BLE_DEVICE_NAME          CUSTOMER_NV_FLAG "BleDeviceName"
#define CUSTOMER_NV_BLUFI_NAME               CUSTOMER_NV_FLAG "BlufiDevName"
#define CUSTOMER_NV_BLE_ROLE                 CUSTOMER_NV_FLAG "BleRole"
#define CUSTOMER_NV_BLE_ADVMODE              CUSTOMER_NV_FLAG "BleAdvMode"
#define CUSTOMER_NV_BLE_SEC_VALUE            CUSTOMER_NV_FLAG "BleSecValue"
#define CUSTOMER_NV_BLE_SAVETRANS_VALUE      CUSTOMER_NV_FLAG "BleSaveTransVal"
#define CUSTOMER_NV_BLE_RAND_ADDR            CUSTOMER_NV_FLAG "BleMacAddr"

// sntp
#define CUSTOMER_NV_SNTP_CONFIG              CUSTOMER_NV_FLAG "SntpCfg"
#define CUSTOMER_NV_SNTP_SERVER              CUSTOMER_NV_FLAG "SntpServer"

// PSM
#define CUSTOMER_NV_PSM_FLAG                 CUSTOMER_NV_FLAG "PsmFlag"
#define CUSTOMER_NV_PSM_TIME                 CUSTOMER_NV_FLAG "PsmDeepSleepTime"  // 0-100 /ms
#define CUSTOMER_NV_PSM_POR_ENABLE           CUSTOMER_NV_FLAG "PsmPOREnable"
#define CUSTOMER_NV_PSM_CW                   CUSTOMER_NV_FLAG "PsmCw"
#define CUSTOMER_NV_PSM_WAIT_BEA_THR         CUSTOMER_NV_FLAG "PsmWatiBea"
#define CUSTOMER_NV_PSM_EN_CW_VAL            CUSTOMER_NV_FLAG "PsmEnCwVal"
#define CUSTOMER_NV_PSM_EN_CW_LOST_BEA_THR   CUSTOMER_NV_FLAG "PsmEnCwLstBea"

// IP
#define CUSTOMER_NV_WIFI_DHCPC_EN            CUSTOMER_NV_FLAG "DHCPC_EN"
#define CUSTOMER_NV_WIFI_DHCPS_EN            CUSTOMER_NV_FLAG "DHCPS_EN"
#define CUSTOMER_NV_WIFI_DHCPS_IP            CUSTOMER_NV_FLAG "DHCPS_IP"
#define CUSTOMER_NV_WIFI_AP_IP               CUSTOMER_NV_FLAG "AP_IP"
#define CUSTOMER_NV_WIFI_AP_GATEWAY          CUSTOMER_NV_FLAG "AP_gateway"
#define CUSTOMER_NV_WIFI_AP_NETMASK          CUSTOMER_NV_FLAG "AP_netmask"
#define CUSTOMER_NV_WIFI_STA_IP              CUSTOMER_NV_FLAG "STA_IP"
#define CUSTOMER_NV_WIFI_STA_GATEWAY         CUSTOMER_NV_FLAG "STA_gateway"
#define CUSTOMER_NV_WIFI_STA_NETMASK         CUSTOMER_NV_FLAG "STA_netmask"
#define CUSTOMER_NV_WIFI_STA_MAC             CUSTOMER_NV_FLAG "STA_MAC"
#define CUSTOMER_NV_TRANSLINK_IP             CUSTOMER_NV_FLAG "TransLinkIP"
#define CUSTOMER_NV_TRANSLINK_PORT           CUSTOMER_NV_FLAG "TransLinkPort"
#define CUSTOMER_NV_TRANSLINK_TYPE           CUSTOMER_NV_FLAG "TransLinkType"
#define CUSTOMER_NV_TRANSLINK_UDPPORT        CUSTOMER_NV_FLAG "TranslinkUDPPort"
#define CUSTOMER_NV_TCPIP_RECONN_INTV        CUSTOMER_NV_FLAG "ReconnIntv"
#define CUSTOMER_NV_TCPIP_DNS_SERVER0        CUSTOMER_NV_FLAG "DNSServer0"
#define CUSTOMER_NV_TCPIP_DNS_SERVER1        CUSTOMER_NV_FLAG "DNSServer1"
#define CUSTOMER_NV_TCPIP_DNS_SERVER2        CUSTOMER_NV_FLAG "DNSServer2"
#define CUSTOMER_NV_TCPIP_OPTIONS            CUSTOMER_NV_FLAG "TcpOption"
#define CUSTOMER_NV_WIFI_IP_MODE             CUSTOMER_NV_FLAG "IPMode"

#define CUSTOMER_NV_SYS_MSG                  CUSTOMER_NV_FLAG "SysMsg"
// UART
#define CUSTOMER_NV_UART_CONFIG              CUSTOMER_NV_FLAG "UartConfig"
#define CUSTOMER_NV_CLIENT_ID            CUSTOMER_NV_FLAG"ClientID"
// develop
#define DEVELOP_NV_FLAG                      "dev"

// otp
#define OTP_FLAG                             "otp"

// efuse
#define EFUSE_FLAG                           "efu"


#define CUSTOMER_NV_VOICE_USER_ID            CUSTOMER_NV_FLAG "VoiceUserId"
#define CUSTOMER_NV_VOICE_MODE               CUSTOMER_NV_FLAG "VoiceMode"

// JoyInside云服务专用Key
#define CUSTOMER_NV_JOYINSIDE_ACCESS_KEY     CUSTOMER_NV_FLAG "JdAccessKey"
#define CUSTOMER_NV_JOYINSIDE_ACCESS_SECRET  CUSTOMER_NV_FLAG "JdAccessSec"
#define CUSTOMER_NV_JOYINSIDE_UID            CUSTOMER_NV_FLAG "JdUid"
#define CUSTOMER_NV_JOYINSIDE_BOT_ID         CUSTOMER_NV_FLAG "JdBotId"
#define CUSTOMER_NV_JOYINSIDE_SERIAL_NUMBER  CUSTOMER_NV_FLAG "JdSerialNum"

// Fanlin API Cloud专用Key（独立命名空间，避免与JoyInside冲突）
#define CUSTOMER_NV_FANLIN_DEVICE_ID         CUSTOMER_NV_FLAG "FanlinDevId"
#define CUSTOMER_NV_FANLIN_DEVICE_SECRET     CUSTOMER_NV_FLAG "FanlinDevSec"
#define CUSTOMER_NV_FANLIN_SERIAL_NUMBER     CUSTOMER_NV_FLAG "FanlinSN"

// Tomcat音频格式配置

#define CUSTOMER_NV_TOMCAT_DATA_FORMAT       CUSTOMER_NV_FLAG "TomcatFmt"
#define CUSTOMER_NV_TOMCAT_VOLUME            CUSTOMER_NV_FLAG "TomcatVolume"

// QZT设备配置
#define CUSTOMER_NV_QZT_DEVICE_TEST          CUSTOMER_NV_FLAG "qztDeviceTest"
#define CUSTOMER_NV_QZT_DEVICE_KEY           CUSTOMER_NV_FLAG "deviceKey"
#define CUSTOMER_NV_QZT_PRODUCT_KEY          CUSTOMER_NV_FLAG "productKey"
#define CUSTOMER_NV_QZT_MQTT_CLIENT_ID       CUSTOMER_NV_FLAG "MqttClientId"
#define CUSTOMER_NV_QZT_MQTT_PASSWORD        CUSTOMER_NV_FLAG "MqttPassword"
/*--------------------------------------------------------------------------
* 	                                        	Types
--------------------------------------------------------------------------*/
/**
 * @brief The brief description
 * @details The detail description
 */

/*--------------------------------------------------------------------------
* 	                                        	Constants
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
* 	                                        	Global  Variables
--------------------------------------------------------------------------*/
/**  Description of global variable  */

/*--------------------------------------------------------------------------
* 	                                        	Function Prototypes
--------------------------------------------------------------------------*/
int hal_system_set_mac(MAC_SAVE_LACATION type, char *mac);
int hal_system_get_sta_mac(unsigned char *mac);
int hal_system_get_ap_mac(unsigned char *mac);
int hal_system_get_ble_mac(unsigned char *mac);

int hal_system_get_config(const char *key, void *buff, unsigned int buff_len);
int hal_system_set_config(const char *key, void *buff, unsigned int buff_len);
int hal_system_del_config(const char *key);

int hal_system_get_primary_if();
void hal_system_set_primary_if(int interface);
void hal_system_compute_secondary_mac(unsigned char *mac);

#endif /*_SYSTEM_CONFIG_H*/
