/**
* @file       flash_PUYA.c
* @brief      flash driver code  
* @author     LvHuan
* @date       2021-2-19
* @version    v0.1
* @par Copyright (c):
*      eswin inc.
* @par History:        
*   version: author, date, desc\n
*/

#include <string.h>
#include <stdio.h>

#include "flash.h"
#include "arch_irq.h"
#include "oshal.h"
#include "cli.h"
#include "flash_internal.h"
#include "pit.h"

#ifdef CONFIG_FLASH_BOYA



/* BOYA */
#define BY25Q32CS			0x164068
#define BY25Q16ES			0x154068
/* memory protect */
#define PROTECT_ED_0x400000			0x021C			//all
#define PROTECT_ED_0x3FF000			0x4244			//1023/1024
#define PROTECT_ED_0x3FE000			0x4248			//511/512
#define PROTECT_ED_0x3FC000			0x424C			//255/256
#define PROTECT_ED_0x3F8000			0x4250			//127/128
#define PROTECT_ED_0x3F0000			0x4204			//63/64
#define PROTECT_ED_0x3E0000			0x4208			//31/32
#define PROTECT_ED_0x3C0000			0x420C			//15/16
#define PROTECT_ED_0x380000			0x4210			//7/8
#define PROTECT_ED_0x300000			0x4214			//3/4
#define PROTECT_ED_0x200000			0x0238			//1/2
#define PROTECT_ED_0x100000			0x0234			//1/4
#define PROTECT_ED_0x80000			0x0230			//1/8
#define PROTECT_ED_0x40000			0x022C			//1/16
#define PROTECT_ED_0x00				0x0200			//NULL


#define BOYA_BP_BIT					0x427C
#define BOYA_BP_MASK				0xBD83

static const T_SPI_FLASH_PARAM boya_flash_params[] = 
{
	{
		.flash_id = BY25Q32CS,
		.flash_size = FLASH_SIZE_4_MB,
		.flash_name = "BY25Q32CS",
		.flash_otp_star_addr  = FLASH_OTP_START_ADDR_0x1000,
		.flash_otp_block_size = FLASH_OTP_BLOCK_SIZE_256_BYTE,
		.flash_otp_block_num  = FLASH_OTP_BLOCK_NUM_3,
		.flash_otp_block_interval = FLASH_OTP_BLOCK_INTERNAL_4096_BYTE,
	},

	{
		.flash_id = BY25Q16ES,
		.flash_size = FLASH_SIZE_2_MB,
		.flash_name = "BY25Q16ES",
		.flash_otp_star_addr  = FLASH_OTP_START_ADDR_0x1000,
		.flash_otp_block_size = FLASH_OTP_BLOCK_SIZE_1024_BYTE,
		.flash_otp_block_num  = FLASH_OTP_BLOCK_NUM_3,
		.flash_otp_block_interval = FLASH_OTP_BLOCK_INTERNAL_4096_BYTE,
	},
};



static int spiFlash_BOYA_check_addr(unsigned int flash_id, int addr, int length)
{
	if(flash_id == BY25Q32CS)
	{
		if(!((addr>=0x001000 && addr<=0x0010FF && ((addr + length - 1) <= 0x0010FF))
			||(addr>=0x002000 && addr<=0x0020FF && ((addr + length - 1) <= 0x0020FF))
			||(addr>=0x003000 && addr<=0x0030FF && ((addr + length - 1) <= 0x0030FF))))
		{
			//cli_printf("addr or length error\n");
			return FLASH_RET_PARAMETER_ERROR;
		}
	}
	else if(flash_id == BY25Q16ES)
	{
		if(!((addr>=0x001000 && addr<=0x0013FF && ((addr + length - 1) <= 0x0013FF))
			||(addr>=0x002000 && addr<=0x0023FF && ((addr + length - 1) <= 0x0023FF))
			||(addr>=0x003000 && addr<=0x0033FF && ((addr + length - 1) <= 0x0033FF))))
		{
			//cli_printf("addr or length error\n");
			return FLASH_RET_PARAMETER_ERROR;
		}
	}
	else
	{
		//os_printf(LM_OS, LL_INFO, "flashID error:0x%x\n",flash_id);		
		return FLASH_RET_ID_ERROR;
	}
	
	return FLASH_RET_SUCCESS;
}



