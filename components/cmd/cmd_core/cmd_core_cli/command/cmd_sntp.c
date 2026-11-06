/**
 * @file cmd_sntp.c
 * @brief SNTP CLI command implementation.
 * @details Provides commands to enable/disable SNTP, configure timezone, sync period, DST, and NTP servers.
 * @par Copyright by http://eswincomputing.com/.
 */

#include <limits.h>
#include "cli.h"
#include "sntp.h"
#include "hal_sntp.h"
#include "hal_rtc.h"

static void print_sntp_servers(void)
{
    unsigned char id;

    os_printf(LM_CMD, LL_INFO, "================================\n");
    os_printf(LM_CMD, LL_INFO, "List of configured NTP servers:\n");
    os_printf(LM_CMD, LL_INFO, "================================\n");

    for (id = 0; id < SNTP_MAX_SERVERS; id++) {
        const char *servername = sntp_getservername(id);
        const ip_addr_t *pip = sntp_getserver(id);
        char ipstr[IPADDR_STRLEN_MAX];

        if (servername) {
            os_printf(LM_CMD, LL_INFO, "Server %d: %s", id, servername);
            if (pip && !ip_addr_isany(pip) && ipaddr_ntoa_r(pip, ipstr, sizeof(ipstr))) {
                os_printf(LM_CMD, LL_INFO, " (IP: %s)", ipstr);
            } else {
                os_printf(LM_CMD, LL_INFO, " (IP: unresolved)");
            }
        } else {
            if (pip && !ip_addr_isany(pip) && ipaddr_ntoa_r(pip, ipstr, sizeof(ipstr))) {
                os_printf(LM_CMD, LL_INFO, "Server %d: %s", id, ipstr);
            } else {
                os_printf(LM_CMD, LL_INFO, "Server %d: Not configured", id);
                continue;
            }
        }

        os_printf(LM_CMD, LL_INFO, " - Status: %s\n",
            sntp_getreachability(id) ? "Reachable" : "Unreachable/Trying");
    }
    os_printf(LM_CMD, LL_INFO, "================================\n");
}

static void print_sync_status(void)
{
    DATE_RTC cur_time = {0};
    int time_sec;

    os_printf(LM_CMD, LL_INFO, "================================\n");
    os_printf(LM_CMD, LL_INFO, "SNTP Synchronization Status:\n");
    os_printf(LM_CMD, LL_INFO, "================================\n");
    os_printf(LM_CMD, LL_INFO, "SNTP Enabled: %s\n", sntp_enabled() ? "Yes" : "No");

    if (sntp_enabled()) {
        os_printf(LM_CMD, LL_INFO, "Sync Mode: %s\n",
            sntp_getoperatingmode() ? "Listen" : "Poll");

        #ifdef SNTP_GET_SERVERS_FROM_DHCP
        os_printf(LM_CMD, LL_INFO, "DHCP NTP: Enabled\n");
        #else
        os_printf(LM_CMD, LL_INFO, "DHCP NTP: Disabled\n");
        #endif

        time_sec = hal_rtc_get_time();
        if (hal_rtc_utctime_to_date(time_sec, &cur_time) == 0) {
            os_printf(LM_CMD, LL_INFO, "Current Time: %04d-%02d-%02d %02d:%02d:%02d (Week: %d)\n",
                cur_time.year, cur_time.month, cur_time.day,
                cur_time.hour, cur_time.min, cur_time.sec, cur_time.wday);
        }

        os_printf(LM_CMD, LL_INFO, "Timezone: %s%f\n", hal_sntp_timezone_get() > 0 ? "+" : "", hal_sntp_timezone_get());
        os_printf(LM_CMD, LL_INFO, "Update Period: %d seconds\n", hal_sntp_periodms_get() / 1000);
    }
    os_printf(LM_CMD, LL_INFO, "================================\n");
}

