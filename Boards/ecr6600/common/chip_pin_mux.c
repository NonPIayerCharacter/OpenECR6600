
#include "chip_pinmux.h"
#include "oshal.h"
#include "gpio.h"

int chip_uart0_pinmux_cfg(int rx_num, int tx_num, int flow_ctrl_en)
{
	if (rx_num == UART0_RX_USED_GPIO5)
	{
		PIN_FUNC_SET(IO_MUX_GPIO5, FUNC_GPIO5_UART0_RXD);
	}
	else if (rx_num == UART0_RX_USED_GPIO21)
	{
		PIN_FUNC_SET(IO_MUX_GPIO21, FUNC_GPIO21_UART0_RXD);
	}
	else
	{
		goto uart0_failed;
	}

	if (tx_num == UART0_TX_USED_GPIO6)
	{
		PIN_FUNC_SET(IO_MUX_GPIO6, FUNC_GPIO6_UART0_TXD);
	}
	else if (tx_num == UART0_TX_USED_GPIO22)
	{
		PIN_FUNC_SET(IO_MUX_GPIO22, FUNC_GPIO22_UART0_TXD);
	}
	else
	{
		goto uart0_failed;
	}

	if (flow_ctrl_en)
	{
		PIN_FUNC_SET(IO_MUX_GPIO3, FUNC_GPIO3_UART0_CTS);
		PIN_FUNC_SET(IO_MUX_GPIO4, FUNC_GPIO4_UART0_RTS);
	}

	return 0;

uart0_failed:
	return -1;
}


void chip_uart1_pinmux_cfg(int flow_ctrl_en)
{
	PIN_FUNC_SET(IO_MUX_GPIO1, FUNC_GPIO1_UART1_RXD);
	PIN_FUNC_SET(IO_MUX_GPIO2, FUNC_GPIO2_UART1_TXD);
	
	drv_gpio_ioctrl(1, DRV_GPIO_CTRL_PULL_TYPE, DRV_GPIO_ARG_PULL_UP);
	drv_gpio_ioctrl(2, DRV_GPIO_CTRL_PULL_TYPE, DRV_GPIO_ARG_PULL_UP);

	if (flow_ctrl_en)
	{
		PIN_FUNC_SET(IO_MUX_GPIO24, FUNC_GPIO24_UART1_CTS);
		PIN_FUNC_SET(IO_MUX_GPIO23, FUNC_GPIO23_UART1_RTS);
	}
}


int chip_uart2_pinmux_cfg(int tx_num)
{
	PIN_FUNC_SET(IO_MUX_GPIO17, FUNC_GPIO17_UART2_RXD);
	
	drv_gpio_ioctrl(17, DRV_GPIO_CTRL_PULL_TYPE, DRV_GPIO_ARG_PULL_UP);
	if (tx_num == UART2_TX_USED_GPIO0)
	{
		PIN_FUNC_SET(IO_MUX_GPIO0, FUNC_GPIO0_UART2_TXD);
		
		drv_gpio_ioctrl(0, DRV_GPIO_CTRL_PULL_TYPE, DRV_GPIO_ARG_PULL_UP);
	}
	else if (tx_num == UART2_TX_USED_GPIO13)
	{
		PIN_FUNC_SET(IO_MUX_GPIO13, FUNC_GPIO13_UART2_TXD);
		drv_gpio_ioctrl(13, DRV_GPIO_CTRL_PULL_TYPE, DRV_GPIO_ARG_PULL_UP);
	}
	else
	{
		goto uart2_failed;
	}
	
	return 0;

uart2_failed:
	return -1;
}




