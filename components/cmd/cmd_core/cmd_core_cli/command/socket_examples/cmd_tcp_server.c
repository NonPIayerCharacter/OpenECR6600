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
static int listen_sockfd = -1;
static int client_sockfd = -1;
static void do_retransmit(const int sockfd)
{
    int len;
    char rx_buffer[128];

    do {
        len = recv(sockfd, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            os_printf(LM_APP, LL_ERR, TAG_LOG" Error occurred during receiving: errno %d\r\n", errno);
        } else if (len == 0) {
            os_printf(LM_APP, LL_WARN, TAG_LOG" Connection closed\r\n");
        } else {
            rx_buffer[len] = 0;
            os_printf(LM_APP, LL_INFO, TAG_LOG" Received %d bytes: %s\r\n", len, rx_buffer);
            int to_write = len;
            while (to_write > 0) {
                int written = send(sockfd, rx_buffer + (len - to_write), to_write, 0);
                if (written < 0) {
                    os_printf(LM_APP, LL_ERR, TAG_LOG" Error occurred during sending: errno %d\r\n", errno);
                    return;
                }
                to_write -= written;
            }
        }
    } while (len > 0);
}

static void cmd_tcp_server_start(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepalive = 1;
    int keepidle = 120;
    int keepinterval = 1;// Time interval for sending probe packets,1S
    int keepcount = 3;// Number of probes 
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(server_port);
        ip_protocol = IPPROTO_IP;
    }
#ifdef CONFIG_IPV6
    else if (addr_family == AF_INET6) {
        struct sockaddr_in6 *dest_addr_ip6 = (struct sockaddr_in6 *)&dest_addr;
        bzero(&dest_addr_ip6->sin6_addr.un, sizeof(dest_addr_ip6->sin6_addr.un));
        dest_addr_ip6->sin6_family = AF_INET6;
        dest_addr_ip6->sin6_port = htons(server_port);
        ip_protocol = IPPROTO_IPV6;
    }
#endif
    listen_sockfd = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sockfd < 0) {
        os_printf(LM_APP, LL_ERR,TAG_LOG" Unable to create socket: errno %d\r\n", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef CONFIG_IPV6
    if (addr_family == AF_INET6) {
        setsockopt(listen_sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
    }
#endif
    os_printf(LM_APP, LL_INFO,TAG_LOG" Socket created\r\n");
    int err = bind(listen_sockfd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        os_printf(LM_APP, LL_ERR,TAG_LOG" Socket unable to bind: errno %d\r\n", errno);
        os_printf(LM_APP, LL_ERR,TAG_LOG" IPPROTO: %d\r\n", addr_family);
        goto CLEAN_UP;
    }
    os_printf(LM_APP, LL_INFO,TAG_LOG" Socket bound, port %d\r\n", server_port);

    err = listen(listen_sockfd, 1);
    if (err != 0) {
        os_printf(LM_APP, LL_ERR, TAG_LOG" Error occurred during listen: errno %d\r\n", errno);
        goto CLEAN_UP;
    }
    if (addr_family == AF_INET) {
        TCP_SERVER_FUNC_SET_START(socket_coninfo);
    }
#ifdef CONFIG_IPV6
    else{
        TCPV6_SERVER_FUNC_SET_START(socket_coninfo);
    }
#endif
    
    while (1) {
        os_printf(LM_APP, LL_INFO,TAG_LOG" Socket listening\r\n");
        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);
        client_sockfd = accept(listen_sockfd, (struct sockaddr *)&source_addr, &addr_len);
        if (client_sockfd < 0) {
            os_printf(LM_APP, LL_ERR,TAG_LOG" Unable to accept connection: errno %d\r\n", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(client_sockfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(int));
        setsockopt(client_sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(int));
        setsockopt(client_sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepinterval, sizeof(int));
        setsockopt(client_sockfd, IPPROTO_TCP, TCP_KEEPCNT, &keepcount, sizeof(int));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
    #ifdef CONFIG_IPV6
        else if (source_addr.ss_family == PF_INET6) {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
        }
    #endif
        os_printf(LM_APP, LL_INFO, TAG_LOG" Socket accepted ip address: %s\r\n", addr_str);
        do_retransmit(client_sockfd);
        if(client_sockfd > -1){
            shutdown(client_sockfd, 0);
            close(client_sockfd);
        }
    }

CLEAN_UP:
    if (addr_family == AF_INET) {
        TCP_SERVER_FUNC_UNSET_START(socket_coninfo);
    }
#ifdef CONFIG_IPV6
    else{
        TCPV6_SERVER_FUNC_UNSET_START(socket_coninfo);
    }
#endif
    if(listen_sockfd > -1){
        close(listen_sockfd);
        listen_sockfd = -1;
    }
    vTaskDelete(NULL);
}
static int cmd_tcp_server( int argc, char *argv[])
{
    if(TCP_SERVER_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" TCP server has started, server port:%d\r\n", server_port);
        return CMD_RET_FAILURE;
    }
    else if(TCPV6_SERVER_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" TCPV6 server has started, server port:%d\r\n", server_port);
        return CMD_RET_FAILURE;
    }
    if (argc != 3)
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" TCP server parameters numbers is wrong! \r\n");
        return CMD_RET_FAILURE;
    }
    server_port = atoi(argv[2]);

    os_task_create("cmd_tcp_s", 5, (4*1024), cmd_tcp_server_start, (void*)AF_INET);
    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(socket, tcp_server, cmd_tcp_server, "test tcp server", "socket tcp_server port xx");

