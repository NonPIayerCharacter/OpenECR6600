#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "cli.h"
#include "oshal.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "system_wifi.h"
#include "system_lwip.h"
#include "system_config.h"
#include "cmd_socket.h"

static char *host_ip_addr = "192.168.31.66";
static uint32_t host_port = 8989;

#ifdef CONFIG_IPV6
static char *host_ip_addr_v6 = "240e:478:e80:456d:89b9:a028:6d86:ceee";
static uint32_t host_port_v6 = 8989;
#endif
static const char *message = "Message from ECR6600";

static void cmd_tcp_client_start(void *pvParameters)
{
    char rx_buffer[128];
    int addr_family = 0;
    int ip_protocol = 0;
    TCP_FUNC_SET_START(socket_coninfo);
    bool loop_test = 1;
    while (loop_test) {
        struct sockaddr_in dest_addr;
        inet_pton(AF_INET, host_ip_addr, &dest_addr.sin_addr);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(host_port);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;

        int sockfd =  socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sockfd < 0) {
            os_printf(LM_APP, LL_ERR, TAG_LOG" Unable to create socket: errno %d\r\n", errno);
            break;
        }
        os_printf(LM_APP, LL_INFO, TAG_LOG" Socket created, connecting to %s:%d\r\n", host_ip_addr, host_port);

        int err = connect(sockfd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0) {
            os_printf(LM_APP, LL_ERR, TAG_LOG" Socket unable to connect: errno %d\r\n", errno);
            break;
        }
        os_printf(LM_APP, LL_INFO, TAG_LOG" Successfully connected\r\n");

        while (loop_test) {
            int err = send(sockfd, message, strlen(message), 0);
            if (err < 0) {
                os_printf(LM_APP, LL_ERR, TAG_LOG" Error occurred during sending: errno %d\r\n", errno);
                break;
            }

            int len = recv(sockfd, rx_buffer, sizeof(rx_buffer) - 1, 0);
            // Received error
            if (len < 0) {
                os_printf(LM_APP, LL_ERR, TAG_LOG" recv failed: errno %d\r\n", errno);
                break;
            }
            // Data received
            else {
                rx_buffer[len] = 0;
                os_printf(LM_APP, LL_INFO, TAG_LOG" Received %d bytes from %s:\r\n", len, host_ip_addr);
                os_printf(LM_APP, LL_INFO, TAG_LOG" %s\r\n", rx_buffer);
            }
        }

        if (sockfd != -1) {
            os_printf(LM_APP, LL_INFO, TAG_LOG" Shutting down socket\r\n");
            shutdown(sockfd, 0);
            close(sockfd);
            loop_test = 0;
        }
    }
    TCP_FUNC_UNSET_START(socket_coninfo);
    vTaskDelete(NULL);
}
static int cmd_tcp_client( int argc, char *argv[])
{
    if(TCP_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_INFO, TAG_LOG" tcp client has started! \r\n");
        return CMD_RET_FAILURE;
    }
    if (argc != 5)
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" tcp client parameters numbers is wrong! \r\n");
        return CMD_RET_FAILURE;
    }
    int i =0;
    for(i=1; i<argc; i+=2)
    {
        if(strcmp(argv[i], "host")==0)
        {
            host_ip_addr = argv[i+1];
        }
        else if(strcmp(argv[i], "port")==0)
        {
            if((atoi(argv[i+1]) >= 0) && (atoi(argv[i+1]) < 65535))
            {
                host_port = atoi(argv[i+1]);
            }
            else
            {
                os_printf(LM_APP, LL_ERR,TAG_LOG" tcp client parameters port is wrong! \r\n");
                return CMD_RET_FAILURE;
            }
        }
        else
        {
            os_printf(LM_CMD, LL_INFO, TAG_LOG" tcp client parameters is wrong! \r\n");
            return CMD_RET_FAILURE;
        }
    }

    os_task_create("cmd_tcp_c", 5, (4*1024), cmd_tcp_client_start, NULL);

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(socket, tcp, cmd_tcp_client, "test tcp client", "socket tcp host xx port xx");

