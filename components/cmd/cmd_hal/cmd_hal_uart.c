

#include "uart.h"
#include "wdt.h"

#include "cli.h"
#include "chip_pinmux.h"
#include "cmd_core_comm.h"
#include "rtos_debug.h"
#include "hal_system.h"

#ifdef CONFIG_CUSTOM_YED
#include "cmd_hal_uart_reliable.h"
#endif

#ifdef CONFIG_CLI
#include "cmd_core_cli.h"
#endif

#define CMD_HAL_UART_RX_BUF_LEN		1024


typedef struct _CMD_HAL_UART
{
	CMD_HAL_DEV_OPS ops;
	unsigned char cmd_buff[CMD_HAL_UART_RX_BUF_LEN];
	unsigned short cmd_buff_rd;
	unsigned short cmd_buff_wr;
	void (*inform)();
	unsigned int cmd_uart_base;
	E_DRV_UART_NUM cmd_uart_num;
	
#ifdef CONFIG_CUSTOM_YED
	yed_uart_reliable_t *reliable;
#endif

} CMD_HAL_UART;

static CMD_HAL_UART * cmd_dev_uart[E_UART_NUM_MAX];

int g_assert_index = 0;
static char s_assert_in_isr[] = "assert_in_isr";
int g_reboot_index = 0;
static char s_reboot_in_isr[] = "scnyscnyscnys";

#ifdef CONFIG_CUSTOM_YED
static void cmd_hal_uart_reliable_replay(void *arg, const uint8_t *data,
					 unsigned int len)
{
	CMD_HAL_UART *p_cmd_dev_uart = (CMD_HAL_UART *)arg;
	unsigned int i;

	for (i = 0; i < len; i++)
	{
		if (((p_cmd_dev_uart->cmd_buff_wr + 1) % CMD_HAL_UART_RX_BUF_LEN) ==
		    p_cmd_dev_uart->cmd_buff_rd)
		{
			continue;
		}

		p_cmd_dev_uart->cmd_buff[p_cmd_dev_uart->cmd_buff_wr++] = data[i];
		p_cmd_dev_uart->cmd_buff_wr %= CMD_HAL_UART_RX_BUF_LEN;
	}
}

static void cmd_hal_uart_reliable_notify(void *arg)
{
	CMD_HAL_UART *p_cmd_dev_uart = (CMD_HAL_UART *)arg;

	p_cmd_dev_uart->inform();
}
#endif

void cmd_hal_uart_isr(void * data)
{
	CMD_HAL_UART *p_cmd_dev_uart = (CMD_HAL_UART *)data;
	unsigned int uart_base_reg = p_cmd_dev_uart->cmd_uart_base;
	char temp_char;

	while (drv_uart_rx_tstc(uart_base_reg))
	{
		temp_char = drv_uart_rx_getc(uart_base_reg);

		if (temp_char != s_assert_in_isr[g_assert_index])
		{
			g_assert_index = 0;
		}
		else
		{
			if (++g_assert_index == 13)
			{
				drv_wdt_stop();
				system_assert(0);
			}
		}
        if (temp_char != s_reboot_in_isr[g_reboot_index])
        {
            g_reboot_index = 0;
        }
        else
        {
            if (++g_reboot_index == 13)
            {
                // 在中断上下文中不能调用 hal_system_reset()，因为其中的 reset handlers 可能不是中断安全的
                // 直接设置重启原因并调用底层 WDT 重启
                hal_set_reset_type(RST_TYPE_SOFTWARE_REBOOT);
                drv_wdt_chip_reset();
            }
        }

#ifdef CONFIG_CUSTOM_YED
		if (p_cmd_dev_uart->reliable)
		{
			yed_uart_reliable_rx_byte(p_cmd_dev_uart->reliable,
						  (unsigned char)temp_char);
			continue;
		}
#endif

		if (((p_cmd_dev_uart->cmd_buff_wr + 1) % CMD_HAL_UART_RX_BUF_LEN) == p_cmd_dev_uart->cmd_buff_rd)
		{
			drv_uart_send_poll(p_cmd_dev_uart->cmd_uart_num, "uart isr no buf!\n", 16);
			continue;
		}

		p_cmd_dev_uart->cmd_buff[p_cmd_dev_uart->cmd_buff_wr++] = temp_char;
		p_cmd_dev_uart->cmd_buff_wr %= CMD_HAL_UART_RX_BUF_LEN;
	}

	p_cmd_dev_uart->inform();
}


