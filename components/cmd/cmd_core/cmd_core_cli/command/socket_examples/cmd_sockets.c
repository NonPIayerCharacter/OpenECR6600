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

#if defined(CONFIG_CMD_SOCKET_EXAMPLE)
uint32_t socket_coninfo = 0;

CLI_CMD(socket, NULL,   "test socket",    "socket tcp/tcpv6/udp/udpv6/tcp_server/tcpv6_server");
#endif 


