#ifndef _BLE_SPI_CMD_H
#define _BLE_SPI_CMD_H

#include <stdint.h>

#define BLE_SPI_TYPE_MASK         0x03
#define BLE_SPI_SUBTYPE_MASK      0xFC
#define BLE_SPI_SUBTYPE_SHIFT     2

#define BLE_SPI_GET_TYPE(type)    ((type) & BLE_SPI_TYPE_MASK)
#define BLE_SPI_SUBTYPE(type)   (((type) & BLE_SPI_SUBTYPE_MASK) >> BLE_SPI_SUBTYPE_SHIFT)
#define BLE_SPI_BUILD_TYPE(type, subtype) (((type) & BLE_SPI_TYPE_MASK) | ((subtype) << BLE_SPI_SUBTYPE_SHIFT))

#define BLE_SPI_TYPE_CTRL                                 0x0
#define BLE_SPI_TYPE_CTRL_SUBTYPE_INIT                    0x00
#define BLE_SPI_TYPE_CTRL_SUBTYPE_DEINIT                  0x01
#define BLE_SPI_TYPE_CTRL_SUBTYPE_START_ADV               0x02
#define BLE_SPI_TYPE_CTRL_SUBTYPE_STOP_ADV                0x03
#define BLE_SPI_TYPE_CTRL_SUBTYPE_START_SCAN              0x04
#define BLE_SPI_TYPE_CTRL_SUBTYPE_STOP_SCAN               0x05
#define BLE_SPI_TYPE_CTRL_SUBTYPE_CONNECT                 0x06
#define BLE_SPI_TYPE_CTRL_SUBTYPE_DISCONNECT              0x07
#define BLE_SPI_TYPE_CTRL_SUBTYPE_GETADDR                 0x08

#define BLE_SPI_TYPE_DATA                                 0x1
#define BLE_SPI_TYPE_DATA_SUBTYPE_SEND_MTU                0x00
#define BLE_SPI_TYPE_DATA_SUBTYPE_SEND_WRITE              0x01
#define BLE_SPI_TYPE_DATA_SUBTYPE_SEND_READ               0x02
#define BLE_SPI_TYPE_DATA_SUBTYPE_SEND_NOTIFY             0x03
#define BLE_SPI_TYPE_DATA_SUBTYPE_RECV_MTU_RSP            0x04
#define BLE_SPI_TYPE_DATA_SUBTYPE_RECV_WRITE              0x05
#define BLE_SPI_TYPE_DATA_SUBTYPE_RECV_READ_RSP           0x06
#define BLE_SPI_TYPE_DATA_SUBTYPE_RECV_NOTIFY             0x07

#define BLE_SPI_TYPE_EVENT                                0x2
#define BLE_SPI_TYPE_EVENT_SUBTYPE_ADV_REPORT             0x00
#define BLE_SPI_TYPE_EVENT_SUBTYPE_CONNECT                0x01
#define BLE_SPI_TYPE_EVENT_SUBTYPE_DISCONNECT             0x02
#define BLE_SPI_TYPE_EVENT_SUBTYPE_SEND_WRITE             0x03 /*Reports the status of whether the write send is successful*/
#define BLE_SPI_TYPE_EVENT_SUBTYPE_SEND_NOTIFY            0x04
#define BLE_SPI_TYPE_EVENT_SUBTYPE_GETADDR                0x05
#define BLE_SPI_TYPE_EVENT_SUBTYPE_MTUREQ                 0x06

#define BLE_SPI_TYPE_IS_CTRL(type)        (BLE_SPI_GET_TYPE((type)) == BLE_SPI_TYPE_CTRL)
#define BLE_SPI_TYPE_IS_DATA(type)        (BLE_SPI_GET_TYPE((type)) == BLE_SPI_TYPE_DATA)
#define BLE_SPI_TYPE_IS_EVENT(type)       (BLE_SPI_GET_TYPE((type)) == BLE_SPI_TYPE_EVENT)

/// Connection role
enum ble_spi_role
{
    /// Master
    BLE_ROLE_MASTER = 0,
    /// Slave
    BLE_ROLE_SLAVE,
};

typedef void *(*blemsgfreefunc)(void *);
typedef struct {
    unsigned int msgType;
    unsigned int msgAddr;
    unsigned int msgLen;
    blemsgfreefunc msgfree;
} ble_spi_msg_t;

/* BLE SPI protocol */
struct ble_spi_hdr{
    uint8_t type;    /*type: lower 2 bit, subtype: high 6 bit*/
    uint8_t data_len;
    uint8_t data[0];
};

int ble_spi_role_init(uint8_t role);
void ble_spi_role_deinit(void);
int ble_spi_adv_start(void);
int ble_spi_adv_stop(void);
int ble_spi_start_scan(void);
int ble_spi_stop_scan(void);
int ble_spi_start_connect(uint8_t *peer_addr);
void ble_spi_start_disconnect();
void ble_spi_get_self_addr();

void ble_spi_gattc_exchange_mtu_req(uint16_t mtu_val);
void ble_spi_gattc_write(uint16_t char_handle, uint16_t data_len, uint8_t *data);
void ble_spi_gattc_read(uint16_t char_handle);
void ble_spi_gatts_notify(uint8_t *data, uint16_t data_len);

int ble_spi_send_msg(ble_spi_msg_t *msg);
void ble_spi_msg_recv_handler(uint8_t *data, int len);
#endif
