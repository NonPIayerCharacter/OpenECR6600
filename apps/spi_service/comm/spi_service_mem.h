#ifndef __SPI_SERVICE_MLIST_H__
#define __SPI_SERVICE_MLIST_H__

#include "spi_service_main.h"
#include "platform_list.h"

#define SPI_SERVICE_ALIGN4(val) (((val)+3)&~3)
#define SPI_SERVICE_MEM_DATA_SIZE 2048
#define SPI_SERVICE_MEM_DATA_MTU 1514
#define SPI_SERVICE_MEM_MSG_SIZE 512

#define SPI_SERVICE_MEM_ALLOC 0xA55A
#define SPI_SERVICE_MEM_FREE 0x5AA5

typedef enum {
    SPI_SERVICE_MEM_STARX,
    SPI_SERVICE_MEM_STATX,
    SPI_SERVICE_MEM_APRX,
    SPI_SERVICE_MEM_APTX,
    SPI_SERVICE_MEM_MSG,
} spi_service_mtype_e;

#define SPI_SERVICE_MEM_ALLOC 0xA55A
#define SPI_SERVICE_MEM_FREE 0x5AA5
typedef struct {
    unsigned char idx;
    unsigned char mtype;
    unsigned short magic;
    unsigned int addr;
} spi_service_rsvhead_t;

typedef struct {
    spi_service_rsvhead_t *mhead;
    platform_list_t list;
} spi_service_lnkhead_t;

int spi_service_mem_init(void);
void *spi_service_mpbuf_alloc(spi_service_mtype_e type, int size);
void *spi_service_mpbuf_free(struct pbuf *p);
void *spi_service_mpool_check(void *memPtr);
void *spi_service_mpool_alloc(spi_service_mtype_e type, int size);
void *spi_service_mpool_free(void *memPtr);
void *spi_service_smem_rx_alloc(spi_service_ctrl_t *mcfg, spi_service_mem_t *smem);
void *spi_service_smem_tx_alloc(spi_service_ctrl_t *mcfg, spi_service_mem_t *smem);
void *spi_service_smem_free(spi_service_mem_t *smem);
#endif
