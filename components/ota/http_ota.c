/*
 * @file http_ota.c
 * @brief HTTP OTA download implementation
 * @details This file contains functions for downloading firmware via HTTP for OTA updates
 * @author your_name
 * @date today
 * @version V0.1
 * @Copyright © 2025 BEIJING ESWIN COMPUTING TECHINOLOGY CO., LTD and its affiliates ("ESWIN").
 *            All rights reserved. Any modification, reproduction, adaption, translation,
 *            distribution is prohibited without consent.
 * @par History 1:
 *      Date:
 *      Version:
 *      Author:
 *      Modification:
 *
 * @par History 2:
 */

/*--------------------------------------------------------------------------
 *                                 Include files
 *--------------------------------------------------------------------------*/
#include "oshal.h"
#include "http_ota.h"
#include "httpclient.h"
#include <strings.h>

/*--------------------------------------------------------------------------
 *                                 Local Macros
 *--------------------------------------------------------------------------*/
/* Overall OTA timeout, including download and Range retries: 20 minutes. */
#define HTTP_OTA_TIMEOUT_MS          (1200000)
#define HTTP_OTA_DUMMY_DATA_SIZE    (1024)
#define HTTP_OTA_DOWNLOADING        (1)

/*--------------------------------------------------------------------------
 *                                 Local Types structs menus
 *--------------------------------------------------------------------------*/
typedef enum {
    HTTP_OTA_REQUEST_PROBE,  /**< Probe Range support and read the OTA package header. */
    HTTP_OTA_REQUEST_FULL,   /**< Download the entire OTA package without a Range header. */
    HTTP_OTA_REQUEST_RANGE,  /**< Download an exact Boot or inactive-partition byte range. */
} http_ota_request_e;

typedef struct {
    unsigned int state;                  /**< Current HTTP OTA state, including the busy marker. */
    volatile unsigned int timed_out;     /**< Overall OTA timeout flag set by the timer callback. */
    unsigned int ota_session_owned;      /**< Whether HTTP OTA successfully created and owns the OTA session. */
    unsigned int full_download;          /**< Probe returned 200 and its body is the complete package. */
    unsigned int request_retryable;      /**< Whether a failed Range request may resume and retry. */
    http_ota_request_e request_mode;     /**< Purpose and response policy of the current HTTP request. */
    unsigned int request_start;          /**< Inclusive first byte requested by the Range header. */
    unsigned int request_end;            /**< Inclusive last byte requested by the Range header. */
    unsigned int request_received;       /**< Body bytes accepted during the current HTTP request. */
    unsigned int response_start;         /**< Inclusive first byte parsed from Content-Range. */
    unsigned int response_end;           /**< Inclusive last byte parsed from Content-Range. */
    unsigned int response_total;         /**< Total package size parsed from Content-Range. */
    unsigned int response_range_valid;   /**< Whether Content-Range was parsed successfully. */
    unsigned int header_received;        /**< OTA package-header bytes accumulated by the probe. */
    ota_package_head_t package_head;     /**< OTA package header used to plan selective downloading. */
    char etag[128];                      /**< Probe ETag reused as If-Range for subsequent requests. */
    os_timer_handle_t otatimer;          /**< Timer enforcing the overall HTTP OTA timeout. */
    http_ota_body_cb_t body_cb;          /**< Optional body notification, preserved across downloads. */
    void *body_user_data;
} http_ota_priv_t;

/*--------------------------------------------------------------------------
 *                                 Global Variables
 *--------------------------------------------------------------------------*/
static http_ota_priv_t g_http_ota_priv;
static unsigned char g_http_ota_dummy_data[HTTP_OTA_DUMMY_DATA_SIZE];

/*--------------------------------------------------------------------------
 *                                 Function Definitions
 *--------------------------------------------------------------------------*/
static http_ota_priv_t *http_client_get_priv(void)
{
    return &g_http_ota_priv;
}

void http_client_download_body_cb_register(http_ota_body_cb_t callback, void *user_data)
{
    g_http_ota_priv.body_cb = callback;
    g_http_ota_priv.body_user_data = user_data;
}

