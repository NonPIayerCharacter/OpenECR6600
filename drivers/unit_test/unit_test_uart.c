#include "cli.h"
#include "hal_uart.h"
#include "stdlib.h"

#include "oshal.h"
#include "chip_pinmux.h"
#include "uart.h"
#define TEST_UART_BUF_SIZE	2048

static unsigned char  uart_buffer[TEST_UART_BUF_SIZE]  __attribute__((section(".dma.data")));

void utest_uart_isr(void * data)
{
	os_printf(LM_CMD,LL_INFO,"unit test uart, uart-isr comes!\r\n");
}

static int utest_uart_open(cmd_tbl_t *t, int argc, char *argv[])
{
	int uart_baudrate, tx_mode, rx_mode;
	chip_uart1_pinmux_cfg(0);

	if (argc >= 4)
	{
		uart_baudrate = (int)strtoul(argv[1], NULL, 0);
		tx_mode = (int)strtoul(argv[2], NULL, 0);
		rx_mode = (int)strtoul(argv[3], NULL, 0);
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, err: no uart-num input!\r\n");
		return 0;
	}
	
	T_DRV_UART_CONFIG config;	
	config.uart_baud_rate = uart_baudrate;
	config.uart_data_bits = UART_DATA_BITS_8;
	config.uart_stop_bits = UART_STOP_BITS_1;
	config.uart_parity_bit = UART_PARITY_BIT_NONE;
	config.uart_flow_ctrl = UART_FLOW_CONTROL_DISABLE;
	config.uart_tx_mode = tx_mode;
	config.uart_rx_mode = rx_mode;
	config.uart_rx_buf_size = 1024;	

	int i;
	for(i=0; i<TEST_UART_BUF_SIZE; i++)
	{
		uart_buffer[i] = (unsigned char)i%256;
	}

	if (hal_uart_open(1, &config) == 0)
	{
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, uart1 open ok!\r\n");
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, uart1 open failed!\r\n");
	}

	return 0;
}

CLI_SUBCMD(ut_uart, open, utest_uart_open, "unit test uart open", "ut_uart open [uart-baudrate] [tx_mode] [rx_mode]");


static int utest_uart_read(cmd_tbl_t *t, int argc, char *argv[])
{
    int  uart_len;

	if (argc >= 2)
	{
		uart_len = (int)strtoul(argv[1], NULL, 0);
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, err: no enough argc!\r\n");
		return 0;
	}
	
    if(uart_len > TEST_UART_BUF_SIZE)
	{
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, err: input length error!\r\n");
	}

	if (hal_uart_read(1, uart_buffer, uart_len, WAIT_FOREVER) == uart_len)
	{
		int i;
		for(i=0; i<uart_len; i++)
		{
			os_printf(LM_CMD,LL_INFO,"\r\nuartbuffer[%d] == 0x%x\r\n",i,uart_buffer[i]);
		}
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, uart1 rx ok!\r\n");
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, uart1 rx failed!\r\n");
	}

	return 0;
}

CLI_SUBCMD(ut_uart, read, utest_uart_read, "unit test uart read", "ut_uart read [uart-len]");


static int utest_uart_write(cmd_tbl_t *t, int argc, char *argv[])
{
    int uart_len;

	if (argc >= 2)
	{
		uart_len = (int)strtoul(argv[1], NULL, 0);
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, err: no enough argc!\r\n");
		return 0;
	}
	
    if(uart_len > TEST_UART_BUF_SIZE)
	{
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, err: input length error!\r\n");
	}

	if (hal_uart_write(1, uart_buffer, uart_len) == 0)
	{
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, uart1 tx ok!\r\n");
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, uart1 tx failed!\r\n");
	}

	return 0;
}

CLI_SUBCMD(ut_uart, write, utest_uart_write, "unit test uart write", "ut_uart write [uart-len]");




static int utest_uart_close(cmd_tbl_t *t, int argc, char *argv[])
{
	if (hal_uart_close(1) == 0)
	{
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, uart1 close ok!\r\n");
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, uart1 close failed!\r\n");
	}

	return 0;
}

CLI_SUBCMD(ut_uart, close, utest_uart_close, "unit test uart close", "ut_uart close");


#include "flash.h"
#include "aon_reg.h"
unsigned int length;
static unsigned int num;
static int task_uart_handle;
static int task_flash_handle;

