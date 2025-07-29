#include "spi_service_mem.h"
#include "spi_service_loop.h"
#include "cli.h"

#define SPI_SERVICE_MEM_NUM_RX 16
#define SPI_SERVICE_MEM_NUM_TX 20
#define SPI_SERVICE_MEM_NUM_MSG 8
#define SPI_SERVICE_MEM_RSV_SIZE SPI_SERVICE_ALIGN4(sizeof(spi_service_rsvhead_t))
#define SPI_SERVICE_MEM_RX_TOTAL (SPI_SERVICE_MEM_NUM_RX*(SPI_SERVICE_MEM_DATA_SIZE+SPI_SERVICE_MEM_RSV_SIZE))
#define SPI_SERVICE_MEM_TX_TOTAL (SPI_SERVICE_MEM_NUM_TX*(SPI_SERVICE_MEM_DATA_SIZE+SPI_SERVICE_MEM_RSV_SIZE))
#define SPI_SERVICE_MEM_MSG_TOTAL (SPI_SERVICE_MEM_NUM_MSG*(SPI_SERVICE_MEM_MSG_SIZE+SPI_SERVICE_MEM_RSV_SIZE))
unsigned char g_spi_rxmem[SPI_SERVICE_MEM_RX_TOTAL] __attribute__ ((section("SHAREDRAM")));
unsigned char g_spi_txmem[SPI_SERVICE_MEM_TX_TOTAL] __attribute__ ((section("SHAREDRAM")));
unsigned char g_spi_msgmem[SPI_SERVICE_MEM_MSG_TOTAL] __attribute__ ((section("SHAREDRAM")));

typedef struct {
    unsigned char status;
    unsigned char txnum;
    unsigned char rxnum;
    unsigned char msgnum;
    platform_list_t rxlist;
    platform_list_t txlist;
    platform_list_t msglist;
    spi_service_lnkhead_t rxlink[SPI_SERVICE_MEM_NUM_RX];
    spi_service_lnkhead_t txlink[SPI_SERVICE_MEM_NUM_TX];
    spi_service_lnkhead_t msglink[SPI_SERVICE_MEM_NUM_MSG];
} spi_server_mem_priv_t;

spi_server_mem_priv_t g_spi_mlist;

static spi_server_mem_priv_t *spi_service_mlist_priv_get(void)
{
    return &g_spi_mlist;
}

