/**
*@file	   i2s.c
*@brief    This part of program is I2S algorithm Software interface
*@author   weifeiting
*@data     2021.2.1
*@version  v0.1
*@par Copyright (c):
     eswin inc.
*@par History:
   version:autor,data,desc\n
*/

#include "i2s.h"  
#include "chip_memmap.h"
#include "chip_pinmux.h"
#include "chip_clk_ctrl.h"
#include "chip_smupd.h"
#include "dma.h"
#include "chip_irqvector.h"
#include "arch_irq.h"
#include "string.h"
#include "oshal.h"

/**
*@brief   I2S register structure 
*/
typedef struct _T_I2S_TX_REG_MAP{
	volatile unsigned int	tx_fifo;		//0x00, I2S TX FIFO Register
	volatile unsigned int	tx_fifo_cfg;		//0x04, I2S TX FIFO Configuration Register
	volatile unsigned int	tx_cfg;			//0x08, I2S TX Configuration Register
	volatile unsigned int	tx_limit;		//0x0C, I2S TX Threshold Register
	volatile unsigned int	tx_ism;			//0x10, I2S TX Interrupt status and mask Register
	volatile unsigned int	rev0[1];		//0x14, reserved reg
	volatile unsigned int	tx_level;		//0x18, I2S TX FIFO Depth Register
}T_I2S_TX_REG_MAP;

typedef struct _T_I2S_RX_REG_MAP{
	volatile unsigned int	rx_fifo;		//0x00, I2S RX FIFO Register
	volatile unsigned int	rx_fifo_cfg;		//0x04, I2S RX FIFO Configuration Register
	volatile unsigned int	rx_cfg;			//0x08, I2S RX Configuration Register
	volatile unsigned int	rx_limit;		//0x0C, I2S RX Threshold Register
	volatile unsigned int	rx_ism;			//0x10, I2S RX Interrupt status and mask Register
	volatile unsigned int	rev0[1];		//0x14, reserved reg
	volatile unsigned int	rx_level;		//0x18, I2S RX FIFO Depth Register
}T_I2S_RX_REG_MAP;

typedef struct _T_I2S_REG_MAP{
	volatile unsigned int	cfg;			//0x00, I2S Configuration Register
	volatile unsigned int	rev0[3];		//0x04~0x0C, reserved Register
	volatile unsigned int	ism;			//0x10, I2S Interrupt status and mask Register
	volatile unsigned int	ris;			//0x14, I2S nterrupt status Register
	volatile unsigned int	mis;			//0x18, I2S Interrupt status Register after mask
	volatile unsigned int	sic;			//0x1C, Interrupt ClearRegister
}T_I2S_REG_MAP;

/**
*@brief   I2S variable definition
*/
#define  I2S_TX_MEM_BASE	MEM_BASE_I2S
#define  I2S_RX_MEM_BASE	( MEM_BASE_I2S + 0x800 )
#define  I2S_MEM_BASE		( MEM_BASE_I2S + 0xC00 )
#define  SYS_CLK				160000000

#define I2S_MASTER_BUF_SIZE	1024*2
static unsigned char i2stxdma[I2S_MASTER_BUF_SIZE] __attribute__((section(".dma.data"),aligned(4)));
static unsigned char i2srxdma[I2S_MASTER_BUF_SIZE] __attribute__((section(".dma.data"),aligned(4)));


unsigned int remain_len = 0;
unsigned int I2S_FIFO_DEPTH = 32;
#define I2S_WAIT_FOREVER 0xffffffff

typedef struct _T_DRV_I2S_DEV
{
    T_I2S_REG_MAP * i2s_reg_base;
    T_I2S_TX_REG_MAP * i2s_tx_reg_base;
    T_I2S_RX_REG_MAP * i2s_rx_reg_base;
    int i2s_dma_chan_tx;		// TX DMA通道，-1表示未分配
    int i2s_dma_chan_rx;		// RX DMA通道，-1表示未分配
    unsigned int i2s_init_flag;
    os_sem_handle_t i2s_tx_sem;        // TX信号量
    os_sem_handle_t i2s_rx_sem;        // RX信号量
} T_DRV_I2S_DEV;

