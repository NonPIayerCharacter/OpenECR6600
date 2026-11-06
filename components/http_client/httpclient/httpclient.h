/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-09 21:31:25
 * @LastEditTime: 2020-06-02 22:36:15
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#ifndef _HTTPCLIENT_H_
#define _HTTPCLIENT_H_

#include <stdio.h>
#include <string.h>

#include "http_parser.h"
#include "http_utils.h"
#include "http_url_parser.h"
#include "http_interceptor.h"

#include "platform_list.h"
#include "platform_timer.h"
#include "platform_memory.h"
#include "http_defconfig.h"
#include "network.h"
#include "http_error.h"
#include "http_log.h"

typedef enum {
    HTTP_CLIENT_DATA_TYPE_STRING,
    HTTP_CLIENT_DATA_TYPE_BIN,
} http_client_post_dtype_e;

typedef struct {
    http_client_post_dtype_e dataType;
    uint32_t datalen;
    void *data;
} http_client_post_data_t;

typedef struct http_client {
    platform_list_t                         list;
    http_interceptor_t                  *interceptor;
    http_connect_params_t               *connect_params;
    http_request_method_t               method;
    uint32_t                            interest_event;
    http_event_t                        *event;
    size_t                              process;
    size_t                              total;
    http_client_post_data_t             postbuf;
    HTTP_GENERAL_FLAG;
} http_client_t;

/**
 * @brief Initialize an HTTP client instance.
 *
 * @param ca CA certificate in PEM format for HTTPS requests, or NULL for plain HTTP.
 * @return Pointer to an HTTP client instance on success, or NULL on failure.
 */
http_client_t *http_client_init(const char *ca);

/**
 * @brief Release an HTTP client instance.
 *
 * @param c HTTP client instance returned by http_client_init().
 */
void http_client_exit(http_client_t *c);
void http_client_release(http_client_t *c);
void http_client_set_interest_event(http_client_t *c, http_event_type_t event);
void http_client_set_method(http_client_t *c, http_request_method_t method);
void http_client_set_data(http_client_t *c, void *data, int len, http_client_post_dtype_e dtype);

/**
 * @brief Send an HTTP request with the selected method.
 *
 * @param c HTTP client instance returned by http_client_init().
 * @param method Request method.
 * @param url HTTP or HTTPS URL.
 * @param cb Event callback invoked while processing the request.
 * @return 0 on success, or non-zero on failure.
 */
int http_client_method_request(http_client_t *c, http_request_method_t method, const char *url, http_event_cb_t cb);

#endif /* _HTTPCLIENT_H_ */
