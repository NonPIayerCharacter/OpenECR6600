#include "sockets.h"
#include "dns.h"
#include "netdb.h"

#include "transport.h"
#include "transport_tcp.h"

typedef struct {
    int sock;
} transport_tcp_t;

static int resolve_dns(const char *host, struct sockaddr_in *ip)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;

    int err = getaddrinfo(host, NULL, &hints, &res);
    if(err != 0 || res == NULL) {
        TRANS_LOGE("DNS lookup failed err=%d res=%p", err, res);
        return TRANS_FAIL;
    }
    ip->sin_family = AF_INET;
    memcpy(&ip->sin_addr, &((struct sockaddr_in *)(res->ai_addr))->sin_addr, sizeof(ip->sin_addr));
    freeaddrinfo(res);
    return TRANS_OK;
}

static int tcp_enable_keep_alive(int fd, transport_keep_alive_t *keep_alive_cfg)
{
    int keep_alive_enable = 1;
    int keep_alive_idle = keep_alive_cfg->keep_alive_idle;
    int keep_alive_interval = keep_alive_cfg->keep_alive_interval;
    int keep_alive_count = keep_alive_cfg->keep_alive_count;

    TRANS_LOGD("Enable TCP keep alive. idle: %d, interval: %d, count: %d", keep_alive_idle, keep_alive_interval, keep_alive_count);
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive_enable, sizeof(keep_alive_enable)) != 0) {
        TRANS_LOGE("Fail to setsockopt SO_KEEPALIVE");
        return -1;
    }
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keep_alive_idle, sizeof(keep_alive_idle)) != 0) {
        TRANS_LOGE("Fail to setsockopt TCP_KEEPIDLE");
        return -1;
    }
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keep_alive_interval, sizeof(keep_alive_interval)) != 0) {
        TRANS_LOGE("Fail to setsockopt TCP_KEEPINTVL");
        return -1;
    }
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keep_alive_count, sizeof(keep_alive_count)) != 0) {
        TRANS_LOGE("Fail to setsockopt TCP_KEEPCNT");
        return -1;
    }

    return 0;
}

static void tcp_set_socket_timeout(int sock, int option, int timeout_ms)
{
#if LWIP_SO_SNDRCVTIMEO_NONSTANDARD
    int timeout = timeout_ms < 0 ? 0 : timeout_ms;

    setsockopt(sock, SOL_SOCKET, option, &timeout, sizeof(timeout));
#else
    struct timeval timeout = { 0 };

    if (timeout_ms >= 0) {
        ms_to_timeval(timeout_ms, &timeout);
    }
    setsockopt(sock, SOL_SOCKET, option, &timeout, sizeof(timeout));
#endif
}