/**    @brief		Read data in flash OTP area api.
 *	   @details 	Read the flash OTP data of the specified length from the specified address in the flash memory into the buff.
 *     @param[in]	addr--The address where you want to read data in flash OTP area.
 *     @param[in]	* buf--Data of specified length read from specified address in flash OTP memory.
 *     @param[in]	len--The specified length read from the specified address in flash OTP memory. 
 *     @return      0--ok, -1--spi bus status busy, -2--spi fifo reset not completes, -4--The input parameter addr or length is out of bounds, -7--flash id is not GD series.
 */
int spiFlash_BOYA_OTP_Read_CMD(struct _T_SPI_FLASH_DEV * p_flash_dev, int addr, int length,unsigned char *pdata)
{
	int i,rx_num;
	unsigned int data;	//word aligned
	unsigned int * rxBuf;

	spiFlash_format_addr(SPIFLASH_CMD_OTP_RD, addr, 
						SPI_TRANSCTRL_CMD_EN |SPI_TRANSCTRL_ADDR_EN 
						 |SPI_TRANSCTRL_TRAMODE_DR	|SPI_TRANSCTRL_DUMMY_CNT_1
						 |SPI_TRANSCTRL_DUALQUAD_REGULAR |SPI_TRANSCTRL_RCNT(length-1));
	
	if(((unsigned int)pdata%4==0) && ((unsigned int) length % 4 == 0))
	{
		rxBuf = (unsigned int *)pdata;
		data = (length+3)/4;

		while((FLASH_SPI_REG->status & SPI_STATUS_BUSY) == 1)
		{
			rx_num = ((FLASH_SPI_REG->status) >> 8) & 0x1F;
			rx_num = MIN(rx_num , data);

			for(i=0; i<rx_num; i++)
			{
				*rxBuf++ = FLASH_SPI_REG->data;
			}

			data -= rx_num;

			if(data == 0)
			{
				return FLASH_RET_SUCCESS;
			}
		}

		for(i=0; i<data; i++)
		{
			*rxBuf++ = FLASH_SPI_REG->data;
		}
	}
	else
	{
		while(length)
		{
			rx_num = MIN(4, length);
			SPI_WAIT_RX_READY(FLASH_SPI_REG->status);
			data = FLASH_SPI_REG->data;

			for(i=0; i<rx_num; i++)
			{
				*pdata++ = (unsigned char)data;
				data >>= 8;
				length--;
			}
		}
	}

	return FLASH_RET_SUCCESS;
}




int spiFlash_BOYA_OTP_Read(struct _T_SPI_FLASH_DEV * p_flash_dev, int addr, int length,unsigned char *pdata)
{
	unsigned int address, leng;
	unsigned int flash_id;

	flash_id = p_flash_dev->flash_param.flash_id;

	if (spiFlash_BOYA_check_addr(flash_id, addr, length))
	{
		return FLASH_RET_PARAMETER_ERROR;
	}

	if ((length <= 0) ||(!pdata))
	{
		return FLASH_RET_PARAMETER_ERROR;
	}

	leng = MIN(FLASH_SIZE_READ_MAX, length);
	address = addr;	
	
	do
	{
		unsigned int irq_flag = system_irq_save();
		
		if (spiFlash_BOYA_OTP_Read_CMD(p_flash_dev, address, leng, pdata+(address - addr)) != FLASH_RET_SUCCESS)
		{
			system_irq_restore(irq_flag);
			return FLASH_RET_READ_ERROR;
		}

		system_irq_restore(irq_flag);
		length -= leng;
		address += leng;
		leng = MIN(FLASH_SIZE_READ_MAX, length);
	}while(leng);

	return FLASH_RET_SUCCESS;	
}



/**    @brief		lock flash OTP area data.
 *	   @details 	The Security Registers Lock Bit can be used to OTP protect the security registers. Once the LB bit is set to 1, the Security Registers will be permanently locked; the Erase and Write command will be ignored.
 *     @param[in]	LB--0x01: lock Security Register 0x001000-0x0010FF, LB--0x02: lock Security Register 0x002000-0x0020FF, LB--0x03: lock Security Register 0x003000-0x0030FF, LB--0x0F: lock all, default:do nothing.
 *     @return      0--ok, -4--The input parameter addr or length is out of bounds, -7--flash id is not GD series.
 */
