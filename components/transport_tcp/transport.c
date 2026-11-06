#include <string.h>

#include "transport.h"

/**
 * Transport layer error structure including
 * * tls last error storage
 * * sock-errno
 */
struct transport_error_s {
    struct tls_last_error tls_err_h_base;   /*!< tls last error container */
    // additional fields
    int    sock_errno;                              /*!< last socket error captured for this transport */
};

/**
 * This list will hold all transport available
 */
STAILQ_HEAD(transport_list_t, transport_item_t);

/**
 * Internal transport structure holding list of transports and other data common to all transports
 */
typedef struct transport_internal {
    struct transport_list_t list;                      /*!< List of transports */
    struct transport_error_s*  error_handle;           /*!< Pointer to the transport error container */
} transport_internal_t;

static transport_handle_t transport_get_default_parent(transport_handle_t t)
{
    /*
    * By default, the underlying transport layer handle is the handle itself
    */
    return t;
}

transport_list_handle_t transport_list_init(void)
{
    transport_list_handle_t transport = calloc(1, sizeof(transport_internal_t));
    TRANSPORT_MEM_CHECK(transport, return NULL);
    STAILQ_INIT(&transport->list);
    transport->error_handle = calloc(1, sizeof(struct transport_error_s));
    return transport;
}

ts_err_t transport_list_add(transport_list_handle_t h, transport_handle_t t, const char *scheme)
{
    if (h == NULL || t == NULL) {
        return ERR_INVALID_ARG;
    }
    t->scheme = calloc(1, strlen(scheme) + 1);
    TRANSPORT_MEM_CHECK(t->scheme, return ERR_NO_MEM);
    strcpy(t->scheme, scheme);
    STAILQ_INSERT_TAIL(&h->list, t, next);
    // Each transport in a list to share the same error tracker
    t->error_handle = h->error_handle;
    return TRANS_OK;
}

transport_handle_t transport_list_get_transport(transport_list_handle_t h, const char *scheme)
{
    if (!h) {
        return NULL;
    }
    if (scheme == NULL) {
        return STAILQ_FIRST(&h->list);
    }
    transport_handle_t item;
    STAILQ_FOREACH(item, &h->list, next) {
        if (strcasecmp(item->scheme, scheme) == 0) {
            return item;
        }
    }
    return NULL;
}

ts_err_t transport_list_destroy(transport_list_handle_t h)
{
    transport_list_clean(h);
    os_free(h->error_handle);
    os_free(h);
    return TRANS_OK;
}

ts_err_t transport_list_clean(transport_list_handle_t h)
{
    transport_handle_t item = STAILQ_FIRST(&h->list);
    transport_handle_t tmp;
    while (item != NULL) {
        tmp = STAILQ_NEXT(item, next);
        transport_destroy(item);
        item = tmp;
    }
    STAILQ_INIT(&h->list);
    return TRANS_OK;
}

transport_handle_t transport_init(void)
{
    transport_handle_t t = calloc(1, sizeof(struct transport_item_t));
    TRANSPORT_MEM_CHECK(t, return NULL);
    return t;
}

transport_handle_t transport_get_payload_transport_handle(transport_handle_t t)
{
    if (t && t->_read) {
        return t->_parent_transfer(t);
    }
    return NULL;
}

ts_err_t transport_destroy(transport_handle_t t)
{
    if (t->_destroy) {
        t->_destroy(t);
    }
    if (t->scheme) {
        os_free(t->scheme);
    }
    os_free(t);
    return TRANS_OK;
}

int transport_connect(transport_handle_t t, const char *host, int port, int timeout_ms)
{
    int ret = TRANS_FAIL;
    if (t && t->_connect) {
        return t->_connect(t, host, port, timeout_ms);
    }
    return ret;
}

int transport_connect_async(transport_handle_t t, const char *host, int port, int timeout_ms)
{
    int ret = TRANS_FAIL;
    if (t && t->_connect_async) {
        return t->_connect_async(t, host, port, timeout_ms);
    }
    return ret;
}

