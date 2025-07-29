#include "spi_repeater.h"
#include "spi_service_mem.h"
#include "system_wifi.h"
#include "system_wifi_def.h"
#include "system_lwip.h"
#include "system_config.h"
#include "lwip/inet.h"
#include "net_def.h"
#include "vnet_register.h"
#include "vnet_int.h"
#include "cli.h"

typedef struct {
    int taskHandle;
    os_queue_handle_t queue;
} spi_repeater_priv_t;

unsigned char g_repeater_apssid[WIFI_SSID_MAX_LEN] = "repeater";
unsigned char g_repeater_appwd[WIFI_PWD_MAX_LEN] = "12345678";
spi_repeater_priv_t g_repeater_priv;

extern uint8_t fhost_tx_check_is_shram(void *p);

int spi_repeater_mem_free(struct pbuf *p, struct netif *inp)
{
    return spi_service_mpbuf_free(p) ? 0 : 1;
}

#ifdef CONFIG_SPI_MASTER
static void spi_repeater_create_apnet(void)
{
    wifi_config_u wifiCfg = {0};
    wifi_work_mode_e wifi_mode = WIFI_MODE_AP;
    //struct ip_info ipconfig = {0};

    wifi_set_opmode(wifi_mode);
    wifiCfg.ap.channel = WIFI_CHANNEL_1;
    wifiCfg.ap.authmode = AUTH_WPA_WPA2_PSK;

    if (strlen((char *)g_repeater_apssid) == 0) {
        os_printf(LM_OS, LL_ERR, "ap ssid can not empty\n");
        return;
    }
    snprintf((char *)wifiCfg.ap.ssid, sizeof(wifiCfg.ap.ssid), "%s", g_repeater_apssid);

    if (strlen((char *)g_repeater_appwd) != 0) {
        snprintf((char *)wifiCfg.ap.password, sizeof(wifiCfg.ap.password), "%s", g_repeater_appwd);
    }
    wifi_stop_softap();
    wifi_start_softap(&wifiCfg);
#ifdef CONFIG_LWIP_NAPT
    enable_lwip_napt(SOFTAP_IF, 1);
#endif
}

static int spi_repeater_start_ap(cmd_tbl_t *t, int argc, char *argv[])
{
    spi_repeater_create_apnet();

    return CMD_RET_SUCCESS;
}

CLI_CMD(spi_repeater_ap, spi_repeater_start_ap, "spi_repeater_start_ap", "spi_repeater_start_ap");

static err_t spi_repeater_sta_send(struct netif *nif, struct pbuf *p_buf)
{
    spi_service_send_pbuf(nif, p_buf);

    return 0;
}

void spi_repeater_create_stanet(void)
{
    vnet_reg_t *vReg = vnet_reg_get_addr();
    net_if_t *net_if = get_netif_by_index(STATION_IF);
#ifndef SPI_SERVICE_DHCP_HOST
    struct ip_info ipconfig = {0};
#endif
    unsigned char mac[NETIF_MAX_HWADDR_LEN] = {0};
    unsigned char *tmpChar = NULL;
    int idx;

    if (vReg->status == 0) {
        wifi_set_mac_addr(STATION_IF, mac);
#ifndef SPI_SERVICE_DHCP_HOST
#ifdef CONFIG_IPV6
        ipconfig.ip.u_addr.ip4.addr = 0;
        ipconfig.gw.u_addr.ip4.addr = 0;
        ipconfig.netmask.u_addr.ip4.addr = 0;
#else
        ipconfig.ip.addr = 0;
        ipconfig.gw.addr = 0;
        ipconfig.netmask.addr = 0;
#endif
        set_sta_ipconfig(&ipconfig);
        wifi_set_ip_info(STATION_IF, &ipconfig);
#else
        wifi_station_dhcpc_stop(STATION_IF);
#endif
        net_if_down(net_if);
        return;
    }

    tmpChar = (unsigned char *)&vReg->stamac[0];
    for (idx = 0; idx < NETIF_MAX_HWADDR_LEN; idx++) {
        if (idx < NETIF_MAX_HWADDR_LEN/2) {
            mac[idx] = tmpChar[NETIF_MAX_HWADDR_LEN/2 - idx - 1];
        } else {
            mac[idx] = tmpChar[NETIF_MAX_HWADDR_LEN + NETIF_MAX_HWADDR_LEN/2 - idx];
        }
    }

    wifi_set_mac_addr(STATION_IF, mac);
#ifndef SPI_SERVICE_DHCP_HOST
#ifdef CONFIG_IPV6
    ipconfig.ip.u_addr.ip4.addr = lwip_htonl(vReg->ipAddr);
    ipconfig.gw.u_addr.ip4.addr = lwip_htonl(vReg->gw[0]);
    ipconfig.netmask.u_addr.ip4.addr = lwip_htonl(vReg->ipMask);
#else
    ipconfig.ip.addr = lwip_htonl(vReg->ipAddr);
    ipconfig.gw.addr = lwip_htonl(vReg->gw[0]);
    ipconfig.netmask.addr = lwip_htonl(vReg->ipMask);
#endif
    set_sta_ipconfig(&ipconfig);
    wifi_set_ip_info(STATION_IF, &ipconfig);
#endif
    net_if->linkoutput = spi_repeater_sta_send;
    net_if_up(net_if);
    netif_set_default(net_if);
#ifdef SPI_SERVICE_DHCP_HOST
    os_printf(LM_OS, LL_ERR, "%s[%d]\n", __FUNCTION__, __LINE__);
    wifi_station_dhcpc_start(STATION_IF); 
#endif
}