int spiFlash_BOYA_OTP_Lock(struct _T_SPI_FLASH_DEV *  p_flash_dev, int LB)
{
	unsigned int irq_flag, data;
	unsigned int flash_id = p_flash_dev->flash_param.flash_id;

	irq_flag = system_irq_save();
	data = spiFlash_cmd_rdSR_2();
	system_irq_restore(irq_flag);

	data = (data & 0xFF)<<8;

	if((flash_id == BY25Q32CS) || (flash_id == BY25Q16ES)  )//BY25Q32CS BY25Q16ES
	{		
	    data = data>>8;
		switch (LB)
		{
			case 0x01:
				data |= 0x08;
				break;
			case 0x02:
				data |= 0x10;
				break;
			case 0x03:
				data |= 0x20;
				break;
			case 0x0F:				
				data |= 0x38;
				break;
			default:
				//os_printf(LM_OS, LL_INFO, "parameter error\n");
				return FLASH_RET_PARAMETER_ERROR;
		}
		
		irq_flag = system_irq_save();
		spiFlash_cmd_wrSR_2(data);
		system_irq_restore(irq_flag);
	}
	else
	{
		//os_printf(LM_OS, LL_INFO, "Flash_ID error:0x%x\n",flash_id);
		return FLASH_RET_ID_ERROR;
	}
	
	return FLASH_RET_SUCCESS;
}


/**    @brief		flash OTP sector erase.
 *	   @details 	Send sector erase CMD, secrot erase one block(0x400) once time.
 *     @param[in]	add--Start erasing address into the flash OTP memory.
 *     @return      0--send CMD ok, -1--spi bus status busy, -2--spi fifo reset not completes, -4--The input parameter addr or length is out of bounds,-5--write enable not set,-6--the memory is busy in erase, not erase completely,-7--flash id is not GD series..
 */
int spiFlash_BOYA_OTP_Se(struct _T_SPI_FLASH_DEV *  p_flash_dev, unsigned int addr)
{
	unsigned int j, data,irq_flag;
	unsigned int flash_id = p_flash_dev->flash_param.flash_id;
	
	if(flash_id == BY25Q32CS)
	{
		if(!((addr>=0x001000 && addr<=0x0010FF)||(addr>=0x002000 && addr<=0x0020FF)||(addr>=0x003000 && addr<=0x0030FF)))
		{
			//cli_printf("addr or length error\n");
			return FLASH_RET_PARAMETER_ERROR;
		}
	}
	else if(flash_id == BY25Q16ES)
	{
		if(!((addr>=0x001000 && addr<=0x0013FF)||(addr>=0x002000 && addr<=0x0023FF)||(addr>=0x003000 && addr<=0x0033FF)))
		{
			//cli_printf("addr or length error\n");
			return FLASH_RET_PARAMETER_ERROR;
		}
	}


	irq_flag = system_irq_save();
	for (j = 0; j<SPIFLASH_TIMEOUT_COUNT; j++)
	{
		spiFlash_cmd_wrEnable();
		data = spiFlash_cmd_rdSR_1();
		if(data & SPIFLASH_STATUS_WEL)
		{
			break;
		}
	}

	if((data & SPIFLASH_STATUS_WEL) == 0)
	{
		system_irq_restore(irq_flag);
		return FLASH_RET_WREN_NOT_SET;
	}

	spiFlash_format_addr(SPIFLASH_CMD_OTP_SE, addr, SPI_TRANSCTRL_CMD_EN |SPI_TRANSCTRL_ADDR_EN|SPI_TRANSCTRL_TRAMODE_NONE);

	for(j = 0; j<SPIFLASH_TIMEOUT_COUNT; j++)
	{
		data = spiFlash_cmd_rdSR_1();
		if((data & SPIFLASH_STATUS_WIP) == 0)
		{
			break;
		}
	}

	system_irq_restore(irq_flag);
		
	if(data & SPIFLASH_STATUS_WIP)
	{
		return FLASH_RET_WIP_NOT_SET;
	}

	return FLASH_RET_SUCCESS;
}


