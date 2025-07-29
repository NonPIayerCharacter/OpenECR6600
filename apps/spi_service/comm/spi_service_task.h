#ifndef __SPI_SERVICE_TASK_H__
#define __SPI_SERVICE_TASK_H__

#include "spi_slave.h"
#include "spi_service_main.h"
#include "system_event.h"
#include "lwip/pbuf.h"

#define SPI_SERVICE_TASK_PRI 4
#define SPI_SERVICE_TASK_QUEUE_DEPTH 32
#define SPI_SERVICE_TASK_STACK 4096

int spi_service_send_task(spi_service_msg_t *msg);
int spi_service_task_init(spi_service_func_t funcset);
int spi_service_send_wifi(spi_service_mem_t *smem);
spi_service_mem_t *spi_service_send_interrput(void);
spi_service_mem_t *spi_service_get_info(unsigned int offset, unsigned int len);
void spi_service_task_wifi_event(void *ctx, system_event_t *event);

#endif

