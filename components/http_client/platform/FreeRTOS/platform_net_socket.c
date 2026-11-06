/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2020-01-10 23:45:59
 * @LastEditTime: 2020-04-25 17:50:58
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#include "platform_net_socket.h"
#include "http_log.h"

#define PLATFORM_NET_SOCKET_CONNECT_RETRY_MAX         2
#define PLATFORM_NET_SOCKET_CONNECT_RETRY_MS          1000
#define PLATFORM_NET_SOCKET_CONNECT_RETRY_BACKOFF_MS  100

static int platform_net_socket_restore_flags(int sockfd, int flags)
{
    if (fcntl(sockfd, F_SETFL, flags) == -1) {
        HTTP_LOG_E("restore socket %d flags failed, err %d", sockfd, errno);
        return -1;
    }

    return 0;
}

static int platform_net_socket_connect_get_remaining_timeout(u32_t start_ms, int timeoutms)
{
    u32_t elapsed_ms = sys_now() - start_ms;

    if (elapsed_ms >= (u32_t)timeoutms) {
        return 0;
    }

    return timeoutms - (int)elapsed_ms;
}

static int platform_net_socket_connect_should_retry(int err)
{
    switch (err) {
        case EINTR:
        case EAGAIN:
#if (EWOULDBLOCK != EAGAIN)
        case EWOULDBLOCK:
#endif
        case ETIMEDOUT:
        case ECONNABORTED:
        case ECONNREFUSED:
        case ENETDOWN:
        case ENETUNREACH:
        case EHOSTUNREACH:
            return 1;
        default:
            break;
    }

    return 0;
}

static int platform_net_socket_connect_timeout(int sockfd, const struct sockaddr *name, socklen_t namelen,
                                               int timeoutms, int *connect_err)
{
    int original_flags = fcntl(sockfd, F_GETFL, 0);
    int flags;
    int ret;

    if (connect_err != NULL) {
        *connect_err = 0;
    }

    if (original_flags == -1) {
        HTTP_LOG_E("get socket %d flags failed, err %d", sockfd, errno);
        if (connect_err != NULL) {
            *connect_err = errno;
        }
        return -1;
    }

    flags = original_flags;
    flags |= O_NONBLOCK;
    if (fcntl(sockfd, F_SETFL, flags) == -1) {
        HTTP_LOG_E("set socket %d nonblock failed, err %d", sockfd, errno);
        if (connect_err != NULL) {
            *connect_err = errno;
        }
        return -1;
    }

    ret = connect(sockfd, name, namelen);
    if (ret < 0) {
        struct timeval tv = {0};
        fd_set wset = {0};
        int so_error = 0;
        socklen_t so_error_len = sizeof(so_error);
        int err = errno;

        if ((err != EINPROGRESS) && (err != EWOULDBLOCK) && (err != EALREADY)) {
            HTTP_LOG_E("connect socket %d failed immediately, err %d", sockfd, err);
            (void)platform_net_socket_restore_flags(sockfd, original_flags);
            if (connect_err != NULL) {
                *connect_err = err;
            }
            return -1;
        }

        tv.tv_sec = timeoutms / 1000;
        tv.tv_usec = (timeoutms % 1000) * 1000;
        FD_ZERO(&wset);
        FD_SET(sockfd, &wset);
        ret = select(sockfd + 1, NULL, &wset, NULL, &tv);
        if (ret == 0) {
            HTTP_LOG_W("connect socket %d timeout after %d ms", sockfd, timeoutms);
            (void)platform_net_socket_restore_flags(sockfd, original_flags);
            if (connect_err != NULL) {
                *connect_err = ETIMEDOUT;
            }
            return -1;
        }
        if (ret < 0) {
            HTTP_LOG_E("select for socket %d connect failed, err %d", sockfd, errno);
            (void)platform_net_socket_restore_flags(sockfd, original_flags);
            if (connect_err != NULL) {
                *connect_err = errno;
            }
            return -1;
        }
        if (!FD_ISSET(sockfd, &wset)) {
            HTTP_LOG_E("socket %d connect select returned without writable event", sockfd);
            (void)platform_net_socket_restore_flags(sockfd, original_flags);
            if (connect_err != NULL) {
                *connect_err = EIO;
            }
            return -1;
        }
        if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) != 0) {
            HTTP_LOG_E("getsockopt SO_ERROR for socket %d failed, err %d", sockfd, errno);
            (void)platform_net_socket_restore_flags(sockfd, original_flags);
            if (connect_err != NULL) {
                *connect_err = errno;
            }
            return -1;
        }
        if (so_error != 0) {
            HTTP_LOG_E("socket %d connect failed, so_error %d", sockfd, so_error);
            (void)platform_net_socket_restore_flags(sockfd, original_flags);
            if (connect_err != NULL) {
                *connect_err = so_error;
            }
            return -1;
        }
    }

    if (platform_net_socket_restore_flags(sockfd, original_flags) != 0) {
        if (connect_err != NULL) {
            *connect_err = errno;
        }
        return -1;
    }

    return 0;
}