static void http_ota_reset(http_ota_priv_t *priv)
{
    http_ota_body_cb_t body_cb = priv->body_cb;
    void *body_user_data = priv->body_user_data;

    /* Reset download state while retaining the caller's body callback registration. */
    memset(priv, 0, sizeof(*priv));
    priv->body_cb = body_cb;
    priv->body_user_data = body_user_data;
}

static void http_client_download_timeout(os_timer_handle_t timer)
{
    (void)timer;
    http_client_get_priv()->timed_out = 1;
}

static void http_ota_timer_stop(http_ota_priv_t *priv)
{
    if (priv->otatimer != NULL) {
        os_timer_stop(priv->otatimer);
        os_timer_delete(priv->otatimer);
        priv->otatimer = NULL;
    }
}

static int http_ota_timer_start(http_ota_priv_t *priv)
{
    priv->otatimer = os_timer_create("ota_timer", HTTP_OTA_TIMEOUT_MS, 0,
                                     http_client_download_timeout, NULL);
    if (priv->otatimer == NULL) {
        os_printf(LM_APP, LL_ERR, "OTA:create http timer failed\n");
        return -1;
    }

    os_timer_start(priv->otatimer);
    return 0;
}

static void http_ota_copy_header_value(char *dest, unsigned int dest_len,
                                       const char *data, unsigned int len,
                                       unsigned int prefix_len)
{
    unsigned int value_len;

    while (prefix_len < len && data[prefix_len] == ' ') {
        prefix_len++;
    }
    value_len = len - prefix_len;
    while (value_len > 0 && (data[prefix_len + value_len - 1] == '\r' ||
                             data[prefix_len + value_len - 1] == '\n')) {
        value_len--;
    }
    if (value_len >= dest_len) {
        value_len = dest_len - 1;
    }
    memcpy(dest, data + prefix_len, value_len);
    dest[value_len] = '\0';
}

static void http_ota_parse_response_header(http_ota_priv_t *priv,
                                           const char *data, unsigned int len)
{
    const char *content_range = "Content-Range: ";
    const char *etag = "ETag: ";
    const char *value;
    unsigned int prefix_len;
    unsigned int start;
    unsigned int end;
    unsigned int total;

    prefix_len = strlen(content_range);
    if (len >= prefix_len && strncasecmp(data, content_range, prefix_len) == 0) {
        value = data + prefix_len;
        if (len >= prefix_len + strlen("bytes ") &&
            strncasecmp(value, "bytes ", strlen("bytes ")) == 0 &&
            sscanf(value + strlen("bytes "), "%u-%u/%u", &start, &end, &total) == 3) {
            priv->response_start = start;
            priv->response_end = end;
            priv->response_total = total;
            priv->response_range_valid = 1;
        }
        return;
    }

    prefix_len = strlen(etag);
    if (priv->request_mode == HTTP_OTA_REQUEST_PROBE && len >= prefix_len &&
        strncasecmp(data, etag, prefix_len) == 0) {
        http_ota_copy_header_value(priv->etag, sizeof(priv->etag), data, len, prefix_len);
    }
}

static int http_ota_start_session(http_ota_priv_t *priv)
{
    int ret;

    if (priv->ota_session_owned) {
        return 0;
    }

    ret = ota_init();
    if (ret != 0) {
        os_printf(LM_APP, LL_ERR, "OTA:init failed ret:%d\n", ret);
        return -1;
    }
    priv->ota_session_owned = 1;
    return 0;
}