static void show_smooth_sync_progress(void)
{
    #ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    int count = 0;
    const int check_interval_ms = 2000;
    const int max_checks = 15; /* Limit CLI blocking to about 30 seconds. */

    os_printf(LM_CMD, LL_INFO, "================================\n");
    os_printf(LM_CMD, LL_INFO, "Monitoring Smooth Sync Progress:\n");
    os_printf(LM_CMD, LL_INFO, "================================\n");

    if (!sntp_enabled()) {
        os_printf(LM_CMD, LL_ERR, "SNTP is not enabled!\n");
        return;
    }

    if (sntp_getoperatingmode() != SNTP_OPMODE_POLL) {
        os_printf(LM_CMD, LL_WARN, "Smooth sync monitoring works best in poll mode\n");
    }

    while (count < max_checks) {
        struct timeval outdelta;

        if (adjtime(NULL, &outdelta) == 0) {
            if (outdelta.tv_sec == 0 && outdelta.tv_usec == 0) {
                os_printf(LM_CMD, LL_INFO, "Time adjustment completed!\n");
                break;
            }

            os_printf(LM_CMD, LL_INFO, "Remaining adjustment: %ld sec, %ld ms, %ld us\n",
                (long)outdelta.tv_sec,
                outdelta.tv_usec / 1000,
                outdelta.tv_usec % 1000);
        } else {
            os_printf(LM_CMD, LL_WARN, "No time adjustment in progress\n");
            break;
        }

        os_msleep(check_interval_ms);
        count++;
    }

    if (count >= max_checks) {
        os_printf(LM_CMD, LL_WARN, "Monitoring timeout. Adjustment may still be in progress.\n");
    }

    os_printf(LM_CMD, LL_INFO, "================================\n");
    #else
    os_printf(LM_CMD, LL_ERR, "Smooth sync mode is not enabled in configuration!\n");
    os_printf(LM_CMD, LL_INFO, "Please enable CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH\n");
    #endif
}

static void set_sntp_enabled(int enable)
{
    if (enable) {
        if (sntp_enabled()) {
            os_printf(LM_CMD, LL_ERR, "SNTP is already enabled\n");
        } else {
            hal_sntp_init(HAL_SNTP_SYNC_NTP);
        }
    } else {
        if (sntp_enabled()) {
            hal_sntp_stop_async();
        } else {
            os_printf(LM_CMD, LL_ERR, "SNTP is already disabled\n");
        }
    }
}

static int set_sntp_server(int argc, char *argv[])
{
    int id;
    const char *id_str;
    const char *server;

    if (argc != 4) {
        return -1;
    }

    id = 0;
    id_str = argv[2];
    if (id_str[0] == '\0') {
        os_printf(LM_CMD, LL_ERR, "Server ID must be 0-%d\n", SNTP_MAX_SERVERS - 1);
        return -1;
    }

    while (*id_str != '\0') {
        if (*id_str < '0' || *id_str > '9') {
            os_printf(LM_CMD, LL_ERR, "Server ID must be 0-%d\n", SNTP_MAX_SERVERS - 1);
            return -1;
        }
        id = id * 10 + (*id_str - '0');
        if (id >= SNTP_MAX_SERVERS) {
            os_printf(LM_CMD, LL_ERR, "Server ID must be 0-%d\n", SNTP_MAX_SERVERS - 1);
            return -1;
        }
        id_str++;
    }

    server = argv[3];
    if (server[0] == '\0') {
        os_printf(LM_CMD, LL_ERR, "server name should not be empty\n");
        return -1;
    }

    if (strlen(server) > SNTP_SERVER_DNS_NAME_MAX_LEN) {
        os_printf(LM_CMD, LL_ERR, "server name must be <= %d chars\n", SNTP_SERVER_DNS_NAME_MAX_LEN);
        return -1;
    }

    return hal_sntp_set_servername((unsigned char)id, server, HAL_SNTP_SYNC_NTP | HAL_SNTP_SETTING_SAVE);
}

static int set_sntp_dst(int argc, char *argv[])
{
    if (argc != 3) {
        return -1;
    }

    if (strcmp(argv[2], "0") == 0) {
        return hal_sntp_set_utc_dst(0, HAL_SNTP_SYNC_NTP | HAL_SNTP_SETTING_SAVE);
    }

    if (strcmp(argv[2], "1") == 0) {
        return hal_sntp_set_utc_dst(1, HAL_SNTP_SYNC_NTP | HAL_SNTP_SETTING_SAVE);
    }

    os_printf(LM_CMD, LL_ERR, "dst must be 0 or 1\n");
    return -1;
}

static int parse_sntp_period_sec(const char *str, unsigned int *period_sec)
{
    unsigned int value = 0;

    if (str[0] == '\0') {
        return -1;
    }

    while (*str != '\0') {
        if (*str < '0' || *str > '9') {
            return -1;
        }

        if (value > (UINT_MAX - (*str - '0')) / 10) {
            return -1;
        }

        value = value * 10 + (*str - '0');
        str++;
    }

    if (value > UINT_MAX / 1000) {
        return -1;
    }

    *period_sec = value;
    return 0;
}