/**    @brief		Write data in OTP area of flash item.
 *	   @details 	Write the specified length of data to the specified address into the flash OTP memory.
 *     @param[in]	* pSpiReg--Structure of SPI register.
 *     @param[in]	addr--The address where you want to write data to flash OTP memory.
 *     @param[in]	* buf--The data information that you want to write to the specified address of flash OTP memory.
 *     @param[in]	length--The length of data to be written to the specified address of flash OTP memory.
 *     @return      0--ok, -1--spi bus status busy, -2--spi fifo reset not completes, -4--The input parameter addr or length is out of bounds, -7--flash id is not GD series.
 */
static int spiFlash_BOYA_OTP_Program(struct _T_SPI_FLASH_DEV * p_flash_dev, unsigned int addr, unsigned char * buf, unsigned int length)
{
	unsigned int i, left, data;
	
	for (i = 0; i<SPIFLASH_TIMEOUT_COUNT; i++)
	{
		spiFlash_cmd_wrEnable();
		data = spiFlash_cmd_rdSR_1();
		if(data & SPIFLASH_STATUS_WEL)
		{
			break;
		}
	}

	if ((data & SPIFLASH_STATUS_WEL) == 0)
	{
		return FLASH_RET_WREN_NOT_SET;
	}

	spiFlash_format_addr(SPIFLASH_CMD_OTP_PP, addr, 
		SPI_TRANSCTRL_CMD_EN|SPI_TRANSCTRL_ADDR_EN|SPI_TRANSCTRL_TRAMODE_WO|SPI_TRANSCTRL_DUALQUAD_REGULAR|SPI_TRANSCTRL_WCNT(length - 1));

	if ((unsigned int)buf%4==0)
	{
		unsigned int *pdata = (unsigned int * )buf;
		
		left = (length + 3)/4;

		for (i = 0; i< left; i++)
		{
			SPI_WAIT_TX_READY(FLASH_SPI_REG->status);
			FLASH_SPI_REG->addr = addr + i*4; 
			FLASH_SPI_REG->data = *pdata++;
		}
	}
	else
	{
		while (length > 0) 
		{
			data = 0;					/* clear the data */			
			left = MIN(length, 4);		/* data need be read 32bits once a time */

			for (i = 0; i < left; i++) 
			{
				data |= *buf++ << (i * 8);
				length--;
			}

			SPI_WAIT_TX_READY(FLASH_SPI_REG->status);
			FLASH_SPI_REG->addr = addr + i*4; 
			FLASH_SPI_REG->data = data;
		}
	}
	return FLASH_RET_SUCCESS;
}


/**    @brief		Write data in OTP area of flash item.
 *	   @details 	Write the specified length of data to the specified address into the flash OTP memory.
 *     @param[in]	addr--The address where you want to write data to flash OTP area.
 *     @param[in]	* buf--The data information that you want to write to the specified address of flash OTP area.
 *     @param[in]	length--The length of data to be written to the specified address of flash OTP area.
 *     @return      0--write ok, -5--write enable not set,-6--the memory is busy in write, not write completely,-8--otp program error.
 */
int spiFlash_BOYA_OTP_Write(struct _T_SPI_FLASH_DEV *  p_flash_dev, unsigned int addr, unsigned int len, unsigned char * buf)
{
	unsigned int address, length;
	
	unsigned int flash_id = p_flash_dev->flash_param.flash_id;

	if (spiFlash_BOYA_check_addr(flash_id, addr, len))
	{
		return FLASH_RET_PARAMETER_ERROR;
	}

	if ((len <= 0) ||(!buf))
	{
		return FLASH_RET_PARAMETER_ERROR;
	}

	length = addr % SPIFLASH_PAGE_SIZE;
	length = MIN(SPIFLASH_PAGE_SIZE - length, len);

	address = addr;	
	
	do
	{
		unsigned int j, data;
		unsigned int irq_flag = system_irq_save();

		if (spiFlash_BOYA_OTP_Program(p_flash_dev, address, buf+(address - addr), length) != FLASH_RET_SUCCESS)
		{
			system_irq_restore(irq_flag);
			return FLASH_RET_PROGRAM_ERROR;
		}

		for (j = 0; j<SPIFLASH_TIMEOUT_COUNT; j++)
		{
			data = spiFlash_cmd_rdSR_1();
			if((data & SPIFLASH_STATUS_WIP) == 0)
			{
				break;
			}
		}

		system_irq_restore(irq_flag);
		if(data & SPIFLASH_STATUS_WIP)
		{
			return FLASH_RET_WIP_NOT_SET;
		}

		len -= length;
		address += length;
		length = MIN(SPIFLASH_PAGE_SIZE, len);
	}while(length);

	return FLASH_RET_SUCCESS;	
}