void chip_pwm_pinmux_cfg(unsigned int cfg)
{
	switch (cfg)
	{
		case PWM_CH0_USED_GPIO0:
			PIN_FUNC_SET(IO_MUX_GPIO0, FUNC_GPIO0_PWM_CTRL0);
			break;

		case PWM_CH0_USED_GPIO22:
			PIN_FUNC_SET(IO_MUX_GPIO22, FUNC_GPIO22_PWM_CTRL0);
			break;

		case PWM_CH1_USED_GPIO1:
			PIN_FUNC_SET(IO_MUX_GPIO1, FUNC_GPIO1_PWM_CTRL1);
			break;

		case PWM_CH1_USED_GPIO23:
			PIN_FUNC_SET(IO_MUX_GPIO23, FUNC_GPIO23_PWM_CTRL1);
			break;

		case PWM_CH2_USED_GPIO2:
			PIN_FUNC_SET(IO_MUX_GPIO2, FUNC_GPIO2_PWM_CTRL2);
			break;

		case PWM_CH2_USED_GPIO24:
			PIN_FUNC_SET(IO_MUX_GPIO24, FUNC_GPIO24_PWM_CTRL2);
			break;

		case PWM_CH2_USED_GPIO16:
			PIN_MUX_SET_REG(TEST_MODE_EXTPAD_RST_EN,PIN_MUX_GET_REG(TEST_MODE_EXTPAD_RST_EN)&~TEST_MODE_ENABLE);	
			PIN_FUNC_SET(IO_MUX_GPIO16, FUNC_GPIO16_PWM_CTRL2);
			break;
		
		case PWM_CH3_USED_GPIO3:
			PIN_FUNC_SET(IO_MUX_GPIO3, FUNC_GPIO3_PWM_CTRL3);
			break;

		case PWM_CH3_USED_GPIO25:
			PIN_FUNC_SET(IO_MUX_GPIO25, FUNC_GPIO25_PWM_CTRL3);
			break;

		case PWM_CH4_USED_GPIO4:
			PIN_FUNC_SET(IO_MUX_GPIO4, FUNC_GPIO4_PWM_CTRL4);
			break;

		case PWM_CH4_USED_GPIO14:
			PIN_FUNC_SET(IO_MUX_GPIO14, FUNC_GPIO14_PWM_CTRL4);
			break;

		case PWM_CH5_USED_GPIO15:
			PIN_FUNC_SET(IO_MUX_GPIO15, FUNC_GPIO15_PWM_CTRL5);
			break;

		case PWM_CH5_USED_GPIO17:
			PIN_FUNC_SET(IO_MUX_GPIO17, FUNC_GPIO17_PWM_CTRL5);
			break;

		default:
			while(1);
	}
}


int chip_i2c_pinmux_cfg(int scl_num, int sda_num)
{
	if (scl_num == I2C_SCL_USED_GPIO18)
	{
		PIN_MUX_SET_REG(TEST_MODE_EXTPAD_RST_EN, 0x0);
		os_usdelay(2);
		PIN_FUNC_SET(IO_MUX_GPIO18, FUNC_GPIO18_I2C_SCI);
	}
	else if (scl_num == I2C_SCL_USED_GPIO13)
	{
		PIN_FUNC_SET(IO_MUX_GPIO13, FUNC_GPIO13_I2C_SCI);
	}
	else if(scl_num == I2C_SCL_USED_GPIO2)
	{
		PIN_FUNC_SET(IO_MUX_GPIO2, FUNC_GPIO2_I2C_SCI);
	}
	else
	{
		goto i2c_failed;
	}

	if (sda_num == I2C_SDA_USED_GPIO21)
	{
		PIN_FUNC_SET(IO_MUX_GPIO21, FUNC_GPIO21_I2C_SDA);
	}
	else if (sda_num == I2C_SDA_USED_GPIO25)
	{
		PIN_FUNC_SET(IO_MUX_GPIO25, FUNC_GPIO25_I2C_SDA);
	}
	else if (sda_num == I2C_SDA_USED_GPIO3)
	{
		PIN_FUNC_SET(IO_MUX_GPIO3, FUNC_GPIO3_I2C_SDA);
	}
	else
	{
		goto i2c_failed;
	}

	return 0;

i2c_failed:
	return -1;
}