static void print_sntp_help(void)
{
    os_printf(LM_CMD, LL_INFO, "SNTP Command Usage:\n");
    os_printf(LM_CMD, LL_INFO, "================================\n");
    os_printf(LM_CMD, LL_INFO, "sntp on                         - Enable SNTP\n");
    os_printf(LM_CMD, LL_INFO, "sntp off                        - Disable SNTP\n");
    os_printf(LM_CMD, LL_INFO, "sntp tz <timezone>              - Set timezone (e.g., 8 for UTC+8)\n");
    os_printf(LM_CMD, LL_INFO, "sntp period <seconds>           - Set update period\n");
    os_printf(LM_CMD, LL_INFO, "sntp server <id> <addr/name>    - Set server (id: 0-%d)\n", SNTP_MAX_SERVERS - 1);
    os_printf(LM_CMD, LL_INFO, "sntp setipv6 <id> <addr>        - Set IPv6 server (id: 0-%d)\n", SNTP_MAX_SERVERS - 1);
    os_printf(LM_CMD, LL_INFO, "sntp setdst <0/1>               - Set dst enable disable\n");
    os_printf(LM_CMD, LL_INFO, "sntp servers                    - List all configured servers\n");
    os_printf(LM_CMD, LL_INFO, "sntp status                     - Show synchronization status\n");
    os_printf(LM_CMD, LL_INFO, "sntp monitor                    - Monitor smooth sync progress\n");
    os_printf(LM_CMD, LL_INFO, "sntp                            - Show this help and current config\n");
    os_printf(LM_CMD, LL_INFO, "================================\n");
    os_printf(LM_CMD, LL_INFO, "Current Configuration:\n");
    os_printf(LM_CMD, LL_INFO, "================================\n");
    os_printf(LM_CMD, LL_INFO, "SNTP: %s\n", sntp_enabled() ? "Enabled" : "Disabled");
    os_printf(LM_CMD, LL_INFO, "TimeZone: %s%f\n", hal_sntp_timezone_get() > 0 ? "+" : "", hal_sntp_timezone_get());
    os_printf(LM_CMD, LL_INFO, "DST: %s\n", hal_sntp_dst_sec() ? "Enabled" : "Disabled");
    os_printf(LM_CMD, LL_INFO, "UpdatePeriod: %ds\n", hal_sntp_periodms_get() / 1000);
    os_printf(LM_CMD, LL_INFO, "OperateMode: %s\n", sntp_getoperatingmode() ? "listen" : "poll");
    os_printf(LM_CMD, LL_INFO, "ServerMaxNumber: %d\n", SNTP_MAX_SERVERS);
    print_sntp_servers();
}

int set_sntp_fun(int argc, char *argv[])
{
    if (argc <= 1) {
        print_sntp_help();
        return CMD_RET_SUCCESS;
    }

    if ((strcmp(argv[1], "on") == 0) && argc == 2) {
        set_sntp_enabled(1);
        return CMD_RET_SUCCESS;
    }

    if ((strcmp(argv[1], "off") == 0) && argc == 2) {
        set_sntp_enabled(0);
        return CMD_RET_SUCCESS;
    }

    if ((strcmp(argv[1], "tz") == 0) && argc == 3) {
        float tz = atof(argv[2]);
        if (hal_sntp_timezone_set(tz, HAL_SNTP_SYNC_NTP | HAL_SNTP_SETTING_SAVE) == 0) {
            return CMD_RET_SUCCESS;
        }
    }

    if ((strcmp(argv[1], "period") == 0) && argc == 3) {
        unsigned int prd;
        if (parse_sntp_period_sec(argv[2], &prd) != 0) {
            os_printf(LM_CMD, LL_ERR, "period must be an unsigned integer\n");
            return CMD_RET_FAILURE;
        }

        if (hal_sntp_period_set(prd * 1000, HAL_SNTP_SYNC_NTP | HAL_SNTP_SETTING_SAVE) == 0) {
            return CMD_RET_SUCCESS;
        }
    }

    if (((strcmp(argv[1], "server") == 0) || (strcmp(argv[1], "setipv6") == 0)) &&
        set_sntp_server(argc, argv) == 0) {
        return CMD_RET_SUCCESS;
    }

    if ((strcmp(argv[1], "setdst") == 0) && set_sntp_dst(argc, argv) == 0) {
        return CMD_RET_SUCCESS;
    }

    if ((strcmp(argv[1], "servers") == 0) && argc == 2) {
        print_sntp_servers();
        return CMD_RET_SUCCESS;
    }

    if ((strcmp(argv[1], "status") == 0) && argc == 2) {
        print_sync_status();
        return CMD_RET_SUCCESS;
    }

    if ((strcmp(argv[1], "monitor") == 0) && argc == 2) {
        show_smooth_sync_progress();
        return CMD_RET_SUCCESS;
    }

    return CMD_RET_FAILURE;
}

CLI_CMD(sntp, set_sntp_fun, "sntp on|off|tz|period|server|setipv6|setdst|servers|status|monitor", "sntp [command] - SNTP time synchronization control");
