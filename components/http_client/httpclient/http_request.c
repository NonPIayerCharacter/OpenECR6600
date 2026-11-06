#include <http_request.h>
#include <http_log.h>
#include <http_error.h>
#include <platform_memory.h>

static const char *_http_request_version = HTTP_DEFAULT_VERSION;
static const char *_http_method_mapping[] = {"INVALID ", "GET ", "POST ", "HEAD ", "PUT ", "DELETE ", "OPTIONS ", "TRACE ", "CONNECT ", "PATCH " };

platform_list_t g_user_header_list;

static void http_request_user_msg_header_init(void)
{
    if (g_user_header_list.next == 0 && g_user_header_list.prev == 0) {
        platform_list_init(&g_user_header_list);
    }
}

platform_list_t *http_request_user_msg_header_get(void)
{
    http_request_user_msg_header_init();
    return &g_user_header_list;
}

void http_request_user_msg_header_insert(http_request_t *req)
{
    platform_list_t *list = NULL;
    http_user_header_t *msg_head = NULL;
    platform_list_t *headlist = http_request_user_msg_header_get();

    PLATFORM_LIST_FOR_EACH(list, headlist) {
        msg_head = PLATFORM_LIST_ENTRY(list, http_user_header_t, list);
        if ((strcmp(msg_head->key, "Content-Length:") != 0) && (msg_head->repeat == 0)) {
            http_request_add_header_form_key(req, msg_head->key, msg_head->value);
    }
    }
}

