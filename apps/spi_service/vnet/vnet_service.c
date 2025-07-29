#include "vnet_service.h"
#include "vnet_filter.h"
#include "vnet_register.h"
#include "spi_service_main.h"
#include "spi_service_mem.h"
#include "spi_slave.h"
#include "system_wifi.h"
#include "lwip/etharp.h"
#include "lwip/icmp.h"

extern void fhost_tx_free(void *buf);
extern int fhost_tx_start(void *net_if, void *net_buf, uint8_t is_raw);
#define VNET_MIN_FRAME_SIZE 42

static struct pbuf *vnet_service_bcopy_send(struct pbuf *p, struct netif *inp)
{
    struct pbuf *pt = NULL;
    unsigned int bcopy = 0;

    if (p != NULL && p->tot_len >= VNET_MIN_FRAME_SIZE) {
        struct eth_hdr *ethhdr = p->payload;
        short type = lwip_ntohs(ethhdr->type);
        unsigned char *packet = (p->payload + sizeof(struct eth_hdr));

        /* ARP REPLY need sendto MCU */
        if (type == ETHTYPE_ARP) {
            struct etharp_hdr *arp_hdr = (struct etharp_hdr*)(p->payload + sizeof(struct eth_hdr));
            short arp_opcode = lwip_ntohs(arp_hdr->opcode);
            if (arp_opcode == ARP_REPLY) {
                bcopy = 1;
            }
        }

        if (type == ETHTYPE_IP) {
            struct ip_hdr *iphdr = (struct ip_hdr *)(p->payload + sizeof(struct eth_hdr));
            if (IPH_PROTO(iphdr) == IP_PROTO_ICMP) {
                struct icmp_echo_hdr *iecho = (struct icmp_echo_hdr *)((u8_t *)p->payload + sizeof(struct eth_hdr) + IPH_HL_BYTES(iphdr));
                /* WIFI HEARTBEAT PING ignore */
                if (iecho->id != WIFI_HEARTBEAT_PING_ID) {
                    /* ICMP need sendto MCU */
                    bcopy = 1;
                }
            }
        }

        if (bcopy == 0 && vnet_ipv4_packet_blist_filter(packet) == VNET_PACKET_DICTION_BOTH) {
            bcopy = 1;
        }
    }

    if (bcopy) {
        spi_service_mtype_e type = (get_netif_by_index(STATION_IF) == inp) ? SPI_SERVICE_MEM_STATX : SPI_SERVICE_MEM_APTX;
        pt = spi_service_mpbuf_alloc(type, p->len);
        memcpy(pt->payload, p->payload, p->len);
        pt->len = p->len;
        pt->tot_len = p->len;
        return pt;
    }

    return NULL;
}

int vnet_service_wifi_rx(struct pbuf *p, struct netif *inp)
{
    spi_service_mem_t *smem = NULL;
    vnet_reg_t *vReg = vnet_reg_get_addr();
    int ret = -1;

    if (spi_service_mpool_check(p) == NULL) {
        p = vnet_service_bcopy_send(p, inp);
        if (p == NULL) {
            os_printf(LM_APP, LL_DBG, "%s[%d]\n", __FUNCTION__, __LINE__);
            return 0;
        }
        ret = 0;
    }

    if (vReg->status == VNET_WIFI_LINK_DOWN) {
        if ((p->flags & PBUF_FLAG_IS_CUSTOM) != 0) {
            struct pbuf_custom *pc = (struct pbuf_custom *)p;
            pc->custom_free_function(p);
        } else {
            pbuf_free(p);
        }
        return ret;
    }

    smem = spi_mqueue_get();
    if (smem == NULL) {
        if ((p->flags & PBUF_FLAG_IS_CUSTOM) != 0) {
            struct pbuf_custom *pc = (struct pbuf_custom *)p;
            pc->custom_free_function(p);
        } else {
            pbuf_free(p);
        }
        return ret;
    }
    memset(smem, 0, sizeof(spi_service_mem_t));
    smem->memAddr = (unsigned int)p;
    smem->memOffset = (unsigned int)p->payload - (unsigned int)p;
    smem->memSlen = smem->memLen = p->len;
    smem->memType = SPI_SERVICE_TYPE_STOH;
    spi_slave_sendto_host(smem);

    return ret;
}

int vnet_service_wifi_tx(spi_service_mem_t *smem)
{
    struct netif *staif = NULL;
    struct pbuf *p = (struct pbuf *)smem->memAddr;
    wifi_work_mode_e wifiMode = wifi_get_opmode();
    staif = get_netif_by_index(wifiMode);

    if (staif == NULL) {
        os_printf(LM_APP, LL_DBG, "%s[%d]\n", __FUNCTION__, __LINE__);
        return 0;
    }

    p->ref++;
    if (fhost_tx_start(staif, p, 0) != 0) {
        fhost_tx_free(p);
    }

    return 0;
}

int vnet_service_wifi_rx_free(struct pbuf *p, struct netif *inp)
{
    return spi_service_mpbuf_free(p) ? 0 : 1;
}

int vnet_service_wifi_rx_done(spi_service_mem_t *smem)
{
    struct pbuf *p = (struct pbuf *)smem->memAddr;

    if ((p->flags & PBUF_FLAG_IS_CUSTOM) != 0) {
        struct pbuf_custom *pc = (struct pbuf_custom *)p;
        pc->custom_free_function(p);
    } else {
        pbuf_free(p);
    }

    return 0;
}
