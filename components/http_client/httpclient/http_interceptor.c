/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2020-04-16 20:31:12
 * @LastEditTime: 2020-06-03 21:19:52
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */

#include <http_interceptor.h>
#include <http_log.h>
#include <http_error.h>
#include <http_utils.h>
#include <http_parser.h>
#include <http_url_parser.h>
#include <platform_memory.h>
#include <platform_timer.h>
#include <httpclient.h>

static const char *_http_interceptor_ca = NULL;

static int _http_read_buffer(http_interceptor_t *interceptor, size_t length)
{
    int len = 0;
    platform_timer_t timer;

    if ((0 == length) || (length > interceptor->buffer_len))
        length = interceptor->buffer_len;
    
    memset(interceptor->buffer, 0, interceptor->buffer_len);

    platform_timer_init(&timer);
    platform_timer_cutdown(&timer, interceptor->cmd_timeout);

    len = network_read(interceptor->network, (unsigned char *)interceptor->buffer, length, platform_timer_remain(&timer));

    RETURN_ERROR(len);
}

static int _http_write_buffer(http_interceptor_t *interceptor, unsigned char *buf, int length)
{
    int len = 0;
    int sent = 0;
    platform_timer_t timer;

    platform_timer_init(&timer);
    platform_timer_cutdown(&timer, interceptor->cmd_timeout);

    /* send http buffer in a blocking manner or exit when it timer is expired */
    while ((sent < length) && (!platform_timer_is_expired(&timer))) {
        len = network_write(interceptor->network, buf, length, platform_timer_remain(&timer));
        if (len <= 0)  // there was an error writing the data
            break;
        sent += len;
    }

    if (sent == length) {
        RETURN_ERROR(HTTP_SUCCESS_ERROR);
    }
    
    RETURN_ERROR(HTTP_SEND_MESSAGE_ERROR);
}

static void _http_interceptor_set_status(http_interceptor_t *interceptor, http_interceptor_status_t status)
{
    interceptor->status = status;
}

static http_interceptor_status_t _http_interceptor_get_status(http_interceptor_t *interceptor)
{
    return interceptor->status;
}

static int _http_interceptor_set_network(http_interceptor_t *interceptor)
{
    int res = HTTP_SUCCESS_ERROR;

    /* network init */
    if (NULL == interceptor->network) {
        interceptor->network = (network_t*) platform_memory_alloc(sizeof(network_t));
        HTTP_ROBUSTNESS_CHECK(interceptor->network, HTTP_MEM_NOT_ENOUGH_ERROR);
        memset(interceptor->network, 0, sizeof(network_t));
    }

    res = network_init( interceptor->network,
                        http_get_connect_params_host(interceptor->connect_params),
                        http_get_connect_params_port(interceptor->connect_params),
                        NULL);
    
    if (http_utils_ignore_case_match(http_get_connect_params_scheme(interceptor->connect_params), "https") == 0) {
        network_set_ca(interceptor->network, _http_interceptor_ca);
    }

    if (HTTP_SUCCESS_ERROR == res) {
        _http_interceptor_set_status(interceptor, http_interceptor_status_init);
    } else {
        _http_interceptor_set_status(interceptor, http_interceptor_status_release);
    }

    RETURN_ERROR(res);
}

static int _http_on_url(http_parser *parser, const char *at, size_t length)
{
    HTTP_LOG_D("%.*s :", (int)length, at);
    return 0;
}

static int _http_on_status(http_parser *parser, const char *at, size_t length)
{
    http_interceptor_t *interceptor = parser->data;
    http_response_set_status(&interceptor->response, parser->status_code);
#ifdef CONFIG_HTTP_REDIRECT_BUILD
    switch (parser->status_code) {
        case 301:
        case 302:
        case 303:
        case 307:
        case 308:
            interceptor->flag.flag_t.again = 1;
            break;
        default:
            break;
    }
#endif
    HTTP_LOG_D("http status:%d\n", parser->status_code);
    return 0;
}