int platform_net_socket_connect(const char *host, const char *port, int proto, int timeoutms)
{
    int fd, ret = HTTP_SOCKET_UNKNOWN_HOST_ERROR;
    int gai_ret;
    u32_t connect_start_ms = 0;
    struct addrinfo hints, *addr_list, *cur;

    if ((host == NULL) || (port == NULL)) {
        HTTP_LOG_E("host or port is NULL");
        return HTTP_NULL_VALUE_ERROR;
    }

    /* Do name resolution with both IPv6 and IPv4 */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = (proto == PLATFORM_NET_PROTO_UDP) ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = (proto == PLATFORM_NET_PROTO_UDP) ? IPPROTO_UDP : IPPROTO_TCP;

    gai_ret = getaddrinfo(host, port, &hints, &addr_list);
    if (gai_ret != 0) {
        HTTP_LOG_E("resolve host %s:%s failed, err %d", host, port, gai_ret);
        return ret;
    }

    if (timeoutms > 0) {
        connect_start_ms = sys_now();
    }

    for (cur = addr_list; cur != NULL; cur = cur->ai_next) {
        int retry_count = 0;

        do {
            int connect_err = 0;
            int remaining_timeout = timeoutms;

            if (timeoutms > 0) {
                remaining_timeout = platform_net_socket_connect_get_remaining_timeout(connect_start_ms, timeoutms);
                if (remaining_timeout <= 0) {
                    HTTP_LOG_W("connect %s:%s total timeout after %d ms", host, port, timeoutms);
                    ret = HTTP_CONNECT_FAILED_ERROR;
                    goto exit;
                }
            }

            fd = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
            if (fd < 0) {
                HTTP_LOG_W("create socket for %s:%s failed, err %d", host, port, errno);
                ret = HTTP_SOCKET_FAILED_ERROR;
                break;
            }

            if (timeoutms > 0) {
                if (platform_net_socket_connect_timeout(fd, cur->ai_addr, cur->ai_addrlen,
                                                        remaining_timeout, &connect_err) == 0) {
                    ret = fd;
                    goto exit;
                }
            } else {
                if (connect(fd, cur->ai_addr, cur->ai_addrlen) == 0) {
                    ret = fd;
                    goto exit;
                }

                connect_err = errno;
                HTTP_LOG_W("connect %s:%s failed, err %d", host, port, connect_err);
            }

            platform_net_socket_close(fd);
            ret = HTTP_CONNECT_FAILED_ERROR;

            if ((timeoutms <= 0) ||
                (retry_count >= PLATFORM_NET_SOCKET_CONNECT_RETRY_MAX) ||
                !platform_net_socket_connect_should_retry(connect_err)) {
                break;
            }

            remaining_timeout = platform_net_socket_connect_get_remaining_timeout(connect_start_ms, timeoutms);
            if (remaining_timeout <= 0) {
                HTTP_LOG_W("connect %s:%s total timeout after %d ms", host, port, timeoutms);
                goto exit;
            }

            retry_count++;
            HTTP_LOG_W("retry connect %s:%s after err %d, retry %d/%d, remaining %d ms",
                       host, port, connect_err, retry_count, PLATFORM_NET_SOCKET_CONNECT_RETRY_MAX,
                       remaining_timeout);
            if (remaining_timeout > PLATFORM_NET_SOCKET_CONNECT_RETRY_MS) {
                os_msleep(PLATFORM_NET_SOCKET_CONNECT_RETRY_MS);
            } else {
                os_msleep(PLATFORM_NET_SOCKET_CONNECT_RETRY_BACKOFF_MS);
            }
        } while (1);
    }

exit:
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

    if (buf == NULL) {
        HTTP_LOG_E("recv buffer is NULL");
        return -1;
    }

    if (len <= 0) {
        return 0;
    }

    memset(&read_set, 0, sizeof(read_set));
    FD_SET(fd, &read_set);
    result = select(fd + 1, &read_set, NULL, NULL, &tv);
    if (result > 0) {
        if (!FD_ISSET(fd, &read_set)) {
            HTTP_LOG_E("recv select returned without socket %d readable", fd);
            return -1;
        }

        result = platform_net_socket_recv(fd, buf, (size_t)len, 0);
        if (result < 0) {
            HTTP_LOG_E("recv from socket %d failed, err %d", fd, errno);
        }

        return result;
    }

    if (result == 0) {
        HTTP_LOG_W("recv socket %d timeout after %d ms", fd, timeout);
    } else {
        HTTP_LOG_E("recv select on socket %d failed, err %d", fd, errno);
    }

    return -1;
}

