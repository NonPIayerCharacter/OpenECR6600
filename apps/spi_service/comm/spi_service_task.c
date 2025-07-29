#include "spi_service_task.h"
#include "vnet_service.h"
#include "vnet_register.h"
#include "vnet_int.h"
#include "vnet_filter.h"
#include "system_config.h"
#include "lwip/tcpip.h"
#include "oshal.h"
#include "ota.h"

typedef struct {
    int taskHandle;
    os_queue_handle_t queue;
} spi_service_task_t;

spi_service_task_t g_spitask_priv;

spi_service_mem_t *spi_service_send_interrput(void)
{
#ifdef CONFIG_SPI_SLAVE
    unsigned int sintAddr = vnet_interrupt_get_addr();
    int sintNum = vnet_interrupt_get_num();

    if (sintAddr != 0) {
        spi_service_mem_t *smem = spi_mqueue_get();
        SPI_SERVICE_CHECK_RETURN(smem == NULL, NULL, "cannot get mqueue");
        smem->memAddr = sintAddr;
        smem->memOffset = 0;
        smem->memType = SPI_SERVICE_TYPE_INT;
        smem->memLen = sizeof(unsigned int);
        smem->memSlen = SPI_SERVICE_DATA_INT | sintNum;
        vnet_interrupt_clear(sintNum);
        return smem;
    }
#endif
    return NULL;
}

spi_service_mem_t *spi_service_get_info(unsigned int addr, unsigned int len)
{
#ifdef CONFIG_SPI_SLAVE
    if (addr < SPI_SERVICE_TYPE_HTOS) {
        unsigned int infoaddr;
        spi_service_mem_t *smem = NULL;
        SPI_SERVICE_CHECK_RETURN(addr+len > SPI_SERVICE_TYPE_HTOS, NULL, "read info overflow");
        smem = spi_mqueue_get();
        SPI_SERVICE_CHECK_RETURN(smem == NULL, NULL, "cannot get mqueue");
        infoaddr = (unsigned int)vnet_reg_get_addr();
        smem->memLen = len;
        smem->memSlen = len;
        smem->memAddr = infoaddr+addr;
        return smem;
    }
#endif
    return NULL;
}

int spi_service_send_wifi(spi_service_mem_t *smem)
{
#ifdef CONFIG_SPI_SLAVE
    vnet_service_wifi_tx(smem);
#endif

    return -1;
}

int spi_service_send_task(spi_service_msg_t *msg)
{
    if (g_spitask_priv.queue) {
        return os_queue_send(g_spitask_priv.queue, (char *)msg, sizeof(spi_service_msg_t), 0);
    } else {
        return -1;
    }
}

#if defined(CONFIG_SPI_SERVICE) && defined(CONFIG_SPI_SLAVE)
void spi_service_task(void *arg)
{
    spi_service_msg_t msg = {0};
    unsigned char *pm = NULL;
    int idx, ret;

    do {
        ret = os_queue_receive(g_spitask_priv.queue, (char *)&msg, sizeof(spi_service_msg_t), WAIT_FOREVER);
        if (ret < 0) {
            os_printf(LM_OS, LL_ERR, "spi service queue recv error.\n");
            break;
        }

        switch (msg.msgType) {
            case SPI_SERVICE_MSG_BASIC:
                pm = (unsigned char *)msg.msgAddr;
                for (idx = 0; idx < msg.msgLen; idx++) {
                    os_printf(LM_OS, LL_ERR, "0x%02x ", pm[idx]);
                    if ((idx + 1) % 16 == 0) {
                        os_printf(LM_OS, LL_ERR, "\n");
                    }
                }
                os_printf(LM_OS, LL_ERR, "\n");
                break;
            case SPI_SERVICE_MSG_OTA:
#ifdef CONFIG_OTA
                ota_update_image((unsigned char *)msg.msgAddr, msg.msgLen);
#endif
                break;
            case SPI_SERVICE_MSG_NET:
                os_printf(LM_OS, LL_INFO, "info trans done, you can do something here\n");
                break;
        }

        if (msg.msgfree != NULL) {
            msg.msgfree((void *)msg.msgAddr);
        }
    } while (1);
}
#endif

