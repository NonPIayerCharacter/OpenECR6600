#include "spi_service_main.h"
#include "spi_service_loop.h"
#include "spi_service_task.h"
#include "spi_service_mem.h"
#include "spi_repeater.h"
#include "vnet_register.h"
#include "system_event.h"
#ifdef CONFIG_BLE_EXAMPLES_SPI_SERVER
#include "ble_spi_cmd.h"
#endif

/* smem used for spi transfor data, smem take the mem attach to spi driver send queue */
/* spi driver giveback the smem at spi finish time */
#define SPI_SMEM_QUEUE_MAX 128
typedef struct spi_smem_queue_ {
    spi_service_mem_t smem;
    struct spi_smem_queue_ *next;
} spi_smem_queue_t;
spi_smem_queue_t *g_smem_queue;

static void spi_mqueue_init(void)
{
    int idx;
    spi_smem_queue_t *mqueue;

    for (idx = 0; idx < SPI_SMEM_QUEUE_MAX; idx++) {
        mqueue = (spi_smem_queue_t *)os_malloc(sizeof(spi_smem_queue_t));
        if (mqueue != NULL) {
            memset(mqueue, 0, sizeof(spi_smem_queue_t));
            mqueue->next = g_smem_queue;
            g_smem_queue = mqueue;
        }
    }
}

spi_service_mem_t *spi_mqueue_get(void)
{
    unsigned int flag = system_irq_save();
    spi_smem_queue_t *mqueue = NULL;

    if (g_smem_queue != NULL) {
        mqueue = g_smem_queue;
        g_smem_queue = g_smem_queue->next;
        mqueue->next = NULL;
    }
    system_irq_restore(flag);
    return (spi_service_mem_t *)mqueue;
}

void spi_mqueue_put(spi_service_mem_t *queue)
{
    unsigned int flag = system_irq_save();
    spi_smem_queue_t *mqueue = (spi_smem_queue_t *)queue;

    if (mqueue != NULL) {
        memset(mqueue, 0, sizeof(spi_smem_queue_t));
        mqueue->next = g_smem_queue;
        g_smem_queue = mqueue;
    }

    system_irq_restore(flag);
}

static void *spi_service_rxhandle(void *arg)
{
    spi_service_mem_t *smem = (spi_service_mem_t *)arg;
    spi_service_msg_t msg = {0};
#ifdef CONFIG_BLE_EXAMPLES_SPI_SERVER
    ble_spi_msg_t blemsg = {0};
#endif
    int ret;

    switch (smem->memType) {
        case SPI_SERVICE_TYPE_HTOS:
            ret = spi_service_packet_pm(smem);
            SPI_SERVICE_CHECK_RETURN(ret == 0, NULL, NULL);
            spi_service_send_wifi(smem);
            break;

        case SPI_SERVICE_TYPE_STOH:
            ret = spi_service_packet_pm(smem);
            SPI_SERVICE_CHECK_RETURN(ret == 0, NULL, NULL);
            msg.msgType = SPI_SERVICE_MSG_PACKET;
            msg.msgAddr = smem->memAddr;
            msg.msgfree = spi_service_mpool_free;
            smem->memType = SPI_SERVICE_TYPE_MAX;
            if (spi_repeater_send_msg(&msg) != 0) {
                smem->memType = SPI_SERVICE_TYPE_STOH;
            }
            break;

        case SPI_SERVICE_TYPE_BLE:
#ifdef CONFIG_BLE_EXAMPLES_SPI_SERVER
            blemsg.msgAddr = smem->memAddr;
            blemsg.msgLen = smem->memLen;
            blemsg.msgfree = spi_service_mpool_free;
            smem->memType = SPI_SERVICE_TYPE_MAX;
            if (ble_spi_send_msg(&blemsg) != 0) {
                smem->memType = SPI_SERVICE_TYPE_BLE;
            }
#endif
            break;

        case SPI_SERVICE_TYPE_OTA:
            msg.msgType = SPI_SERVICE_MSG_OTA;
            msg.msgAddr = smem->memAddr;
            msg.msgLen = smem->memLen;
            msg.msgfree = spi_service_mpool_free;
            /* OTA handle the mem and msgfree to release it */
            /* rxDone ignore the memtype as unkown */
            smem->memType = SPI_SERVICE_TYPE_MAX;
            if (spi_service_send_task(&msg) != 0) {
                /* rxDone to release the mem, set back the memtype */
                smem->memType = SPI_SERVICE_TYPE_OTA;
            }
            break;

        case SPI_SERVICE_TYPE_MSG:
            msg.msgType = SPI_SERVICE_MSG_BASIC;
            msg.msgAddr = smem->memAddr;
            msg.msgLen = smem->memLen;
            msg.msgfree = spi_service_mpool_free;
            smem->memType = SPI_SERVICE_TYPE_MAX;
            if ((spi_service_send_task(&msg) != 0) && (spi_repeater_send_msg(&msg) != 0)) {
                smem->memType = SPI_SERVICE_TYPE_MSG;
            }
            break;

        default:
            msg.msgType = SPI_SERVICE_MSG_NET;
            msg.msgAddr = smem->memAddr;
            msg.msgLen = smem->memLen;
            /* SPI MASTER read SLAVE netinfo */
            spi_repeater_send_msg(&msg);
            /* SPI MASTER set netinfo to SLAVE */
            spi_service_send_task(&msg);
            break;
    }

    return 0;
}

