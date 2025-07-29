#include "spi_service_main.h"
#include "spi_service_mem.h"
#include "spi_service_loop.h"
#include "pit.h"
#include "cli.h"

#ifdef SPI_SERVICE_LOOP_TEST
unsigned int g_packet_size;
unsigned int g_packet_count;
unsigned int g_time_cnt;

static int spi_service_packet_check(spi_service_mem_t *smem)
{
    unsigned int *pdata = (unsigned int *)smem->memAddr;
    int index;

    for (index = 0; index < smem->memLen/4; index++) {
        SPI_SERVICE_CHECK_RETURN(pdata[index] != index, -1, "data not correct");
    }

    return 0;
}
#endif

int spi_service_packet_pm(spi_service_mem_t *smem)
{
#ifdef SPI_SERVICE_LOOP_TEST
    unsigned int time_cnt;

    g_packet_size += smem->memLen;
    if (g_time_cnt == 0) {
        g_time_cnt = drv_pit_get_tick();
    }

    g_packet_count++;
    time_cnt = drv_pit_get_tick();
    time_cnt -= g_time_cnt;
    if (time_cnt / 40 > 1000000) {
        os_printf(LM_OS, LL_INFO, "%u[%u][%u]\n", g_packet_size, g_packet_count, time_cnt);
        g_time_cnt = 0;
        g_packet_size = 0;
        g_packet_count = 0;
    }

    spi_service_packet_check(smem);
    return 0;
#endif

    return -1;
}

void *spi_service_loop_alloc(spi_service_mem_t *smem)
{
#ifdef SPI_SERVICE_LOOP_TEST
    void *mem = spi_service_mpool_alloc(SPI_SERVICE_MEM_STARX, smem->memLen);
    if (mem != NULL) {
        smem->memAddr = (unsigned int)mem;
    } else {
        smem->memLen = smem->memSlen = 0;
    }

    return smem;
#endif

    return NULL;
}

void *spi_service_loop_free(spi_service_mem_t *smem)
{
#ifdef SPI_SERVICE_LOOP_TEST
    spi_service_mpool_free((void *)smem->memAddr);

    return NULL;
#endif

    return smem;
}

#ifdef SPI_SERVICE_LOOP_TEST
static int spi_service_send_loop(cmd_tbl_t *t, int argc, char *argv[])
{
    spi_service_mem_t *smem = NULL;
    unsigned int *pdata = NULL;
    int index;

    do {
        smem = spi_mqueue_get();
        SPI_SERVICE_CHECK_RETURN(smem == NULL, CMD_RET_FAILURE, "cannot get mqueue");
        pdata = (unsigned int *)spi_service_mpool_alloc(SPI_SERVICE_MEM_STATX, 1460);
        if (pdata == NULL) {
            spi_mqueue_put(smem);
            os_msleep(1);
            continue;
        }
        for (index = 0; index < 1460/sizeof(unsigned int); index++) {
            pdata[index] = index;
        }

        smem->memAddr = (unsigned int)pdata;
        smem->memLen = smem->memSlen = 1460;
        smem->memType = SPI_SEND_DATA_EVENTYPE;
        smem->memOffset = 0;

        SPI_SEND_DATA_TO_PEER(smem);
    } while(1);

    return CMD_RET_SUCCESS;
}

CLI_CMD(spi_service_loop, spi_service_send_loop, "spi_service_send_loop", "spi_service_send_loop");
#endif