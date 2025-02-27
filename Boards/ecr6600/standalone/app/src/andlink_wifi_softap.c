/**
* @file andlink_wifi_softap.c
* @author 小王同学
* @brief andlink_wifi_softap module is used to 
* @version 0.1
* @date 2023-04-26
*
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "andlink_wifi_softap.h"
#include "system_wifi.h"
#include "system_wifi_def.h"
#include "system_network.h"
#include "lwip/ip4_addr.h"
#include "andlink_wifi_coap.h"
#include "andlink_wifi_connect.h"

/*********************************************************** \
*************************micro define*********************** \
***********************************************************/
#define ANDLINK_DEVICE_AP_SSID              "CMQLINK"
// #define ANDLINK_DEVICE_TYPE                 "590332"    
#define ANDLINK_DEVICE_TYPE                 "590488"    

/***********************************************************
***********************typedef define***********************
***********************************************************/
 
 
/***********************************************************
***********************variable define**********************
***********************************************************/
 
 
/***********************************************************
***********************function define**********************
***********************************************************/
void andlink_softap_start_task(void *arg)
{
    sys_err_t op_ret = SYS_ERR;
    uint8_t wifi_mac[6] = {0};
    wifi_config_u wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_u));

    // sys_err_t op_ret = SYS_ERR;
    // op_ret = andlink_connect_wifi();
    // if (SYS_ERR != op_ret) {
    //     vTaskDelay((TickType_t)NULL);
    //     return;
    // }
    op_ret = wifi_get_mac_addr(SOFTAP_IF, wifi_mac); // 获取mac地址
    if (SYS_OK != op_ret) {
        os_printf(LM_WIFI, LL_ERR, "get wifi mac error!\n");
        return;
    }
    os_printf(LM_WIFI, LL_INFO, "wifi mac:%02x:%02x:%02x:%02x:%02x:%02x\n", wifi_mac[0], wifi_mac[1], wifi_mac[2], wifi_mac[3], wifi_mac[4], wifi_mac[5]);

    wifi_set_opmode(WIFI_MODE_AP);  // 设置为ap模式
    while(!wifi_is_ready()) {       // 判断wifi内核是否初始化完成
        os_printf(LM_WIFI, LL_INFO, " err !set AP / STA information must after wifi initialized !\n ");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    sprintf((char *)wifi_config.ap.ssid, "%s-%s-%02x%02x", ANDLINK_DEVICE_AP_SSID, ANDLINK_DEVICE_TYPE, wifi_mac[4], wifi_mac[5]);
    os_printf(LM_WIFI, LL_INFO, "ssid:%s\n", wifi_config.ap.ssid);

    wifi_config.ap.authmode = AUTH_OPEN;
    wifi_config.ap.channel = 9;
    wifi_config.ap.hidden_ssid = 0;
    wifi_config.ap.max_connect = 3;
    sprintf((char *)wifi_config.ap.password, "12345678");
    op_ret = wifi_start_softap(&wifi_config);
    if (SYS_OK != op_ret) {
        os_printf(LM_WIFI, LL_ERR, "wifi_start_softap error!\n");
        // return op_ret;
    }

    // 配置ap热点的ip地址，网关地址以及网络掩码
    ip_info_t ip_info;
    memset((void *)&ip_info, 0, sizeof(ip_info_t));
    IP4_ADDR(&ip_info.ip, 192, 168, 188, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 188, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    set_softap_ipconfig(&ip_info);

    // dhcp(动态主机配置协议)
    struct dhcps_lease dhcp_cfg_info;
    dhcp_cfg_info.enable = true;
    IP4_ADDR(&dhcp_cfg_info.start_ip, 192, 168, 188, 1);
    IP4_ADDR(&dhcp_cfg_info.end_ip, 192, 168, 188, 149);
    // set_softap_dhcps_cfg((void *)&dhcp_cfg_info);
    wifi_softap_set_dhcps_lease(&dhcp_cfg_info);
    // wifi_softap_dhcps_start(SOFTAP_IF);
    // return op_ret;
    andlink_coap_start();
    vTaskDelay((TickType_t)NULL);
}

void andlink_softap_start(void)
{
    xTaskCreate(&andlink_softap_start_task, "andlink_softap_start_task", 1024, NULL, 15, NULL); // 创建扫描任务
}