static int _http_on_header_field(http_parser *parser, const char *at, size_t length)
{
    http_interceptor_t *interceptor = parser->data;
    HTTP_LOG_D("%.*s :", (int)length, at);

#ifdef CONFIG_HTTP_REDIRECT_BUILD
    if (interceptor->header_value) {
        interceptor->hdritems[0] = '\0';
        interceptor->header_value = 0;
        interceptor->flag.flag_t.redirects = 0;
    }
    size_t used = strlen(interceptor->hdritems);
    if (length > sizeof(interceptor->hdritems) - used - sizeof(": \r\n")) {
        return HTTP_FAILED_ERROR;
    }
    memcpy(interceptor->hdritems + used, at, length);
    interceptor->hdritems[used + length] = '\0';
    return 0;
#else
    if(0 == http_utils_ignore_case_nmatch(at, "Transfer-Encoding", 17)) {
        interceptor->flag.flag_t.chunked = 1;
    } else if(0 == http_utils_ignore_case_nmatch(at, "WWW-Authenticate", 16)) {
        interceptor->flag.flag_t.authenticate = 1;
    }

    memset(interceptor->hdritems, 0, sizeof(interceptor->hdritems));
    if (length > sizeof(interceptor->hdritems) - sizeof(": \r\n")) {
        length = sizeof(interceptor->hdritems) - sizeof(": \r\n");
    }

    if (length > 0) {
        memcpy(interceptor->hdritems, at, length);
    }
    snprintf(interceptor->hdritems + strlen(interceptor->hdritems), sizeof(interceptor->hdritems) - strlen(interceptor->hdritems), "%s", ": ");

    return 0;
    //return http_event_dispatch(interceptor->evetn, http_event_type_on_headers, interceptor, (void *)at, length);
#endif
}

static int _http_on_header_value(http_parser *parser, const char *at, size_t length)
{
    http_interceptor_t *interceptor = parser->data;

#ifdef CONFIG_HTTP_REDIRECT_BUILD
    if (!interceptor->header_value) {
        interceptor->flag.flag_t.redirects =
            http_utils_ignore_case_match(interceptor->hdritems, "Location") == 0;
        interceptor->flag.flag_t.chunked |=
            http_utils_ignore_case_match(interceptor->hdritems, "Transfer-Encoding") == 0;
        size_t used = strlen(interceptor->hdritems);
        snprintf(interceptor->hdritems + used, sizeof(interceptor->hdritems) - used, ": ");
        interceptor->header_value = 1;
    } else {
        size_t used = strlen(interceptor->hdritems);
        if (used >= 2) {
            interceptor->hdritems[used - 2] = '\0';
        }
    }
    if (interceptor->flag.flag_t.redirects) {
        size_t used = strlen(interceptor->redirect_location);
        if (length >= sizeof(interceptor->redirect_location) - used) {
            return HTTP_FAILED_ERROR;
        }
        memcpy(interceptor->redirect_location + used, at, length);
        interceptor->redirect_location[used + length] = '\0';
    }
#endif

    HTTP_LOG_D("%.*s\n", (int)length, at);
    size_t used = strlen(interceptor->hdritems);
    size_t available = sizeof(interceptor->hdritems) - used - sizeof("\r\n");
    if (length > available) {
        length = available;
    }
    memcpy(interceptor->hdritems + used, at, length);
    memcpy(interceptor->hdritems + used + length, "\r\n", sizeof("\r\n"));
#ifdef CONFIG_HTTP_REDIRECT_BUILD
    if (interceptor->flag.flag_t.again) {
        return HTTP_SUCCESS_ERROR;
    }
#endif
    return http_event_dispatch(interceptor->evetn, http_event_type_on_headers, interceptor, (void *)interceptor->hdritems, strlen((char *)interceptor->hdritems));
}

