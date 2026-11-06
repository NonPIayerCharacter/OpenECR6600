#include <string.h>
#include "transport.h"
#include "transport_ssl.h"
#include "sockets.h"
#include "sys.h"
#include "netdb.h"
#include "dns.h"

#include "ssl.h"
#include "entropy.h"
#include "ctr_drbg.h"
#include "error.h"
#include "certs.h"

#include "trs_tls.h"

typedef enum {
    TRANS_SSL_INIT = 0,
    TRANS_SSL_CONNECTING,
} transport_ssl_conn_state_t;

typedef struct {
    trs_tls_t               *tls;
    trs_tls_cfg_t            cfg;
    bool                     ssl_initialized;
    transport_ssl_conn_state_t conn_state;
} transport_ssl_t;

static int ssl_close(transport_handle_t t);

static int ssl_connect(transport_handle_t t, const char *host, int port, int timeout_ms)
{
    transport_ssl_t *ssl = transport_get_context_data(t);

    ssl->cfg.timeout_ms = timeout_ms;
    ssl->ssl_initialized = true;
    ssl->tls = trs_tls_conn_new(host, strlen(host), port, &ssl->cfg);
    if (!ssl->tls) {
        TRANS_LOGE("Failed to open a new connection");
        //transport_set_errors(t, ssl->tls->error_handle);
        //tls_conn_destroy(ssl->tls);
        ssl->tls = NULL;
        return -1;
    }

    return 0;
}

static int ssl_poll_read(transport_handle_t t, int timeout_ms)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    int ret = -1;
    #if 1 
    int remain = 0;
    struct timeval timeout;
    fd_set readset;
    fd_set errset;
    FD_ZERO(&readset);
    FD_ZERO(&errset);
    FD_SET(ssl->tls->sockfd, &readset);
    FD_SET(ssl->tls->sockfd, &errset);
    if ((remain = trs_tls_get_bytes_avail(ssl->tls)) > 0) {
        TRANS_LOGD("remain data in cache, need to read again");
        return remain;
    }
    ms_to_timeval(timeout_ms, &timeout);
    ret = select(ssl->tls->sockfd + 1, &readset, NULL, &errset, &timeout);
    if (ret > 0 && FD_ISSET(ssl->tls->sockfd, &errset)) {
        int sock_errno = 0;
        uint32_t optlen = sizeof(sock_errno);
        getsockopt(ssl->tls->sockfd, SOL_SOCKET, SO_ERROR, &sock_errno, &optlen);
        TRANS_LOGE("ssl_poll_read select error %d, errno = %s, fd = %d", sock_errno, strerror(sock_errno), ssl->tls->sockfd);
        ret = -1;
    }
    #endif
    return ret;
}

static int ssl_poll_write(transport_handle_t t, int timeout_ms)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    int ret = -1;
    #if 1
    struct timeval timeout;
    fd_set writeset;
    fd_set errset;
    FD_ZERO(&writeset);
    FD_ZERO(&errset);
    FD_SET(ssl->tls->sockfd, &writeset);
    FD_SET(ssl->tls->sockfd, &errset);
    ms_to_timeval(timeout_ms, &timeout);
    ret = select(ssl->tls->sockfd + 1, NULL, &writeset, &errset, &timeout);
    if (ret > 0 && FD_ISSET(ssl->tls->sockfd, &errset)) {
        int sock_errno = 0;
        uint32_t optlen = sizeof(sock_errno);
        getsockopt(ssl->tls->sockfd, SOL_SOCKET, SO_ERROR, &sock_errno, &optlen);
        TRANS_LOGE("ssl_poll_write select error %d, errno = %s, fd = %d", sock_errno, strerror(sock_errno), ssl->tls->sockfd);
        ret = -1;
    }
    #endif
    return ret;
}

static int ssl_write(transport_handle_t t, const char *buffer, int len, int timeout_ms)
{
    int poll, ret;
    transport_ssl_t *ssl = transport_get_context_data(t);

    if ((poll = transport_poll_write(t, timeout_ms)) <= 0) {
        TRANS_LOGW("Poll timeout or error, errno=%s, fd=%d, timeout_ms=%d", strerror(errno), ssl->tls->sockfd, timeout_ms);
        return poll;
    }
    ret = trs_tls_conn_write(ssl->tls, (const unsigned char *) buffer, len);
    if (ret < 0) {
        TRANS_LOGE("tls_conn_write error, errno=%s", strerror(errno));
        //transport_set_errors(t, ssl->tls->error_handle);
    }
    return ret;
}


static int ssl_read(transport_handle_t t, char *buffer, int len, int timeout_ms)
{
    int poll, ret;
    #if 1 
    transport_ssl_t *ssl = transport_get_context_data(t);

    poll = transport_poll_read(t, timeout_ms);
    if (poll <= 0) {
        return poll;
    }
    ret = trs_tls_conn_read(ssl->tls, (unsigned char *)buffer, len);
    if (ret < 0) {
        TRANS_LOGE("tls_conn_read error, errno=%s", strerror(errno));
        //transport_set_errors(t, ssl->tls->error_handle);
    }
    if (ret == 0) {
        ret = -1;
    }
    #endif
    return ret;
}

