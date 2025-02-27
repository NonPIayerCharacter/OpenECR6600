/**
 * @file user_wifi_status.c
 * @author 小王同学
 * @brief user_wifi_status module is used to
 * @version 0.1
 * @date 2023-04-26
 *
 */
#include "system_event.h"
#include "andlink_wifi_https.h"
#include "user_wifi_status.h"

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
sys_err_t wifi_status_event(void *ctx, system_event_t *event)
{
    sys_err_t op_ret = SYS_OK;
    os_printf(LM_WIFI, LL_ERR, "event->event_id:%d\n", event->event_id);
    switch (event->event_id) {
        case SYSTEM_EVENT_AP_START:
            break;

        case SYSTEM_EVENT_AP_STOP:
            break;

        case SYSTEM_EVENT_STA_START:
            break;

        case SYSTEM_EVENT_STA_GOT_IP:
            andlink_https_start();
            break;

        case SYSTEM_EVENT_AP_STACONNECTED:
            break;

        case SYSTEM_EVENT_AP_STADISCONNECTED:
            break;

        case SYSTEM_EVENT_STA_CONNECTED:
            break;

        case SYSTEM_EVENT_STA_DISCONNECTED:
            break;
        
        default:
            break;
    }
    return op_ret;
}
