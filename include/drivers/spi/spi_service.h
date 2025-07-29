#ifndef __SPI_SERVICE_H__
#define __SPI_SERVICE_H__

#include "spi.h"
#include "chip_pinmux.h"

#define SPI_SERVICE_CHECK_RETURN(condition, ret, logstr) \
    do { \
        if ((condition) != 0) { \
            if ((logstr) != NULL) { \
                os_printf(LM_APP, LL_ERR, "%s[%d]%s, ret %d\r\n", __FUNCTION__, __LINE__, logstr, ret); \
            } \
            return ret; \
        } \
    } while (0)

typedef void *(*spi_service_call)(void *);
typedef struct {
    spi_service_call rxPrepare;
    spi_service_call rxDone;
    spi_service_call txPrepare;
    spi_service_call txDone;
    spi_service_call dataHandle;
} spi_service_func_t;

typedef struct {
    unsigned int memAddr;
    unsigned int memOffset;
    unsigned int memLen;
    unsigned int memSlen;
    unsigned int memType;
    unsigned int memNet;
    unsigned int memNext;
} spi_service_mem_t;

typedef struct {
    /* spi host data type sendto slave */
    unsigned int evt:12;
    /* spi host sta&ap netif data sendto slave */
    unsigned int net:2;
    /* spi host data len sendto slave */
    /* spi host data read ignore it */
    unsigned int len:16;
    /* spi host read or write data about slave */
    unsigned int type:2;
} spi_service_ctrl_t;

typedef enum {
    /* spi host notice read/write event, spi host send (spi_service_ctrl_t)data */
    SPI_SERVICE_EVENT_CTRL,
    /* spi host/slave read data from peer point */
    SPI_SERVICE_EVENT_RX,
    /* spi host/slave write data to peer point */
    SPI_SERVICE_EVENT_TX,
} spi_service_event_e;

#define SPI_SERVICE_GPIO_REG(X)     SPI_SERVICE_GPIO_REG2(X)
#define SPI_SERVICE_GPIO_REG2(X)    IO_MUX_GPIO##X##_REG
#define SPI_SERVICE_GPIO_BIT(X)     SPI_SERVICE_GPIO_BIT2(X)
#define SPI_SERVICE_GPIO_BIT2(X)    IO_MUX_GPIO##X##_BITS
#define SPI_SERVICE_GPIO_OFFSET(X)  SPI_SERVICE_GPIO_OFFSET2(X)
#define SPI_SERVICE_GPIO_OFFSET2(X) IO_MUX_GPIO##X##_OFFSET
#define SPI_SERVICE_GPIO_NUM(X)     SPI_SERVICE_GPIO_NUM2(X)
#define SPI_SERVICE_GPIO_NUM2(X)    GPIO_NUM_##X
#define SPI_SERVICE_GPIO_FUN(X)     SPI_SERVICE_GPIO_FUN2(X)
#define SPI_SERVICE_GPIO_FUN2(X)    FUNC_GPIO##X##_GPIO##X

#ifdef CONFIG_SPI_REPEATER
#ifdef CONFIG_SPI_MASTER
#define SPI_SERVICE_TRANSCTL_MODE   (SPI_TRANSCTRL_DUALQUAD_DUAL | SPI_TRANSCTRL_DUMMY_CNT_2)
#else
#define SPI_SERVICE_TRANSCTL_MODE   SPI_TRANSCTRL_DUALQUAD_DUAL
#endif
#define SPI_SERVICE_READ_CMD        0x0C
#define SPI_SERVICE_WRITE_CMD       0x52
#define SPI_SERVICE_STATE_CMD       0x15
#else
#define SPI_SERVICE_TRANSCTL_MODE   SPI_TRANSCTRL_DUALQUAD_REGULAR
#define SPI_SERVICE_READ_CMD        0x0B
#define SPI_SERVICE_WRITE_CMD       0x51
#define SPI_SERVICE_STATE_CMD       0x05
#endif

typedef enum {
    SPI_SERVICE_TYPE_INFO,
    /* host to slave ethernet data type */
    SPI_SERVICE_TYPE_HTOS = 0x100,
    /* slave to host ethernet data type */
    SPI_SERVICE_TYPE_STOH = 0x200,
    /* host send ota data to update slave firmware */
    SPI_SERVICE_TYPE_OTA = 0x300,
    /* host send ble msg to contorl slave ble module */
    SPI_SERVICE_TYPE_BLE = 0x400,
    /* host send msg to slave and slave send msg to host */
    SPI_SERVICE_TYPE_MSG = 0x500,
    /* just support slave send interrupt to host */
    /* also you can send special msg to instead this */
    /* msg send spi driver queue and wait to send, int send before spi drvier queue */
    /* scence: wifi linkup/linkdown; gpio interrupt sendto host to play music(lockdoor); etc */
    SPI_SERVICE_TYPE_INT,
    SPI_SERVICE_TYPE_MAX
} spi_service_type_e;

/* spi slave status register define */
/* spi slave notice the data type sendto host */
#define SPI_SERVICE_MASK_EVT 0xF000
#define SPI_SERVICE_MASK_DAT 0x0FFF
typedef enum {
    SPI_SERVICE_DATA_STA = 0x1000,
    SPI_SERVICE_DATA_AP = 0x2000,
    SPI_SERVICE_DATA_INFO = 0x3000,
    SPI_SERVICE_DATA_INT = 0x9000,
    SPI_SERVICE_DATA_MSG = 0xA000,
} spi_service_event_t;

typedef enum {
    SPI_SERVICE_NET_STA,
    SPI_SERVICE_NET_AP,
} spi_service_net_e;
#endif
