/**
 ****************************************************************************************
 *
 * @file example_blufi.c
 *
 * @brief blufi example
 *
 * @par Copyright (C):
 *      ESWIN 2015-2020
 *
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if 0
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#endif
#include "esp_blufi_api.h"

#include "esp_blufi.h"

#include <string.h>
#include "example_blufi.h"
#include "bluetooth.h"
#include "system_def.h"
#include "system_event.h"
#include "system_wifi.h"
#include "system_network.h"
#include "system_config.h"
#include "ble_thread.h"
#include "oshal.h"
#include "os_task_config.h"
#include "timers.h"

#ifdef CONFIG_APP_AT_COMMAND
#include "at.h"
#endif

/*
 * DEFINES
 ****************************************************************************************
 */
#define BLE_DEVICE_NAME_MAX_LEN (27)
static void example_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);
	
static wifi_config_u sta_cfg = {0};
static wifi_config_u ap_cfg = {0};

/* store the station info for send back to phone */
static bool gl_sta_connected = false;
static bool ble_is_connected = false;

static int ble_apps_task_handle = 0;
static wifi_info_t wifi_info = {0};
static bool blufi_en = false;
static wifi_auth_mode_e wifi_authmode = AUTH_OPEN;
static os_sem_handle_t blufi_sem = NULL;
static bool g_is_scan_start = false;
static TimerHandle_t softap_start = NULL;

extern int8_t fhost_cntrl_map_check(uint8_t vif_type);

sys_err_t wifi_event_handle(system_event_t *event)
{
	switch(event->event_id)
	{
		case SYSTEM_EVENT_STA_GOT_IP:
			{
				esp_blufi_extra_info_t info;
		        memset(&info, 0, sizeof(esp_blufi_extra_info_t));
				
				memset(&wifi_info, 0, sizeof(wifi_info_t));
				wifi_get_wifi_info(&wifi_info);

				wifi_work_mode_e mode = wifi_get_opmode();
				
		        memcpy(info.sta_bssid, wifi_info.bssid, 6);
		        info.sta_bssid_set = true;
		        info.sta_ssid = wifi_info.ssid;
		        info.sta_ssid_len = wifi_info.ssid_len;
		        if (ble_is_connected == true) {
		            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
		            wifi_work_mode_e mode = wifi_get_opmode();
                    if (mode == WIFI_MODE_AP_STA && softap_start) {
                        os_timer_start(softap_start);
                    }
		        } else {
		            os_printf(LM_APP, LL_INFO, "BLUFI BLE is not connected yet\n");
		        }
			}
			break;
		case SYSTEM_EVENT_SCAN_DONE:
			{
				wifi_info_t ap_info;
				int apCount = wpa_get_scan_num();
				os_printf(LM_APP, LL_INFO, "WIIF scan done++++ %d\n",apCount);
				if (apCount == 0 || g_is_scan_start == false) {
		            os_printf(LM_APP, LL_INFO, "AP num = %d;or report is:%d\n",apCount, g_is_scan_start);
		            break;
		        }
				g_is_scan_start = false;
				esp_blufi_ap_record_t * blufi_ap_list = (esp_blufi_ap_record_t *)malloc(apCount * sizeof(esp_blufi_ap_record_t));
		        if (!blufi_ap_list) {
		            os_printf(LM_APP, LL_ERR, "malloc error, blufi_ap_list is NULL\n");
		            break;
		        }
		        for (int i = 0; i < apCount; ++i)
		        {
		        	memset((void *)&ap_info, 0, sizeof(wifi_info_t));
        			wifi_get_scan_result(i, &ap_info);
		            blufi_ap_list[i].rssi = ap_info.rssi;
		            memcpy(blufi_ap_list[i].ssid, ap_info.ssid, sizeof(ap_info.ssid));
		        }

		        if (ble_is_connected == true) {
		            esp_blufi_send_wifi_list(apCount, blufi_ap_list);
		        } else {
		            os_printf(LM_APP, LL_INFO, "BLUFI BLE is not connected yet\n");
		        }
				
				free(blufi_ap_list);
			}
			break;
        case SYSTEM_EVENT_AP_START:
        {
            wifi_work_mode_e mode = wifi_get_opmode();

            /* TODO: get config or information of softap, then set to report extra_info */
            if (ble_is_connected == true) {
                if (gl_sta_connected) {
                    esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, NULL);
                } else {
                    esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
                }
            } else {
                os_printf(LM_APP, LL_INFO, "BLUFI BLE is not connected yet\n");
            }
            break;
        }
		default:
			break;
	}
	return 0;
}

static void initialise_wifi(void)
{
	wifi_event_cb_register(wifi_event_handle);
}