static T_DRV_I2S_DEV g_i2s_dev;

/**
@brief   i2c dma callback
*/
#if 0
void i2s_dma_callback_isr(void *data)
{
	os_sem_post(g_i2s_dev.i2s_process);
}
#endif
void i2s_dma_tx_callback_isr(void *data)
{
    os_sem_post(g_i2s_dev.i2s_tx_sem);
}

void i2s_dma_rx_callback_isr(void *data)
{
    os_sem_post(g_i2s_dev.i2s_rx_sem);
}

/**
@brief   i2s tx isr function
*/
void i2s_tx_isr(void)
{
	#ifdef CONFIG_SYSTEM_IRQ
	unsigned long flags = system_irq_save();
	#endif
	
	unsigned int index,value = 0;
	unsigned int cnt=0;
	while(remain_len >0)
	{
		if(remain_len <I2S_FIFO_DEPTH)
		{
			I2S_FIFO_DEPTH = remain_len ;
		}
		for (index = 0; index < I2S_FIFO_DEPTH; index++)
		{
			switch (i2s_cfg.sample_width) 
			{			
				case I2S_BIT_8_WIDTH:
					value |= i2stxdma[index] << 8 * cnt;
					if (cnt++ != 1) 
					{
						continue;
					}
					break;
				case I2S_BIT_16_WIDTH:
					value |= i2stxdma[index] << 8 * cnt;
					if (cnt++ != 3) 
					{
						continue;
					}
					break;
				case I2S_BIT_32_WIDTH:
					value |= i2stxdma[index] << 8 * cnt;
					if (cnt++ != 3) 
					{
						continue;
					}
					break;
				default:
					break;
				
			}
		   g_i2s_dev.i2s_tx_reg_base->tx_fifo = value;
		   value = 0;
		   cnt = 0;
		}
		remain_len -= I2S_FIFO_DEPTH;
	}
	if(remain_len <= 0)
	{
		/* i2s tx rx ism */
		g_i2s_dev.i2s_tx_reg_base->tx_ism = 0;
		//p_i2s_rx_reg->rx_ism = 1;
	}

	#ifdef CONFIG_SYSTEM_IRQ
	system_irq_restore(flags);
	#endif
}

/**
@brief   i2c software reset
*/
void i2s_software_reset()
{
	unsigned int value;
	value = REG_PL_RD(CHIP_SMU_PD_SW_RESET) ;
	REG_PL_WR(CHIP_SMU_PD_SW_RESET, value & ~(1<<4));
	os_usdelay(10);
	REG_PL_WR(CHIP_SMU_PD_SW_RESET, value | (1<<4));
	/*	set up i2s mode	*/
	REG_PL_WR(CHIP_SMU_PD_I2S_MOD, 0x7);
}