static int _http_on_headers_complete(http_parser *parser)
{
    http_interceptor_t *interceptor = parser->data;
    int ret;

    http_response_set_status(&interceptor->response, parser->status_code);
    http_response_set_offset(&interceptor->response, parser->nread);
    http_response_set_length(&interceptor->response, parser->content_length);

    //HTTP_LOG_I("_http_on_headers_complete, status=%d, offset=%ld, nread=%ld, content_length = %ld\n",
    //            parser->status_code, interceptor->response.offset, parser->nread, interceptor->response.length);

    ret = HTTP_SUCCESS_ERROR;
#ifdef CONFIG_HTTP_REDIRECT_BUILD
    if (!interceptor->flag.flag_t.again)
#endif
    ret = http_event_dispatch(interceptor->evetn, http_event_type_on_headers, interceptor, NULL, 0);
    if (ret == HTTP_EVENT_SKIP_BODY) {
        interceptor->flag.flag_t.keep_alive = 0;
        interceptor->flag.flag_t.complete = 1;
        _http_interceptor_set_status(interceptor, http_interceptor_status_headers_complete);
        /* Stop even when the same receive buffer also contains body bytes. */
        http_parser_pause(parser, 1);
        return 0;
    }
    if (ret == HTTP_SUCCESS_ERROR)
        _http_interceptor_set_status(interceptor, http_interceptor_status_headers_complete);
    else {
        _http_interceptor_set_status(interceptor, http_interceptor_status_release);
    }

    if (ret != HTTP_SUCCESS_ERROR) {
        return HTTP_FAILED_ERROR;
    }
    return interceptor->request.method == HTTP_REQUEST_METHOD_HEAD ? 1 : 0;
}

static int _http_on_message_begin(http_parser *parser)
{
    http_interceptor_t *interceptor = parser->data;
    HTTP_LOG_D("_http_on_message_begin");
    http_event_dispatch(interceptor->evetn, http_event_type_on_response, interceptor, NULL, 0);
    return 0;
}

static int _http_on_message_complete(http_parser *parser)
{
    http_interceptor_t *interceptor = parser->data;

    HTTP_LOG_D("_http_on_message_complete %d", interceptor->flag.flag_t.chunked);

    if (0!= interceptor->flag.flag_t.chunked)
        interceptor->flag.flag_t.chunked_complete = 1;

    interceptor->flag.flag_t.complete = 1;      /* complete */

    return 0;
}

static int _http_on_body(http_parser *parser, const char *at, size_t length)
{
    http_interceptor_t *interceptor = parser->data;

    // HTTP_LOG_W("data_process len: %d\n", (int)length);

#ifdef CONFIG_HTTP_REDIRECT_BUILD
    if (interceptor->flag.flag_t.again) {
        return HTTP_SUCCESS_ERROR;
    }
#endif
    interceptor->data_process += length;

    return http_event_dispatch(interceptor->evetn, http_event_type_on_body, interceptor, (void *)at, length);
}

static int _http_on_chunk_header(http_parser *parser)
{
    HTTP_LOG_D("_http_on_chunk_header");
    return 0;
}

static int _http_on_chunk_complete(http_parser *parser)
{
    HTTP_LOG_D("_http_on_chunk_complete");
    return 0;
}

static int _http_interceptor_parser_setting(http_interceptor_t *interceptor)
{
    interceptor->parser_settings->on_url = _http_on_url;
    interceptor->parser_settings->on_status = _http_on_status;
    interceptor->parser_settings->on_header_field = _http_on_header_field;
    interceptor->parser_settings->on_header_value = _http_on_header_value;
    interceptor->parser_settings->on_headers_complete = _http_on_headers_complete;
    interceptor->parser_settings->on_message_begin = _http_on_message_begin;
    interceptor->parser_settings->on_message_complete = _http_on_message_complete;
    interceptor->parser_settings->on_body = _http_on_body;
    interceptor->parser_settings->on_chunk_header = _http_on_chunk_header;
    interceptor->parser_settings->on_chunk_complete = _http_on_chunk_complete;
    interceptor->parser->data = interceptor;

    return 0;
}

