#ifndef __VNET_SERVICE_H__
#define __VNET_SERVICE_H__

#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "spi_service.h"

int vnet_service_wifi_rx(struct pbuf *p, struct netif *inp);
int vnet_service_wifi_rx_free(struct pbuf *p, struct netif *inp);
int vnet_service_wifi_tx(spi_service_mem_t *smem);
int vnet_service_wifi_rx_done(spi_service_mem_t *smem);

#endif