/**
@brief  I2S initial configuration
@details I2S initialization main configuration completes the configuration of master-slave mode,
		 data alignment, audio file sampling rate, sampling width, etc.
return		    -1--i2s dma mode allocate channel failure,
		    0--i2s init config success.
*/	
int i2s_init_cfg(i2s_cfg_dev *i2s_cfg_dev)
{
	unsigned int interger,fra_div,tx_value,rx_value,value = 0;
	float fs_data = 0.0;
	unsigned long flags = arch_irq_save();
	i2s_cfg = *i2s_cfg_dev;
	g_i2s_dev.i2s_tx_reg_base = (T_I2S_TX_REG_MAP * )I2S_TX_MEM_BASE;
	g_i2s_dev.i2s_rx_reg_base = (T_I2S_RX_REG_MAP * )I2S_RX_MEM_BASE;
	g_i2s_dev.i2s_reg_base = (T_I2S_REG_MAP * )I2S_MEM_BASE;

	if (g_i2s_dev.i2s_init_flag == 1)
	{
		arch_irq_restore(flags);
		return 0;
	}
	else
	{
		g_i2s_dev.i2s_init_flag = 1;
	}
	g_i2s_dev.i2s_dma_chan_tx = -1;
	g_i2s_dev.i2s_dma_chan_rx = -1;
	g_i2s_dev.i2s_tx_sem = NULL;
	g_i2s_dev.i2s_rx_sem = NULL;

	/*	enable clk	*/
	chip_clk_enable(CLK_I2S);
	chip_clk_enable(CLK_I2S_AHB);
	#if CONFIG_LIERDA
	PIN_FUNC_SET(IO_MUX_GPIO20, FUNC_GPIO20_I2S_MCLK);
	PIN_FUNC_SET(IO_MUX_GPIO0,  FUNC_GPIO0_I2S_TXSCK);
	PIN_FUNC_SET(IO_MUX_GPIO15, FUNC_GPIO15_I2S_TXWS);
	PIN_FUNC_SET(IO_MUX_GPIO14, FUNC_GPIO14_I2S_TXD);
	PIN_FUNC_SET(IO_MUX_GPIO1,  FUNC_GPIO1_I2S_RXD);
	#else
	PIN_FUNC_SET(IO_MUX_GPIO24, FUNC_GPIO24_I2S_MCLK);
	PIN_FUNC_SET(IO_MUX_GPIO23, FUNC_GPIO23_I2S_TXSCK);
	PIN_FUNC_SET(IO_MUX_GPIO22, FUNC_GPIO22_I2S_TXWS);
	PIN_FUNC_SET(IO_MUX_GPIO21, FUNC_GPIO21_I2S_TXD);
	PIN_FUNC_SET(IO_MUX_GPIO13, FUNC_GPIO13_I2S_RXD);
	#endif

	i2s_software_reset();

	tx_value = g_i2s_dev.i2s_tx_reg_base->tx_cfg;
	rx_value = g_i2s_dev.i2s_rx_reg_base->rx_cfg;

	/* master or slave mode   */
	if(i2s_cfg_dev->mode == I2S_MODE_MASTER)
	{
		tx_value |=(1<<22);
		rx_value |=(1<<22);
	}
	else
	{
		tx_value |=(~(1<<22));
		rx_value |=(~(1<<22));
	}

	/* data align format   */
	if(i2s_cfg_dev->data_align_format == DATA_ALIGN_LEFT)
	{
		tx_value &= ~(0x3<<28);
		rx_value &= ~(0x3<<28);
	}
	else if(i2s_cfg_dev->data_align_format == DATA_ALIGN_RIGHT)
	{
		tx_value &= ~(0x3<<28);
		tx_value |=(1<<29);
		rx_value &= ~(0x3<<28);
		rx_value |=(1<<29);
	}
	else if(i2s_cfg_dev->data_align_format == DATA_ALIGN_I2S)
	{
		tx_value &= ~(0x3<<28);
		tx_value |=(1<<28);
		rx_value &= ~(0x3<<28);
		rx_value |=(1<<28);
	}
	else
	{
		os_printf(LM_OS, LL_ERR, "data align format cfg errno\r\n");
	}
	/*	fifo empty,fifo trans 0  */
	tx_value &= ~(0x1<<23);	
	/*  trans format */
	tx_value = (tx_value & 0xFCFFFFFF)|((i2s_cfg_dev->trans_mode)<<24);

	/* system data width */
	//tx_value = (tx_value & 0xFFFFFC0F)|(0x1F<<4);

	/*  sample value width only rigth aglin use*/ 
	if(i2s_cfg_dev->sample_width==I2S_BIT_32_WIDTH)
	{
		tx_value = (tx_value & 0xFFFF03FF)|(0x1F<<10);
		rx_value = (rx_value & 0xFFFF03FF)|(0x1F<<10);
    	tx_value = (tx_value & 0xFFFFFC0F)|((32-1) << 4);
    	rx_value = (rx_value & 0xFFFFFC0F)|((32-1) << 4);
	}
	else if(i2s_cfg_dev->sample_width==I2S_BIT_16_WIDTH)
	{
		tx_value = (tx_value & 0xFFFF03FF)|(0xF<<10);
		rx_value = (rx_value & 0xFFFF03FF)|(0xF<<10);
        
        tx_value = (tx_value & 0xFFFFFC0F)|((16-1) << 4);
        rx_value = (rx_value & 0xFFFFFC0F)|((16-1) << 4);
	}
	else
	{
		tx_value = (tx_value & 0xFFFF03FF)|(0x7<<10);
		rx_value = (rx_value & 0xFFFF03FF)|(0x7<<10);
        
        tx_value = (tx_value & 0xFFFFFC0F)|((8-1) << 4);
        rx_value = (rx_value & 0xFFFFFC0F)|((8-1) << 4);
	}

    if(i2s_cfg_dev->trans_mode == MONO_MODE)
    {
        tx_value = (tx_value & 0xFFFFFFF7);
        rx_value = (rx_value & 0xFFFFFFF7);
    }
    else
    {
        tx_value = (tx_value & 0xFFFFFFF7)|(1<<3);
        rx_value = (rx_value & 0xFFFFFFF7)|(1<<3);
    }
    
	g_i2s_dev.i2s_tx_reg_base->tx_cfg = tx_value;
	g_i2s_dev.i2s_rx_reg_base->rx_cfg = rx_value;


	/* compressd_stereo must cfg */
	if( (i2s_cfg_dev->trans_mode == COMPRESSED_STEREO_MODE)&&(i2s_cfg_dev->compre_bit_width == COMPRE_BIT_16_WIDTH) )
	{
		g_i2s_dev.i2s_tx_reg_base->tx_fifo_cfg = 0;
		g_i2s_dev.i2s_rx_reg_base->rx_fifo_cfg = 0;
	}
	else if( (i2s_cfg_dev->trans_mode == COMPRESSED_STEREO_MODE)&&(i2s_cfg_dev->compre_bit_width == COMPRE_BIT_8_WIDTH) )
	{
		g_i2s_dev.i2s_tx_reg_base->tx_fifo_cfg = 2;
		g_i2s_dev.i2s_rx_reg_base->rx_fifo_cfg = 2;
	}
	else if(i2s_cfg_dev->trans_mode == STEREO_MODE)
	{
		g_i2s_dev.i2s_rx_reg_base->rx_fifo_cfg = 0;
	}
	else if(i2s_cfg_dev->trans_mode == MONO_MODE)
	{
		g_i2s_dev.i2s_rx_reg_base->rx_fifo_cfg = 4;
	}
	
	/* i2s tx rx limit  */
	g_i2s_dev.i2s_tx_reg_base->tx_limit = 1;
	g_i2s_dev.i2s_rx_reg_base->rx_limit = 1;

	/*i2s fenpin 8fenpin----0, 4fenpin-----1  */
	g_i2s_dev.i2s_tx_reg_base->tx_cfg = ((g_i2s_dev.i2s_tx_reg_base->tx_cfg) & 0xFFFFFFF7)|0x8;
	g_i2s_dev.i2s_rx_reg_base->rx_cfg = ((g_i2s_dev.i2s_rx_reg_base->rx_cfg) & 0xFFFFFFF7)|0x8;

    g_i2s_dev.i2s_tx_sem = os_sem_create(1, 0);
    g_i2s_dev.i2s_rx_sem = os_sem_create(1, 0);
    if (!g_i2s_dev.i2s_tx_sem || !g_i2s_dev.i2s_rx_sem)
    {
        goto init_failed;
    }

    if(i2s_cfg_dev->i2s_dma == 1)
    {
        g_i2s_dev.i2s_tx_reg_base->tx_ism = 0x1;
        g_i2s_dev.i2s_rx_reg_base->rx_ism = 0x1;
        g_i2s_dev.i2s_dma_chan_tx = drv_dma_ch_alloc();
        if (g_i2s_dev.i2s_dma_chan_tx < 0)
        {
            goto init_failed;
        }
        g_i2s_dev.i2s_dma_chan_rx = drv_dma_ch_alloc();
        if (g_i2s_dev.i2s_dma_chan_rx < 0)
        {
            goto init_failed;
        }
        drv_dma_isr_register(g_i2s_dev.i2s_dma_chan_tx, i2s_dma_tx_callback_isr, NULL);
        drv_dma_isr_register(g_i2s_dev.i2s_dma_chan_rx, i2s_dma_rx_callback_isr, NULL);
    }
    else
    {
		#if 0
	    arch_irq_register(VECTOR_NUM_I2S, (void *)i2s_tx_isr);
		arch_irq_clean(VECTOR_NUM_I2S);
		arch_irq_unmask(VECTOR_NUM_I2S);
		p_i2s_tx_reg->tx_ism = 0x1;
		p_i2s_rx_reg->rx_ism = 0x1;
		#endif
	}

	/* CFG fs ratio */
	//interger = SYS_CLK/1000/(i2s_cfg_dev->sample_rate *4*16*2);
	//fs_data = SYS_CLK/1000/(i2s_cfg_dev->sample_rate *4*16*2);
    
	interger = SYS_CLK/1000/(i2s_cfg_dev->sample_rate *128);
	fs_data = SYS_CLK/1000/(i2s_cfg_dev->sample_rate *128);
	fs_data -= interger;
	if(interger % 2 == 0)
	{
		interger = interger/2 - 1;
	}
	else
	{
		interger = interger/2;
	}

	/* 	setup fs interger number	*/
	value=REG_PL_RD(CHIP_SMU_PD_I2S_DIV_INT) & 0xFFFFFF00;
	REG_PL_WR(CHIP_SMU_PD_I2S_DIV_INT, value|interger);

	/*	setup fs frac number	*/
	value=REG_PL_RD(CHIP_SMU_PD_I2S_DIV_FRAC) & 0xFFFF;
	fra_div = fs_data*100;
	REG_PL_WR(CHIP_SMU_PD_I2S_DIV_FRAC, value|fra_div|(100<<16));

	/* i2s intr SIM*/
	g_i2s_dev.i2s_reg_base->ism = 0x33;
	/*i2s cfg enable */
	g_i2s_dev.i2s_reg_base->cfg = 0x33;
	arch_irq_restore(flags);
	return 0;

init_failed:
    g_i2s_dev.i2s_tx_reg_base->tx_ism = 0;
    g_i2s_dev.i2s_rx_reg_base->rx_ism = 0;
    g_i2s_dev.i2s_reg_base->cfg = 0;
    if (g_i2s_dev.i2s_dma_chan_rx >= 0)
    {
        drv_dma_ch_release(g_i2s_dev.i2s_dma_chan_rx);
        g_i2s_dev.i2s_dma_chan_rx = -1;
    }
    if (g_i2s_dev.i2s_dma_chan_tx >= 0)
    {
        drv_dma_ch_release(g_i2s_dev.i2s_dma_chan_tx);
        g_i2s_dev.i2s_dma_chan_tx = -1;
    }
    if (g_i2s_dev.i2s_tx_sem)
    {
        os_sem_destroy(g_i2s_dev.i2s_tx_sem);
        g_i2s_dev.i2s_tx_sem = NULL;
    }
    if (g_i2s_dev.i2s_rx_sem)
    {
        os_sem_destroy(g_i2s_dev.i2s_rx_sem);
        g_i2s_dev.i2s_rx_sem = NULL;
    }
    g_i2s_dev.i2s_init_flag = 0;
    chip_clk_disable(CLK_I2S);
    chip_clk_disable(CLK_I2S_AHB);
    arch_irq_restore(flags);
    return -1;
}