int spi_service_mem_init(void)
{
    spi_server_mem_priv_t *priv = spi_service_mlist_priv_get();
    unsigned int memaddr, flag;
    int idx;

    SPI_SERVICE_CHECK_RETURN(priv->status == (SPI_SERVICE_MEM_ALLOC&0xFF), -1, "spi mem already init");
    flag = system_irq_save();
    platform_list_init(&priv->rxlist);
    memset(g_spi_rxmem, 0, sizeof(g_spi_rxmem));
    memaddr = SPI_SERVICE_ALIGN4((unsigned int)g_spi_rxmem);
    for (idx = 0; idx < SPI_SERVICE_MEM_NUM_RX; idx++) {
        spi_service_rsvhead_t *rsvhead = (spi_service_rsvhead_t *)memaddr;
        rsvhead->addr = (unsigned int)memaddr + SPI_SERVICE_MEM_RSV_SIZE;
        rsvhead->idx = idx;
        rsvhead->magic = SPI_SERVICE_MEM_FREE;
        rsvhead->mtype = 0xFF;
        priv->rxlink[idx].mhead = rsvhead;
        platform_list_add_tail(&priv->rxlink[idx].list, &priv->rxlist);
        priv->rxnum++;
        memaddr = (unsigned int)memaddr + SPI_SERVICE_MEM_RSV_SIZE + SPI_SERVICE_MEM_DATA_SIZE;
    }

    platform_list_init(&priv->txlist);
    memset(g_spi_txmem, 0, sizeof(g_spi_txmem));
    memaddr = SPI_SERVICE_ALIGN4((unsigned int)g_spi_txmem);
    for (idx = 0; idx < SPI_SERVICE_MEM_NUM_TX; idx++) {
        spi_service_rsvhead_t *rsvhead = (spi_service_rsvhead_t *)memaddr;
        rsvhead->addr = (unsigned int)memaddr + SPI_SERVICE_MEM_RSV_SIZE;
        rsvhead->idx = idx;
        rsvhead->magic = SPI_SERVICE_MEM_FREE;
        rsvhead->mtype = 0xFF;
        priv->txlink[idx].mhead = rsvhead;
        platform_list_add_tail(&priv->txlink[idx].list, &priv->txlist);
        priv->txnum++;
        memaddr = (unsigned int)memaddr + SPI_SERVICE_MEM_RSV_SIZE + SPI_SERVICE_MEM_DATA_SIZE;
    }

    platform_list_init(&priv->msglist);
    memset(g_spi_msgmem, 0, sizeof(g_spi_msgmem));
    memaddr = SPI_SERVICE_ALIGN4((unsigned int)g_spi_msgmem);
    for (idx = 0; idx < SPI_SERVICE_MEM_NUM_MSG; idx++) {
        spi_service_rsvhead_t *rsvhead = (spi_service_rsvhead_t *)memaddr;
        rsvhead->addr = (unsigned int)memaddr + SPI_SERVICE_MEM_RSV_SIZE;
        rsvhead->idx = idx;
        rsvhead->magic = SPI_SERVICE_MEM_FREE;
        rsvhead->mtype = 0xFF;
        priv->msglink[idx].mhead = rsvhead;
        platform_list_add_tail(&priv->msglink[idx].list, &priv->msglist);
        priv->msgnum++;
        memaddr = (unsigned int)memaddr + SPI_SERVICE_MEM_RSV_SIZE + SPI_SERVICE_MEM_MSG_SIZE;
    }
    system_irq_restore(flag);
    priv->status = SPI_SERVICE_MEM_ALLOC & 0xFF;

#ifdef CONFIG_SPI_SLAVE
    typedef void *(*macif_spi_buff_alloc)(int memtype, uint32_t size);
    void macif_spi_buff_call(macif_spi_buff_alloc cb);
    macif_spi_buff_call((macif_spi_buff_alloc)spi_service_mpool_alloc);

    typedef void *(*rxl_vnet_packet_free)(void *rx_buff);
    void fhost_rx_buf_push_call(rxl_vnet_packet_free call);
    fhost_rx_buf_push_call(spi_service_mpool_free);
#endif

    return 0;
}

void spi_service_mem_deinit(void)
{
    spi_server_mem_priv_t *priv = spi_service_mlist_priv_get();

    memset(priv, 0, sizeof(spi_server_mem_priv_t));
    spi_service_mem_init();
}

static void *spi_service_mem_alloc(spi_service_mtype_e type)
{
    spi_server_mem_priv_t *priv = spi_service_mlist_priv_get();
    spi_service_lnkhead_t *lnkhead = NULL;
    unsigned int flag = system_irq_save();

    if (type == SPI_SERVICE_MEM_STARX || type == SPI_SERVICE_MEM_APRX) {
        if (priv->rxnum > 0) {
            lnkhead = PLATFORM_LIST_FIRST_ENTRY_OR_NULL(&priv->rxlist, spi_service_lnkhead_t, list);
            SPI_SERVICE_CHECK_RETURN(lnkhead == NULL, NULL, "mem rxlist error");
            platform_list_del(&lnkhead->list);
            priv->rxnum--;
        }
    } else if (type == SPI_SERVICE_MEM_STATX || type == SPI_SERVICE_MEM_APTX) {
        if (priv->txnum > 0) {
            lnkhead = PLATFORM_LIST_FIRST_ENTRY_OR_NULL(&priv->txlist, spi_service_lnkhead_t, list);
            SPI_SERVICE_CHECK_RETURN(lnkhead == NULL, NULL, "mem txlist error");
            platform_list_del(&lnkhead->list);
            priv->txnum--;
        }
    } else if (type == SPI_SERVICE_MEM_MSG) {
        if (priv->msgnum > 0) {
            lnkhead = PLATFORM_LIST_FIRST_ENTRY_OR_NULL(&priv->msglist, spi_service_lnkhead_t, list);
            SPI_SERVICE_CHECK_RETURN(lnkhead == NULL, NULL, "mem msglist error");
            platform_list_del(&lnkhead->list);
            priv->msgnum--;
        }
    }
    system_irq_restore(flag);

    if (lnkhead != NULL) {
        SPI_SERVICE_CHECK_RETURN(lnkhead->mhead->magic != SPI_SERVICE_MEM_FREE, NULL, "mem magic error");
        lnkhead->mhead->magic = SPI_SERVICE_MEM_ALLOC;
        lnkhead->mhead->mtype = type;
        return (void *)lnkhead->mhead->addr;
    }

    return NULL;
}

