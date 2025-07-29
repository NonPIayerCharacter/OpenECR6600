#ifndef __SPI_SERVICE_MAIN_H__
#define __SPI_SERVICE_MAIN_H__
#include "config.h"
#include "spi_service.h"
#include "spi_slave.h"
#include "spi_master.h"
#include "lwip/pbuf.h"
#include "lwip/inet.h"

spi_service_mem_t *spi_mqueue_get(void);
void spi_mqueue_put(spi_service_mem_t *queue);
int spi_service_send_msg(unsigned char *data, unsigned int len);
int spi_service_send_pbuf(struct netif *nif, struct pbuf *p);
int spi_service_send_part_info(unsigned int regaddr, unsigned int len);

typedef enum {
    SPI_SERVICE_MSG_BASIC,
    SPI_SERVICE_MSG_OTA,
    SPI_SERVICE_MSG_BLE,
    SPI_SERVICE_MSG_PACKET,
    SPI_SERVICE_MSG_INT,
    SPI_SERVICE_MSG_NET,
} spi_service_msgtrs_e;

typedef void *(*msgfreefunc)(void *);
typedef struct {
    unsigned int msgType;
    unsigned int msgAddr;
    unsigned int msgLen;
    msgfreefunc msgfree;
} spi_service_msg_t;

#ifdef CONFIG_SPI_MASTER
#define SPI_SEND_DATA_TO_PEER(X) spi_master_sendto_slave(X)
#define SPI_SEND_DATA_EVENTYPE SPI_SERVICE_TYPE_HTOS
#define SPI_SEND_MSG_EVENTYPE SPI_SLAVE_TYPE_WRITE
#else
#define SPI_SEND_DATA_TO_PEER(X) spi_slave_sendto_host(X)
#define SPI_SEND_DATA_EVENTYPE SPI_SERVICE_TYPE_STOH
#define SPI_SEND_MSG_EVENTYPE SPI_MASTER_TYPE_WRITE
#endif

//#define SPI_SERVICE_DHCP_HOST

#endif