static int http_ota_validate_response(http_ota_priv_t *priv, http_client_t *client)
{
    unsigned int expected_len = priv->request_end - priv->request_start + 1;
    unsigned int status = client->interceptor->response.status;
    unsigned int content_len = client->interceptor->response.length;

    if (priv->request_mode == HTTP_OTA_REQUEST_PROBE && status == http_response_status_ok) {
        priv->full_download = 1;
        return http_ota_start_session(priv);
    }
    if (priv->request_mode == HTTP_OTA_REQUEST_FULL) {
        if (status == http_response_status_ok) {
            return 0;
        }
        priv->request_retryable = 0;
        return -1;
    }
    switch (status) {
        case http_response_status_request_timeout:
        case http_response_status_internal_server_error:
        case http_response_status_bad_gateway:
        case http_response_status_service_unavailable:
        case http_response_status_gateway_timeout:
            os_printf(LM_APP, LL_WARN, "OTA:temporary http status:%u, retry range request\n", status);
            return -1;
        default:
            break;
    }
    if (status != http_response_status_partial_content ||
        !priv->response_range_valid ||
        priv->response_start != priv->request_start ||
        priv->response_end != priv->request_end ||
        content_len != expected_len) {
        os_printf(LM_APP, LL_ERR, "OTA:invalid range response status:%u range:%u-%u expect:%u-%u len:%u\n",
                  status, priv->response_start, priv->response_end,
                  priv->request_start, priv->request_end, content_len);
        priv->request_retryable = 0;
        return -1;
    }

    return 0;
}

static int http_client_download_data(void *data)
{
    http_ota_priv_t *priv = http_client_get_priv();
    http_event_t *event = data;
    http_client_t *client = event->context;
    unsigned int expected_len;
    int ret;

    if (client == NULL) {
        priv->request_retryable = 0;
        return -1;
    }
    if (priv->timed_out) {
        priv->request_retryable = 0;
        return -1;
    }
    if (event->type == http_event_type_on_headers) {
        if (event->len != 0) {
            http_ota_parse_response_header(priv, event->data, event->len);
            return 0;
        }
        return http_ota_validate_response(priv, client);
    }
    if (event->type != http_event_type_on_body || event->len == 0) {
        return 0;
    }

    if (priv->request_mode == HTTP_OTA_REQUEST_PROBE && !priv->full_download) {
        expected_len = sizeof(ota_package_head_t) - priv->header_received;
        if (event->len > expected_len) {
            priv->request_retryable = 0;
            return -1;
        }
        memcpy((unsigned char *)&priv->package_head + priv->header_received,
               event->data, event->len);
        priv->header_received += event->len;
    } else {
        if (priv->request_mode == HTTP_OTA_REQUEST_RANGE) {
            expected_len = priv->request_end - priv->request_start + 1;
            if (event->len > expected_len - priv->request_received) {
                priv->request_retryable = 0;
                return -1;
            }
        }
        ret = ota_write((unsigned char *)event->data, event->len);
        if (ret != 0) {
            os_printf(LM_APP, LL_ERR, "OTA:write failed ret:%d\n", ret);
            priv->request_retryable = 0;
            return -1;
        }
    }
    priv->request_received += event->len;
    if (priv->body_cb != NULL) {
        unsigned int offset = priv->request_received - event->len;
        unsigned int total = client->interceptor->response.length;

        if (priv->request_mode == HTTP_OTA_REQUEST_RANGE ||
            (priv->request_mode == HTTP_OTA_REQUEST_PROBE && !priv->full_download)) {
            offset += priv->request_start;
            total = priv->response_total;
        }
        priv->body_cb(event->data, event->len, offset, total, priv->body_user_data);
    }
    return 0;
}