static void *spi_service_mem_free(void *maddr)
{
    spi_server_mem_priv_t *priv = spi_service_mlist_priv_get();
    spi_service_rsvhead_t *rsvhead = (spi_service_rsvhead_t *)((unsigned int)maddr - SPI_SERVICE_MEM_RSV_SIZE);
    unsigned int flag;

    flag = system_irq_save();
    if (((unsigned int)maddr > (unsigned int)g_spi_rxmem) &&
        ((unsigned int)maddr < (unsigned int)g_spi_rxmem + SPI_SERVICE_MEM_RX_TOTAL)) {
        SPI_SERVICE_CHECK_RETURN(rsvhead->magic != SPI_SERVICE_MEM_ALLOC, maddr, "rxmem magic error");
        rsvhead->magic = SPI_SERVICE_MEM_FREE;
        rsvhead->mtype = 0xFF;
        platform_list_add(&priv->rxlink[rsvhead->idx].list, &priv->rxlist);
        priv->rxnum++;
        system_irq_restore(flag);
        return NULL;
    }

    if (((unsigned int)maddr > (unsigned int)g_spi_txmem) &&
        ((unsigned int)maddr < (unsigned int)g_spi_txmem + SPI_SERVICE_MEM_TX_TOTAL)) {
        SPI_SERVICE_CHECK_RETURN(rsvhead->magic != SPI_SERVICE_MEM_ALLOC, maddr, "txmem magic error");
        rsvhead->magic = SPI_SERVICE_MEM_FREE;
        rsvhead->mtype = 0xFF;
        platform_list_add(&priv->txlink[rsvhead->idx].list, &priv->txlist);
        priv->txnum++;
        system_irq_restore(flag);
        return NULL;
    }

    if (((unsigned int)maddr > (unsigned int)g_spi_msgmem) &&
        ((unsigned int)maddr < (unsigned int)g_spi_msgmem + SPI_SERVICE_MEM_MSG_TOTAL)) {
            SPI_SERVICE_CHECK_RETURN(rsvhead->magic != SPI_SERVICE_MEM_ALLOC, maddr, "msgmem magic error");
            rsvhead->magic = SPI_SERVICE_MEM_FREE;
            rsvhead->mtype = 0xFF;
            platform_list_add(&priv->msglink[rsvhead->idx].list, &priv->msglist);
            priv->msgnum++;
            system_irq_restore(flag);
            return NULL;
    }
    system_irq_restore(flag);

    return maddr;
}

void *spi_service_mpool_check(void *maddr)
{
    if ((((unsigned int)maddr > (unsigned int)g_spi_rxmem) &&
        ((unsigned int)maddr < (unsigned int)g_spi_rxmem + SPI_SERVICE_MEM_RX_TOTAL)) ||
        (((unsigned int)maddr > (unsigned int)g_spi_txmem) &&
        ((unsigned int)maddr < (unsigned int)g_spi_txmem + SPI_SERVICE_MEM_TX_TOTAL)) ||
        (((unsigned int)maddr > (unsigned int)g_spi_msgmem) &&
        ((unsigned int)maddr < (unsigned int)g_spi_msgmem + SPI_SERVICE_MEM_MSG_TOTAL))) {
            return maddr;
    }

    return NULL;
}

void *spi_service_mpool_alloc(spi_service_mtype_e type, int size)
{
    SPI_SERVICE_CHECK_RETURN(type == SPI_SERVICE_MEM_MSG && size > SPI_SERVICE_MEM_MSG_SIZE, NULL, "msg overflow");
    SPI_SERVICE_CHECK_RETURN(type != SPI_SERVICE_MEM_MSG && size > SPI_SERVICE_MEM_DATA_SIZE, NULL, "data overflow");

    return spi_service_mem_alloc(type);
}

void *spi_service_mpool_free(void *maddr)
{
    return spi_service_mem_free(maddr);
}

