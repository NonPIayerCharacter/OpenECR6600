#include "ci1302_uart.h"
#include "chip_pinmux.h"
#include "uart.h"
#include <string.h>

#ifndef CONFIG_CI1302_UART_BAUDRATE
#define CONFIG_CI1302_UART_BAUDRATE 921600
#endif

int ci1302_uart_open(void)
{
    T_DRV_UART_CONFIG config;
    memset(&config, 0, sizeof(config));
    config.uart_baud_rate = CONFIG_CI1302_UART_BAUDRATE;
    config.uart_data_bits = UART_DATA_BITS_8;
    config.uart_stop_bits = UART_STOP_BITS_1;
    config.uart_parity_bit = UART_PARITY_BIT_NONE;
    config.uart_flow_ctrl = UART_FLOW_CONTROL_DISABLE;
    config.uart_tx_mode = UART_TX_MODE_DMA;
    config.uart_rx_mode = UART_RX_MODE_DMA_POLLLIST;
    config.uart_rx_buf_size = 1024;
    chip_uart1_pinmux_cfg(0);
    return drv_uart_open(E_UART_NUM_1, &config);
}

int ci1302_uart_close(void) { return drv_uart_close(E_UART_NUM_1); }
int ci1302_uart_read(void *data, unsigned int length, unsigned int timeout_ms)
{
    return drv_uart_read(E_UART_NUM_1, data, length, timeout_ms);
}
int ci1302_uart_write(const void *data, unsigned int length)
{
    return drv_uart_write(E_UART_NUM_1, (char *)data, length);
}
