/**
 * @file main.c
 * @brief Eswin Main
 * @details This is the detail description
 * @author liuyong
 * @date -
 * @version V0.1
 * @Copyright © 2022 BEIJING ESWIN COMPUTING TECHINOLOGY CO., LTD and its affiliates (“ESWIN”).
 *          All rights reserved. Any modification, reproduction, adaption, translation,
 *          distribution is prohibited without consent.
 * @par History 1:
 *      Date:
 *      Version:
 *      Author:
 *      Modification:
 *
 * @par History 2:
 */



#include "oshal.h"
#include "cli.h"
#include "flash.h"
#include "sdk_version.h"

#define DFE_VAR
#include "tx_power_config.h"
#include "at_customer_wrapper.h"

#if defined(CONFIG_ECR_BLE)
#include "ble_thread.h"
#endif

#ifdef CONFIG_PSM_SURPORT
#include "psm_system.h"
#endif

#ifdef CONFIG_RTC
#include "rtc.h"
#endif

#ifdef CONFIG_WDT
#include "wdt.h"
#endif

#if defined(CONFIG_OTA_LOCAL)
// #include "local_ota.h"
#endif

#if defined(CONFIG_OTA_SERVICE)
// #include "mqtt_ota.h"
#endif

#ifdef CONFIG_APP_AT_COMMAND
// #include "at.h"
#endif

extern void vTaskStartScheduler(void);
extern void wifi_main();
extern void rf_platform_int();
extern void ke_init(void);
extern void amt_cal_info_obtain();
extern void wifi_conn_main();

#if defined(CONFIG_WIFI_AMT_VERSION)
extern int AmtGetVerStart();
extern void AmtInit();
extern void modem_init();
#endif

#if defined(CONFIG_ASIC_RF) && defined(CFG_WLAN_COEX)
extern void rf_pta_config();
#endif// CFG_WLAN_COEX
extern int version_get(void);

#include "led_driver.h"
#include "led.h"
#include "andlink_wifi_softap.h"
#include "user_wifi_status.h"
#include "system_event.h"
#include "ipotek_prov.h"
#include "andlink_wifi_connect.h"
#include "sntp_tr.h"

int main(void)
{
    component_cli_init(E_UART_SELECT_BY_KCONFIG);
    os_printf(LM_OS, LL_INFO, "SDK version %s, Release version %s\n",
              sdk_version, RELEASE_VERSION);

#if defined(CONFIG_ASIC_RF)
    rf_platform_int();
#endif //CONFIG_ASIC_RF

#if defined(CONFIG_NV)
    if (partion_init() != 0) {
        os_printf(LM_OS, LL_ERR, "partition error\n");
    }
    if (easyflash_init() != 0) {
        os_printf(LM_OS, LL_ERR, "easyflash error\n");
    }
#endif //CONFIG_NV

    if (drv_otp_encrypt_init() != 0)  {
        os_printf(LM_OS, LL_ERR, "encrypt error\n");
    }

#if defined(CONFIG_STANDALONE_UART) || defined(CFG_TRC_EN)
    extern void ble_hci_uart_init(E_DRV_UART_NUM uart_num);
    ble_hci_uart_init(E_UART_SELECT_BY_KCONFIG);
#endif //CONFIG_STANDALONE_UART

#if defined(CONFIG_ECR6600_WIFI) || defined(CONFIG_ECR_BLE)
    ke_init();
#endif

// #if defined(CONFIG_ECR_BLE)
//     ble_main();
//     BLE_APPS_INIT();
// #endif

#if defined(CFG_WLAN_COEX)
    rf_pta_config();
#endif //CONFIG_ASIC_RF && CFG_WLAN_COEX

    if (version_get() == 1) //6600A temperature compensation
    {
    // #ifdef CONFIG_ADC 
        drv_adc_init();
        get_volt_calibrate_data();
        time_check_temp();
        os_task_create("time_check_temp", SYSTEM_EVENT_LOOP_PRIORITY, 4096, time_check_temp, NULL);
    // #endif
    }

    amt_cal_info_obtain();

#if defined(CONFIG_WIFI_AMT_VERSION)
    if (AmtGetVerStart() == 1)
    {
        modem_init();
        AmtInit();
        vTaskStartScheduler();
        return(0);
    }
#endif

#ifdef CONFIG_ECR6600_WIFI
    wifi_main();
#endif

#ifdef CONFIG_PSM_SURPORT
    psm_wifi_ble_init();
    psm_boot_flag_dbg_op(true, 1);
#endif

// #if defined(CONFIG_RTC)
    extern volatile int rtc_task_handle;
    extern void calculate_rtc_task();
    rtc_task_handle = os_task_create("calculate_rtc", SYSTEM_EVENT_LOOP_PRIORITY, 4096, calculate_rtc_task, NULL);
    if (rtc_task_handle) {
        os_printf(LM_OS, LL_INFO, "rtc calculate start!\r\n");
    }
// #endif

#if 0
#ifdef CONFIG_WDT_FEED_TASK
    extern WDT_RET_CODE creat_wdt_feed_task_with_nv();
    creat_wdt_feed_task_with_nv();
#endif
#endif

#ifdef CONFIG_HEALTH_MONITOR
    extern int health_mon_create_by_nv();
    health_mon_create_by_nv();
#endif

    // wifi_conn_main();

// #if defined(CONFIG_OTA_LOCAL)
//     local_ota_main();
// #endif

// #if defined(CONFIG_OTA_SERVICE)
//     service_ota_main();
// #endif

// #ifdef CONFIG_APP_AT_COMMAND
    // AT_command_init();
// #endif
    set_timezone(8);
    extern WDT_RET_CODE creat_wdt_feed_task();
    creat_wdt_feed_task();

    ipotek_prov_data_init();

    sys_event_loop_init(wifi_status_event, NULL);
    //user_led_test();

    andlink_softap_start();

    //os_psram_mem_init();
    vTaskStartScheduler();
    return(0);
}