void task_pt2(void *arg)  // uart
{
	int num = *((int *)arg);
	T_DRV_UART_CONFIG config;	
	config.uart_baud_rate = 115200;
	config.uart_data_bits = UART_DATA_BITS_8;
	config.uart_stop_bits = UART_STOP_BITS_1;
	config.uart_parity_bit = UART_PARITY_BIT_NONE;
	config.uart_flow_ctrl = UART_FLOW_CONTROL_DISABLE;
	config.uart_tx_mode = UART_TX_MODE_POLL;
	config.uart_rx_mode = UART_RX_MODE_DMA_POLLLIST;
    config.uart_rx_buf_size = 1024;
    if(num ==1)
    {
		chip_uart1_pinmux_cfg(0);
    }
	else if(num == 2)
	{
		PIN_FUNC_SET(IO_MUX_GPIO17, FUNC_GPIO17_UART2_RXD);
		PIN_FUNC_SET(IO_MUX_GPIO13, FUNC_GPIO13_UART2_TXD);
		PIN_MUX_SET_REG(AON_PAD_MODE_REG,PIN_MUX_GET_REG(AON_PAD_MODE_REG) & (~(1 << 17)));
		PIN_MUX_SET_REG(AON_PAD_MODE_REG,PIN_MUX_GET_REG(AON_PAD_MODE_REG) & (~(1 << 13)));
	}
    
	if (drv_uart_open(num, &config) == 0)
	{
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, uart1 open ok!\r\n");
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, uart1 open failed!\r\n");
	}
	
    char buff[1024];
    int len = 0;
	
	int index = 0;
	int rx_len = 0;
	unsigned int i = 0;
    while(1)
    {
    	index++;
        len = drv_uart_read(num, buff, 300, 1000);
		os_printf(LM_OS, LL_INFO, "1111recv_buf:len:%d\r\n", len);
		if(len < 300)
		{
			len += drv_uart_read(num, &buff[len], 300-len, 1000);
			os_printf(LM_OS, LL_INFO, "222recv_buf:len:%d\r\n", len);
		}
		length += len;

		if (len != 300 && len != 0)
		{
			os_printf(LM_OS, LL_INFO, "U2 test error\r\n");
			rx_len = 1;
		}
		if (rx_len == 0)
		{
			for (i = 0; i < len; i++)
			{
				if ((*(buff + i)-48) != (i % 10))
				{
					rx_len = 1;
				}
			}
			if (rx_len == 1)
			{
				os_printf(LM_OS, LL_INFO, "\n************U2 data error\r\n");
				for (i = 0; i < len; i++)
				{
					os_printf(LM_OS, LL_INFO," %d", *(buff + i));
							if (i % 20 == 0)
								os_printf(LM_OS, LL_INFO,"\n");
				}
				
				if(task_uart_handle)
				{
					os_printf(LM_CMD,LL_INFO,"\r\ndelete uart task!\r\n");
					os_task_delete(task_uart_handle);
					task_uart_handle = 0;
				}
				if(task_flash_handle)
				{
					os_printf(LM_CMD,LL_INFO,"\r\ndelete flash task!\r\n");
					os_task_delete(task_flash_handle);
					task_flash_handle = 0;
				}
				break;
				rx_len = 0;
			}
			else
			{
				os_printf(LM_OS, LL_INFO, "U2 test OK\r\n");
			}
		}
//	os_printf(0,0,"length = %d \n",length);
	}
	hal_uart_close(num);
}

void task_pt11(void *arg)  // flash
{
    unsigned int addr =0x1c0000;
    //unsigned int len = 0x30000;
    unsigned int len = 0x4000;
    int index = 0;
    while(1)
    {
        os_printf(0,0,"flash test %d \n", index++);
        drv_spiflash_erase(addr, len);
        drv_spiflash_write(addr, (unsigned char*)0x60000, len);
    }
}

static int pt_cmd(cmd_tbl_t *h, int argc, char *argv[])
{
	if (argc >= 2)
	{
		num = (int)strtoul(argv[1], NULL, 0);
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,"\r\nunit test uart, err: no enough argc!\r\n");
		return 0;
	}
	if(num>=0 && num<=2) 
	{
		task_uart_handle = os_task_create("pt2", 4, 4096, task_pt2, &num);
		task_flash_handle = os_task_create("pt11", 4, 4096, task_pt11, NULL);
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,"\r\ndelete task!\r\n");
		if(task_uart_handle)
		{
			os_printf(LM_CMD,LL_INFO,"\r\ndelete uart task!\r\n");
			os_task_delete(task_uart_handle);
			task_uart_handle = 0;
		}
		
		if(task_flash_handle)
		{
			os_printf(LM_CMD,LL_INFO,"\r\ndelete flash task!\r\n");
			os_task_delete(task_flash_handle);
			task_flash_handle = 0;
		}
	}
	return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_uart, dma_loop_pt, pt_cmd, "unit test uart close", "ut_uart close");


CLI_CMD(ut_uart, NULL, "unit test uart", "test_uart");

