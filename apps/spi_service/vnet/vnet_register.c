#include "vnet_register.h"
#include "vnet_int.h"
#include "system_wifi.h"
#include "system_config.h"
#include "spi_repeater.h"
#include "cli.h"

vnet_reg_t g_vnet_reg __SHAREDRAM;

vnet_reg_t *vnet_reg_get_addr(void)
{
    return &g_vnet_reg;
}

void vnet_reg_get_info(void)
{
    wifi_work_mode_e wifiMode = wifi_get_opmode();
    struct netif *staif = get_netif_by_index(wifiMode);
    unsigned char addr[6] = {0};
    unsigned int infoValue = 0;
    int index;

    if (staif == NULL) {
        os_printf(LM_APP, LL_ERR, "wifiMode(0x%x) not support\n", wifiMode);
        return;
    }

    if (wifiMode == WIFI_MODE_STA) {
        hal_system_get_sta_mac(addr);
    }

    if (wifiMode == WIFI_MODE_AP) {
        hal_system_get_ap_mac(addr);
    }

    memset(&g_vnet_reg, 0, sizeof(vnet_reg_t));
    g_vnet_reg.stamac[0] = (addr[0] << 16) | (addr[1] << 8) | addr[2];
    g_vnet_reg.stamac[1] = (addr[3] << 16) | (addr[4] << 8) | addr[5];
    wifi_get_ip_addr(wifiMode, &infoValue);
    g_vnet_reg.ipAddr = ((infoValue & 0xFF) << 24) | (((infoValue >> 8) & 0xFF) << 16);
    g_vnet_reg.ipAddr |= (((infoValue >> 16) & 0xFF) << 8) | ((infoValue >> 24) & 0xFF);
    wifi_get_mask_addr(wifiMode, &infoValue);
    g_vnet_reg.ipMask = ((infoValue & 0xFF) << 24) | (((infoValue >> 8) & 0xFF) << 16);
    g_vnet_reg.ipMask |= (((infoValue >> 16) & 0xFF) << 8) | ((infoValue >> 24) & 0xFF);
    wifi_get_gw_addr(wifiMode, &infoValue);
    g_vnet_reg.gw[0] = ((infoValue & 0xFF) << 24) | (((infoValue >> 8) & 0xFF) << 16);
    g_vnet_reg.gw[0] |= (((infoValue >> 16) & 0xFF) << 8) | ((infoValue >> 24) & 0xFF);

    for (index = 0; index < DNS_MAX_SERVERS; index++) {
        wifi_get_dns_addr(index, &infoValue);
        unsigned int *pd = (unsigned int *)&g_vnet_reg.dns[index];
        *(pd + index) = ((infoValue & 0xFF) << 24) | (((infoValue >> 8) & 0xFF) << 16);
        *(pd + index) |= (((infoValue >> 16) & 0xFF) << 8) | ((infoValue >> 24) & 0xFF);
    }
}

void vnet_reg_wifilink_status(wifi_interface_e iface, int link)
{
    unsigned int flag;

    if ((link == VNET_WIFI_LINK_UP) && (link != g_vnet_reg.status)) {
        vnet_reg_get_info();
        flag = system_irq_save();
        g_vnet_reg.status = VNET_WIFI_LINK_UP;
        vnet_interrupt_enable(VNET_INTERRUPT_LINK_CS);
        vnet_interrupt_handle(VNET_INTERRUPT_LINK_CS);
        system_irq_restore(flag);
    }

    if ((link == VNET_WIFI_LINK_DOWN) && (link != g_vnet_reg.status)) {
        flag = system_irq_save();
        g_vnet_reg.status = VNET_WIFI_LINK_DOWN;
        vnet_interrupt_enable(VNET_INTERRUPT_LINK_CS);
        vnet_interrupt_handle(VNET_INTERRUPT_LINK_CS);
        system_irq_restore(flag);
    }
}

static int vnet_reg_get_value(cmd_tbl_t *t, int argc, char *argv[])
{
    int idx = 0;
    int regNum = sizeof(g_vnet_reg) / sizeof(unsigned int);
    unsigned int *pdata = (unsigned int *)&g_vnet_reg;

    for (idx = 0; idx < regNum; idx++) {
        os_printf(LM_OS, LL_INFO, "addr:0x%08x\t value:0x%08x\n", pdata+idx, pdata[idx]);
    }

    return CMD_RET_SUCCESS;
}

CLI_CMD(vnet_reg_read, vnet_reg_get_value, "vnet_reg_get_value", "vnet_reg_get_value");
