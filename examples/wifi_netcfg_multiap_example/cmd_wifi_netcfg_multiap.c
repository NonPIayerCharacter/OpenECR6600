#include <string.h>

#include "cli.h"
#include "oshal.h"
#include "wifi_netcfg_multiap_example.h"

#define WIFI_NETCFG_CMD_TAG "[sc_test]"

static void wifi_netcfg_multiap_cmd_usage(void)
{
    os_printf(LM_CMD, LL_INFO,
              WIFI_NETCFG_CMD_TAG " Usage: sc_test [list|status|add|del|clear|start|connect|submit|scan|disc]\r\n");
    wifi_netcfg_multiap_example_print_help();
}

static int sc_test_func(int argc, char *argv[])
{
    if (argc <= 1) {
        wifi_netcfg_multiap_cmd_usage();
        return CMD_RET_SUCCESS;
    }

    if (strcmp(argv[1], "list") == 0) {
        return (wifi_netcfg_multiap_example_print_storage() == 0) ?
            CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    if (strcmp(argv[1], "status") == 0) {
        return (wifi_netcfg_multiap_example_print_status() == 0) ?
            CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    if (strcmp(argv[1], "add") == 0) {
        if (argc < 4) {
            wifi_netcfg_multiap_cmd_usage();
            return CMD_RET_FAILURE;
        }
        return (wifi_netcfg_multiap_example_add(argv[2], argv[3]) == 0) ?
            CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    if (strcmp(argv[1], "del") == 0) {
        if (argc < 3) {
            wifi_netcfg_multiap_cmd_usage();
            return CMD_RET_FAILURE;
        }
        return (wifi_netcfg_multiap_example_delete(argv[2]) == 0) ?
            CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    if (strcmp(argv[1], "clear") == 0) {
        return (wifi_netcfg_multiap_example_clear() == 0) ?
            CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    if (strcmp(argv[1], "start") == 0) {
        return (wifi_netcfg_multiap_example_start_auto_connect() == 0) ?
            CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    if (strcmp(argv[1], "connect") == 0) {
        if (argc < 3) {
            wifi_netcfg_multiap_cmd_usage();
            return CMD_RET_FAILURE;
        }
        return (wifi_netcfg_multiap_example_connect(argv[2]) == 0) ?
            CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    if (strcmp(argv[1], "submit") == 0) {
        if (argc < 4) {
            wifi_netcfg_multiap_cmd_usage();
            return CMD_RET_FAILURE;
        }
        (void)wifi_netcfg_multiap_example_start_config_mode();
        return (wifi_netcfg_multiap_example_submit_ble_config(argv[2], argv[3]) == 0) ?
            CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    if (strcmp(argv[1], "scan") == 0) {
        return (wifi_netcfg_multiap_example_trigger_scan() == 0) ?
            CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    if ((strcmp(argv[1], "disc") == 0) || (strcmp(argv[1], "disconnect") == 0)) {
        return (wifi_netcfg_multiap_example_disconnect() == 0) ?
            CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    wifi_netcfg_multiap_cmd_usage();
    return CMD_RET_FAILURE;
}

CLI_CMD(sc_test,
        sc_test_func,
        "wifi_netcfg multi-AP example command",
        "sc_test [list|status|add|del|clear|start|connect|submit|scan|disc]");

static int smartcfg_cmd_func(int argc, char *argv[])
{
    if (argc <= 1) {
        os_printf(LM_CMD, LL_INFO,
                  "[smartcfg] Usage: smartcfg [start|stop|status]\r\n");
        return CMD_RET_SUCCESS;
    }

    if (strcmp(argv[1], "start") == 0) {
        return (wifi_netcfg_multiap_example_smartcfg_start() == 0) ?
            CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    if (strcmp(argv[1], "stop") == 0) {
        return (wifi_netcfg_multiap_example_smartcfg_stop() == 0) ?
            CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    if (strcmp(argv[1], "status") == 0) {
        return (wifi_netcfg_multiap_example_smartcfg_print_status() == 0) ?
            CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }

    os_printf(LM_CMD, LL_INFO,
              "[smartcfg] Usage: smartcfg [start|stop|status]\r\n");
    return CMD_RET_FAILURE;
}

CLI_CMD(smartcfg,
        smartcfg_cmd_func,
        "wifi_netcfg smartconfig bridge command",
        "smartcfg [start|stop|status]");