static int spi_repeater_sta_recv(unsigned int addr)
{
    net_if_t *net_if = get_netif_by_index(STATION_IF);
    struct pbuf *p_buf = (struct pbuf *)addr;

    if (net_if) {
        if (net_if->input(p_buf, net_if) != 0) {
            spi_repeater_mem_free(p_buf, net_if);
        }
    } else {
        spi_repeater_mem_free(p_buf, net_if);
    }

    return 0;
}
#endif

int spi_repeater_send_msg(spi_service_msg_t *msg)
{
    if (g_repeater_priv.queue) {
        return os_queue_send(g_repeater_priv.queue, (char *)msg, sizeof(spi_service_msg_t), 0);
    } else {
        return -1;
    }
}

/* spi slave send interrupt to host */
int spi_repeater_rx_interrupt(unsigned int type, unsigned int value)
{
#ifdef CONFIG_SPI_MASTER
    if (type == SPI_SERVICE_TYPE_INT) {
        spi_service_msg_t msg = {0};
        msg.msgType = SPI_SERVICE_MSG_INT;
        msg.msgLen = value;
        spi_repeater_send_msg(&msg);
        return 0;
    }
#endif

    return -1;
}

spi_service_mem_t *spi_repeater_get_netinfo(unsigned int addr, unsigned int len)
{
#ifdef CONFIG_SPI_MASTER
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

#ifdef CONFIG_SPI_MASTER
static int spi_repeater_handle_interrupt(int ivalue)
{
    if (ivalue == VNET_INTERRUPT_LINK_CS) {
        spi_master_read_info(0, sizeof(vnet_reg_t));
        return 0;
    }
    os_printf(LM_OS, LL_INFO, "handle interrupt num %d\n", ivalue);

    return 0;
}

static int spi_repeater_handle_net(unsigned int addr, unsigned int len)
{
    if (len == sizeof(vnet_reg_t)) {
        spi_repeater_create_stanet();
        return 0;
    }

    os_printf(LM_OS, LL_INFO, "info trans done, you can do something here\n");
    return 0;
}

void spi_repeater_task(void *arg)
{
    spi_service_msg_t msg = {0};
    unsigned char *pm = NULL;
    int ret, idx;

    do {
        spi_master_gpio_init();
        ret = os_queue_receive(g_repeater_priv.queue, (char *)&msg, sizeof(spi_service_msg_t), WAIT_FOREVER);
        if (ret < 0) {
            os_printf(LM_OS, LL_ERR, "spi repeater queue recv error.\n");
            break;
        }

        switch (msg.msgType) {
            case SPI_SERVICE_MSG_INT:
                spi_repeater_handle_interrupt(msg.msgLen);
                break;
            case SPI_SERVICE_MSG_NET:
                spi_repeater_handle_net(msg.msgAddr, msg.msgLen);
                break;
            case SPI_SERVICE_MSG_PACKET:
                spi_repeater_sta_recv(msg.msgAddr);
                break;
            case SPI_SERVICE_MSG_BASIC:
                pm = (unsigned char *)msg.msgAddr;
                for (idx = 0; idx < msg.msgLen; idx++) {
                    os_printf(LM_OS, LL_ERR, "0x%02x ", pm[idx]);
                    if ((idx + 1) % 16 == 0) {
                        os_printf(LM_OS, LL_ERR, "\n");
                    }
                }
                os_printf(LM_OS, LL_ERR, "\n");
                if (msg.msgfree != NULL) {
                    msg.msgfree((void *)msg.msgAddr);
                }
                break;
        }
    } while (1);
}

int spi_repeater_build_netinfo(void)
{
    spi_service_msg_t msg = {0};
    msg.msgType = SPI_SERVICE_MSG_INT;
    msg.msgLen = VNET_INTERRUPT_LINK_CS;
    spi_repeater_send_msg(&msg);

    return 0;
}
#endif

void spi_repeater_wifi_event(void *ctx, system_event_t *event)
{
#ifdef CONFIG_SPI_MASTER
    vnet_reg_t *vreg = NULL;

    switch (event->event_id) {
        case SYSTEM_EVENT_WIFI_READY:
            vreg = vnet_reg_get_addr();
            memset(vreg, 0, sizeof(vnet_reg_t));
            /* spi slave wifi linkup to notice host to build network */
            /* spi host reboot and slave work normal, host lost the linkup interrupt */
            /* spi host read slave network info at start time once */
            spi_repeater_build_netinfo();
            break;
        default:
            break;
    }
#endif
}

int spi_repeater_init(spi_service_func_t funcset)
{
#ifdef CONFIG_SPI_MASTER
    spi_master_init();
    spi_master_funcset_register(funcset);
    tcpip_pbuf_vnet_free(spi_repeater_mem_free);
    g_repeater_priv.queue = os_queue_create("spi repeater queue", SPI_REPEATER_TASK_QDEPTH, sizeof(spi_service_msg_t), 0);
    SPI_SERVICE_CHECK_RETURN(g_repeater_priv.queue == NULL, -1, "cannot create repeater queue");
    g_repeater_priv.taskHandle = os_task_create("spiRepeaterTask", SPI_REPEATER_TASK_PRI, SPI_REPEATER_TASK_STACK, spi_repeater_task, NULL);
    SPI_SERVICE_CHECK_RETURN(g_repeater_priv.taskHandle == 0, -1, "cannot create repeater task");
#endif

    return 0;
}
