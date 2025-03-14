/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2020-01-10 23:45:59
 * @LastEditTime: 2020-04-25 17:50:58
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#include "platform_net_socket.h"
#include "http_log.h"

int platform_net_socket_connect(const char *host, const char *port, int proto)
{
    int fd, ret = HTTP_SOCKET_UNKNOWN_HOST_ERROR;
    struct addrinfo hints, *addr_list, *cur;
    
    /* Do name resolution with both IPv6 and IPv4 */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = (proto == PLATFORM_NET_PROTO_UDP) ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = (proto == PLATFORM_NET_PROTO_UDP) ? IPPROTO_UDP : IPPROTO_TCP;

    if (getaddrinfo(host, port, &hints, &addr_list) != 0) {
        return ret;
    }
    
    for (cur = addr_list; cur != NULL; cur = cur->ai_next) {
        fd = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        if (fd < 0) {
            ret = HTTP_SOCKET_FAILED_ERROR;
            continue;
        }

        if (connect(fd, cur->ai_addr, cur->ai_addrlen) == 0) {
            ret = fd;
            break;
        }

        platform_net_socket_close(fd);
        ret = HTTP_CONNECT_FAILED_ERROR;
    }

    freeaddrinfo(addr_list);

    return ret;
}

int platform_net_socket_recv(int fd, void *buf, size_t len, int flags)
{
    return recv(fd, buf, len, flags);
}

int platform_net_socket_recv_timeout(int fd, unsigned char *buf, int len, int timeout)
{
    int result = 0;
    fd_set read_set;

    struct timeval tv = {
        timeout / 1000, 
        (timeout % 1000) * 1000
    };
    
    if (tv.tv_sec < 0 || (tv.tv_sec == 0 && tv.tv_usec <= 0)) {
        tv.tv_sec = 0;
        tv.tv_usec = 100;
    }

    memset(&read_set, 0, sizeof(read_set));
    FD_SET(fd, &read_set);
    result = select(fd + 1, &read_set, NULL, NULL, &tv);
    if (result > 0) {
        return platform_net_socket_recv(fd, buf, (size_t)len, 0);
    } else {
        HTTP_LOG_E("\n%s[%d]fd %d timeout %d\n", __FUNCTION__, __LINE__, fd, timeout);
        return -1;
    }
}

int platform_net_socket_write(int fd, void *buf, size_t len)
{
    return send(fd, buf, len, 0);
}

int platform_net_socket_write_timeout(int fd, unsigned char *buf, int len, int timeout)
{
    struct timeval tv = {
        timeout / 1000, 
        (timeout % 1000) * 1000
    };

    if (tv.tv_sec < 0 || (tv.tv_sec == 0 && tv.tv_usec <= 0)) {
        tv.tv_sec = 0;
        tv.tv_usec = 100;
    }

    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv,sizeof(struct timeval));

    return send(fd, buf, len, 0);
}

int platform_net_socket_close(int fd)
{
    return closesocket(fd);
}

int platform_net_socket_set_block(int fd)
{
    unsigned long mode = 0;
    return ioctlsocket(fd, FIONBIO, &mode);
}

int platform_net_socket_set_nonblock(int fd)
{
    unsigned long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode);
}

int platform_net_socket_setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    return setsockopt(fd, level, optname, optval, optlen);
}