#ifdef CONFIG_IPV6
static int cmd_tcpv6_server( int argc, char *argv[])
{
    if(TCP_SERVER_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" TCP server has started, server port:%d\r\n", server_port);
        return CMD_RET_FAILURE;
    }
    else if(TCPV6_SERVER_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" TCPV6 server has started, server port:%d\r\n", server_port);
        return CMD_RET_FAILURE;
    }
    if (argc != 3)
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" TCPV6 server parameters numbers is wrong! \r\n");
        return CMD_RET_FAILURE;
    }
    server_port = atoi(argv[2]);
    os_task_create("cmd_tcpv6_s", 5, (4*1024), cmd_tcp_server_start, (void*)AF_INET6);

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(socket, tcpv6_server, cmd_tcpv6_server, "test tcpv6 server", "socket tcpv6_server port xx");
#endif

static int cmd_tcp_stop_server( int argc, char *argv[])
{
    if(!TCP_SERVER_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" TCP server no started\r\n");
        return CMD_RET_FAILURE;
    }
    if (argc != 1)
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" Stop TCP server parameters numbers is wrong! \r\n");
        return CMD_RET_FAILURE;
    }
    if(listen_sockfd > -1){
        close(listen_sockfd);
        listen_sockfd = -1;
    }
    if(client_sockfd > -1){
        close(client_sockfd);
        client_sockfd = -1;
    }
    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(socket, tcp_stop_server, cmd_tcp_stop_server, "test tcp stop server", "socket tcp_stop_server");

static int cmd_tcpv6_stop_server( int argc, char *argv[])
{
    if(!TCPV6_SERVER_FUNC_ISSET_START(socket_coninfo) )
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" TCPV6 server no started\r\n");
        return CMD_RET_FAILURE;
    }
    if (argc != 1)
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" Stop TCPV6 server parameters numbers is wrong! \r\n");
        return CMD_RET_FAILURE;
    }
    if(listen_sockfd > -1){
        close(listen_sockfd);
        listen_sockfd = -1;
    }
    if(client_sockfd > -1){
        close(client_sockfd);
        client_sockfd = -1;
    }
    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(socket, tcpv6_stop_server, cmd_tcpv6_stop_server, "test tcpv6 stop server", "socket tcpv6_stop_server");