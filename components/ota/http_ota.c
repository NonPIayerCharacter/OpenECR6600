#include "oshal.h"
#include "ota.h"
#include "httpclient.h"

static int http_client_download_data(void *data)
{
    http_event_t *event = data;
    http_client_t *client = event->context;
    int ret;

    if (client == NULL) {
        return -1;
    }

    os_printf(LM_APP, LL_DBG, "event type 0x%x len %d totlen %d\n", event->type, event->len, client->total);

    if (event->type == http_event_type_on_headers) {
        if (client->interceptor->response.status != http_response_status_ok) {
            os_printf(LM_APP, LL_ERR, "Err Header status 0x%x\n", client->interceptor->response.status);
            return -1;
        }
    }

    if (event->type == http_event_type_on_body) {
        ret = ota_write((unsigned char *)event->data, event->len);
        if (ret != 0) {
            os_printf(LM_APP, LL_ERR, "ota write failed 0x%x\n", ret);
            return -1;
        }
    }

    return 0;
}

int http_client_download_file(const char *url)
{
    http_client_t *client = http_client_init(NULL);
    int ret = ota_init();

    if (ret != 0) {
        os_printf(LM_APP, LL_ERR, "ota init failed 0x%x\n", ret);
        return -1;
    }

    ret = http_client_method_request(client, HTTP_REQUEST_METHOD_GET, url, http_client_download_data);
    (ret == 0) ? ota_done(1) : ota_done(0);
    http_client_exit(client);

    return 0;
}
