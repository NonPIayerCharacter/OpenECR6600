#ifndef __SPI_REPEATER_H__
#define __SPI_REPEATER_H__

#include "spi_master.h"
#include "lwip/pbuf.h"
#include "system_event.h"
#include "spi_service_main.h"

#define SPI_REPEATER_TASK_PRI 4
#define SPI_REPEATER_TASK_QDEPTH 32
#define SPI_REPEATER_TASK_STACK 4096

int spi_repeater_send_msg(spi_service_msg_t *msg);
int spi_repeater_init(spi_service_func_t funcset);
int spi_repeater_mem_free(struct pbuf *p, struct netif *inp);
int spi_repeater_rx_interrupt(unsigned int type, unsigned int value);
spi_service_mem_t *spi_repeater_get_netinfo(unsigned int addr, unsigned int len);
void spi_repeater_wifi_event(void *ctx, system_event_t *event);

#endif
