#ifndef __NETIF_DBG_H__
#define __NETIF_DBG_H__

#include "lwip/pbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NETDBG_MASK_IPADDR      = 0x01,
    NETDBG_MASK_LENGTH      = 0x02,
    NETDBG_MASK_PROTCL      = 0x04,
    NETDBG_MASK_SRCPORT     = 0x08,
    NETDBG_MASK_DSTPORT     = 0x10,
    NETDBG_MASK_DIR         = 0x20,
    NETDBG_MASK_PORT        = 0x40,
    NETDBG_MASK_MAX,
} NETDBG_MASK_E;

typedef enum {
    NETDBG_PACKET_DIR_RX    = 0,
    NETDBG_PACKET_DIR_TX    = 1,
    NETDBG_PACKET_DIR_BOTH  = 2,
} NETDBG_PACKET_DIR_E;

typedef struct {
    unsigned int ipaddr;
    unsigned int len;
    unsigned short dstPort;
    unsigned short srcPort;
    unsigned short port;
    unsigned char packeType;
    unsigned char dir;
    unsigned char mask;
    unsigned char resv;
} netdbg_packet_t;

typedef struct {
    unsigned int seqNum;
    unsigned int ackNum;
} netdbg_tcp_seqack_t;

/* flags 记录最近一次命中的 TCP 标记，max/last 分别记录累计最大值和最近值。 */
typedef struct {
    unsigned char flags;
    unsigned char resv[3];
    netdbg_tcp_seqack_t max;
    netdbg_tcp_seqack_t last;
} netdbg_tcp_dir_t;

typedef struct {
    netdbg_tcp_dir_t rx;
    netdbg_tcp_dir_t tx;
} netdbg_tcp_t;

typedef struct {
    netdbg_packet_t packet;
    netdbg_tcp_t tcp;
} netdbg_info_t;

extern netdbg_info_t g_ipdbg;

int netdbg_packet_rule_set(const netdbg_packet_t *rule);
int netdbg_packet_rule_get(netdbg_packet_t *rule);
void netdbg_packet_rule_clear(void);

int netdbg_packet_filter_rx(struct pbuf *packet);
int netdbg_packet_filter_tx(struct pbuf *packet);

#ifdef __cplusplus
}
#endif

#endif