/**
@brief  I2S close
@details Mainly release the DMA channel,clear init flag,and stop I2S
@return	   0--i2s close.
*/
int drv_i2s_close()
{
    unsigned long flags = arch_irq_save();

	if (g_i2s_dev.i2s_init_flag == 0)
	{
		arch_irq_restore(flags);
		return 0;
	}
	else
	{
		g_i2s_dev.i2s_init_flag = 0;
	}

    if (g_i2s_dev.i2s_dma_chan_tx >= 0)
    {
        drv_dma_stop(g_i2s_dev.i2s_dma_chan_tx);
        drv_dma_ch_release(g_i2s_dev.i2s_dma_chan_tx);
        g_i2s_dev.i2s_dma_chan_tx = -1;
    }
    if (g_i2s_dev.i2s_dma_chan_rx >= 0)
    {
        drv_dma_stop(g_i2s_dev.i2s_dma_chan_rx);
        drv_dma_ch_release(g_i2s_dev.i2s_dma_chan_rx);
        g_i2s_dev.i2s_dma_chan_rx = -1;
    }
    chip_clk_disable(CLK_I2S);
    chip_clk_disable(CLK_I2S_AHB);
    if(g_i2s_dev.i2s_tx_sem)
    {
        os_sem_destroy(g_i2s_dev.i2s_tx_sem);
        g_i2s_dev.i2s_tx_sem = NULL;
    }
    if(g_i2s_dev.i2s_rx_sem)
    {
        os_sem_destroy(g_i2s_dev.i2s_rx_sem);
        g_i2s_dev.i2s_rx_sem = NULL;
    }
    arch_irq_restore(flags);
    return 0;
}