void *spi_service_mpbuf_alloc(spi_service_mtype_e type, int size)
{
    struct pbuf *buf = NULL;
    int allocLen = SPI_SERVICE_ALIGN4(PBUF_LINK) + SPI_SERVICE_ALIGN4(sizeof(struct pbuf)) + SPI_SERVICE_ALIGN4(size);

    SPI_SERVICE_CHECK_RETURN(size > SPI_SERVICE_MEM_DATA_MTU, NULL, "data len overflow mtu");
    buf = (struct pbuf *)spi_service_mpool_alloc(type, allocLen);
    SPI_SERVICE_CHECK_RETURN(buf == NULL, NULL, NULL);
    memset((void *)buf, 0, allocLen);
    buf->payload = (void *)((unsigned char *)buf + SPI_SERVICE_ALIGN4(PBUF_LINK) + SPI_SERVICE_ALIGN4(sizeof(struct pbuf)));
    buf->len = buf->tot_len = size;
    buf->type_internal = (unsigned char)PBUF_RAM;
    buf->ref = 1;

    return (void *)buf;
}

void *spi_service_mpbuf_free(struct pbuf *buf)
{
    if (spi_service_mpool_check(buf) != NULL) {
        unsigned int flag = system_irq_save();
        if (buf->ref > 0) {
            buf->ref--;
        }
        system_irq_restore(flag);

        if (buf->ref == 0) {
            return spi_service_mpool_free(buf);
        }

        return NULL;
    }

    return buf;
}

void *spi_service_smem_rx_alloc(spi_service_ctrl_t *mcfg, spi_service_mem_t *smem)
{
    struct pbuf *buf = NULL;
    spi_service_mtype_e type;

    smem->memSlen = smem->memLen = mcfg->len;
    smem->memType = mcfg->evt;

    switch (mcfg->evt) {
        case SPI_SERVICE_TYPE_STOH:
            SPI_SERVICE_CHECK_RETURN(spi_service_loop_alloc(smem) != NULL, smem, NULL);
            /* spi repeater alloc mem send to lwip stack */
            type = (mcfg->net == SPI_SERVICE_NET_STA) ? SPI_SERVICE_MEM_STARX : SPI_SERVICE_MEM_APRX;
            buf = (struct pbuf *)spi_service_mpbuf_alloc(type, mcfg->len);
            if (buf != NULL) {
                smem->memAddr = (unsigned int)buf;
                smem->memOffset = (unsigned int)(buf->payload) - (unsigned int)buf;
                return smem;
            }
            return NULL;

        case SPI_SERVICE_TYPE_HTOS:
            SPI_SERVICE_CHECK_RETURN(spi_service_loop_alloc(smem) != NULL, smem, NULL);
            type = (mcfg->net == SPI_SERVICE_NET_STA) ? SPI_SERVICE_MEM_STARX : SPI_SERVICE_MEM_APRX;
            buf = (struct pbuf *)spi_service_mpbuf_alloc(type, mcfg->len);
            if (buf != NULL) {
                smem->memNet = (mcfg->net == SPI_SERVICE_NET_STA) ? SPI_SERVICE_MEM_STARX : SPI_SERVICE_MEM_APRX;
                smem->memAddr = (unsigned int)buf;
                smem->memOffset = (unsigned int)(buf->payload) - (unsigned int)buf;
                return smem;
            }
            return NULL;
        case SPI_SERVICE_TYPE_OTA:
            type = SPI_SERVICE_MEM_STARX;
            break;
        case SPI_SERVICE_TYPE_BLE:
        case SPI_SERVICE_TYPE_MSG:
            type = SPI_SERVICE_MEM_MSG;
            break;
        default:
            SPI_SERVICE_CHECK_RETURN(-1, NULL, "unkonwn evt");
    }

    buf = spi_service_mpool_alloc(type, mcfg->len);
    if (buf != NULL) {
        smem->memAddr = (unsigned int)buf;
        return smem;
    }
    return NULL;
}

