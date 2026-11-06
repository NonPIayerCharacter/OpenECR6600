#include "cli.h"
#include "hal_flash.h"

extern void doreset_function();
static int FactoryResetHandler( int argc, char *argv[])
{

    if (argc != 1)
    {
        os_printf(LM_CMD, LL_ERR, "Command arg error\n");
        return CMD_RET_USAGE;
    }
    doreset_function();
    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(matter,factoryreset, FactoryResetHandler,  "factoryreset",  "matter factoryreset");

#define MATTER_CERT_DATA_OFFSET 16
typedef struct {
    unsigned char name[16];
    unsigned int len;
    unsigned int offset;
    unsigned int uartbase;
    T_UART_ISR_CALLBACK callback;
    unsigned char data[1024];
} matter_cert_t;

matter_cert_t g_matter_certdata;

static int WriteCertToFlashPartition(void)
{
    unsigned int addr = 0;
    unsigned int len = 0;
    int ret = partion_info_get((char *)g_matter_certdata.name, &addr, &len);

    if (ret == 0) {
        hal_flash_erase(addr, len);
        hal_flash_write(addr, g_matter_certdata.data, g_matter_certdata.len + MATTER_CERT_DATA_OFFSET);
    }

    return 0;
}

static void matter_get_uart_data(void *data)
{
    E_DRV_UART_NUM uart_num;
    matter_cert_t *mcdata = (matter_cert_t *)data;

    if (mcdata->offset < mcdata->len) {
        while (drv_uart_rx_tstc(mcdata->uartbase)) {
            mcdata->data[MATTER_CERT_DATA_OFFSET + mcdata->offset] = drv_uart_rx_getc(mcdata->uartbase);
            mcdata->offset += 1;
            if (mcdata->offset >= mcdata->len) {
                break;
            }
        }
    }

#ifdef CONFIG_CLI_UART_0
    uart_num	= E_UART_NUM_0;
#endif
#ifdef CONFIG_CLI_UART_1
    uart_num	= E_UART_NUM_1;
#endif
#ifdef CONFIG_CLI_UART_2
    uart_num	= E_UART_NUM_2;
#endif

    if (mcdata->offset >= mcdata->len) {
        drv_uart_ioctrl(uart_num, DRV_UART_CTRL_REGISTER_RX_CALLBACK, &mcdata->callback);
        os_printf(LM_CMD, LL_ERR, "get cert data finished\n");
        WriteCertToFlashPartition();
        mcdata->offset = 0;
    }
}

static int GetMatterCertData(int argc, char *argv[])
{
    E_DRV_UART_NUM uart_num;
    T_UART_ISR_CALLBACK mcback = {0};
    unsigned int len;
    char *stop;

    if (argc != 3) {
        return CMD_RET_USAGE;
    }

    if (strcmp(argv[1], "nv_dac_prikey") && strcmp(argv[1], "nv_pai_cert") && strcmp(argv[1], "nv_dac_cert") && strcmp(argv[1], "nv_cd")) {
        os_printf(LM_CMD, LL_ERR, "matter cert partname not correct\n");
        return CMD_RET_FAILURE;
    }
    snprintf((char *)g_matter_certdata.name, sizeof(g_matter_certdata.name), "%s", argv[1]);

    len = (unsigned int)strtoul(argv[2], &stop, 0);
    if (len > 600) {
        os_printf(LM_CMD, LL_ERR, "matter cert cannot bigger than 600\n");
        return CMD_RET_FAILURE;
    }

#ifdef CONFIG_CLI_UART_0
    uart_num	= E_UART_NUM_0;
#endif
#ifdef CONFIG_CLI_UART_1
    uart_num	= E_UART_NUM_1;
#endif
#ifdef CONFIG_CLI_UART_2
    uart_num	= E_UART_NUM_2;
#endif

    mcback.uart_callback = matter_get_uart_data;
    mcback.uart_data = &g_matter_certdata;
    drv_uart_ioctrl(uart_num, DRV_UART_CTRL_GET_RX_CALLBACK, &g_matter_certdata.callback);
    drv_uart_ioctrl(uart_num, DRV_UART_CTRL_GET_REG_BASE, (void *)(&g_matter_certdata.uartbase));
    drv_uart_ioctrl(uart_num, DRV_UART_CTRL_REGISTER_RX_CALLBACK, &mcback);
    os_printf(LM_CMD, LL_ERR, "wait cert data %d\n", len);
    g_matter_certdata.len = len;
    memcpy(g_matter_certdata.data, &len, sizeof(len));

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(matter, writecert, GetMatterCertData, "matter write cert data",  "matter writecert name len");

CLI_CMD(matter, NULL, "matter cmd", "matter [subcmd]");

#ifdef CONFIG_MATTER
extern int MatterSetAttrCliHandler(int argc, char *argv[]);
extern int MatterSetAttrListCliHandler(int argc, char *argv[]);
extern int MatterNotifyCallbackCliHandler(int argc, char *argv[]);

CLI_SUBCMD(matter, setattr, MatterSetAttrCliHandler, "simulate external matter set-api call",
           "matter setattr <endpointId> <clusterId> <attributeId> <value>");
CLI_SUBCMD(matter, setlist, MatterSetAttrListCliHandler, "list supported set-api endpoint/cluster/attribute combinations",
           "matter setlist");
CLI_SUBCMD(matter, notifycb, MatterNotifyCallbackCliHandler, "register notify callback logger",
           "matter notifycb");
#endif

/********************* set UserConsentState for ota test *****************************/
extern int UserConsentStateHandler( int argc, char ** argv);

CLI_CMD(matter_ucstate, UserConsentStateHandler, "Set UserConsentState", "matter_ucstate ['granted' or 'denied' or 'deferred']");

/********************* set RequestorCanConsent for ota test *****************************/
extern int RequestorCanConsentHandler( int argc, char ** argv);

CLI_CMD(matter_recanconsent, RequestorCanConsentHandler, "Set RequestorCanConsent", "matter_recanconsent ['true' or 'false']");

/********************* set RequestorCanConsent for ota test *****************************/
extern int SetBlockSizeHandler( int argc, char ** argv);

CLI_CMD(matter_setblocksize, SetBlockSizeHandler, "Set BlockSize", "matter_setblocksize [1024]");

/********************* set PeriodicQueryTimeout for ota test *****************************/
extern int SetPeriodicQueryTimeoutHandler( int argc, char ** argv);

CLI_CMD(matter_setPQtime, SetPeriodicQueryTimeoutHandler, "Set PeriodicQueryTimeout", "matter_setPQtime [time]");



#ifdef CONFIG_LIGHT_SWITCH_APP
/********************* switch On *****************************/
extern int SwitchCommandHandler( int argc, char ** argv);
CLI_SUBCMD(switch,onoff, SwitchCommandHandler, "Set Switch Status", "switch onoff ['on' or 'off' or 'toggle']");

/********************* Group switch  *****************************/
extern int GroupSwitchCommandHandler( int argc, char ** argv);
CLI_SUBCMD(groups, onoff, GroupSwitchCommandHandler, "Set Switch groups onoff Statuss", "switch groups onoff ['on' or 'off' or 'toggle']");
CLI_SUBCMD(switch, groups, GroupSwitchCommandHandler, "Set Switch groups onoff Status", "switch groups onoff ['on' or 'off' or 'toggle']");

/********************* Group/Unicast binding  *****************************/
extern int BindingGroup_or_UnicastBindCommandHandler( int argc, char ** argv);
CLI_SUBCMD(switch,binding, BindingGroup_or_UnicastBindCommandHandler, "Set binding type", "switch binding ['group' or 'unicast']");
CLI_CMD(switch, NULL, "log cmd", "log <subcmd>");
#endif