static int http_ota_request(const char *url, http_ota_request_e mode,
                            unsigned int start, unsigned int end, int use_range)
{
    http_ota_priv_t *priv = http_client_get_priv();
    http_client_t *client;
    char range[64];
    int range_added = 0;
    int if_range_added = 0;
    int ret;

    priv->request_mode = mode;
    priv->request_start = start;
    priv->request_end = end;
    priv->request_received = 0;
    priv->response_start = 0;
    priv->response_end = 0;
    priv->response_range_valid = 0;
    priv->request_retryable = 1;

    client = http_client_init(NULL);
    if (client == NULL) {
        return -1;
    }
    if (use_range) {
        snprintf(range, sizeof(range), "bytes=%u-%u", start, end);
        ret = http_request_user_msg_header_add("Range: ", strlen("Range: "),
                                               range, strlen(range), 0);
        if (ret != 0) {
            http_client_exit(client);
            return -1;
        }
        range_added = 1;
        if (mode != HTTP_OTA_REQUEST_FULL && priv->etag[0] != '\0') {
            ret = http_request_user_msg_header_add("If-Range: ", strlen("If-Range: "),
                                                   priv->etag, strlen(priv->etag), 0);
            if (ret != 0) {
                http_request_user_msg_header_release("Range: ", strlen("Range: "));
                http_client_exit(client);
                return -1;
            }
            if_range_added = 1;
        }
    }

    ret = http_client_method_request(client, HTTP_REQUEST_METHOD_GET, url,
                                     http_client_download_data);
    http_client_exit(client);
    if (if_range_added) {
        http_request_user_msg_header_release("If-Range: ", strlen("If-Range: "));
    }
    if (range_added) {
        http_request_user_msg_header_release("Range: ", strlen("Range: "));
    }
    if (priv->timed_out) {
        priv->request_retryable = 0;
        return -1;
    }
    return ret;
}

static int http_ota_probe(const char *url)
{
    http_ota_priv_t *priv = http_client_get_priv();
    unsigned int header_len = sizeof(ota_package_head_t);
    unsigned int request_len;
    int ret;

    while (priv->header_received < header_len && !priv->timed_out) {
        ret = http_ota_request(url, HTTP_OTA_REQUEST_PROBE,
                               priv->header_received, header_len - 1, 1);
        if (priv->full_download) {
            return ret;
        }

        request_len = priv->request_end - priv->request_start + 1;
        if (priv->request_received > request_len) {
            return -1;
        }
        if (ret == 0) {
            if (priv->request_received != request_len) {
                return -1;
            }
            continue;
        }
        if (!priv->request_retryable) {
            return -1;
        }
        os_msleep(1000);
    }

    return (priv->header_received == header_len && !priv->timed_out) ? 0 : -1;
}

static int http_ota_download_range(const char *url, unsigned int start, unsigned int len)
{
    http_ota_priv_t *priv = http_client_get_priv();
    unsigned int received = 0;
    unsigned int request_len;
    int ret;

    while (received < len && !priv->timed_out) {
        ret = http_ota_request(url, HTTP_OTA_REQUEST_RANGE, start + received,
                               start + len - 1, 1);
        request_len = priv->request_end - priv->request_start + 1;
        if (priv->request_received > request_len) {
            return -1;
        }
        received += priv->request_received;
        if (ret == 0) {
            if (priv->request_received != request_len) {
                return -1;
            }
            continue;
        }
        if (!priv->request_retryable) {
            return -1;
        }
        os_msleep(1000);
    }

    return (received == len) ? 0 : -1;
}

static int http_ota_write_dummy(unsigned int len)
{
    unsigned int write_len;
    int ret;

    while (len > 0) {
        write_len = (len > sizeof(g_http_ota_dummy_data)) ?
                    sizeof(g_http_ota_dummy_data) : len;
        ret = ota_write(g_http_ota_dummy_data, write_len);
        if (ret != 0) {
            os_printf(LM_APP, LL_ERR, "OTA:write dummy failed ret:%d\n", ret);
            return -1;
        }
        len -= write_len;
    }

    return 0;
}

static int http_ota_package_is_selectable(http_ota_priv_t *priv, unsigned int *active_part)
{
    ota_package_head_t *head = &priv->package_head;
    unsigned long long package_size;

    if (memcmp(head->magic, "FotaPKG", sizeof(head->magic)) != 0 ||
        ((head->version >> 4) / 4) != DOWNLOAD_DUAL_METHOD) {
        return 0;
    }
    package_size = sizeof(*head) + (unsigned long long)head->boot_size +
                   head->firmware_size + head->firmware_new_size;
    if (package_size != head->package_size ||
        (priv->response_total != 0 && package_size > priv->response_total)) {
        return 0;
    }

    *active_part = ota_get_active_part();
    if ((*active_part == ACTIVE_OTA_PARTA && head->firmware_new_size == 0) ||
        (*active_part == ACTIVE_OTA_PARTB && head->firmware_size == 0)) {
        return 0;
    }
    return (*active_part == ACTIVE_OTA_PARTA || *active_part == ACTIVE_OTA_PARTB);
}