int http_request_user_msg_header_check_contentlength(void)
{
    platform_list_t *list = NULL;
    http_user_header_t *msg_head = NULL;
    platform_list_t *headlist = http_request_user_msg_header_get();

    PLATFORM_LIST_FOR_EACH(list, headlist) {
        msg_head = PLATFORM_LIST_ENTRY(list, http_user_header_t, list);
        if(strcmp(msg_head->key, "Content-Length:") == 0) {
            HTTP_LOG_D("Content-Length in msg header.\n");
            RETURN_ERROR(HTTP_FAILED_ERROR);
        }
    }

    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

static int http_request_user_msg_header_check(const char *key, unsigned int keylen)
{
    platform_list_t *headlist = http_request_user_msg_header_get();
    http_user_header_t *msg_head = NULL;
    platform_list_t *list = NULL;

    PLATFORM_LIST_FOR_EACH(list, headlist) {
        msg_head = PLATFORM_LIST_ENTRY(list, http_user_header_t, list);
        if ((keylen == strlen(msg_head->key)) && (strncmp(key, msg_head->key, keylen) == 0)) {
            HTTP_LOG_E("Warning!!! Same header, Wasting memory and ineffective.\n");
            RETURN_ERROR(HTTP_FAILED_ERROR);
        }
    }

    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

void http_request_user_msg_header_release(const char *key, unsigned int keylen)
{
    platform_list_t *headlist = http_request_user_msg_header_get();
    http_user_header_t *msg_head = NULL;
    platform_list_t *list = NULL;
    platform_list_t *tmplist = NULL;

    PLATFORM_LIST_FOR_EACH_SAFE(list, tmplist, headlist) {
            msg_head = PLATFORM_LIST_ENTRY(list, http_user_header_t, list);
        if ((keylen == strlen(msg_head->key)) && (strncmp(key, msg_head->key, keylen) == 0)) {
                platform_list_del(&msg_head->list);
                platform_memory_free(msg_head->value);
                platform_memory_free(msg_head->key);
                platform_memory_free(msg_head);
                return;
            }
        }
}

void http_request_user_msg_header_release_all(void)
{
    http_user_header_t *msg_head = NULL;
    platform_list_t *headlist = http_request_user_msg_header_get();

    msg_head = PLATFORM_LIST_FIRST_ENTRY_OR_NULL(headlist, http_user_header_t, list);
        while (msg_head != NULL) {
            platform_list_del(&msg_head->list);
            platform_memory_free(msg_head->value);
        platform_memory_free(msg_head->key);
            platform_memory_free(msg_head);
        msg_head = PLATFORM_LIST_FIRST_ENTRY_OR_NULL(headlist, http_user_header_t, list);
    }
}

void http_request_user_msg_header_clean_all(void)
{
    platform_list_t *headlist = http_request_user_msg_header_get();
    http_user_header_t *msg_head = NULL;
    platform_list_t *list = NULL;
    platform_list_t *tmplist = NULL;

    PLATFORM_LIST_FOR_EACH_SAFE(list, tmplist, headlist) {
        msg_head = PLATFORM_LIST_ENTRY(list, http_user_header_t, list);
        if (msg_head->release == 0) {
            platform_list_del(&msg_head->list);
            platform_memory_free(msg_head->value);
            platform_memory_free(msg_head->key);
            platform_memory_free(msg_head);
        }
    }
}

int http_request_user_msg_header_add(const char *key, unsigned int keylen, const char *value, unsigned int vlen, unsigned char release)
{
    platform_list_t *headlist = http_request_user_msg_header_get();
    http_user_header_t *msg_head = NULL;
    int repeat;
    
    HTTP_ROBUSTNESS_CHECK(key, HTTP_NULL_VALUE_ERROR);
    msg_head = platform_memory_alloc(sizeof(http_user_header_t));
    if (msg_head == NULL) {
        RETURN_ERROR(HTTP_FAILED_ERROR);
    }

    msg_head->key = platform_memory_alloc(keylen + 1);
    if(msg_head->key == NULL){
        platform_memory_free(msg_head);
        RETURN_ERROR(HTTP_FAILED_ERROR);
    }
    memcpy(msg_head->key, key, keylen);

    msg_head->value = platform_memory_alloc(vlen + 1);
    if (msg_head->value == NULL) {
        platform_memory_free(msg_head);
        platform_memory_free(msg_head->key);
        RETURN_ERROR(HTTP_FAILED_ERROR);
    }
    memcpy(msg_head->value, value, vlen);
    msg_head->release = release;
    repeat = http_request_user_msg_header_check(key, keylen);
    msg_head->repeat = (repeat == HTTP_SUCCESS_ERROR) ? 0 : 1;

    platform_list_init(&msg_head->list);
    platform_list_add(&msg_head->list, headlist);
    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

int http_request_init(http_request_t *req)
{
    HTTP_ROBUSTNESS_CHECK(req , HTTP_NULL_VALUE_ERROR);

    req->req_msg.line = http_message_buffer_init(HTTP_MESSAGE_BUFFER_GROWTH);
    req->req_msg.header = http_message_buffer_init(HTTP_MESSAGE_BUFFER_GROWTH);
    req->req_msg.body = http_message_buffer_init(HTTP_MESSAGE_BUFFER_GROWTH);
    http_request_user_msg_header_init();

    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

int http_request_release(http_request_t *req)
{
    HTTP_ROBUSTNESS_CHECK(req , HTTP_NULL_VALUE_ERROR);

    http_message_buffer_release(req->req_msg.line );
    http_message_buffer_release(req->req_msg.header);
    http_message_buffer_release(req->req_msg.body);

    req->req_msg.line = NULL;
    req->req_msg.header = NULL;
    req->req_msg.body = NULL;
    
    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

int http_request_header_init(http_request_t *req)
{
    HTTP_ROBUSTNESS_CHECK(req , HTTP_NULL_VALUE_ERROR);

    req->header_index[0] = 0;
    req->header_index[1] = 0;
    http_message_buffer_reinit(req->req_msg.header);

    http_request_user_msg_header_insert(req);
    http_request_add_header_form_key(req, "User-Agent: ", "ECR6600 HTTP Client/1.0");

    if (req->flag.flag_t.keep_alive) {
        http_request_add_header_form_key(req, "Connection: ", "keep-alive");
    } else {
        http_request_add_header_form_key(req, "Connection: ", "Close");
    }

    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}


int http_request_set_version(http_request_t *req, const char *str)
{
    HTTP_ROBUSTNESS_CHECK((req && str) , HTTP_NULL_VALUE_ERROR);
    if (http_utils_nmatch(str, "HTTP/", 5) == 0) {
        _http_request_version = str;
    } else {
        RETURN_ERROR(HTTP_NULL_VALUE_ERROR);
    }

    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

int http_request_set_keep_alive(http_request_t *req)
{
    HTTP_ROBUSTNESS_CHECK(req , HTTP_NULL_VALUE_ERROR);
    req->flag.flag_t.keep_alive = 1;
    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

int http_request_set_method(http_request_t *req,  http_request_method_t method)
{
    HTTP_ROBUSTNESS_CHECK((req && method), HTTP_NULL_VALUE_ERROR);

    req->method = method;

    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

int http_request_set_start_line(http_request_t *req, const char *path)
{
    HTTP_ROBUSTNESS_CHECK((req && path), HTTP_NULL_VALUE_ERROR);
    
    const char *m = _http_method_mapping[req->method];
    
    http_message_buffer_concat(req->req_msg.line, m, path, " ", _http_request_version, HTTP_CRLF, NULL);

    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

int http_request_set_start_line_with_query(http_request_t *req, const char *path, const char *query)
{
    HTTP_ROBUSTNESS_CHECK((req && path && query), HTTP_NULL_VALUE_ERROR);
    
    const char *m = _http_method_mapping[req->method];
    
    http_message_buffer_concat(req->req_msg.line, m, path, "?", query, " ", _http_request_version, HTTP_CRLF, NULL);

    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

/* adds a header to the request with given key and value. */
void http_request_add_header(http_request_t *req, const char *key, const char *value)
{
    http_message_buffer_concat(req->req_msg.header, key, ": ", value, HTTP_CRLF, NULL);
}

/* adds a header to the request with given header index and value. */
void http_request_add_header_form_key(http_request_t *req, const char *key, const char *value)
{
    //uint32_t *hindex = &req->header_index[0];
    HTTP_ROBUSTNESS_CHECK((req && key && value), HTTP_VOID);

    // if (index >= 32) {
    //     index -= 32;
    //     hindex = &req->header_index[1];
    // }

    // /* request header field already exists... */
    // if (*hindex & (0x01 << index))
    //     return;
    // *hindex |= (0x01 << index);
    /* request header field already exists... */

    // extern void at_printf(const char *f, ...);
    // at_printf("key = %s\r\n",key);


    if(http_request_get_header_form_key(req,key) != NULL){
        return;
    }

    http_message_buffer_concat(req->req_msg.header, key, value, HTTP_CRLF, NULL);
}

char *http_request_get_header(http_request_t *req, const char *key)
{
    char *line;
    char *line_end;
    size_t key_len;

    HTTP_ROBUSTNESS_CHECK((req && key), NULL);

    key_len = strlen(key);
    line = req->req_msg.header->data;
    while (key_len != 0 && line != NULL && *line != '\0') {
        line_end = strstr(line, HTTP_CRLF);
        if (line_end == NULL) {
            line_end = line + strlen(line);
        }

        /* Match at a line boundary so Range cannot match inside If-Range. */
        if ((size_t)(line_end - line) >= key_len &&
            http_utils_ignore_case_nmatch(line, key, (int)key_len) == 0) {
            return line + key_len;
        }
        line = (*line_end == '\0') ? NULL : line_end + strlen(HTTP_CRLF);
    }

    return NULL;
}

char *http_request_get_header_form_key(http_request_t *req, const char *key)
{
    HTTP_ROBUSTNESS_CHECK((req && key), NULL);

    // const char *key = _http_request_headers_mapping[index];
    // uint32_t *hindex = &req->header_index[0];

    // if (index >= 32) {
    //     index -= 32;
    //     hindex = &req->header_index[1];
    // }

    // /* request header field already exists... */
    // if (!(*hindex & (0x01 << index)))
    //     return NULL;

    return http_request_get_header(req, key);
}


int http_request_set_body(http_request_t *req, const char *buf, size_t size)
{
    HTTP_ROBUSTNESS_CHECK((req && buf && size), HTTP_NULL_VALUE_ERROR);

    http_message_buffer_append(req->req_msg.body, buf, size);

    char *str = http_utils_itoa(req->req_msg.body->used - 1, NULL, 10);
    http_request_add_header_form_key(req, "Content-Length: ", str);
    http_utils_release_string(str);

    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

int http_request_set_body_form_pointer(http_request_t *req, const char *buf, size_t size)
{
    HTTP_ROBUSTNESS_CHECK((req && buf && size), HTTP_NULL_VALUE_ERROR);

    http_message_buffer_pointer(req->req_msg.body, buf, size);

    char *str = http_utils_itoa(req->req_msg.body->used - 1, NULL, 10);
    http_request_add_header_form_key(req, "Content-Length: ", str);
    http_utils_release_string(str);

    RETURN_ERROR(HTTP_SUCCESS_ERROR);
}

char *http_request_get_start_line_data(http_request_t *req)
{
    HTTP_ROBUSTNESS_CHECK(req, NULL);
    return req->req_msg.line->data;
}

char *http_request_get_header_data(http_request_t *req)
{
    HTTP_ROBUSTNESS_CHECK(req, NULL);
    return req->req_msg.header->data;
}

char *http_request_get_body_data(http_request_t *req)
{
    HTTP_ROBUSTNESS_CHECK(req, NULL);
    return req->req_msg.body->data;
}

size_t http_request_get_start_line_length(http_request_t *req)
{
    HTTP_ROBUSTNESS_CHECK(req, 0);
    return req->req_msg.line->length;
}

size_t http_request_get_header_length(http_request_t *req)
{
    HTTP_ROBUSTNESS_CHECK(req, 0);
    return req->req_msg.header->length;
}

size_t http_request_get_body_length(http_request_t *req)
{
    HTTP_ROBUSTNESS_CHECK(req, 0);
    return req->req_msg.body->length;
}