int cmd_hal_uart_getc(void * dev, char * c)
{
	CMD_HAL_UART *p_cmd_dev_uart = (CMD_HAL_UART *)dev;

	unsigned int flags = system_irq_save();

	if (p_cmd_dev_uart->cmd_buff_wr != p_cmd_dev_uart->cmd_buff_rd)
	{
		*c = (char)p_cmd_dev_uart->cmd_buff[p_cmd_dev_uart->cmd_buff_rd++];
		p_cmd_dev_uart->cmd_buff_rd %= CMD_HAL_UART_RX_BUF_LEN;
		system_irq_restore(flags);
		return 	0;
	}
	else
	{
		system_irq_restore(flags);		
		return 	1;
	}
}

bool cmd_hal_uart_buf_is_empty(void * dev)
{
    CMD_HAL_UART *p_cmd_dev_uart = (CMD_HAL_UART *)dev;

	unsigned int flags = system_irq_save();

	if (p_cmd_dev_uart->cmd_buff_wr != p_cmd_dev_uart->cmd_buff_rd)
	{
		system_irq_restore(flags);
		return 	0;
	}
	else
	{
		system_irq_restore(flags);		
		return 	1;
	}
}

int cmd_hal_uart_puts(void * dev, char * str, unsigned int len)
{
	CMD_HAL_UART *p_cmd_dev_uart = (CMD_HAL_UART *)dev;

#ifdef CONFIG_CUSTOM_YED
	if (p_cmd_dev_uart->reliable)
	{
		return yed_uart_reliable_write(p_cmd_dev_uart->reliable,
						(unsigned char *)str, len);
	}
#endif

	return drv_uart_send_poll(p_cmd_dev_uart->cmd_uart_num, str, len);
}


unsigned int cmd_hal_get_uart_base(void)
{
#ifdef CONFIG_CLI_UART_0
	return cmd_dev_uart[E_UART_NUM_0]->cmd_uart_base;
#endif

#ifdef CONFIG_CLI_UART_1
	return cmd_dev_uart[E_UART_NUM_1]->cmd_uart_base;
#endif

#ifdef CONFIG_CLI_UART_2
	return cmd_dev_uart[E_UART_NUM_2]->cmd_uart_base;
#endif

	return 0;
}


void *cmd_hal_uart_init(E_DRV_UART_NUM uart_num, void * cb, T_DRV_UART_CONFIG * p_config)
{
	T_DRV_UART_CONFIG config;
	T_UART_ISR_CALLBACK callback;
#ifdef CONFIG_CUSTOM_YED
	int uart_open_ret;
#endif
	CMD_HAL_UART *p_cmd_dev_uart = (CMD_HAL_UART *)os_zalloc(sizeof(CMD_HAL_UART));
	if (!p_cmd_dev_uart)
	{
		return NULL;
	}

	if (p_config == NULL)
	{
		config.uart_baud_rate = 115200;
		config.uart_data_bits = UART_DATA_BITS_8;
		config.uart_stop_bits = UART_STOP_BITS_1;
		config.uart_parity_bit = UART_PARITY_BIT_NONE;
		config.uart_flow_ctrl = UART_FLOW_CONTROL_DISABLE;
		config.uart_tx_mode = UART_TX_MODE_STREAM;
	}
	else
	{
		config = *p_config;
	}

	config.uart_rx_mode = UART_RX_MODE_USER;

	switch(uart_num)
	{
		case E_UART_NUM_0:
			//chip_uart0_pinmux_cfg(UART0_RX_USED_GPIO5, UART0_TX_USED_GPIO22, 0);
			break;
		case E_UART_NUM_1:
			chip_uart1_pinmux_cfg(0); // 0:Do not use CTS RTS
			break;
		case E_UART_NUM_2:
			chip_uart2_pinmux_cfg(UART2_TX_USED_GPIO13);
			break;
		default:
			uart_num = E_UART_NUM_0;
			break;
	}

#ifdef CONFIG_CUSTOM_YED
	uart_open_ret = drv_uart_open(uart_num, &config);
	if (uart_open_ret != UART_RET_SUCCESS)
	{
		if (uart_open_ret != UART_RET_EBUSY &&
		    uart_open_ret != UART_RET_ENOMEM)
		{
			drv_uart_close(uart_num);
		}
		os_free(p_cmd_dev_uart);
		return NULL;
	}
#else
	if((cmd_dev_uart[uart_num]) && (uart_num != p_cmd_dev_uart->cmd_uart_num))
	{
		drv_uart_close(p_cmd_dev_uart->cmd_uart_num);
		drv_uart_open(uart_num, &config);
	}
	else
	{
		drv_uart_open(uart_num, &config);
	}
#endif

	callback.uart_callback = cmd_hal_uart_isr;
	callback.uart_data = (void *)p_cmd_dev_uart;

	drv_uart_ioctrl(uart_num, DRV_UART_CTRL_REGISTER_RX_CALLBACK, &callback);
	drv_uart_ioctrl(uart_num, DRV_UART_CTRL_GET_REG_BASE, (void *)(&p_cmd_dev_uart->cmd_uart_base));

	p_cmd_dev_uart->ops.getc = cmd_hal_uart_getc;
	p_cmd_dev_uart->ops.puts = cmd_hal_uart_puts;

	p_cmd_dev_uart->cmd_uart_num = uart_num;
	p_cmd_dev_uart->inform = (void (*)())cb;

	cmd_dev_uart[uart_num] = p_cmd_dev_uart;

#ifdef CONFIG_CUSTOM_YED
	if (uart_num == E_UART_NUM_1)
	{
		p_cmd_dev_uart->reliable = yed_uart_reliable_create(
			uart_num, cmd_hal_uart_reliable_replay,
			cmd_hal_uart_reliable_notify, p_cmd_dev_uart);
		if (!p_cmd_dev_uart->reliable)
		{
			cmd_dev_uart[uart_num] = NULL;
			drv_uart_close(uart_num);
			os_free(p_cmd_dev_uart);
			return NULL;
		}
	}
#endif

	return (void *)p_cmd_dev_uart;
}

