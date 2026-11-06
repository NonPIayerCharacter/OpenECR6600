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
/***
 UDP
***/
static bool udp_state_flag = 0;
static void cmd_udp_client_start(void *pvParameters)
{
    char rx_buffer[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in dest_addr;
#ifdef CONFIG_IPV6
    struct sockaddr_in6 dest_addr_v6 = { 0 };
#endif

    if(addr_family == AF_INET)
    {
        dest_addr.sin_addr.s_addr = inet_addr(host_ip_addr);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(host_port);
        ip_protocol = IPPROTO_IP;
        UDP_FUNC_SET_START(socket_coninfo);
    }
#ifdef CONFIG_IPV6
    else if (addr_family == AF_INET6) 
    {

        inet6_aton(host_ip_addr_v6, &dest_addr_v6.sin6_addr);
        dest_addr_v6.sin6_family = AF_INET6;
        dest_addr_v6.sin6_port = htons(host_port_v6);
        ip_protocol = IPPROTO_IPV6;
        UDPV6_FUNC_SET_START(socket_coninfo);
    }
#endif
    int sockfd = socket(addr_family, SOCK_DGRAM, ip_protocol);
    if (sockfd < 0) {
        os_printf(LM_APP, LL_ERR, TAG_LOG" Unable to create socket: errno %d\r\n", errno);
        return;
    }

    // Set timeout
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    if(ip_protocol == IPPROTO_IP){
        os_printf(LM_APP, LL_INFO, TAG_LOG" Socket created, sending to %s:%d\r\n", host_ip_addr, host_port);
    }
#ifdef CONFIG_IPV6
    else{
        os_printf(LM_APP, LL_INFO, TAG_LOG" Socket created, sending to %s:%d\r\n", host_ip_addr_v6, host_port_v6);
    }
#endif
    int err = -1;
    udp_state_flag = 1;
    while (udp_state_flag) {
        if(ip_protocol == IPPROTO_IP){
            err = sendto(sockfd, message, strlen(message), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        }else{
            err = sendto(sockfd, message, strlen(message), 0, (struct sockaddr *)&dest_addr_v6, sizeof(dest_addr_v6));
        }
        if (err < 0) {
            os_printf(LM_APP, LL_ERR, TAG_LOG" Error occurred during sending: errno %d\r\n", errno);
            break;
        }
        os_printf(LM_APP, LL_INFO, TAG_LOG" Message sent\r\n");

        struct sockaddr_storage source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sockfd, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

        // Received error
        if (len < 0) {
            os_printf(LM_APP, LL_ERR, TAG_LOG" recvfrom failed: errno %d\r\n", errno);
        }
        // Data received
        else {
            rx_buffer[len] = 0;
            if(ip_protocol == IPPROTO_IP){
                os_printf(LM_APP, LL_INFO, TAG_LOG" Received %d bytes from %s:\r\n", len, host_ip_addr);
            }else{
                os_printf(LM_APP, LL_INFO, TAG_LOG" Received %d bytes from %s:\r\n", len, host_ip_addr_v6);
            }
            os_printf(LM_APP, LL_INFO, TAG_LOG" %s\r\n", rx_buffer);
            if (strncmp(rx_buffer, "OK", 2) == 0) {
                os_printf(LM_APP, LL_INFO, TAG_LOG" Received expected message \r\n");
                break;
            }
        }
        os_msleep(10);
    }

    if (sockfd != -1) {
        os_printf(LM_APP, LL_ERR, TAG_LOG" Shutting down socket...\r\n");
        shutdown(sockfd, 0);
        close(sockfd);
    }

    if(ip_protocol == IPPROTO_IP){
        UDP_FUNC_UNSET_START(socket_coninfo);
    }else{
        UDPV6_FUNC_UNSET_START(socket_coninfo);
    }

    vTaskDelete(NULL);
}
static int cmd_udp_client( int argc, char *argv[])
{
    if(UDP_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" udp client has started \r\n");
        return CMD_RET_FAILURE;
    }
    if (argc != 5)
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" udp parameters numbers is wrong! \r\n");
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
                os_printf(LM_APP, LL_ERR,TAG_LOG" udp parameters port is wrong! \r\n");
                return CMD_RET_FAILURE;
            }
        }
        else
        {
            os_printf(LM_CMD, LL_INFO, TAG_LOG" udp parameters is wrong! \r\n");
            return CMD_RET_FAILURE;
        }
    }

    os_task_create("cmd_udp_c", 5, (4*1024), cmd_udp_client_start, (void*)AF_INET);

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(socket, udp, cmd_udp_client, "test udp client", "socket udp");

static int cmd_udp_stop_client( int argc, char *argv[])
{
    if(!UDP_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" udp client has not started \r\n");
        return CMD_RET_FAILURE;
    }
    if (argc != 1)
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" stop udp parameters numbers is wrong! \r\n");
        return CMD_RET_FAILURE;
    }
    udp_state_flag = 0;
    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(socket, udp_stop, cmd_udp_stop_client, " test udp_stop client", "socket udp_stop");

#ifdef CONFIG_IPV6
static int cmd_udpv6_client( int argc, char *argv[])
{
    if(UDPV6_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" udpv6 client has started \r\n");
        return CMD_RET_FAILURE;
    }
    if (argc != 5)
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" udpv6 parameters numbers is wrong! \r\n");
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
                os_printf(LM_APP, LL_ERR,TAG_LOG" udpv6 parameters port is wrong! \r\n");
                return CMD_RET_FAILURE;
            }
        }
        else
        {
            os_printf(LM_CMD, LL_INFO, TAG_LOG" udpv6 parameters is wrong! \r\n");
            return CMD_RET_FAILURE;
        }
    }
    os_task_create("cmd_udpv6_c", 5, (4*1024), cmd_udp_client_start, (void*)AF_INET6);

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(socket, udpv6, cmd_udpv6_client, "test udpv6 client", "socket udpv6");

static int cmd_udpv6_stop_client( int argc, char *argv[])
{
    if(!UDPV6_FUNC_ISSET_START(socket_coninfo))
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" udpv6 client has not started \r\n");
        return CMD_RET_FAILURE;
    }
    if (argc != 1)
    {
        os_printf(LM_CMD, LL_ERR, TAG_LOG" Stop udpv6 parameters numbers is wrong! \r\n");
        return CMD_RET_FAILURE;
    }
    udp_state_flag = 0;
    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(socket, udpv6_stop, cmd_udpv6_stop_client, " test udpv6_stop client", "socket udpv6_stop");
#endif