int  spiflash_BOYA_wr_protect(unsigned int addr)
{
	unsigned int protect_value = 0;
	unsigned int rdsr = 0;
	if ( (Flash_ID & 0xFFFFFF)  == BY25Q32CS )
	{
		if(addr == 0x021C)
		{
			//os_printf(LM_CMD,LL_INFO,"PROTECT_ED_ALL\n ");
			protect_value = PROTECT_ED_0x400000;
		}
		else if((addr >= 0x3FF000))
		{
			//os_printf(LM_CMD,LL_INFO,"PROTECT_ED_0x3FF000\n ");
			protect_value = PROTECT_ED_0x3FF000;
		}
		else if((addr >= 0x3FE000))
		{
			//os_printf(LM_CMD,LL_INFO,"PROTECT_ED_0x3FE000\n ");
			protect_value = PROTECT_ED_0x3FE000;
		}
		else if(addr >= 0x3FC000)
		{
			//os_printf(LM_CMD,LL_INFO,"PROTECT_ED_0x3FC000\n ");
			protect_value = PROTECT_ED_0x3FC000;
		}
		else if(addr >= 0x3F8000)
		{
			//os_printf(LM_CMD,LL_INFO,"PROTECT_ED_0x3F8000\n ");
			protect_value = PROTECT_ED_0x3F8000;
		}
		else if(addr >= 0x3F0000)
		{
			//os_printf(LM_CMD,LL_INFO,"PROTECT_ED_0x3F0000\n ");
			protect_value = PROTECT_ED_0x3F0000;
		}
		else if(addr >= 0x3E0000)
		{
			//os_printf(LM_CMD,LL_INFO,"PROTECT_ED_0x3E0000\n ");
			protect_value = PROTECT_ED_0x3E0000;
		}
		else if(addr >= 0x3C0000)
		{
			//os_printf(LM_CMD,LL_INFO,"PROTECT_ED_0x3C0000\n ");
			protect_value = PROTECT_ED_0x3C0000;
		}
		else if(addr >= 0x380000)
		{
			//os_printf(LM_CMD,LL_INFO,"PROTECT_ED_0x380000\n ");
			protect_value = PROTECT_ED_0x380000;
		}
		else if(addr >= 0x300000)
		{
			//os_printf(LM_CMD,LL_INFO,"PROTECT_ED_0x300000\n ");
			protect_value = PROTECT_ED_0x300000;
		}
		else if(addr >= 0x200000)
		{
			//os_printf(LM_CMD,LL_INFO,"PROTECT_ED_0x200000\n ");
			protect_value = PROTECT_ED_0x200000;
		}
		else if(addr >= 0x100000)
		{
			//os_printf(LM_CMD,LL_INFO,"PROTECT_ED_0x100000\n ");
			protect_value = PROTECT_ED_0x100000;
		}
		else if(addr >= 0x80000)
		{
			//os_printf(LM_CMD,LL_INFO,"PROTECT_ED_0x80000\n ");
			protect_value = PROTECT_ED_0x80000;
		}
		else if(addr >= 0x40000)
		{
			//os_printf(LM_CMD,LL_INFO,"PROTECT_ED_0x40000\n ");
			protect_value = PROTECT_ED_0x40000;
		}
		else
		{
			//os_printf(LM_CMD,LL_INFO,"222PPROTECT_ED_0x00\n ");
			protect_value = PROTECT_ED_0x00;
		}
		
		spiFlash_format_wr_vol(SPIFLASH_CMD_WRSR_1, protect_value , 2);
		
		//spiFlash_format_wr(SPIFLASH_CMD_WRSR_1, protect_value , 2);
		//os_printf(LM_CMD,LL_INFO,"protect_value = 0x%x\n ",protect_value);
    	rdsr = (spiFlash_cmd_rdSR_1() & 0xff )| ((spiFlash_cmd_rdSR_2() & 0xff) << 8);
    	if((rdsr & BOYA_BP_BIT)   != protect_value )
    	{
    		os_printf(LM_CMD,LL_INFO,"error:flash protect_value = 0x%x,rdsr = 0x%x\n ",protect_value, rdsr);
    		system_assert(0);
    	}
	}
	return protect_value;
}


