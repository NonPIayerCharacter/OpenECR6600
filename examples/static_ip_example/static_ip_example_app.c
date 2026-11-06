#include <string.h>

#include "oshal.h"
#include "system_event.h"
#include "system_network.h"
#include "cmd_static_ip.h"

#define STATIC_IP_EXAMPLE_TAG "[STATIC_IP_EXAMPLE]"
#define STATIC_IP_APPLY_MAX_RETRIES 3
#define STATIC_IP_APPLY_RETRY_DELAY_MS 100
#define STATIC_IP_APPLY_INITIAL_DELAY_MS 50

static sys_err_t static_ip_example_event_handler(void *ctx, system_event_t *event)
{
    static_ip_status_t status;
    int retry;

    (void)ctx;

    if (event == NULL) {
        return SYS_OK;
    }

    switch (event->event_id) {
    case SYSTEM_EVENT_STA_CONNECTED:
        if ((static_ip_get_status(STATION_IF, &status) == 0) && status.configured) {
            os_printf(LM_APP, LL_INFO,
                      STATIC_IP_EXAMPLE_TAG " STA connected, checking static IP profile %s\r\n",
                      status.config.ip);

            if (wifi_station_dhcpc_status(STATION_IF) != TCPIP_DHCP_STATIC) {
                os_msleep(STATIC_IP_APPLY_INITIAL_DELAY_MS);
                for (retry = 0; retry < STATIC_IP_APPLY_MAX_RETRIES; retry++) {
                    if (static_ip_set(STATION_IF, &status.config) == 0) {
                        break;
                    }

                    if (retry + 1 < STATIC_IP_APPLY_MAX_RETRIES) {
                        os_printf(LM_APP, LL_WARN,
                                  STATIC_IP_EXAMPLE_TAG " static IP reapply retry %d/%d\r\n",
                                  retry + 1,
                                  STATIC_IP_APPLY_MAX_RETRIES);
                        os_msleep(STATIC_IP_APPLY_RETRY_DELAY_MS);
                    }
                }
            }
        } else {
            os_printf(LM_APP, LL_INFO,
                      STATIC_IP_EXAMPLE_TAG " STA connected, DHCP will run normally\r\n");
        }
        break;

    case SYSTEM_EVENT_STA_GOT_IP:
        os_printf(LM_APP, LL_INFO,
                  STATIC_IP_EXAMPLE_TAG " STA got IP, current configuration follows\r\n");
        static_ip_get(STATION_IF, NULL);
        break;

    case SYSTEM_EVENT_STA_DISCONNECTED:
        os_printf(LM_APP, LL_INFO,
                  STATIC_IP_EXAMPLE_TAG " STA disconnected, reason=%u\r\n",
                  event->event_info.disconnected.reason);
        break;

    case SYSTEM_EVENT_STA_LOST_IP:
        os_printf(LM_APP, LL_INFO,
                  STATIC_IP_EXAMPLE_TAG " STA lost IP\r\n");
        break;

    default:
        break;
    }

    return SYS_OK;
}

void app_example_main(void)
{
    static_ip_config_t defaults;

    static_ip_get_defaults(&defaults);

    os_printf(LM_APP, LL_INFO, STATIC_IP_EXAMPLE_TAG " =========================================\r\n");
    os_printf(LM_APP, LL_INFO, STATIC_IP_EXAMPLE_TAG " Static IP example started\r\n");
    os_printf(LM_APP, LL_INFO, STATIC_IP_EXAMPLE_TAG " Defaults: %s / %s / %s\r\n",
              defaults.ip, defaults.netmask, defaults.gateway);
    os_printf(LM_APP, LL_INFO, STATIC_IP_EXAMPLE_TAG " Workflow:\r\n");
    os_printf(LM_APP, LL_INFO, STATIC_IP_EXAMPLE_TAG "   static_ip set [sta|ap] ip <ip> mask <mask> gw <gw> [dns1 <d>] [dns2 <d>]\r\n");
    os_printf(LM_APP, LL_INFO, STATIC_IP_EXAMPLE_TAG "   wifi_sta <ssid> <password>\r\n");
    os_printf(LM_APP, LL_INFO, STATIC_IP_EXAMPLE_TAG "   static_ip get [sta|ap]\r\n");
    os_printf(LM_APP, LL_INFO, STATIC_IP_EXAMPLE_TAG "   static_ip dhcp [sta|ap]\r\n");
    os_printf(LM_APP, LL_INFO, STATIC_IP_EXAMPLE_TAG " =========================================\r\n");

    sys_event_loop_set_cb(static_ip_example_event_handler, NULL);
}
