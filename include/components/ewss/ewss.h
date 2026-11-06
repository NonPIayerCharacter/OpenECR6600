#include <stddef.h>
#include <stdint.h>

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ROLE_NONE = 0, // must be 0
    ROLE_ENROLLEE,
    ROLE_REGISTRAR,
} ewss_role;

struct ewss_ap_info {
    uint16_t channel; // 1 to 14
    uint8_t bssid[6];
};

#define EWSS_FLAG_BCAST_PROBE_RESPONSE (1u << 0)

typedef void (*ewss_recv_cb)(const uint8_t *src_addr, const void *data, size_t data_len, void *cb_arg);
int ewss_enable(ewss_role role, ewss_recv_cb cb, void *cb_arg, uint32_t flags);
int ewss_set_request_payload(const void *buf, size_t buf_len);
int ewss_disable(void);
ewss_role ewss_get_current_role(void);
int ewss_get_announced_ap_info(struct ewss_ap_info *info);
int ewss_tx_response(const uint8_t *dst_addr, const void *data, size_t data_len);

#ifdef __cplusplus
}
#endif