void cmd_clear_uart_buf(void * dev)
{
	CMD_HAL_UART *p_cmd_dev_uart = (CMD_HAL_UART *)dev;
	unsigned short cmd_buff_wr_old = p_cmd_dev_uart->cmd_buff_wr;
	while (1)
	{
		if(cmd_buff_wr_old != p_cmd_dev_uart->cmd_buff_wr){
			cmd_buff_wr_old = p_cmd_dev_uart->cmd_buff_wr;
		}else{
			os_msdelay(1);
			if(cmd_buff_wr_old != p_cmd_dev_uart->cmd_buff_wr){
				cmd_buff_wr_old = p_cmd_dev_uart->cmd_buff_wr;				
			}else{
				break;
			}
		}
	}
#ifdef CONFIG_CUSTOM_YED
	{
		unsigned int flags = system_irq_save();

		p_cmd_dev_uart->cmd_buff_rd = 0;
		p_cmd_dev_uart->cmd_buff_wr = 0;
		memset(p_cmd_dev_uart->cmd_buff, 0, CMD_HAL_UART_RX_BUF_LEN);
		system_irq_restore(flags);
	}
#else
	p_cmd_dev_uart->cmd_buff_rd = 0;
	p_cmd_dev_uart->cmd_buff_wr = 0;
	memset(p_cmd_dev_uart->cmd_buff,0,CMD_HAL_UART_RX_BUF_LEN);
#endif
}

void cli_cmd_deliver(char *cmd)
{
    CMD_CLI_DEV * p_cli_dev = cli_get_dev_cfg();
	int len = strlen(cmd);
	if(p_cli_dev == NULL ||  p_cli_dev->cmd_dev_open != 1)
	{
			return;
	}
	CMD_HAL_UART *p_cmd_dev_uart = (CMD_HAL_UART *)p_cli_dev->cmd_dev_ops;
	if(p_cmd_dev_uart == NULL)
	{
			return;
	}
    if (CMD_HAL_UART_RX_BUF_LEN - p_cmd_dev_uart->cmd_buff_wr > len)
    {
        memcpy(&p_cmd_dev_uart->cmd_buff[p_cmd_dev_uart->cmd_buff_wr], cmd, len);
    }
    else
    {
        int first_section_len  = CMD_HAL_UART_RX_BUF_LEN - p_cmd_dev_uart->cmd_buff_wr;
        int second_section_len = len - first_section_len;
        memcpy(&p_cmd_dev_uart->cmd_buff[p_cmd_dev_uart->cmd_buff_wr], cmd, first_section_len);
        memcpy(p_cmd_dev_uart->cmd_buff, cmd + first_section_len, second_section_len);
    }
    p_cmd_dev_uart->cmd_buff_wr += len;
    p_cmd_dev_uart->cmd_buff_wr %= CMD_HAL_UART_RX_BUF_LEN;
    p_cmd_dev_uart->cmd_buff[p_cmd_dev_uart->cmd_buff_wr++] = '\r';
    p_cmd_dev_uart->cmd_buff_wr %= CMD_HAL_UART_RX_BUF_LEN;
    p_cmd_dev_uart->inform();
}