static void *spi_service_rx_prepare(void *arg)
{
    spi_service_ctrl_t *scfg = (spi_service_ctrl_t *)arg;
    spi_service_mem_t *smem = NULL;

    /* SPI master handle interrupt event */
    SPI_SERVICE_CHECK_RETURN(spi_repeater_rx_interrupt(scfg->evt, scfg->len) == 0, NULL, NULL);
    smem = spi_repeater_get_netinfo(scfg->evt, scfg->len);
    SPI_SERVICE_CHECK_RETURN(smem != NULL, smem, NULL);
    /* SPI master set netinfo to slave */
    smem = spi_service_get_info(scfg->evt, scfg->len);
    SPI_SERVICE_CHECK_RETURN(smem != NULL, smem, NULL);

    smem = spi_mqueue_get();
    if (smem != NULL) {
        if (spi_service_smem_rx_alloc(scfg, smem) == NULL) {
            spi_mqueue_put(smem);
            smem = NULL;
        }
    }

    return smem;
}

static void *spi_service_rx_done(void *arg)
{
    /* rxdataHanle take the mem and free it, like the data sendto TCPIP or WIFI? but spi driver error? */
    spi_service_mem_t *smem = (spi_service_mem_t *)arg;

    spi_service_smem_free(smem);
    spi_mqueue_put(smem);
    return 0;
}

static void *spi_service_tx_prepare(void *arg)
{
    spi_service_ctrl_t *scfg = (spi_service_ctrl_t *)arg;
    spi_service_mem_t *smem = spi_service_send_interrput();

    /* SPI SLAVE send interrupt higher */
    SPI_SERVICE_CHECK_RETURN(smem != NULL, smem, NULL);
    /* SPI HOST read netinfo evt */
    smem = spi_service_get_info(scfg->evt, scfg->len);
    SPI_SERVICE_CHECK_RETURN(smem != NULL, smem, NULL);

    /* SPI SLAVE get driver queue data to send */
    return NULL;
}

static void *spi_service_tx_done(void *arg)
{
    spi_service_mem_t *smem = (spi_service_mem_t *)arg;

    spi_service_smem_free(smem);
    spi_mqueue_put(smem);

    return NULL;
}

/* HOST send msg to SLAVE or SLAVE send msg to HOST */
int spi_service_send_msg(unsigned char *data, unsigned int len)
{
    spi_service_ctrl_t mcfg = {0};
    spi_service_mem_t *smem = NULL;

    SPI_SERVICE_CHECK_RETURN(len == 0, -1, "length not correct");
    smem = spi_mqueue_get();
    SPI_SERVICE_CHECK_RETURN(smem == NULL, -1, "cannot get mqueue");

    mcfg.evt = SPI_SERVICE_TYPE_MSG;
    mcfg.type = SPI_SEND_MSG_EVENTYPE;
    mcfg.len = len;
    if (spi_service_smem_tx_alloc(&mcfg, smem) == NULL) {
        spi_mqueue_put(smem);
        return -1;
    }

    memcpy((void *)smem->memAddr, data, len);
    smem->memSlen = SPI_SERVICE_DATA_MSG | len;
    SPI_SEND_DATA_TO_PEER(smem);

    return 0;
}

/* spi master write part netinfo to slave */
int spi_service_send_part_info(unsigned int regaddr, unsigned int len)
{
    spi_service_mem_t *smem = NULL;
    unsigned int infoaddr = (unsigned int)vnet_reg_get_addr();

    SPI_SERVICE_CHECK_RETURN(regaddr + len > sizeof(vnet_reg_t), -1, "length not correct");
    smem = spi_mqueue_get();
    SPI_SERVICE_CHECK_RETURN(smem == NULL, -1, "cannot get mqueue");

    smem->memType = regaddr;
    smem->memAddr = infoaddr + regaddr;
    smem->memLen = len;
    smem->memSlen = len;
    SPI_SEND_DATA_TO_PEER(smem);

    return 0;
}

/* spi master send tcpip packet */
int spi_service_send_pbuf(struct netif *nif, struct pbuf *p)
{
    spi_service_ctrl_t mcfg = {0};
    spi_service_mem_t *smem = NULL;
    unsigned int offset = 0;

    smem = spi_mqueue_get();
    SPI_SERVICE_CHECK_RETURN(smem == NULL, -1, "cannot get mqueue");
    mcfg.evt = SPI_SEND_DATA_EVENTYPE;
    mcfg.type = SPI_SEND_MSG_EVENTYPE;
    mcfg.len = p->tot_len;
    if (spi_service_smem_tx_alloc(&mcfg, smem) == NULL) {
        spi_mqueue_put(smem);
        return -1;
    }

    while (p != NULL) {
        memcpy((void *)(smem->memAddr + smem->memOffset + offset), p->payload, p->len);
        offset += p->len;
        p = p->next;
    }
    SPI_SEND_DATA_TO_PEER(smem);

    return 0;
}

static int32_t spi_service_wifi_event_handle(void *ctx, system_event_t *event)
{
    spi_repeater_wifi_event(ctx, event);
    spi_service_task_wifi_event(ctx, event);
    
    return 0;
}

void spi_service_main(void)
{
    spi_service_func_t funcset;

    spi_service_mem_init();
    spi_mqueue_init();

    /* SPI RX: rxprepare=>dataHandle=>rxdone */
    /* SPI RX: rxprepare=>rxdone(spi driver error) */
    /* SPI TX: txprepare=>txdone */
    funcset.rxPrepare = spi_service_rx_prepare;
    funcset.rxDone = spi_service_rx_done;
    funcset.txPrepare = spi_service_tx_prepare;
    funcset.txDone = spi_service_tx_done;
    funcset.dataHandle = spi_service_rxhandle;

    spi_repeater_init(funcset);
    spi_service_task_init(funcset);

    /* wifi event user handle after wifi default handle */
    sys_event_loop_init(spi_service_wifi_event_handle, NULL);
}