int spiFlash_BOYA_probe(T_SPI_FLASH_DEV *p_flash_dev)
{
	int i;
	const T_SPI_FLASH_PARAM * param;
	T_SPI_FLASH_OTP_OPS * ops;

	for (i=0; i<FLASH_ARRAY_SIZE(boya_flash_params); i++)
	{
		param = &boya_flash_params[i];
		if (p_flash_dev->flash_param.flash_id == param->flash_id)
		{
			p_flash_dev->flash_param.flash_size = param->flash_size;
			p_flash_dev->flash_param.flash_name = param->flash_name;
			p_flash_dev->flash_param.flash_otp_star_addr  = param->flash_otp_star_addr;
			p_flash_dev->flash_param.flash_otp_block_size = param->flash_otp_block_size;
			p_flash_dev->flash_param.flash_otp_block_num  = param->flash_otp_block_num;
			p_flash_dev->flash_param.flash_otp_block_interval = param->flash_otp_block_interval;
			break;
		}
	}

	if (i == FLASH_ARRAY_SIZE(boya_flash_params))
	{
		return FLASH_RET_ID_ERROR;
	}

	ops = &p_flash_dev->flash_otp_ops;
	ops->flash_otp_read = spiFlash_BOYA_OTP_Read;
	ops->flash_otp_write = spiFlash_BOYA_OTP_Write;
	ops->flash_otp_erase= spiFlash_BOYA_OTP_Se;
	ops->flash_otp_lock = spiFlash_BOYA_OTP_Lock;
	
	return FLASH_RET_SUCCESS;
}

#define CNT_NUM 3
unsigned int flash_crc[CNT_NUM] = {0};
unsigned int offset[CNT_NUM] = {4096,8192,0x10000};
unsigned char*  check_buff;

unsigned int i,cnt;

int spiFlash_BOYA_FT(void)
{
	check_buff = (unsigned char *)os_zalloc(64*1024);		
	if (!check_buff)
	{
		return FLASH_RET_MALLOC_ERROR;
	}
	
	if(Flash_ID == 0x164068)
	{		
		unsigned irq_flag = system_irq_save_tick_compensation();
		//start_time = drv_pit_get_tick();
		for(i=0;i<CNT_NUM;i++)
		{
			flash_crc[i] = 0;
			cnt = 0;

			while(cnt < (0x400000/offset[i]))
			{
				memcpy(check_buff,(char *)(0x800000 +  (cnt *offset[i])), offset[i]);
				flash_crc[i] = ef_calc_crc32(flash_crc[i], check_buff, offset[i]);
				cnt++;
			}
		}
		//end_time = drv_pit_get_tick();
		system_irq_restore_tick_compensation(irq_flag);
		//os_printf(LM_CMD,LL_ERR,"boya ft :%d us\r\n", (unsigned int)((end_time  - start_time) / 40));
		if((flash_crc[0] != flash_crc[1])  || (flash_crc[0] != flash_crc[2]))
		{
			//os_printf(LM_CMD,LL_ERR,"flash_crc[0]=0x%08x [1]=0x%08x [2]=0x%08x \r\n", flash_crc[0], flash_crc[1], flash_crc[2]);
			if(check_buff != NULL)
			{
				os_free(check_buff);
			}
			return (FLASH_RET_CRC_ERROR);
		}
	}
	
	if(check_buff != NULL)
	{
		os_free(check_buff);
	}
	return FLASH_RET_SUCCESS;
}
#endif//end CONFIG_FLASH_BOYA


