/**
 * @file cmd_iperf.c
 * @brief This is a brief description
 * @details This is the detail description
 * @author liuyong
 * @date 2021-6-9
 * @version V0.1
 * @par Copyright by http://eswin.com/.
 * @par History 1:
 *      Date:
 *      Version:
 *      Author:
 *      Modification:
 *
 * @par History 2:
 */

#ifdef CONFIG_WIRELESS_IPERF_3
#include "cli.h"
#include "iperf.h"
#include "iperf_api.h"

#define IPERF3_LINK_MAX_NUM 16
#define IPERF3_LINK_SERVER 8
#define IPERF3_LINK_CLIENT 8

struct iperf_test *g_iperf3_threads[IPERF3_LINK_MAX_NUM];
extern int iperf3_main(int argc , char *argv[]);

static bool iperf_bandwidth_limit = true;

int iperf_toggle_limit(cmd_tbl_t *t, int argc, char *argv[])
{
    iperf_bandwidth_limit = !iperf_bandwidth_limit;
    return CMD_RET_SUCCESS;
}

CLI_CMD(iperf_toggle_limit, iperf_toggle_limit, "toggle iperf bandwidth limit", "");

static int iperf3_thread_dump(void)
{
    int index;
    struct iperf_test *test;

    for (index = 0; index < IPERF3_LINK_MAX_NUM; index++)
    {
        test = g_iperf3_threads[index];
        if (test == NULL)
            continue;

        if (test->role == 's')
            printf("server->role %c port=%d state=%d snum=%d\n", test->role, test->server_port, test->state, test->num_streams);
        else
            printf("server->role %c ip %s port=%d state=%d snum=%d\n", test->role, test->server_hostname, test->server_port, test->state, test->num_streams);
    }

    return CMD_RET_SUCCESS;
}

static int iperf3_stop_server(int port)
{
    int index;
    struct iperf_test *server;

    for (index = 0; index < IPERF3_LINK_MAX_NUM; index++)
    {
        server = g_iperf3_threads[index];
        if (server == NULL)
            continue;

        if (server->role == 's' && server->server_port == port) {
            server->state = IPERF_DONE;
            server->one_off = 1;
            return CMD_RET_SUCCESS;
        }
    }

    printf("server not start with port %d\n", port);
    return CMD_RET_FAILURE;
}

static int iperf3_stop_client(char *ip, int port)
{
    int index;
    struct iperf_test *client;

    for (index = 0; index < IPERF3_LINK_MAX_NUM; index++)
    {
        client = g_iperf3_threads[index];
        if (client == NULL)
            continue;

        if (client->role == 'c' && client->server_port == port && !strcmp(client->server_hostname, ip)) {
            if (client->state != IPERF_DONE) {
                client->state = IPERF_DONE;
                if (client->state == TEST_RUNNING) {
                    if (iperf_set_send_state(client, TEST_END) != 0)
                        return CMD_RET_FAILURE;
                }
            }
            return CMD_RET_SUCCESS;
        }
    }

    printf("client not start with ip %s port %d\n", ip, port);
    return CMD_RET_FAILURE;
}