static int ssl_close(transport_handle_t t)
{
    int ret = -1;
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (ssl->ssl_initialized) {
        trs_tls_conn_delete(ssl->tls);
        ssl->conn_state = TRANS_SSL_INIT;
        ssl->ssl_initialized = false;
    }
    return ret;
}

static int ssl_destroy(transport_handle_t t)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    transport_close(t);
    os_free(ssl);
    return 0;
}

void transport_ssl_enable_global_ca_store(transport_handle_t t)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (t && ssl) {
        ssl->cfg.use_global_ca_store = true;
    }
}

void transport_ssl_set_psk_key_hint(transport_handle_t t, const psk_hint_key_t* psk_hint_key)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (t && ssl) {
        ssl->cfg.psk_hint_key =  psk_hint_key;
    }
}

void transport_ssl_set_cert_data(transport_handle_t t, const char *data, int len)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (t && ssl) {
        ssl->cfg.cacert_pem_buf = (void *)data;
        ssl->cfg.cacert_pem_bytes = len + 1;
    }
}

void transport_ssl_set_cert_data_der(transport_handle_t t, const char *data, int len)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (t && ssl) {
        ssl->cfg.cacert_buf = (void *)data;
        ssl->cfg.cacert_bytes = len;
    }
}

void transport_ssl_set_client_cert_data(transport_handle_t t, const char *data, int len)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (t && ssl) {
        ssl->cfg.clientcert_pem_buf = (void *)data;
        ssl->cfg.clientcert_pem_bytes = len + 1;
    }
}

void transport_ssl_set_client_cert_data_der(transport_handle_t t, const char *data, int len)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (t && ssl) {
        ssl->cfg.clientcert_buf = (void *)data;
        ssl->cfg.clientcert_bytes = len;
    }
}

void transport_ssl_set_client_key_data(transport_handle_t t, const char *data, int len)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (t && ssl) {
        ssl->cfg.clientkey_pem_buf = (void *)data;
        ssl->cfg.clientkey_pem_bytes = len + 1;
    }
}

void transport_ssl_set_client_key_password(transport_handle_t t, const char *password, int password_len)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (t && ssl) {
        ssl->cfg.clientkey_password = (void *)password;
        ssl->cfg.clientkey_password_len = password_len;
    }
}

void transport_ssl_set_client_key_data_der(transport_handle_t t, const char *data, int len)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (t && ssl) {
        ssl->cfg.clientkey_buf = (void *)data;
        ssl->cfg.clientkey_bytes = len;
    }
}

void transport_ssl_set_alpn_protocol(transport_handle_t t, const char **alpn_protos)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (t && ssl) {
        ssl->cfg.alpn_protos = alpn_protos;
    }
}

void transport_ssl_skip_common_name_check(transport_handle_t t)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (t && ssl) {
        ssl->cfg.skip_common_name = true;
    }
}

void transport_ssl_set_common_name(transport_handle_t t, const char *common_name)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (t && ssl) {
          ssl->cfg.common_name = common_name;
    } 
}

void transport_ssl_use_secure_element(transport_handle_t t)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (t && ssl) {
        ssl->cfg.use_secure_element = true;
    }
}

static int ssl_get_socket(transport_handle_t t)
{
    if (t) {
        transport_ssl_t *ssl = (transport_ssl_t *)t->data;
        if (ssl && ssl->tls ) {
           return ssl->tls->sockfd;
        }
    }
    return -1;
}

void transport_ssl_set_ds_data(transport_handle_t t, void *ds_data)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (t && ssl) {
        ssl->cfg.ds_data = ds_data;
    }
}

void transport_ssl_set_keep_alive(transport_handle_t t, transport_keep_alive_t *keep_alive_cfg)
{
    transport_ssl_t *ssl = transport_get_context_data(t);
    if (t && ssl) {
        ssl->cfg.keep_alive_cfg = (tls_keep_alive_cfg_t *)keep_alive_cfg;
    }
}

transport_handle_t transport_ssl_init(void)
{
    transport_handle_t t = transport_init();
    transport_ssl_t *ssl = calloc(1, sizeof(transport_ssl_t));
    TRANSPORT_MEM_CHECK(ssl, {
        transport_destroy(t);
        return NULL;
    });
    
    transport_set_context_data(t, ssl);
    transport_set_func(t, ssl_connect, ssl_read, ssl_write, ssl_close, ssl_poll_read, ssl_poll_write, ssl_destroy);
   // transport_set_async_connect_func(t, ssl_connect_async);
    t->_get_socket = ssl_get_socket;
    return t;
}