void *spi_service_smem_tx_alloc(spi_service_ctrl_t *mcfg, spi_service_mem_t *smem)
{
    void *sp = NULL;
    struct pbuf *buf = NULL;
    spi_service_mtype_e type;

    smem->memLen = mcfg->len;
    smem->memType = mcfg->evt;

    switch (mcfg->evt) {
        case SPI_SERVICE_TYPE_HTOS:
            type = (mcfg->net == SPI_SERVICE_NET_AP) ? SPI_SERVICE_MEM_APTX : SPI_SERVICE_MEM_STATX;
            buf = spi_service_mpbuf_alloc(type, mcfg->len);
            SPI_SERVICE_CHECK_RETURN(buf == NULL, NULL, NULL);
            smem->memAddr = (unsigned int)buf;
            smem->memOffset = (unsigned int)(buf->payload) - (unsigned int)buf;
            smem->memSlen = ((mcfg->net == SPI_SERVICE_NET_AP) ? SPI_SERVICE_DATA_AP : SPI_SERVICE_DATA_STA) | mcfg->len;
            return smem;
        case SPI_SERVICE_TYPE_BLE:
        case SPI_SERVICE_TYPE_MSG:
            sp = spi_service_mpool_alloc(SPI_SERVICE_MEM_MSG, mcfg->len);
            SPI_SERVICE_CHECK_RETURN(sp == NULL, NULL, NULL);
            smem->memAddr = (unsigned int)sp;
            smem->memSlen = SPI_SERVICE_DATA_MSG | mcfg->len;
            return smem;
        default:
            return NULL;
    }

    return NULL;
}

void *spi_service_smem_free(spi_service_mem_t *smem)
{
    struct pbuf *buf = NULL;

    switch (smem->memType) {
        case SPI_SERVICE_TYPE_STOH:
            buf = (struct pbuf *)smem->memAddr;
            if ((buf->flags & PBUF_FLAG_IS_CUSTOM) != 0) {
                struct pbuf_custom *pc = (struct pbuf_custom *)buf;
                SPI_SERVICE_CHECK_RETURN(pc->custom_free_function == NULL, NULL, "wifi buff not correct");
                pc->custom_free_function(buf);
                break;
            }
            spi_service_mpbuf_free((void *)smem->memAddr);
            break;
        case SPI_SERVICE_TYPE_HTOS:
            spi_service_mpbuf_free((void *)smem->memAddr);
            break;
        case SPI_SERVICE_TYPE_OTA:
        case SPI_SERVICE_TYPE_BLE:
        case SPI_SERVICE_TYPE_MSG:
            spi_service_mpool_free((void *)smem->memAddr);
            break;
        default:
            break;
    }

    return NULL;
}

static void spi_service_mlist_dump(void)
{
    spi_server_mem_priv_t *priv = spi_service_mlist_priv_get();
    unsigned int flag = system_irq_save();
    spi_service_rsvhead_t *rsvhead = NULL;
    int idx;

    os_printf(LM_APP, LL_INFO, "rxnum %d txnum %d msgnum %d\n", priv->rxnum, priv->txnum, priv->msgnum);
    os_printf(LM_APP, LL_INFO, "###########################################\n");
    for (idx = 0; idx < SPI_SERVICE_MEM_NUM_RX; idx++) {
        rsvhead = priv->rxlink[idx].mhead;
        os_printf(LM_APP, LL_INFO, "RXLIST(%d) DATA ADDR:0x%08x TYPE:%d MAGIC:0x%x\n", rsvhead->idx, rsvhead->addr, rsvhead->mtype, rsvhead->magic);
    }

    for (idx = 0; idx < SPI_SERVICE_MEM_NUM_TX; idx++) {
        rsvhead = priv->txlink[idx].mhead;
        os_printf(LM_APP, LL_INFO, "TXLIST(%d) DATA ADDR:0x%08x TYPE:%d MAGIC:0x%x\n", rsvhead->idx, rsvhead->addr, rsvhead->mtype, rsvhead->magic);
    }

    for (idx = 0; idx < SPI_SERVICE_MEM_NUM_MSG; idx++) {
        rsvhead = priv->msglink[idx].mhead;
        os_printf(LM_APP, LL_INFO, "MSGLIST(%d) DATA ADDR:0x%08x TYPE:%d MAGIC:0x%x\n", rsvhead->idx, rsvhead->addr, rsvhead->mtype, rsvhead->magic);
    }

    system_irq_restore(flag);
}

static int spi_service_cmd_dump(cmd_tbl_t *t, int argc, char *argv[])
{
    spi_service_mlist_dump();

    return CMD_RET_SUCCESS;
}

CLI_CMD(spi_mem_dump, spi_service_cmd_dump, "spi service memory info", "spi service mem info");