static int tcp_connect(transport_handle_t t, const char *host, int port, int timeout_ms)
{
    struct sockaddr_in remote_ip;
    struct timeval tv = { 0 };
    transport_tcp_t *tcp = transport_get_context_data(t);

    bzero(&remote_ip, sizeof(struct sockaddr_in));

    //if stream_host is not ip address, resolve it AF_INET,servername,&serveraddr.sin_addr
    if (inet_pton(AF_INET, host, &remote_ip.sin_addr) != 1) {
        if (resolve_dns(host, &remote_ip) < 0) {
            TRANS_LOGE("host:%s",host);
            return -1;
        }
    }

    tcp->sock = socket(PF_INET, SOCK_STREAM, 0);
    TRANS_LOGE("[sock=%d] ", tcp->sock);
    if (tcp->sock < 0) {
        TRANS_LOGE("Error create socket");
        return -1;
    }

    remote_ip.sin_family = AF_INET;
    remote_ip.sin_port = htons(port);

    ms_to_timeval(timeout_ms, &tv);
    //transport_utils_ms_to_timeval(timeout_ms, &tv); // if timeout=-1, tv is unchanged, 0, i.e. waits forever

    tcp_set_socket_timeout(tcp->sock, SO_RCVTIMEO, timeout_ms);
    tcp_set_socket_timeout(tcp->sock, SO_SNDTIMEO, timeout_ms);
    // Set socket keep-alive option
    if (t->keep_alive_cfg && t->keep_alive_cfg->keep_alive_enable) {
        if (tcp_enable_keep_alive(tcp->sock, t->keep_alive_cfg) < 0) {
            TRANS_LOGE("Error to set tcp [socket=%d] keep-alive", tcp->sock);
            goto error;
        }
    }
    // Set socket to non-blocking
    int flags;
    if ((flags = fcntl(tcp->sock, F_GETFL, 0)) < 0) {
        TRANS_LOGE("[sock=%d] get file flags error: %s", tcp->sock, strerror(errno));
        goto error;
    }
    // this is bug of gcc,the non_block value is set O_NONBLOCK in sockets.h
   int non_block = 1;
    if (fcntl(tcp->sock, F_SETFL, flags |= non_block) < 0) {
        TRANS_LOGE("[sock=%d] %x set nonblocking error: %s", tcp->sock, non_block,strerror(errno));
        goto error;
    }

    TRANS_LOGD("[sock=%d] Connecting to server. IP: %s, Port: %d",
            tcp->sock, ipaddr_ntoa((const ip_addr_t*)&remote_ip.sin_addr.s_addr), port);

    if (connect(tcp->sock, (struct sockaddr *)(&remote_ip), sizeof(struct sockaddr)) < 0) {
        if (errno == EINPROGRESS) {
            fd_set fdset;

            //transport_utils_ms_to_timeval(timeout_ms, &tv);
            ms_to_timeval(timeout_ms, &tv);
            FD_ZERO(&fdset);
            FD_SET(tcp->sock, &fdset);

            int res = select(tcp->sock+1, NULL, &fdset, NULL, &tv);
            if (res < 0) {
                TRANS_LOGE("[sock=%d] select() error: %s", tcp->sock, strerror(errno));
                transport_capture_errno(t, errno);
                goto error;
            }
            else if (res == 0) {
                TRANS_LOGE("[sock=%d] select() timeout", tcp->sock);
                transport_capture_errno(t, EINPROGRESS);    // errno=EINPROGRESS indicates connection timeout
                goto error;
            } else {
                int sockerr;
                socklen_t len = (socklen_t)sizeof(int);

                if (getsockopt(tcp->sock, SOL_SOCKET, SO_ERROR, (void*)(&sockerr), &len) < 0) {
                    TRANS_LOGE("[sock=%d] getsockopt() error: %s", tcp->sock, strerror(errno));
                    goto error;
                }
                else if (sockerr) {
                    transport_capture_errno(t, sockerr);
                    TRANS_LOGE("[sock=%d] delayed connect error: %s", tcp->sock, strerror(sockerr));
                    goto error;
                }
            }
        } else {
            TRANS_LOGE("[sock=%d] connect() error: %s", tcp->sock, strerror(errno));
            goto error;
        }
    }
    // Reset socket to blocking
    if ((flags = fcntl(tcp->sock, F_GETFL, 0)) < 0) {
        TRANS_LOGE("[sock=%d] get file flags error: %s", tcp->sock, strerror(errno));
        goto error;
    }
    if (fcntl(tcp->sock, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        TRANS_LOGE("[sock=%d] reset blocking error: %s", tcp->sock, strerror(errno));
        goto error;
    }
    return tcp->sock;
error:
    close(tcp->sock);
    tcp->sock = -1;
    return -1;
}

static int tcp_write(transport_handle_t t, const char *buffer, int len, int timeout_ms)
{
    int poll;
    uint32_t started_ms = (uint32_t)os_time_get();
    transport_tcp_t *tcp = transport_get_context_data(t);
    if ((poll = transport_poll_write(t, timeout_ms)) <= 0) {
        return poll;
    }
    if (timeout_ms == 0) {
        /* SO_SNDTIMEO=0 means infinite blocking, so use a nonblocking send instead. */
        return send(tcp->sock, buffer, len, MSG_DONTWAIT);
    }
    if (timeout_ms > 0) {
        uint32_t elapsed_ms = (uint32_t)os_time_get() - started_ms;
        if (elapsed_ms >= (uint32_t)timeout_ms) {
            return 0;
        }
        timeout_ms -= (int)elapsed_ms;
    }
    /* Waiting for writability and sending must share the same timeout budget. */
    tcp_set_socket_timeout(tcp->sock, SO_SNDTIMEO, timeout_ms);

    return write(tcp->sock, buffer, len);
}

static int tcp_read(transport_handle_t t, char *buffer, int len, int timeout_ms)
{
    transport_tcp_t *tcp = transport_get_context_data(t);
    int poll = -1;
    if ((poll = transport_poll_read(t, timeout_ms)) <= 0) {
        return poll;
    }
    tcp_set_socket_timeout(tcp->sock, SO_RCVTIMEO, timeout_ms);

    int read_len = read(tcp->sock, buffer, len);
    if (read_len == 0) {
        return -1;
    }
    return read_len;
}

static int tcp_poll_read(transport_handle_t t, int timeout_ms)
{
    transport_tcp_t *tcp = transport_get_context_data(t);
    int ret = -1;
    struct timeval timeout;
    fd_set readset;
    fd_set errset;
    FD_ZERO(&readset);
    FD_ZERO(&errset);
    FD_SET(tcp->sock, &readset);
    FD_SET(tcp->sock, &errset);
    ms_to_timeval(timeout_ms, &timeout);
    ret = select(tcp->sock + 1, &readset, NULL, &errset,&timeout);
    if (ret > 0 && FD_ISSET(tcp->sock, &errset)) {
        int sock_errno = 0;
        uint32_t optlen = sizeof(sock_errno);
        getsockopt(tcp->sock, SOL_SOCKET, SO_ERROR, &sock_errno, &optlen);
        transport_capture_errno(t, sock_errno);
        TRANS_LOGE("tcp_poll_read select error %d, errno = %s, fd = %d", sock_errno, strerror(sock_errno), tcp->sock);
        ret = -1;
    }
    return ret;
}

static int tcp_poll_write(transport_handle_t t, int timeout_ms)
{
    transport_tcp_t *tcp = transport_get_context_data(t);
    int ret = -1;
    struct timeval timeout;
    fd_set writeset;
    fd_set errset;
    FD_ZERO(&writeset);
    FD_ZERO(&errset);
    FD_SET(tcp->sock, &writeset);
    FD_SET(tcp->sock, &errset);
    ms_to_timeval(timeout_ms, &timeout);
    ret = select(tcp->sock + 1, NULL, &writeset, &errset, &timeout);
    if (ret > 0 && FD_ISSET(tcp->sock, &errset)) {
        int sock_errno = 0;
        uint32_t optlen = sizeof(sock_errno);
        getsockopt(tcp->sock, SOL_SOCKET, SO_ERROR, &sock_errno, &optlen);
        transport_capture_errno(t, sock_errno);
        TRANS_LOGE("tcp_poll_write select error %d, errno = %s, fd = %d", sock_errno, strerror(sock_errno), tcp->sock);
        ret = -1;
    }
    return ret;
}

static int tcp_close(transport_handle_t t)
{
    transport_tcp_t *tcp = transport_get_context_data(t);
    int ret = -1;
    if (tcp->sock >= 0) {
        ret = close(tcp->sock);
        tcp->sock = -1;
    }
    return ret;
}

static ts_err_t tcp_destroy(transport_handle_t t)
{
    transport_tcp_t *tcp = transport_get_context_data(t);
    transport_close(t);
    os_free(tcp);
    return 0;
}

static int tcp_get_socket(transport_handle_t t)
{
    if (t) {
        transport_tcp_t *tcp = t->data;
        if (tcp) {
            return tcp->sock;
        }
    }
    return -1;
}

void transport_tcp_set_keep_alive(transport_handle_t t, transport_keep_alive_t *keep_alive_cfg)
{
    if (t && keep_alive_cfg) {
        t->keep_alive_cfg = keep_alive_cfg;
    }
}

transport_handle_t transport_tcp_init(void)
{
    transport_handle_t t = transport_init();
    transport_tcp_t *tcp = calloc(1, sizeof(transport_tcp_t));
    TRANSPORT_MEM_CHECK(tcp, {
        transport_destroy(t);
        return NULL;
    });

    tcp->sock = -1;
    transport_set_func(t, tcp_connect, tcp_read, tcp_write, tcp_close, tcp_poll_read, tcp_poll_write, tcp_destroy);
    transport_set_context_data(t, tcp);
    t->_get_socket = tcp_get_socket;

    return t;
}