static esp_blufi_callbacks_t example_callbacks = {
    .event_cb = example_event_callback,
    .negotiate_data_handler = blufi_dh_negotiate_data_handler,
    .encrypt_func = blufi_aes_encrypt,
    .decrypt_func = blufi_aes_decrypt,
    .checksum_func = blufi_crc_checksum,
};

static void example_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
    /* actually, should post to blufi_task handle the procedure,
     * now, as a example, we do it more simply */
    switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH:
        os_printf(LM_APP, LL_INFO, "BLUFI init finish\n");

        esp_blufi_adv_start();
        break;
    case ESP_BLUFI_EVENT_DEINIT_FINISH:
        os_printf(LM_APP, LL_INFO, "BLUFI deinit finish\n");
		if(blufi_sem) os_sem_post(blufi_sem);
        break;
    case ESP_BLUFI_EVENT_BLE_CONNECT:
        os_printf(LM_APP, LL_INFO, "BLUFI ble connect\n");
        #ifdef CONFIG_APP_AT_COMMAND
        at_printf("+BLUFICONN\n");
        #endif
        ble_is_connected = true;
        esp_blufi_adv_stop();
        blufi_security_init();
        break;
    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        os_printf(LM_APP, LL_INFO, "BLUFI ble disconnect\n");
        #ifdef CONFIG_APP_AT_COMMAND
        at_printf("+BLUFIDISCONN\n");
        #endif
        ble_is_connected = false;
        blufi_security_deinit();
        esp_blufi_adv_start();
		if(blufi_sem) os_sem_post(blufi_sem);
        break;
    case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
        os_printf(LM_APP, LL_INFO, "BLUFI Set WIFI opmode %d\n", param->wifi_mode.op_mode);
        if (param->wifi_mode.op_mode < WIFI_MODE_MAX) {
            wifi_set_opmode(param->wifi_mode.op_mode);
        }
		memset(&ap_cfg, 0x0, sizeof(ap_cfg));
        //ESP_ERROR_CHECK( esp_wifi_set_mode(param->wifi_mode.op_mode) );
        break;
    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
    	{
            //int timeout_cnt = 200;
	        os_printf(LM_APP, LL_INFO, "BLUFI requset wifi connect to AP\n");
	        /* there is no wifi callback when the device has already connected to this wifi
	        so disconnect wifi before connection.
	        */
	        //esp_wifi_disconnect();
	        //esp_wifi_connect();
	        wifi_work_mode_e mode = wifi_get_opmode();
	        if(mode == WIFI_MODE_STA || mode == WIFI_MODE_AP_STA)
	        {
    	        wifi_status_e sta_status = wifi_get_sta_status();
                if (sta_status == STA_STATUS_CONNECTED) {
                    wifi_stop_station();

                    while (1) {
                        sta_status = wifi_get_sta_status();
                        if (fhost_cntrl_map_check(0) != 1) {
                            os_msleep(1);
                            continue;
                        }
                        if (sta_status == STA_STATUS_STOP || sta_status == STA_STATUS_DISCON) {
                            break;
                        }
                        os_msleep(1);
                    }
                }
                wifi_start_station(&sta_cfg);
			}
    	}
        break;
    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        {
	        os_printf(LM_APP, LL_INFO, "BLUFI requset wifi disconnect from AP\n");
	        //esp_wifi_disconnect();
	        wifi_work_mode_e mode = wifi_get_opmode();
			if(mode == WIFI_MODE_STA || mode == WIFI_MODE_AP_STA) {
				wifi_stop_station();
			}
    	}
        break;
    case ESP_BLUFI_EVENT_REPORT_ERROR:
        os_printf(LM_APP, LL_ERR, "BLUFI report error, error code %d\n", param->report_error.state);
        esp_blufi_send_error_info(param->report_error.state);
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_STATUS: {
        wifi_work_mode_e mode = wifi_get_opmode();
        esp_blufi_extra_info_t info;

		struct ip_info if_ip;
		wifi_get_ip_info(STATION_IF,&if_ip);
		#ifdef CONFIG_IPV6
		if(STA_STATUS_CONNECTED==wifi_get_status(STATION_IF)&& !ip4_addr_isany_val(if_ip.ip.u_addr.ip4))
		#else
		if(STA_STATUS_CONNECTED==wifi_get_status(STATION_IF)&& !ip4_addr_isany(&(if_ip.ip)))
		#endif
		{
			os_printf(LM_APP, LL_INFO, "wifi connected\n");
			gl_sta_connected = true;
		}else
		{
			os_printf(LM_APP, LL_INFO, "wifi disconnected\n");
			gl_sta_connected = false;
		}

        if (gl_sta_connected) {

			memset(&wifi_info, 0, sizeof(wifi_info_t));
			wifi_get_wifi_info(&wifi_info);

            memset(&info, 0, sizeof(esp_blufi_extra_info_t));
            memcpy(info.sta_bssid, wifi_info.bssid, 6);
            info.sta_bssid_set = true;
            info.sta_ssid = wifi_info.ssid;
            info.sta_ssid_len = wifi_info.ssid_len;
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
        } else {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
        }
        os_printf(LM_APP, LL_INFO, "BLUFI get wifi status from AP\n");
        break;
    }
    case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
        os_printf(LM_APP, LL_INFO, "blufi close a gatt connection");
        esp_blufi_disconnect();
        break;
    case ESP_BLUFI_EVENT_DEAUTHENTICATE_STA:
        /* TODO */
        break;
	case ESP_BLUFI_EVENT_RECV_STA_BSSID:
        //sta_config.sta.bssid_set = 1;
		memcpy(sta_cfg.sta.bssid, param->sta_bssid.bssid, 6);
        os_printf(LM_APP, LL_INFO, "Recv STA BSSID %s\n", sta_cfg.sta.bssid);
        break;
	case ESP_BLUFI_EVENT_RECV_STA_SSID:
        strncpy((char *)sta_cfg.sta.ssid, (char *)param->sta_ssid.ssid, param->sta_ssid.ssid_len);
        sta_cfg.sta.ssid[param->sta_ssid.ssid_len] = '\0';
        //esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        os_printf(LM_APP, LL_INFO, "Recv STA SSID %s\n", sta_cfg.sta.ssid);
        break;
	case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        strncpy((char *)sta_cfg.sta.password, (char *)param->sta_passwd.passwd, param->sta_passwd.passwd_len);
        sta_cfg.sta.password[param->sta_passwd.passwd_len] = '\0';
        //esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        os_printf(LM_APP, LL_INFO, "Recv STA PASSWORD %s\n", sta_cfg.sta.password);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_SSID:
        strncpy((char *)ap_cfg.ap.ssid, (char *)param->softap_ssid.ssid, param->softap_ssid.ssid_len);
        ap_cfg.ap.ssid[param->softap_ssid.ssid_len] = '\0';
        //ap_cfg.ap.ssid_len = param->softap_ssid.ssid_len;
        //esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        os_printf(LM_APP, LL_INFO, "Recv SOFTAP SSID %s, ssid len %d\n", ap_cfg.ap.ssid, param->softap_ssid.ssid_len);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_PASSWD:
        strncpy((char *)ap_cfg.ap.password, (char *)param->softap_passwd.passwd, param->softap_passwd.passwd_len);
        ap_cfg.ap.password[param->softap_passwd.passwd_len] = '\0';
        //esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        os_printf(LM_APP, LL_INFO, "Recv SOFTAP PASSWORD %s len = %d\n", ap_cfg.ap.password, param->softap_passwd.passwd_len);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_MAX_CONN_NUM:
        if (param->softap_max_conn_num.max_conn_num > 4) {
            return;
        }
        ap_cfg.ap.max_connect = param->softap_max_conn_num.max_conn_num;
        //esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        os_printf(LM_APP, LL_INFO, "Recv SOFTAP MAX CONN NUM %d\n", ap_cfg.ap.max_connect);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_AUTH_MODE:
        if (param->softap_auth_mode.auth_mode >= AUTH_MAX) {
            return;
        }
        #ifdef CONFIG_APP_AT_COMMAND
        if (param->softap_auth_mode.auth_mode < wifi_authmode) {
            return;
        }
        #endif
        ap_cfg.ap.authmode = param->softap_auth_mode.auth_mode;
        //esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        os_printf(LM_APP, LL_INFO, "Recv SOFTAP AUTH MODE %d\n", ap_cfg.ap.authmode);
        wifi_work_mode_e mode = wifi_get_opmode();
        if (mode == WIFI_MODE_AP && softap_start) {
            os_timer_start(softap_start);
        }
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_CHANNEL:
        if (param->softap_channel.channel > 13) {
            return;
        }
        ap_cfg.ap.channel = param->softap_channel.channel;
        //esp_wifi_set_config(WIFI_IF_AP, &ap_config);
		//wifi_set_opmode(WIFI_MODE_AP);
        os_printf(LM_APP, LL_INFO, "Recv SOFTAP CHANNEL %d\n", ap_cfg.ap.channel);
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_LIST:{
		#if 0
        wifi_scan_config_t scanConf = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = false
        };
        esp_wifi_scan_start(&scanConf, true);
		#endif
			wifi_work_mode_e mode = wifi_get_opmode();

		    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_AP_STA)
		    {
		        os_printf(LM_APP, LL_INFO, "not in sta or sta+ap mode\n");
				return;
		    }
			wifi_scan_config_t scan_cfg;
			memset(&scan_cfg, 0, sizeof(wifi_scan_config_t));
			scan_cfg.channel = 0;
			wifi_scan_start(false, &scan_cfg);
            g_is_scan_start = true;
    	}
		break;
    case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA:
        os_printf(LM_APP, LL_INFO, "Recv Custom Data %d\n", param->custom_data.data_len);
		#if defined(CONFIG_APP_AT_COMMAND)
		extern void at_ble_blufi_print_data(uint8_t * data, uint32_t data_len);
		at_ble_blufi_print_data(param->custom_data.data, param->custom_data.data_len);
		#endif
        //esp_log_buffer_hex("Custom Data", param->custom_data.data, param->custom_data.data_len);
        break;
	case ESP_BLUFI_EVENT_RECV_USERNAME:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CA_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CLIENT_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_SERVER_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CLIENT_PRIV_KEY:
        /* Not handle currently */
        break;;
	case ESP_BLUFI_EVENT_RECV_SERVER_PRIV_KEY:
        /* Not handle currently */
        break;
    default:
        break;
    }
}