void chip_io_mode_pinmux_cfg(int io_num,int mode)
{
	switch (io_num)
	{
		case 0:
			PIN_FUNC_SET(IO_MUX_GPIO0, mode);
			break;

		case 1:
			PIN_FUNC_SET(IO_MUX_GPIO1, mode);
			break;

		case 2:
			PIN_FUNC_SET(IO_MUX_GPIO2, mode);
			break;

		case 3:
			PIN_FUNC_SET(IO_MUX_GPIO3, mode);
			break;

		case 4:
			PIN_FUNC_SET(IO_MUX_GPIO4, mode);
			break;

		case 5:
			PIN_FUNC_SET(IO_MUX_GPIO5, mode);
			break;
		
		case 6:
			PIN_FUNC_SET(IO_MUX_GPIO6, mode);
			break;

		case 7:
			PIN_FUNC_SET(IO_MUX_GPIO7, mode);
			break;

		case 8:
			PIN_FUNC_SET(IO_MUX_GPIO8, mode);
			break;
		
		case 9:
			PIN_FUNC_SET(IO_MUX_GPIO9, mode);
			break;

		case 10:
			PIN_FUNC_SET(IO_MUX_GPIO10, mode);
			break;

		case 11:
			PIN_FUNC_SET(IO_MUX_GPIO11, mode);
			break;
		
		case 12:
			PIN_FUNC_SET(IO_MUX_GPIO12, mode);
			break;

		case 13:
			PIN_FUNC_SET(IO_MUX_GPIO13, mode);
			break;

		case 14:
			PIN_FUNC_SET(IO_MUX_GPIO14, mode);
			break;

		case 15:
			PIN_FUNC_SET(IO_MUX_GPIO15, mode);
			break;

		case 16:
			if(mode == 1)
			{
				PIN_MUX_SET_REG(TEST_MODE_EXTPAD_RST_EN,PIN_MUX_GET_REG(TEST_MODE_EXTPAD_RST_EN)&~TEST_MODE_ENABLE);	
			}
			PIN_FUNC_SET(IO_MUX_GPIO16, mode);
			break;

		case 17:
			PIN_FUNC_SET(IO_MUX_GPIO17, mode);
			break;

		case 18:
			if(mode == 1)
			{
				PIN_MUX_SET_REG(TEST_MODE_EXTPAD_RST_EN,PIN_MUX_GET_REG(TEST_MODE_EXTPAD_RST_EN)&~EXTPAD_RST_ENABLE);
			}
			PIN_FUNC_SET(IO_MUX_GPIO18, mode);
			break;
		
		case 20:
			PIN_FUNC_SET(IO_MUX_GPIO20, mode);
			break;	

		case 21:
			PIN_FUNC_SET(IO_MUX_GPIO21, mode);
			break;

		case 22:
			PIN_FUNC_SET(IO_MUX_GPIO22, mode);
			break;

		case 23:
			PIN_FUNC_SET(IO_MUX_GPIO23, mode);
			break;

		case 24:
			PIN_FUNC_SET(IO_MUX_GPIO24, mode);
			break;

		case 25:
			PIN_FUNC_SET(IO_MUX_GPIO25, mode);
			break;

		default:
			while(1);
	}
}

int chip_io_mode_pinmux_get (int io_num)
{
	int ret;

	switch (io_num)
	{
		case 0:
			ret = PIN_FUNC_GET(IO_MUX_GPIO0);
			break;

		case 1:
			ret = PIN_FUNC_GET(IO_MUX_GPIO1);
			break;

		case 2:
			ret = PIN_FUNC_GET(IO_MUX_GPIO2);
			break;

		case 3:
			ret = PIN_FUNC_GET(IO_MUX_GPIO3);
			break;

		case 4:
			ret = PIN_FUNC_GET(IO_MUX_GPIO4);
			break;

		case 5:
			ret = PIN_FUNC_GET(IO_MUX_GPIO5);
			break;
		
		case 6:
			ret = PIN_FUNC_GET(IO_MUX_GPIO6);
			break;

		case 7:
			ret = PIN_FUNC_GET(IO_MUX_GPIO7);
			break;

		case 8:
			ret = PIN_FUNC_GET(IO_MUX_GPIO8);
			break;
		
		case 9:
			ret = PIN_FUNC_GET(IO_MUX_GPIO9);
			break;

		case 10:
			ret = PIN_FUNC_GET(IO_MUX_GPIO10);
			break;

		case 11:
			ret = PIN_FUNC_GET(IO_MUX_GPIO11);
			break;
		
		case 12:
			ret = PIN_FUNC_GET(IO_MUX_GPIO12);
			break;

		case 13:
			ret = PIN_FUNC_GET(IO_MUX_GPIO13);
			break;

		case 14:
			ret = PIN_FUNC_GET(IO_MUX_GPIO14);
			break;

		case 15:
			ret = PIN_FUNC_GET(IO_MUX_GPIO15);
			break;

		case 16:
			ret = PIN_FUNC_GET(IO_MUX_GPIO16);
			break;

		case 17:
			ret = PIN_FUNC_GET(IO_MUX_GPIO17);
			break;

		case 18:
			ret = PIN_FUNC_GET(IO_MUX_GPIO18);
			break;
		
		case 20:
			ret = PIN_FUNC_GET(IO_MUX_GPIO20);
			break;	

		case 21:
			ret = PIN_FUNC_GET(IO_MUX_GPIO21);
			break;

		case 22:
			ret = PIN_FUNC_GET(IO_MUX_GPIO22);
			break;

		case 23:
			ret = PIN_FUNC_GET(IO_MUX_GPIO23);
			break;

		case 24:
			ret = PIN_FUNC_GET(IO_MUX_GPIO24);
			break;

		case 25:
			ret = PIN_FUNC_GET(IO_MUX_GPIO25);
			break;

		default:
			ret = -1;
	}

	return ret;
}




