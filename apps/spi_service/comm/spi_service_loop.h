#ifndef __SPI_SERVICE_LOOP_H__
#define __SPI_SERVICE_LOOP_H__

//#define SPI_SERVICE_LOOP_TEST

int spi_service_packet_pm(spi_service_mem_t *smem);
void *spi_service_loop_alloc(spi_service_mem_t *smem);
void *spi_service_loop_free(spi_service_mem_t *smem);

#endif