int transport_read(transport_handle_t t, char *buffer, int len, int timeout_ms)
{
    if (t && t->_read) {
        return t->_read(t, buffer, len, timeout_ms);
    }
    return TRANS_FAIL;
}

int transport_write(transport_handle_t t, const char *buffer, int len, int timeout_ms)
{
    if (t && t->_write) {
        return t->_write(t, buffer, len, timeout_ms);
    }
    return TRANS_FAIL;
}

int transport_poll_read(transport_handle_t t, int timeout_ms)
{
    if (t && t->_poll_read) {
        return t->_poll_read(t, timeout_ms);
    }
    return TRANS_FAIL;
}

int transport_poll_write(transport_handle_t t, int timeout_ms)
{
    if (t && t->_poll_write) {
        return t->_poll_write(t, timeout_ms);
    }
    return TRANS_FAIL;
}

int transport_close(transport_handle_t t)
{
    if (t && t->_close) {
        return t->_close(t);
    }
    return TRANS_OK;
}

void *transport_get_context_data(transport_handle_t t)
{
    if (t) {
        return t->data;
    }
    return NULL;
}

ts_err_t transport_set_context_data(transport_handle_t t, void *data)
{
    if (t) {
        t->data = data;
        return TRANS_OK;
    }
    return TRANS_FAIL;
}

ts_err_t transport_set_func(transport_handle_t t,
                             connect_func _connect,
                             io_read_func _read,
                             io_func _write,
                             trans_func _close,
                             poll_func _poll_read,
                             poll_func _poll_write,
                             trans_func _destroy)
{
    if (t == NULL) {
        return TRANS_FAIL;
    }
    t->_connect = _connect;
    t->_read = _read;
    t->_write = _write;
    t->_close = _close;
    t->_poll_read = _poll_read;
    t->_poll_write = _poll_write;
    t->_destroy = _destroy;
    t->_connect_async = NULL;
    t->_parent_transfer = transport_get_default_parent;
    return TRANS_OK;
}

int transport_get_default_port(transport_handle_t t)
{
    if (t == NULL) {
        return -1;
    }
    return t->port;
}

ts_err_t transport_set_default_port(transport_handle_t t, int port)
{
    if (t == NULL) {
        return TRANS_FAIL;
    }
    t->port = port;
    return TRANS_OK;
}

ts_err_t transport_set_async_connect_func(transport_handle_t t, connect_async_func _connect_async_func)
{
    if (t == NULL) {
        return TRANS_FAIL;
    }
    t->_connect_async = _connect_async_func;
    return TRANS_OK;
}

ts_err_t transport_set_parent_transport_func(transport_handle_t t, payload_transfer_func _parent_transport)
{
    if (t == NULL) {
        return TRANS_FAIL;
    }
    t->_parent_transfer = _parent_transport;
    return TRANS_OK;
}

tls_error_handle_t transport_get_error_handle(transport_handle_t t)
{
    if (t) {
        return &t->error_handle->tls_err_h_base;
    }
    return NULL;
}

int transport_get_errno(transport_handle_t t)
{
    if (t && t->error_handle) {
        int actual_errno = t->error_handle->sock_errno;
        t->error_handle->sock_errno = 0;
        return actual_errno;
    }
    return TRANS_FAIL;
}

void transport_set_errors(transport_handle_t t, const tls_error_handle_t error_handle)
{
    if (t && t->error_handle) {
        memcpy(&t->error_handle->tls_err_h_base, error_handle, sizeof(tls_last_error_t));
        /*int sock_error;
        if (tls_get_and_clear_error_type(error_handle, TLS_ERR_TYPE_SYSTEM, &sock_error) == TRANS_OK) {
            t->error_handle->sock_errno = sock_error;
        }*/
    }
}

void transport_capture_errno(transport_handle_t t, int sock_errno)
{
    if (t && t->error_handle) {
        t->error_handle->sock_errno = sock_errno;
    }
}

int transport_get_socket(transport_handle_t t)
{
    if (t && t->_get_socket)  {
        return  t->_get_socket(t);
    }
    return TRANS_FAIL;
}