static int http_ota_download_selective(const char *url, unsigned int active_part)
{
    http_ota_priv_t *priv = http_client_get_priv();
    ota_package_head_t *head = &priv->package_head;
    unsigned int offset = sizeof(*head);
    int ret;

    ret = http_ota_start_session(priv);
    if (ret != 0 || ota_write((unsigned char *)head, sizeof(*head)) != 0) {
        return -1;
    }
    if (head->boot_size != 0) {
        ret = http_ota_download_range(url, offset, head->boot_size);
        if (ret != 0) {
            return ret;
        }
        offset += head->boot_size;
    }

    if (active_part == ACTIVE_OTA_PARTA) {
        ret = http_ota_write_dummy(head->firmware_size);
        offset += head->firmware_size;
        if (ret == 0) {
            ret = http_ota_download_range(url, offset, head->firmware_new_size);
        }
    } else {
        ret = http_ota_download_range(url, offset, head->firmware_size);
        offset += head->firmware_size;
        if (ret == 0) {
            ret = http_ota_write_dummy(head->firmware_new_size);
        }
    }
    return ret;
}

static int http_ota_download_remaining(const char *url)
{
    http_ota_priv_t *priv = http_client_get_priv();
    unsigned int header_len = sizeof(ota_package_head_t);
    int ret;

    if (priv->response_total < header_len ||
        priv->package_head.package_size != priv->response_total) {
        return -1;
    }
    ret = http_ota_start_session(priv);
    if (ret != 0 || ota_write((unsigned char *)&priv->package_head, header_len) != 0) {
        return -1;
    }
    if (priv->response_total == header_len) {
        return 0;
    }
    return http_ota_download_range(url, header_len,
                                   priv->response_total - header_len);
}

static int http_ota_finish(http_ota_priv_t *priv, int success, bool reboot)
{
    int ret;

    http_ota_timer_stop(priv);
    if (success && priv->timed_out) {
        success = 0;
    }
    if (!success) {
        if (priv->ota_session_owned) {
            ota_cleanup();
        }
        http_ota_reset(priv);
        return -1;
    }

    ret = ota_done(reboot);
    if (ret != 0) {
        ota_cleanup();
    }
    http_ota_reset(priv);
    return (ret == 0) ? 0 : -1;
}

int http_client_download_file(const char *url, bool reboot)
{
    http_ota_priv_t *priv = http_client_get_priv();
    unsigned int active_part;
    int ret;

    if (url == NULL || priv->state == HTTP_OTA_DOWNLOADING) {
        return -1;
    }
    if (ota_is_ab_update_pending()) {
        os_printf(LM_APP, LL_ERR, "OTA:AB update pending confirm, reject http OTA\n");
        return -1;
    }

    http_ota_reset(priv);
    priv->state = HTTP_OTA_DOWNLOADING;
    if (http_ota_timer_start(priv) != 0) {
        http_ota_reset(priv);
        return -1;
    }

    ret = http_ota_probe(url);
    if (ret != 0) {
        return http_ota_finish(priv, 0, reboot);
    }
    if (priv->full_download) {
        return http_ota_finish(priv, 1, reboot);
    }
    if (priv->header_received != sizeof(ota_package_head_t)) {
        return http_ota_finish(priv, 0, reboot);
    }

    if (http_ota_package_is_selectable(priv, &active_part)) {
        os_printf(LM_APP, LL_INFO, "OTA:range download active part:%c dummy size:%u\n",
                  (active_part == ACTIVE_OTA_PARTA) ? 'A' : 'B',
                  HTTP_OTA_DUMMY_DATA_SIZE);
        ret = http_ota_download_selective(url, active_part);
    } else {
        ret = http_ota_download_remaining(url);
    }
    return http_ota_finish(priv, ret == 0, reboot);
}