/**
@brief     Statement i2s master transmit data
@details   In cpu mode, when sending data with different data widths, you need to fill in 0 bits by hand.
		   In stereo mode, each 32-bit unit is a sample value. The serial audio data should be Right channel... Write in this order,
		   and you can use the LRS bit of the I2S transmit FIFO configuration register (I2STXFIFOCFG) to determine which channel data should be written next.
		   In the 16-bit compressed stereo mode, the bit field [31:16] of each unit contains the sample value of the right channel, 
		   and the bit field [15:0] contains the sample value of the left channel. In 8-bit compressed stereo mode, 
		   the bit field [15:8] of each unit contains the sample value of the right channel, 
		   and the bit field [7:0] contains the sample value of the left channel. In mono mode, 
		   each 32-bit unit is a sample value. 	

		   * In dma mode, when sending 16-bit compressed stereo, the dma transmission mode needs to be configured as half-word.
		   *In CPU Mode, mono and 16-bit compressed stereo are 32bit data, and 8-bit compressed stereo is low 16bit data.
@param[in]  *bufptr : Buffer for sending data
@param[in]  len : The length of the data sent by the host
*/
void i2s_master_write(unsigned char * bufptr, unsigned int length)
{
	unsigned int fifo,index,value = 0;
	unsigned int cnt=0;
	unsigned long flags = arch_irq_save();
	if(i2s_cfg.i2s_dma==0)
	{
		for (index = 0; index < length; index++)
		{
			switch (i2s_cfg.sample_width) 
			{			
				case I2S_BIT_8_WIDTH:
					value |= bufptr[index] << 8 * cnt;
					if (cnt++ != 1) 
					{
						continue;
					}
					break;
				case I2S_BIT_16_WIDTH:
					value |= bufptr[index] << 8 * cnt;
					if (cnt++ != 3) 
					{
						continue;
					}
					break;
				case I2S_BIT_32_WIDTH:
					value |= bufptr[index] << 8 * cnt;
					if (cnt++ != 3) 
					{
						continue;
					}
					break;
				default:
					break;
				
			}
			do 
			{
				fifo = (g_i2s_dev.i2s_tx_reg_base->tx_level) & 0x1F;
			} while (fifo > 14);
		   
		   g_i2s_dev.i2s_tx_reg_base->tx_fifo = value;
		   value = 0;
		   cnt = 0;
		}
	}
	else
	{
        memcpy(i2stxdma, bufptr, length);
        T_DMA_CFG_INFO i2s_tx_cfg;
        i2s_tx_cfg.src = (unsigned int)i2stxdma ;
        i2s_tx_cfg.dst = (unsigned int)&(g_i2s_dev.i2s_tx_reg_base->tx_fifo);
        i2s_tx_cfg.len = length;
        i2s_tx_cfg.mode = DMA_CHN_I2S_TX;
        drv_dma_cfg(g_i2s_dev.i2s_dma_chan_tx,  &i2s_tx_cfg);
        drv_dma_start(g_i2s_dev.i2s_dma_chan_tx);
        arch_irq_restore(flags);
        os_sem_wait(g_i2s_dev.i2s_tx_sem, I2S_WAIT_FOREVER);
    }
}