void spi_service_task_wifi_event(void *ctx, system_event_t *event)
{
#ifdef CONFIG_SPI_SLAVE
    wifi_work_mode_e wifi_mode = WIFI_MODE_STA;
#ifdef SPI_SERVICE_DHCP_HOST
    netif_db_t *netdb = NULL;
#endif
    vnet_reg_t *vreg = NULL;

    switch (event->event_id) {
        case SYSTEM_EVENT_WIFI_READY:
            /* init wifi mode at system start time */
            wifi_mode = WIFI_MODE_STA;
            wifi_set_opmode(WIFI_MODE_STA);
            hal_system_set_config(CUSTOMER_NV_WIFI_OP_MODE, &wifi_mode, sizeof(wifi_mode));
            vreg = vnet_reg_get_addr();
            memset(vreg, 0, sizeof(vnet_reg_t));
            /* spi slave get the netinfo at wifi ready time */
            /* spi host will get the netinfo at start time */
            vnet_reg_get_info();
#ifdef SPI_SERVICE_DHCP_HOST
            /* setting ip zero and static ip, DHCPC cannot start at wpa connect */
            netdb = get_netdb_by_index(STATION_IF);
            memset(&netdb->ipconfig, 0, sizeof(netdb->ipconfig));
            netdb->dhcp_stat = TCPIP_DHCP_STATIC;
#endif
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
#ifndef SPI_SERVICE_DHCP_HOST
            wifi_mode = WIFI_MODE_STA;
            vnet_reg_wifilink_status(STATION_IF, VNET_WIFI_LINK_UP);
            wifi_set_opmode(WIFI_MODE_STA);
            hal_system_set_config(CUSTOMER_NV_WIFI_OP_MODE, &wifi_mode, sizeof(wifi_mode));
#endif
            break;
        case SYSTEM_EVENT_STA_LOST_IP:
#ifndef SPI_SERVICE_DHCP_HOST
            vnet_reg_wifilink_status(STATION_IF, VNET_WIFI_LINK_DOWN);
#endif
            break;
        case SYSTEM_EVENT_AP_START:
            wifi_mode = WIFI_MODE_AP;
            wifi_set_opmode(WIFI_MODE_AP);
            hal_system_set_config(CUSTOMER_NV_WIFI_OP_MODE, &wifi_mode, sizeof(wifi_mode));
            vnet_reg_wifilink_status(SOFTAP_IF, VNET_WIFI_LINK_UP);
            break;
        case SYSTEM_EVENT_AP_STOP:
            vnet_reg_wifilink_status(SOFTAP_IF, VNET_WIFI_LINK_DOWN);
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
#ifdef SPI_SERVICE_DHCP_HOST
            vnet_reg_wifilink_status(STATION_IF, VNET_WIFI_LINK_UP);
#endif
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
#ifdef SPI_SERVICE_DHCP_HOST
            vnet_reg_wifilink_status(STATION_IF, VNET_WIFI_LINK_DOWN);
#endif
            break;
        default:
            break;
    }
#endif
}

int spi_service_task_init(spi_service_func_t funcset)
{
#ifdef CONFIG_SPI_SLAVE
    spi_slave_init(0);
    spi_slave_funcset_register(funcset);
    g_spitask_priv.queue = os_queue_create("spi_mqueue", SPI_SERVICE_TASK_QUEUE_DEPTH, sizeof(spi_service_msg_t), 0);
    SPI_SERVICE_CHECK_RETURN(g_spitask_priv.queue == NULL, -1, "spitask queue create failed");
    g_spitask_priv.taskHandle = os_task_create("spi_mtask", SPI_SERVICE_TASK_PRI, SPI_SERVICE_TASK_STACK, spi_service_task, NULL);
    SPI_SERVICE_CHECK_RETURN(g_spitask_priv.taskHandle == 0, -1, "spitask create failed");

    typedef int (*rxl_vnet_packet_parse)(uint16_t type, unsigned char *data);
    void rxl_vnet_packet_call(rxl_vnet_packet_parse cb);
    tcpip_pbuf_vnet_handle(vnet_service_wifi_rx);
    tcpip_pbuf_vnet_free(vnet_service_wifi_rx_free);
    rxl_vnet_packet_call(vnet_filter_wifi_handle);
    vnet_set_default_filter(VNET_PACKET_DICTION_HOST);
#endif

    return 0;
}

