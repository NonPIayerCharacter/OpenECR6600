/**
* @file andlink_wifi_connect.c
* @author 小王同学
* @brief andlink_wifi_connect module is used to 
* @version 0.1
* @date 2023-04-28
*
*/
#include <string.h>
#include "system_wifi.h"
#include "andlink_wifi_connect.h"
#include "ipotek_prov.h"
/***********************************************************
*************************micro define***********************
***********************************************************/
 
 
/***********************************************************
***********************typedef define***********************
***********************************************************/
 
 
/***********************************************************
***********************variable define**********************
***********************************************************/
 
 
/***********************************************************
***********************function define**********************
***********************************************************/
sys_err_t andlink_connect_wifi(void)
{
    char ssid[32], password[32];
    sys_err_t op_ret = SYS_ERR;

    op_ret = prov_get_ssid(ssid);
    if (SYS_OK != op_ret) {
        os_printf(LM_WIFI, LL_ERR, "prov_get_ssid error :%s\r\n", op_ret);
        return op_ret;
    }

    op_ret = prov_get_password(password);
    if (SYS_OK != op_ret) {
        os_printf(LM_WIFI, LL_ERR, "prov_get_password error :%s\r\n", op_ret);
        return op_ret;
    }

    os_printf(LM_WIFI, LL_ERR, "prov_get_ssid :%s\r\n", ssid);
    os_printf(LM_WIFI, LL_ERR, "prov_get_password :%s\r\n", password);
    // step 1 close AP mode
    wifi_stop_softap();
    wifi_set_opmode(WIFI_MODE_STA); // 切换至STA模式
    while (!wifi_is_ready()) { // 判断wifi内核是否初始化完成
        os_printf(LM_WIFI, LL_INFO, " err !set AP / STA information must after wifi initialized !\n ");
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // config sta_mode info
    wifi_config_u sta_config;
    memset(&sta_config, 0, sizeof(sta_config));

    memcpy(sta_config.sta.ssid, ssid, strlen(ssid));
    memcpy(sta_config.sta.password, password, strlen(password));
    sta_config.sta.channel = 1;
    // prov_get_wifi_channel(&sta_config.sta.channel);

    // step 2 connect to the router
    wifi_stop_station();
    op_ret = wifi_start_station(&sta_config);
    if (SYS_OK != op_ret) {
        os_printf(LM_WIFI, LL_INFO, "wifi_start_station err:%d\n", op_ret);
        return op_ret;
    }

    // ip_info_t ip_info;
    // IP4_ADDR(&ip_info.ip, 192, 168, 188, 1);
    // IP4_ADDR(&ip_info.gw, 192, 168, 188, 1);
    // IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    // set_sta_ipconfig(&ip_info);

    os_printf(LM_WIFI, LL_INFO, "wifi_start_station success\n");
    return op_ret;
}