static int _http_interceptor_prepare(http_interceptor_t *interceptor)
{
    if (NULL == interceptor->parser)
        interceptor->parser = platform_memory_alloc(sizeof(struct http_parser));

    if (NULL == interceptor->parser_settings)
        interceptor->parser_settings = platform_memory_alloc(sizeof(struct http_parser_settings));
    
    HTTP_ROBUSTNESS_CHECK((interceptor->parser && interceptor->parser_settings), HTTP_MEM_NOT_ENOUGH_ERROR);

    http_parser_init(interceptor->parser, HTTP_RESPONSE);
    _http_interceptor_parser_setting(interceptor);

    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

int http_interceptor_check_response(http_interceptor_t *interceptor)
{
    if (http_interceptor_status_headers_complete != _http_interceptor_get_status(interceptor))
        RETURN_ERROR(HTTP_FAILED_ERROR);

    if (0 != interceptor->flag.flag_t.complete) {
        interceptor->flag.flag_t.complete = 0;
        _http_interceptor_set_status(interceptor, http_interceptor_status_response_complete);
    }

#ifdef CONFIG_HTTP_REDIRECT_BUILD
    if (interceptor->flag.flag_t.again) {
        _http_interceptor_set_status(interceptor, http_interceptor_status_release);
        if (interceptor->redirect_location[0] == '\0') {
            RETURN_ERROR(HTTP_FAILED_ERROR);
        }
    }
#endif
    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

int http_interceptor_init(http_interceptor_t *interceptor)
{
    int res = HTTP_SUCCESS_ERROR;
    HTTP_ROBUSTNESS_CHECK(interceptor, HTTP_NULL_VALUE_ERROR);

    if (http_interceptor_status_invalid != _http_interceptor_get_status(interceptor))
        RETURN_ERROR(HTTP_SUCCESS_ERROR);

    if (NULL == interceptor->network) {
        interceptor->network = (network_t*) platform_memory_alloc(sizeof(network_t));
        if (interceptor->network != NULL) {
            memset(interceptor->network, 0, sizeof(network_t));
            interceptor->network->socket = -1;
        }
    }
    
    HTTP_ROBUSTNESS_CHECK(interceptor->network, HTTP_MEM_NOT_ENOUGH_ERROR);

    http_response_init(&interceptor->response);

    if(interceptor->cmd_timeout == 0)
       interceptor->cmd_timeout = HTTP_DEFAULT_CMD_TIMEOUT;

    if(interceptor->buffer_len == 0)   
       interceptor->buffer_len = HTTP_DEFAULT_BUF_SIZE;

    interceptor->network->timeout_ms = interceptor->cmd_timeout;
    interceptor->data_process = 0;
    interceptor->flag.all_flag = 0;
    interceptor->owner = NULL;
#ifdef CONFIG_HTTP_REDIRECT_BUILD
    interceptor->redirect_location[0] = '\0';
    interceptor->hdritems[0] = '\0';
    interceptor->header_value = 0;
#endif
    
    if (NULL == interceptor->evetn)
        interceptor->evetn = http_event_init();

    if (NULL == interceptor->buffer)
        interceptor->buffer = platform_memory_alloc(interceptor->buffer_len);

    HTTP_ROBUSTNESS_CHECK((interceptor->evetn && interceptor->buffer), HTTP_MEM_NOT_ENOUGH_ERROR);

    res = _http_interceptor_prepare(interceptor);
    if (HTTP_SUCCESS_ERROR != res)
        RETURN_ERROR(res);
    
    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}


void http_interceptor_set_ca(const char *ca)
{
    _http_interceptor_ca = ca;
}

void http_interceptor_set_owner(http_interceptor_t *interceptor, void *owner)
{
    HTTP_ROBUSTNESS_CHECK(interceptor, HTTP_VOID);
    if (http_interceptor_status_invalid != _http_interceptor_get_status(interceptor))
        return;

    interceptor->owner = owner;
}

void http_interceptor_event_register(http_interceptor_t *interceptor, http_event_cb_t cb)
{
    HTTP_ROBUSTNESS_CHECK(interceptor, HTTP_VOID);

    if (http_interceptor_status_invalid != _http_interceptor_get_status(interceptor))
        return;

    if (NULL == interceptor->evetn)
        interceptor->evetn = http_event_init();

    if (NULL != cb) {
        http_event_register(interceptor->evetn, cb);
    }
}

int http_interceptor_set_connect_params(http_interceptor_t *interceptor, http_connect_params_t *conn_param)
{
    HTTP_ROBUSTNESS_CHECK((interceptor && conn_param), HTTP_NULL_VALUE_ERROR);

    if (http_interceptor_status_invalid != _http_interceptor_get_status(interceptor))
        RETURN_ERROR(HTTP_FAILED_ERROR);


    interceptor->connect_params = conn_param;
    
    return _http_interceptor_set_network(interceptor);
}

int http_interceptor_connect(http_interceptor_t *interceptor)
{
    int res = HTTP_SUCCESS_ERROR;

    HTTP_ROBUSTNESS_CHECK(interceptor, HTTP_NULL_VALUE_ERROR);
    
    if (http_interceptor_status_init != _http_interceptor_get_status(interceptor))
        RETURN_ERROR(HTTP_FAILED_ERROR);

    res = network_connect(interceptor->network);

    if (HTTP_SUCCESS_ERROR == res) {
        _http_interceptor_set_status(interceptor, http_interceptor_status_connect);
        HTTP_LOG_D("interceptor connect success ...\n");
    } else {
        HTTP_LOG_D("interceptor connect fail ...\n");
        _http_interceptor_set_status(interceptor, http_interceptor_status_release);
    }

    RETURN_ERROR(res);
}

int http_interceptor_request(http_interceptor_t *interceptor, http_request_method_t mothod, const char *post_buf)
{
    int res = HTTP_SUCCESS_ERROR;
    HTTP_ROBUSTNESS_CHECK((interceptor && mothod), HTTP_NULL_VALUE_ERROR);

    if (http_interceptor_status_connect != _http_interceptor_get_status(interceptor))
        RETURN_ERROR(HTTP_FAILED_ERROR);
    
    http_request_init(&interceptor->request);

    if (NULL == interceptor->message)
        interceptor->message = http_message_buffer_init(0);

    if (http_interceptor_status_connect != _http_interceptor_get_status(interceptor))
        RETURN_ERROR(HTTP_SUCCESS_ERROR);

    http_request_set_method(&interceptor->request, mothod);

    http_event_dispatch(interceptor->evetn, http_event_type_on_request, interceptor, NULL, 0);

    if (NULL == http_get_connect_params_query(interceptor->connect_params)) {
        http_request_set_start_line(&interceptor->request, http_get_connect_params_path(interceptor->connect_params));
    } else {
        http_request_set_start_line_with_query(&interceptor->request, 
                                                http_get_connect_params_path(interceptor->connect_params), 
                                                http_get_connect_params_query(interceptor->connect_params));
    }

    http_request_header_init(&interceptor->request);
#ifdef CONFIG_HTTP_REDIRECT_BUILD
    char host[HTTP_REDIRECT_URL_SIZE];
    const char *hostname = http_get_connect_params_host(interceptor->connect_params);
    int hostlen = snprintf(host, sizeof(host), strchr(hostname, ':') ? "[%s]:%s" : "%s:%s",
                           hostname, http_get_connect_params_port(interceptor->connect_params));
    if (hostlen < 0 || hostlen >= sizeof(host)) {
        http_request_release(&interceptor->request);
        return HTTP_FAILED_ERROR;
    }
    http_request_add_header_form_key(&interceptor->request, "Host: ", host);
#else
    http_request_add_header_form_key(&interceptor->request, 
                                        "Host: ", 
                                        http_get_connect_params_host(interceptor->connect_params));
#endif
    
    {
        http_client_post_data_t *pdata = (http_client_post_data_t *)post_buf;

        if (pdata->datalen != 0) {
            HTTP_LOG_D("post buff %s len %d\n", (char *)pdata->data, pdata->datalen);
            if (pdata->datalen > HTTP_DEFAULT_BUF_SIZE) {
                http_request_set_body_form_pointer(&interceptor->request, pdata->data, pdata->datalen);
            } else {
                http_request_set_body(&interceptor->request, pdata->data, pdata->datalen);
            }
        } else {
            if (http_request_user_msg_header_check_contentlength() != 0) {
                http_request_add_header_form_key(&interceptor->request, "Content-Length: ", "0");
            }
        }
    }

    http_message_buffer_reinit(interceptor->message);

    http_message_buffer_cover(interceptor->message, 
                              http_request_get_start_line_data(&interceptor->request), 
                              http_request_get_header_data(&interceptor->request), 
                              HTTP_CRLF, NULL);                  

    http_message_buffer_append( interceptor->message, http_request_get_body_data(&interceptor->request), 
        http_message_buffer_get_used(interceptor->request.req_msg.body) - 1);
    res = _http_write_buffer(interceptor, (unsigned char *)http_message_buffer_get_data(interceptor->message), 
        http_message_buffer_get_used(interceptor->message) - 1);

    /* send data successfully, free the memory space of the request message*/
    http_request_release(&interceptor->request);
    
    if (HTTP_SUCCESS_ERROR == res) {

        _http_interceptor_set_status(interceptor, http_interceptor_status_request);

        if (interceptor->message) {
            http_message_buffer_release(interceptor->message);
            interceptor->message = NULL;
        }

    } else {
        _http_interceptor_set_status(interceptor, http_interceptor_status_release);
    }
    
    RETURN_ERROR(res);
}

int http_interceptor_fetch_headers(http_interceptor_t *interceptor)
{
    int len = 0;

    HTTP_ROBUSTNESS_CHECK(interceptor, 0);

    if (http_interceptor_status_request != _http_interceptor_get_status(interceptor))
        RETURN_ERROR(HTTP_FAILED_ERROR);

    _http_interceptor_set_status(interceptor, http_interceptor_status_response_headers);

    while (http_interceptor_status_response_headers == _http_interceptor_get_status(interceptor)) {
        len = _http_read_buffer(interceptor, interceptor->buffer_len);

        if (len <= 0) {
            _http_interceptor_set_status(interceptor, http_interceptor_status_release);
            RETURN_ERROR(len < 0 ? len : HTTP_FAILED_ERROR);
        } else {
            http_parser_execute(interceptor->parser, interceptor->parser_settings, interceptor->buffer, len);
            if (interceptor->parser->http_errno == HPE_PAUSED &&
                _http_interceptor_get_status(interceptor) == http_interceptor_status_headers_complete &&
                interceptor->flag.flag_t.complete) {
                RETURN_ERROR(HTTP_SUCCESS_ERROR);
            }
            if (interceptor->parser->http_errno) {
                _http_interceptor_set_status(interceptor, http_interceptor_status_release);
                RETURN_ERROR(HTTP_FAILED_ERROR);
            }
        }
    }

    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

int http_interceptor_submit_data(http_interceptor_t *interceptor)
{
    // HTTP_LOG_I("http_interceptor_submit_data:\n%s\n", http_response_get_message_body(&interceptor->response));

    // if ((http_interceptor_status_response_complete != _http_interceptor_get_status(interceptor)) || 
    //     (http_interceptor_status_response_body != _http_interceptor_get_status(interceptor)))
    //     RETURN_ERROR(HTTP_SUCCESS_ERROR);
    http_event_dispatch(interceptor->evetn, http_event_type_on_submit, interceptor, NULL, 0);

    if (interceptor->flag.flag_t.keep_alive) {
        _http_interceptor_set_status(interceptor, http_interceptor_status_connect);
    } else {
        _http_interceptor_set_status(interceptor, http_interceptor_status_release);
    }
    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

int http_interceptor_release(http_interceptor_t *interceptor)
{
    HTTP_ROBUSTNESS_CHECK(interceptor, HTTP_NULL_VALUE_ERROR);
    
    if (http_interceptor_status_release != _http_interceptor_get_status(interceptor))
        RETURN_ERROR(HTTP_FAILED_ERROR);

    if (interceptor->network) {
        network_release(interceptor->network);
        platform_memory_free(interceptor->network);
        interceptor->network = NULL;
    }

    if (interceptor->parser) {
        platform_memory_free(interceptor->parser);
        interceptor->parser = NULL;
    }
  
    if (interceptor->parser_settings) {
        platform_memory_free(interceptor->parser_settings);
        interceptor->parser_settings = NULL;
    }

    if (interceptor->message) {
        http_message_buffer_release(interceptor->message);
        interceptor->message = NULL;
    }

    if (interceptor->evetn) {
        http_event_release(interceptor->evetn);
        interceptor->evetn = NULL;
    }

    if (interceptor->buffer) {
        platform_memory_free(interceptor->buffer);
        interceptor->buffer = NULL;
    }

    // http_request_release(&interceptor->request);

    _http_interceptor_set_status(interceptor, http_interceptor_status_invalid);

    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

int http_interceptor_fetch_data(http_interceptor_t *interceptor)
{
    int len = 0;
    HTTP_ROBUSTNESS_CHECK(interceptor, len);

    if (http_interceptor_status_response_complete == _http_interceptor_get_status(interceptor))
        RETURN_ERROR(HTTP_SUCCESS_ERROR);
    
    if (http_interceptor_status_headers_complete != _http_interceptor_get_status(interceptor))
        RETURN_ERROR(HTTP_FAILED_ERROR);

    do {
        len = _http_read_buffer(interceptor, (http_response_get_length(&interceptor->response) - interceptor->data_process));
        
        if (len <= 0) {
            _http_interceptor_set_status(interceptor, http_interceptor_status_release);
            return HTTP_SOCKET_TIMEOUT_ERROR;
        } else {
            http_parser_execute(interceptor->parser, interceptor->parser_settings, interceptor->buffer, len);

            if (interceptor->parser->http_errno) {
                _http_interceptor_set_status(interceptor, http_interceptor_status_release);
                RETURN_ERROR(interceptor->parser->http_errno);
            }
        }

        if ((interceptor->flag.flag_t.chunked && interceptor->flag.flag_t.chunked_complete)) {
            break;
        }
    } while (interceptor->data_process < interceptor->response.length);

    _http_interceptor_set_status(interceptor, http_interceptor_status_response_complete);

    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

#ifdef CONFIG_HTTP_REDIRECT_BUILD
static void _http_redirect_remove_dot_segments(char *url)
{
    char *path = strstr(url, "://");
    if (path == NULL) {
        return;
    }
    path += 3;
    path += strcspn(path, "/?#");
    char *src = path;
    char *dst = path;
    while (*src && *src != '?' && *src != '#') {
        if (strncmp(src, "/./", 3) == 0) {
            src += 2;
        } else if (strncmp(src, "/../", 4) == 0) {
            src += 3;
            while (dst > path && *--dst != '/') {
            }
        } else if ((strncmp(src, "/.", 2) == 0 &&
                    (src[2] == '\0' || src[2] == '?' || src[2] == '#')) ||
                   (strncmp(src, "/..", 3) == 0 &&
                    (src[3] == '\0' || src[3] == '?' || src[3] == '#'))) {
            if (src[2] == '.') {
                while (dst > path && *--dst != '/') {
                }
                src++;
            }
            src += 2;
            *dst++ = '/';
        } else {
            *dst++ = *src++;
            while (*src && *src != '/' && *src != '?' && *src != '#') {
                *dst++ = *src++;
            }
        }
    }
    memmove(dst, src, strlen(src) + 1);
}

static int _http_interceptor_redirect(http_interceptor_t *interceptor)
{
    http_connect_params_t *params = interceptor->connect_params;
    const char *base = params->http_url;
    const char *location = interceptor->redirect_location;
    char url[HTTP_REDIRECT_URL_SIZE];
    const char *authority = strstr(base, "://");
    size_t prefix;
    int len;

    if (authority == NULL || location[0] == '\0') {
        return HTTP_FAILED_ERROR;
    }
    if (http_utils_ignore_case_nmatch(location, "http://", 7) == 0 ||
        http_utils_ignore_case_nmatch(location, "https://", 8) == 0) {
        prefix = 0;
    } else if (location[0] == '/' && location[1] == '/') {
        prefix = authority - base + 1;
    } else if (strchr(location, ':') != NULL &&
               strcspn(location, ":") < strcspn(location, "/?#")) {
        return HTTP_FAILED_ERROR;
    } else {
        const char *path = authority + 3 + strcspn(authority + 3, "/?#");
        prefix = path - base;
        if (location[0] == '?') {
            prefix += strcspn(path, "?#");
        } else if (location[0] == '#') {
            prefix += strcspn(path, "#");
        } else if (location[0] != '/') {
            size_t end = prefix + strcspn(path, "?#");
            while (end > prefix && base[end - 1] != '/') {
                end--;
            }
            prefix = end;
        }
        if (base[prefix] != '/' && prefix == (size_t)(path - base) &&
            location[0] != '/' && location[0] != '?' && location[0] != '#') {
            len = snprintf(url, sizeof(url), "%.*s/%s", (int)prefix, base, location);
            goto parsed;
        }
    }
    len = snprintf(url, sizeof(url), "%.*s%s", (int)prefix, base, location);
parsed:
    if (len < 0 || len >= sizeof(url)) {
        return HTTP_FAILED_ERROR;
    }
    _http_redirect_remove_dot_segments(url);
    HTTP_LOG_D("redirect to %s", url);
    /* The old transport is closed before freeing its borrowed host/port. */
    http_release_connect_params_variables(params);
    return http_url_parsing(params, url);
}
#endif

int http_interceptor_process(http_interceptor_t *interceptor,
                             http_connect_params_t *connect_params,
                             http_request_method_t mothod, 
                             void *post_buf,
                             void *owner,
                             http_event_cb_t cb)
{
    int res = HTTP_SUCCESS_ERROR;
#ifdef CONFIG_HTTP_REDIRECT_BUILD
    int redirect_count = 0;
#endif
    HTTP_ROBUSTNESS_CHECK((interceptor && connect_params && mothod), HTTP_NULL_VALUE_ERROR);
    do {
        HTTP_LOG_D("interceptor->status : %d", interceptor->status);

        switch (interceptor->status) {
            case http_interceptor_status_invalid:
                res = http_interceptor_init(interceptor);
                if (res != HTTP_SUCCESS_ERROR) {
                    goto failed;
                }
                http_interceptor_set_owner(interceptor, owner);
                http_interceptor_event_register(interceptor, cb);
                res = http_interceptor_set_connect_params(interceptor, connect_params);
                if (res != HTTP_SUCCESS_ERROR) {
                    goto failed;
                }
                
            case http_interceptor_status_init:
                res = http_interceptor_connect(interceptor);
                if (res != HTTP_SUCCESS_ERROR) {
                    goto failed;
                }

            case http_interceptor_status_connect:
                res = http_interceptor_request(interceptor, mothod, post_buf);
                if (res != HTTP_SUCCESS_ERROR) {
                    goto failed;
                }

            case http_interceptor_status_request:
                res = http_interceptor_fetch_headers(interceptor);
                if (res != HTTP_SUCCESS_ERROR) {
                    goto failed;
                }

            case http_interceptor_status_headers_complete:
#ifdef CONFIG_HTTP_REDIRECT_BUILD
                res = http_interceptor_check_response(interceptor);
                if (res != HTTP_SUCCESS_ERROR) {
                    goto failed;
                }
                if (interceptor->flag.flag_t.again) {
                    http_interceptor_release(interceptor);
                    if (++redirect_count > HTTP_REDIRECT_MAX_COUNT) {
                        HTTP_LOG_E("http redirect too many times");
                        return HTTP_FAILED_ERROR;
                    }
                    res = _http_interceptor_redirect(interceptor);
                    if (res != HTTP_SUCCESS_ERROR) {
                        return res;
                    }
                    continue;
                }
#endif
                if (mothod != HTTP_REQUEST_METHOD_HEAD) {
#ifndef CONFIG_HTTP_REDIRECT_BUILD
                    res = http_interceptor_check_response(interceptor);
#endif
                    res = http_interceptor_fetch_data(interceptor);
                    if (res != HTTP_SUCCESS_ERROR) {
                        goto failed;
                    }
                }
            
            case http_interceptor_status_response_body:
            case http_interceptor_status_response_complete:
                http_interceptor_submit_data(interceptor);

            case http_interceptor_status_release:
                http_interceptor_release(interceptor);
                return res;

            default:
                res = HTTP_FAILED_ERROR;
                goto failed;
        }
    } while (interceptor->flag.flag_t.again);
    return res;

failed:
    interceptor->flag.flag_t.again = 0;
    _http_interceptor_set_status(interceptor, http_interceptor_status_release);
    http_interceptor_release(interceptor);
    return res;
}

void http_interceptor_set_keep_alive(http_interceptor_t *interceptor)
{
    http_request_set_keep_alive(&interceptor->request);
    interceptor->flag.flag_t.keep_alive = 1;
}
