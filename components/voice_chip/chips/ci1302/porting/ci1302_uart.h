#ifndef CI1302_UART_H
#define CI1302_UART_H

int ci1302_uart_open(void);
int ci1302_uart_close(void);
int ci1302_uart_read(void *data, unsigned int length, unsigned int timeout_ms);
int ci1302_uart_write(const void *data, unsigned int length);

#endif
