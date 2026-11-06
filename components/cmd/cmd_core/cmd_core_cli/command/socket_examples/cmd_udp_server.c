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

static uint32_t server_port = 9001;
static bool udp_server_state_flag = 0;

static void cmd_udp_server_start(void *pvParameters)
{
    char rx_buffer[128];
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;
    udp_server_state_flag = 1;
    
    while (udp_server_state_flag) {
        if (addr_family == AF_INET) {
            struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
            dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
            dest_addr_ip4->sin_family = AF_INET;
            dest_addr_ip4->sin_port = htons(server_port);
            ip_protocol = IPPROTO_IP;
            UDP_SERVER_FUNC_SET_START(socket_coninfo);
        }
    #if defined(CONFIG_IPV6)
        else if (addr_family == AF_INET6) {
            bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
            dest_addr.sin6_family = AF_INET6;
            dest_addr.sin6_port = htons(server_port);
            ip_protocol = IPPROTO_IPV6;
            UDPV6_SERVER_FUNC_SET_START(socket_coninfo);
        }
    #endif
        int sockfd = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sockfd < 0) {
            os_printf(LM_CMD, LL_ERR, TAG_LOG" Unable to create socket: errno %d\r\n",errno);
            break;
        }
        os_printf(LM_CMD, LL_INFO, TAG_LOG" Socket created\r\n");

    #if defined(CONFIG_IPV6)
        if (addr_family == AF_INET6) {
            int opt = 1;
            setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
        }
    #endif
        // Set timeout
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

        int err = bind(sockfd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            os_printf(LM_CMD, LL_ERR, TAG_LOG" Socket unable to bind: errno %d\r\n", errno);
        }
        os_printf(LM_CMD, LL_INFO, TAG_LOG" Socket bound, port %d\r\n", server_port);
        
        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t socklen = sizeof(source_addr);

        while (1) {
            os_printf(LM_CMD, LL_INFO, TAG_LOG" Waiting for data\r\n");
            int len = recvfrom(sockfd, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);
            // Error occurred during receiving
            if (len < 0) {
                os_printf(LM_CMD, LL_ERR, TAG_LOG" recvfrom failed: errno %d\r\n", errno);
                break;
            }
            else {
                // Get the sender's ip address as string
                if (source_addr.ss_family == PF_INET) {
                    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
                } 
            #if defined(CONFIG_IPV6)
                else if (source_addr.ss_family == PF_INET6) {
                    inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
                }
            #endif
                rx_buffer[len] = 0; 
                os_printf(LM_CMD, LL_INFO, TAG_LOG" Received %d bytes from %s:\r\n", len, addr_str);
                os_printf(LM_CMD, LL_INFO, TAG_LOG" %s\r\n", rx_buffer);

                int err = sendto(sockfd, rx_buffer, len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                if (err < 0) {
                    os_printf(LM_CMD, LL_ERR, TAG_LOG" Error occurred during sending: errno %d\r\n", errno);
                    break;
                }
            }
        }

        if (sockfd != -1) {
            os_printf(LM_CMD, LL_ERR, TAG_LOG" Shutting down socket and restarting...\r\n");
            shutdown(sockfd, 0);
            close(sockfd);
        }
    }
    os_printf(LM_CMD, LL_INFO, TAG_LOG" Close socket !!\r\n");
    UDP_SERVER_FUNC_UNSET_START(socket_coninfo);
    UDPV6_SERVER_FUNC_UNSET_START(socket_coninfo);
    vTaskDelete(NULL);
}

static int cmd_udp_server( int argc, char *argv[])
{
    if(UDP_SERVER_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" UDP server has started, server port:%d\r\n", server_port);
        return CMD_RET_FAILURE;
    }
    else if(UDPV6_SERVER_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" UDPV6 server has started, server port:%d\r\n", server_port);
        return CMD_RET_FAILURE;
    }
    if (argc != 3)
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" UDP server parameters numbers is wrong! \r\n");
        return CMD_RET_FAILURE;
    }
    server_port = atoi(argv[2]);

    os_task_create("cmd_udp_s", 5, (4*1024), cmd_udp_server_start, (void*)AF_INET);
    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(socket, udp_server, cmd_udp_server, "test udp server", "socket udp_server port xx");

#ifdef CONFIG_IPV6
static int cmd_udpv6_server( int argc, char *argv[])
{
    if(UDP_SERVER_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" UDP server has started, server port:%d\r\n", server_port);
        return CMD_RET_FAILURE;
    }
    else if(UDPV6_SERVER_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" UDPV6 server has started, server port:%d\r\n", server_port);
        return CMD_RET_FAILURE;
    }
    if (argc != 3)
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" UDPV6 server parameters numbers is wrong! \r\n");
        return CMD_RET_FAILURE;
    }
    server_port = atoi(argv[2]);
    os_task_create("cmd_udpv6_s", 5, (4*1024), cmd_udp_server_start, (void*)AF_INET6);

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(socket, udpv6_server, cmd_udpv6_server, "test udpv6 server", "socket udpv6_server port xx");
#endif

static int cmd_udp_stop_server( int argc, char *argv[])
{
    if(!UDP_SERVER_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" UDP server no started\r\n");
        return CMD_RET_FAILURE;
    }
    if (argc != 1)
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" Stop UDP server parameters numbers is wrong! \r\n");
        return CMD_RET_FAILURE;
    }

    udp_server_state_flag = 0;
    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(socket, udp_stop_server, cmd_udp_stop_server, "test udp stop server", "socket udp_stop_server");

#ifdef CONFIG_IPV6
static int cmd_udpv6_stop_server( int argc, char *argv[])
{
    if(!UDPV6_SERVER_FUNC_ISSET_START(socket_coninfo) )
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" UDPV6 server no started\r\n");
        return CMD_RET_FAILURE;
    }
    if (argc != 1)
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" Stop UDPV6 server parameters numbers is wrong! \r\n");
        return CMD_RET_FAILURE;
    }

    udp_server_state_flag = 0;
    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(socket, udpv6_stop_server, cmd_udpv6_stop_server, "test udpv6 stop server", "socket udpv6_stop_server");
#endif