int platform_net_socket_write(int fd, void *buf, size_t len)
{
    return send(fd, buf, len, 0);
}

int platform_net_socket_write_timeout(int fd, unsigned char *buf, int len, int timeout)
{
    int result;
    struct timeval tv = {
        timeout / 1000, 
        (timeout % 1000) * 1000
    };

    if (buf == NULL) {
        HTTP_LOG_E("write buffer is NULL");
        return -1;
    }

    if (len <= 0) {
        return 0;
    }

    if (tv.tv_sec < 0 || (tv.tv_sec == 0 && tv.tv_usec <= 0)) {
        tv.tv_sec = 0;
        tv.tv_usec = 100;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(struct timeval)) != 0) {
        HTTP_LOG_W("set socket %d send timeout failed, err %d", fd, errno);
    }

    result = send(fd, buf, len, 0);
    if (result < 0) {
        HTTP_LOG_E("send to socket %d failed, err %d", fd, errno);
    }

    return result;
}

int platform_net_socket_close(int fd)
{
    int ret = closesocket(fd);

    if (ret != 0) {
        HTTP_LOG_W("close socket %d failed, err %d", fd, errno);
    }

    return ret;
}

int platform_net_socket_set_block(int fd)
{
    unsigned long mode = 0;
    int ret = ioctlsocket(fd, FIONBIO, &mode);

    if (ret != 0) {
        HTTP_LOG_W("set socket %d block failed, err %d", fd, errno);
    }

    return ret;
}

int platform_net_socket_set_nonblock(int fd)
{
    unsigned long mode = 1;
    int ret = ioctlsocket(fd, FIONBIO, &mode);

    if (ret != 0) {
        HTTP_LOG_W("set socket %d nonblock failed, err %d", fd, errno);
    }

    return ret;
}

int platform_net_socket_setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    int ret = setsockopt(fd, level, optname, optval, optlen);

    if (ret != 0) {
        HTTP_LOG_W("setsockopt socket %d level %d opt %d failed, err %d",
                   fd, level, optname, errno);
    }

    return ret;
}