int iperf3_sc_check(struct iperf_test *sc)
{
    int index;
    int tcpnum = 0;
    int udpnum = 0;
    int snum = 0;

    for (index = 0; index < IPERF3_LINK_MAX_NUM; index++)
    {
        if (g_iperf3_threads[index] == NULL)
            continue;

        if (sc->role == 'c')
        {
            if (sc != g_iperf3_threads[index] && sc->server_port == g_iperf3_threads[index]->server_port && !strcmp(sc->server_hostname, g_iperf3_threads[index]->server_hostname))
            {
                printf("client donot start same ip %s port %d\n", sc->server_hostname, sc->server_port);
                return CMD_RET_FAILURE;
            }
        }

        if (sc->role == 's')
        {
            if (sc != g_iperf3_threads[index] && g_iperf3_threads[index]->role == 's' && sc->server_port == g_iperf3_threads[index]->server_port)
            {
                printf("server donot start same port %d\n", sc->server_port);
                return CMD_RET_FAILURE;
            }
        }

        if (g_iperf3_threads[index]->role == 's')
        {
            if ((g_iperf3_threads[index]->state > 0) && g_iperf3_threads[index]->state != IPERF_START)
            {
                if (g_iperf3_threads[index]->protocol->id == SOCK_DGRAM)
                {
                    udpnum += g_iperf3_threads[index]->num_streams;
                }
                else
                {
                    tcpnum += g_iperf3_threads[index]->num_streams;
                }
            }
            snum += 1;
            if (snum > IPERF3_LINK_SERVER)
            {
                printf("server max link, please check...\n");
                iperf3_thread_dump();
                return CMD_RET_FAILURE;
            }
        }
        else
        {
            if (g_iperf3_threads[index]->protocol->id == SOCK_DGRAM)
            {
                udpnum += g_iperf3_threads[index]->num_streams;
            }
            else
            {
                tcpnum += g_iperf3_threads[index]->num_streams;
            }
        }

        if (tcpnum > IPERF3_LINK_CLIENT || udpnum > IPERF3_LINK_CLIENT)
        {
            printf("iperf max link, please check...\n");
            iperf3_thread_dump();
            return CMD_RET_FAILURE;
        }
    }

    if (udpnum + tcpnum > 1) {
        for (index = 0; index < IPERF3_LINK_MAX_NUM; index++) {
            if (sc->role == 's') {
                continue;
            }

            if (iperf_bandwidth_limit && sc->settings->rate > 1000*1000) {
                sc->settings->rate = 1000*1000;
            }

            if (sc->settings->blksize > 1460) {
                //printf("blksize %d too big with multicast iperf running, auto to 1460 \n");
                sc->settings->blksize = 1460;
            }
        }
    }

    return CMD_RET_SUCCESS;
}

int iperf3_test(cmd_tbl_t *t, int argc, char *argv[])
{
    int index;
    int port = PORT;
    char taskname[32] = {0};

    if (argc <= 1) {
        return CMD_RET_FAILURE;
    }

    if (strcmp(argv[1], "stop") == 0)
    {
        //iperf stop port xxx
        if (strcmp(argv[2], "port") == 0)
        {
            if (argc == 4)
                port = atoi(argv[3]);
            return iperf3_stop_server(port);
        }

        if (strcmp(argv[2], "ip") == 0)
        {
            if (strcmp(argv[4], "port") == 0)
            {
                port = atoi(argv[5]);
            }
            return iperf3_stop_client(argv[3], port);
        }

        os_printf(LM_CMD, LL_INFO, "iperf stop port[ip] xxx\n");
        return CMD_RET_FAILURE;
    }

    if (strcmp(argv[1], "print") == 0)
    {
        return iperf3_thread_dump();
    }

    if (argc == 2 && !strcmp(argv[1], "-h"))
    {
        iperf3_main(argc, argv);
        return CMD_RET_SUCCESS;
    }

    for (index = 0; index < IPERF3_LINK_MAX_NUM; index++)
    {
        if (g_iperf3_threads[index] == NULL)
        {
            g_iperf3_threads[index] = iperf_new_test();
            if (!g_iperf3_threads[index])
            {
                printf("iperf_new_test failed\n");
                return CMD_RET_FAILURE;
            }

            iperf_defaults(g_iperf3_threads[index]);
            if (iperf_parse_arguments(g_iperf3_threads[index], argc, argv) < 0)
            {
                printf("iperf_parse_arguments failed\n");
                iperf_free_test(g_iperf3_threads[index]);
                g_iperf3_threads[index] = NULL;
                return CMD_RET_SUCCESS;
            }

            if (!iperf3_sc_check(g_iperf3_threads[index]))
            {
                snprintf(taskname, sizeof(taskname), "iperf_%c_%d", g_iperf3_threads[index]->role, g_iperf3_threads[index]->server_port);
                os_task_create(taskname, LWIP_IPERF_TASK_PRIORITY, LWIP_IPERF_TASK_STACK_SIZE, iperf3_thread, &g_iperf3_threads[index]);
                printf("iperf %c port %d start\n",  g_iperf3_threads[index]->role, g_iperf3_threads[index]->server_port);
                return CMD_RET_SUCCESS;
            } else {
                iperf_free_test(g_iperf3_threads[index]);
                g_iperf3_threads[index] = NULL;
            }

            return CMD_RET_FAILURE;
        }
    }

    printf("max link start\n");
    return CMD_RET_FAILURE;
}


CLI_CMD(iperf, iperf3_test, "iperf3 cli", "iperf3 test cli");
#endif