static void ble_blufi_start_softap(os_timer_handle_t xTimer)
{
    wifi_status_e ap_status = wifi_get_ap_status();
    if (ap_status == AP_STATUS_STARTED) {
        wifi_stop_softap();
        while (1) {
            ap_status = wifi_get_ap_status();

            if (fhost_cntrl_map_check(2) != 1) {
                os_msleep(1);
                continue;
            }
            if (ap_status == AP_STATUS_STOP) {
                break;
            }
            os_msleep(1);
        }
    }
    wifi_start_softap(&ap_cfg);
}

static void blufi_example_init(void *arg)
{
	int rc = 0;
    initialise_wifi();

    rc = esp_blufi_register_callbacks(&example_callbacks);
    if(rc){
        os_printf(LM_APP, LL_ERR, "blufi_example_init blufi register failed, error code = %x\n",rc);
        return;
    }

    ecr_blufi_gatt_svr_init();
    ecr_blufi_btc_init();
	esp_blufi_profile_init();

    softap_start = os_timer_create("start softap", 500, 0, ble_blufi_start_softap, NULL);
    if (softap_start == NULL) {
        os_printf(LM_BLE, LL_ERR, "Error!!!, start softap alloc timeout failed\n");
    }

	blufi_en = true;
	
	os_task_delete(ble_apps_task_handle);
	ble_apps_task_handle = 0;
}