void i2s_master_write_dma_no_copy(unsigned char * bufptr, unsigned int length)
{
    if(bufptr == NULL || length == 0 || ((int)bufptr & 0x3) != 0)
    {
        os_printf(LM_OS, LL_ERR, "buffptr:%d", bufptr ,length);
        return;
    }
    os_sem_wait(g_i2s_dev.i2s_tx_sem, 0);
    unsigned long flags = arch_irq_save();

    T_DMA_CFG_INFO i2s_tx_cfg;
    i2s_tx_cfg.src = (unsigned int)bufptr ;
    i2s_tx_cfg.dst = (unsigned int)&(g_i2s_dev.i2s_tx_reg_base->tx_fifo);
    i2s_tx_cfg.len = length;
    i2s_tx_cfg.mode = DMA_CHN_I2S_TX;
    drv_dma_cfg(g_i2s_dev.i2s_dma_chan_tx,  &i2s_tx_cfg);
    drv_dma_start(g_i2s_dev.i2s_dma_chan_tx);
    arch_irq_restore(flags);
    os_sem_wait(g_i2s_dev.i2s_tx_sem, I2S_WAIT_FOREVER);
}

/**
@brief     Statement i2s master transmit data
@details  If the FIFO is empty, reading this register will return 0x0000 0000 and generate a receive FIFO read error. 	
@param[in]  *bufptr : Buffer for sending data
@param[in]  len : The length of the data sent by the host
*/
void i2s_master_read(unsigned   char *bufptr, unsigned int length)
{
    unsigned int cnt = 0;

    if(i2s_cfg.i2s_dma == 1)
    {
        T_DMA_CFG_INFO i2s_rx_cfg;
        i2s_rx_cfg.src = (unsigned int)&g_i2s_dev.i2s_rx_reg_base->rx_fifo;
        i2s_rx_cfg.dst = (unsigned int)i2srxdma;
        if(i2s_cfg.sample_width == I2S_BIT_32_WIDTH)
        {
            i2s_rx_cfg.len = length;
        }
        else if(i2s_cfg.sample_width == I2S_BIT_16_WIDTH)
        {
            i2s_rx_cfg.len = length * 2;
        }
        else if(i2s_cfg.sample_width == I2S_BIT_8_WIDTH)
        {
            i2s_rx_cfg.len = length * 4;
        }
        else
        {
            i2s_rx_cfg.len = length ;
        }
        i2s_rx_cfg.mode = DMA_CHN_I2S_RX;

        drv_dma_cfg(g_i2s_dev.i2s_dma_chan_rx, &i2s_rx_cfg);
        drv_dma_start(g_i2s_dev.i2s_dma_chan_rx);
        os_sem_wait(g_i2s_dev.i2s_rx_sem, I2S_WAIT_FOREVER);

        if(i2s_cfg.sample_width == I2S_BIT_32_WIDTH)
        {
            memcpy(bufptr, i2srxdma, length);
        }
        else if(i2s_cfg.sample_width == I2S_BIT_16_WIDTH)
        {
            unsigned short *src_ptr = (unsigned short *)i2srxdma;
            unsigned short *dst_ptr = (unsigned short *)bufptr;
            for(cnt = 0; cnt < length/2; cnt++)
            {
                dst_ptr[cnt] = src_ptr[cnt * 2 + 1];
                // os_printf(LM_APP, LL_INFO, "wav data:%x\n", dst_ptr[cnt]);
            }
        }
        else if(i2s_cfg.sample_width == I2S_BIT_8_WIDTH)
        {
            unsigned char *src_ptr = (unsigned char *)i2srxdma;
            unsigned char *dst_ptr = (unsigned char *)bufptr;
            for(cnt = 0; cnt < length; cnt++)
            {
                dst_ptr[cnt] = src_ptr[cnt * 4 + 3];
                // os_printf(LM_APP, LL_INFO, "wav data:%x\n", dst_ptr[cnt]);
            }
        }
        else
        {
            memcpy(bufptr, i2srxdma, length);
        }
    }
    else
    { 
    unsigned int data_bit = 32;
    void *pbuf = bufptr;
    
    if (i2s_cfg.sample_width == I2S_BIT_32_WIDTH)
    {
        data_bit = 32;
    }
    else if (i2s_cfg.sample_width == I2S_BIT_16_WIDTH)
    {
        data_bit = 16;
    }
    else if (i2s_cfg.sample_width == I2S_BIT_8_WIDTH)
    {
        data_bit = 8;
    }
    
    while (cnt < length / (32 / data_bit))
    {
        while (((g_i2s_dev.i2s_rx_reg_base->rx_level) & 0x1F) == 0);
        if (data_bit == 32)
        {
            unsigned int *p = (unsigned int *)pbuf;
            *(p + cnt) = g_i2s_dev.i2s_rx_reg_base->rx_fifo >> (32 - data_bit);
        }
        else if (data_bit == 16)
        {
            unsigned short *p = (unsigned short *)pbuf;
            *(p + cnt) = g_i2s_dev.i2s_rx_reg_base->rx_fifo >> (32 - data_bit);
        }
        else if (data_bit == 8)
        {
            unsigned char *p = (unsigned char *)pbuf;
            *(p + cnt) = g_i2s_dev.i2s_rx_reg_base->rx_fifo >> (32 - data_bit);
        }
        //os_printf(LM_APP, LL_INFO, "wav data:%x\n", *(pbuf + cnt));
        cnt++;
    }

    }
}