#ifdef CONFIG_IPV6
static void cmd_tcp_v6_client_start(void *pvParameters)
{
    char rx_buffer[128];
    int addr_family = 0;
    int ip_protocol = 0;
    bool loop_test = 0;
    TCPV6_FUNC_SET_START(socket_coninfo);
    while (loop_test) {
        struct sockaddr_in6 dest_addr = { 0 };
        inet_pton(AF_INET6, host_ip_addr_v6, &dest_addr.sin6_addr);
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(host_port_v6);
        addr_family = AF_INET6;
        ip_protocol = IPPROTO_TCP;

        int sockfd =  socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sockfd < 0) {
            os_printf(LM_APP, LL_ERR, TAG_LOG" Unable to create socket: errno %d\r\n", errno);
            break;
        }
        os_printf(LM_APP, LL_INFO, TAG_LOG" Socket created, connecting to %s:%d\r\n", host_ip_addr_v6, host_port_v6);

        int err = connect(sockfd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0) {
            os_printf(LM_APP, LL_ERR, TAG_LOG" Socket unable to connect: errno %d\r\n", errno);
            break;
        }
        os_printf(LM_APP, LL_INFO, TAG_LOG" Successfully connected\r\n");

        while (loop_test) {
            int err = send(sockfd, message, strlen(message), 0);
            if (err < 0) {
                os_printf(LM_APP, LL_ERR, TAG_LOG" Error occurred during sending: errno %d\r\n", errno);
                break;
            }

            int len = recv(sockfd, rx_buffer, sizeof(rx_buffer) - 1, 0);
            // Received error
            if (len < 0) {
                os_printf(LM_APP, LL_ERR, TAG_LOG" recv failed: errno %d\r\n", errno);
                break;
            }
            // Data received
            else {
                rx_buffer[len] = 0;
                os_printf(LM_APP, LL_INFO, TAG_LOG" Received %d bytes from %s:\r\n", len, host_ip_addr_v6);
                os_printf(LM_APP, LL_INFO, TAG_LOG" %s\r\n", rx_buffer);
            }
        }

        if (sockfd != -1) {
            os_printf(LM_APP, LL_ERR, TAG_LOG" Shutting down socket and restarting...\r\n");
            shutdown(sockfd, 0);
            close(sockfd);
            loop_test = 0;
        }
    }
    TCPV6_FUNC_UNSET_START(socket_coninfo);
    vTaskDelete(NULL);
}
static int cmd_tcp_v6_client( int argc, char *argv[])
{
    if(TCPV6_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" tcpv6 client has started \r\n");
        return CMD_RET_FAILURE;
    }
    if (argc != 5)
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" tcpv6 client parameters numbers is wrong! \r\n");
        return CMD_RET_FAILURE;
    }
    int i =0;
    for(i=1; i<argc; i+=2)
    {
        if(strcmp(argv[i], "host")==0)
        {
            host_ip_addr_v6 = argv[i+1];
        }
        else if(strcmp(argv[i], "port")==0)
        {
            if((atoi(argv[i+1]) >= 0) && (atoi(argv[i+1]) < 65535))
            {
                host_port_v6 = atoi(argv[i+1]);
            }
            else
            {
                os_printf(LM_APP, LL_ERR,TAG_LOG" tcpv6 client parameters port is wrong! \r\n");
                return CMD_RET_FAILURE;
            }
        }
        else
        {
            os_printf(LM_CMD, LL_INFO, TAG_LOG" tcpv6 client parameters is wrong! \r\n");
            return CMD_RET_FAILURE;
        }
    }
    os_task_create("cmd_tcpv6_c", 5, (4*1024), cmd_tcp_v6_client_start, NULL);

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(socket, tcp_v6, cmd_tcp_v6_client, "test tcp_v6 client", "socket tcp_v6 host xx port xx");
#endif