void ble_apps_init(void)
{
	if(!blufi_en)
	{
		ble_apps_task_handle = os_task_create( "ble_apps_task", BLE_APPS_PRIORITY, 4096, (task_entry_t)blufi_example_init, NULL);
	}
}

void ble_apps_deinit(void)
{
	blufi_sem = os_sem_create(1, 0);

	if(ble_is_connected)
	{
		esp_blufi_disconnect();
		if(0 == os_sem_get(blufi_sem)) os_sem_wait(blufi_sem, WAIT_FOREVER);
		ble_is_connected = false;
	}
	
	if(wifi_get_status(STATION_IF) != STA_STATUS_CONNECTED)
	{
		wifi_stop_station();
	}

	if(wifi_get_status(SOFTAP_IF) != AP_STATUS_STARTED)
	{
		wifi_stop_softap();
	}

    if (softap_start != NULL) {
        os_timer_delete(softap_start);
        softap_start = NULL;
    }

	wifi_event_cb_register(NULL);
	ecr_ble_reset();

    if (ecr_blufi_get_queue() != NULL) {
        esp_blufi_profile_deinit();
        if(0 == os_sem_get(blufi_sem)) os_sem_wait(blufi_sem, WAIT_FOREVER);
    }

	ecr_blufi_btc_deinit();
	
	os_sem_destroy(blufi_sem);
	blufi_sem = NULL;
	blufi_en = false;
	wifi_authmode = AUTH_OPEN;
}

bool blufi_is_enable(void)
{
	return blufi_en;
}

bool blufi_is_connect(void)
{
    return ble_is_connected;
}

void blufi_set_wifi_authmode(uint8_t authmode)
{
	assert((authmode >= AUTH_OPEN)&&(authmode < AUTH_MAX));
	wifi_authmode = authmode;
}

uint16_t blufi_get_char_handler(void)
{
    return esp_blufi_get_notify_hanlder();
}

uint16_t blufi_get_conn_handler(void)
{
    return esp_blufi_get_conn_hanlder();
}