void i2s_master_read_stop()
{
    if (g_i2s_dev.i2s_dma_chan_rx >= 0)
    {
        drv_dma_stop(g_i2s_dev.i2s_dma_chan_rx);
    }
    if (g_i2s_dev.i2s_rx_sem)
    {
        os_sem_post(g_i2s_dev.i2s_rx_sem);
    }
}

void i2s_master_write_stop()
{
    if (g_i2s_dev.i2s_dma_chan_tx >= 0)
    {
        drv_dma_stop(g_i2s_dev.i2s_dma_chan_tx);
    }
    if (g_i2s_dev.i2s_tx_sem)
    {
        os_sem_post(g_i2s_dev.i2s_tx_sem);
    }
}

#if 0
void i2s_master_write_intr(unsigned char * bufptr, unsigned int length)
{
	T_I2S_TX_REG_MAP * p_i2s_tx_reg = (T_I2S_TX_REG_MAP * )I2S_TX_MEM_BASE;
	T_I2S_RX_REG_MAP * p_i2s_rx_reg = (T_I2S_RX_REG_MAP * )I2S_RX_MEM_BASE;
	unsigned int fifo,index,value = 0;
	unsigned int cnt=0;
	unsigned long flags = arch_irq_save();
	if(length <I2S_FIFO_DEPTH)
	{
		I2S_FIFO_DEPTH = length ;
	}

	for (index = 0; index < I2S_FIFO_DEPTH; index++)
	{
		switch (i2s_cfg.sample_width) 
		{			
			case I2S_BIT_8_WIDTH:
				value |= i2stxdma[index] << 8 * cnt;
				if (cnt++ != 1) 
				{
					continue;
				}
				break;
			case I2S_BIT_16_WIDTH:
				value |= i2stxdma[index] << 8 * cnt;
				if (cnt++ != 3) 
				{
					continue;
				}
				break;
			case I2S_BIT_32_WIDTH:
				value |= i2stxdma[index] << 8 * cnt;
				if (cnt++ != 3) 
				{
					continue;
				}
				break;
			default:
				break;
			
		}
	   p_i2s_tx_reg->tx_fifo = value;
	   value = 0;
	   cnt = 0;
	}
	length -= I2S_FIFO_DEPTH;
	if(length > 0)
	{
		/* i2s tx rx ism */
		p_i2s_tx_reg->tx_ism = 1;
		p_i2s_rx_reg->rx_ism = 1;
		remain_len = length - I2S_FIFO_DEPTH ;
	}
	arch_irq_restore(flags);
}
#endif

void i2s_master_read_loop(void)
{
    while (1)
    {
        if(((g_i2s_dev.i2s_rx_reg_base->rx_level) & 0x1F) > 0)
        {
            g_i2s_dev.i2s_tx_reg_base->tx_fifo = g_i2s_dev.i2s_rx_reg_base->rx_fifo;
        }
    }
}

