#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>


#include "cli.h"
#include "oshal.h"
#include "hal_flash.h"
#include "flash.h"
#include "flash_internal.h"

//#ifndef MIN
//#define MIN(x,y) (x < y ? x : y)
//#endif
//
//#ifndef MAX
//#define MAX(x,y) (x > y ? x : y)
//#endif

#define F_SIZE	1024
//#define F_SIZE	512

#if 0
#define F_SIZE_1		4096
#define cyc_t 		(0x100000/F_SIZE_1)

unsigned char sfTest[F_SIZE_1] = {0};


void xip_stress_test(void)
{	
	unsigned int i,j,ret;
	unsigned int rnum;
	unsigned int rdata;
	unsigned int cycle_time = 0;
	unsigned int error_num = 0;
	
	//DBGLOG_INFO("please entry test time\r\n");
	//cycle_time = utils_ParseData();
	
	for(i = 0; i < F_SIZE_1; i++)
	{
		//sfTest[i] = (i+cycle_time)%256;
		
		sfTest[i] = i;
	}
	
	while(1)
	{
		os_printf(LM_CMD,LL_ERR,"erase all flash memory\r\n");
		
		//ret = drv_spiflash_chiperase();
		
		os_printf(LM_CMD,LL_ERR,"write all flash memory\r\n");
		for(j = 0; j < cyc_t; j++)
		{
		
			ret = drv_spiflash_erase(0x100000 + j*F_SIZE_1,  F_SIZE_1);
			
			if(ret != 0){
				os_printf(LM_CMD,LL_ERR,"drv_spiflash_erase, ret = %d, add = 0x%x\n", ret,0x100000 + j*F_SIZE_1);
				return ;
			}
			ret = drv_spiflash_write(0x100000 + j*F_SIZE_1, (unsigned char *)sfTest, F_SIZE_1);
			if(ret != 0){
				os_printf(LM_CMD,LL_ERR,"drv_spiflash_write, ret = %d, add = 0x%x\n", ret,0x100000 + j*F_SIZE_1);
				return ;
			}
		}
		
		os_printf(LM_CMD,LL_ERR,"xip read data 100000 times \r\n");
		drv_spiflash_read(0x100000, (unsigned char *)&rdata, 4);
		os_printf(LM_CMD,LL_ERR,"test rdata = 0x%x\r\n", rdata);
		for(i = 0; i < 100000; i++)
		{
			//rnum = boot_trng_get();
			
			rnum = rand();
			rnum = rnum % 0x100000;
			drv_spiflash_read(0x100000 + rnum, (unsigned char *)&rdata, 4);
			if((sfTest[rnum%256]) != ((unsigned char)rdata&0xff))
			{ 
				os_printf(LM_CMD,LL_ERR,"!!!! ---------i=%d, rnum = 0x%x, rnum%%256 = 0x%x)\r\n", i, rnum , rnum%256);
				os_printf(LM_CMD,LL_ERR,"!!!! ---------error: rdata = 0x%x, rnum = 0x%x)\r\n", rdata , sfTest[rnum%256]);
				error_num++;
				//return;
			}
		}
		
		//os_printf(LM_CMD,LL_ERR,"0x201228 = %x\r\n",inw(SOC_AON_PAD_SR));
		os_printf(LM_CMD,LL_ERR,"[%d] ok [%d] no\r\n",cycle_time++, error_num);
	}
}

static int stress_tet(int argc, char *argv[])
{
	xip_stress_test();
	return CMD_RET_SUCCESS;
	
}
CLI_SUBCMD(ut_flash, stress, stress_tet, "unit test flash erase", "ut_flash erase [address] [length]");
#endif

static int utest_flash_status(int argc, char *argv[])
{
	os_printf(LM_CMD,LL_ERR,"SR1 =  0x%x\n", spiFlash_cmd_rdSR_1() & 0xffff);
	os_printf(LM_CMD,LL_ERR,"SR2 =  0x%x\n", spiFlash_cmd_rdSR_2() & 0xffff);
	
	return CMD_RET_SUCCESS;
	
}
CLI_SUBCMD(ut_flash, status, utest_flash_status, "unit test flash status", "ut_flash status");


static int utest_flash_status_wr1(int argc, char *argv[])
{
	unsigned int stadata = 0;
	if (argc == 2)
	{
		stadata = (int)strtoul(argv[1], NULL, 0);
	}
	os_printf(LM_CMD,LL_ERR,"stadata =  0x%x\n",stadata);
	
	spiFlash_cmd_wrSR_0(stadata);
	return CMD_RET_SUCCESS;
	
}
CLI_SUBCMD(ut_flash, wr1, utest_flash_status_wr1, "unit test flash status", "ut_flash status");

static int utest_flash_status_wr2(int argc, char *argv[])
{
	unsigned int stadata = 0;
	if (argc == 2)
	{
		stadata = (int)strtoul(argv[1], NULL, 0);
	}
	os_printf(LM_CMD,LL_ERR,"stadata =  0x%x\n",stadata);
	
	spiFlash_cmd_wrSR_2(stadata);
	return CMD_RET_SUCCESS;
	
}
CLI_SUBCMD(ut_flash, wr2, utest_flash_status_wr2, "unit test flash status", "ut_flash status");

static int utest_flash_erase(int argc, char *argv[])
{
	int ret = 0;
	unsigned int addr;
	unsigned int length;
	
	if (argc == 3)
	{
		addr = (int)strtoul(argv[1], NULL, 0);
		length = (int)strtoul(argv[2], NULL, 0);
	}
	else
	{
		os_printf(LM_CMD,LL_ERR,"PARAMETER NUM(: %d) IS ERROR!!  ut_flash erase [address] [length]\n", argc);
		return CMD_RET_FAILURE;
	}
	
	ret = hal_flash_erase(addr, length);
	
	if(ret != 0)
	{
		os_printf(LM_CMD,LL_ERR,"flash erase error ,ret =  %d\n", ret);
		return CMD_RET_FAILURE;
	}
	
	os_printf(LM_CMD,LL_INFO,"sf_erase, addr = 0x%x,  length = 0x%x\n", addr,length);
	return CMD_RET_SUCCESS;
	
}
CLI_SUBCMD(ut_flash, erase, utest_flash_erase, "unit test flash erase", "ut_flash erase [address] [length]");


static int utest_flash_write(int argc, char *argv[])
{
	int i, ret = 0;
	unsigned int addr;
	unsigned int length;
	unsigned int buff_write;
	unsigned int write_data = 0;
	unsigned int offset = 0;
	unsigned char sfTest[F_SIZE] = {0};

	if (argc >= 3)
	{
		addr = strtoul(argv[1], NULL, 0);
		length = strtoul(argv[2], NULL, 0);
		buff_write = strtoul(argv[3], NULL, 0);
	}
	else
	{
		os_printf(LM_CMD,LL_ERR,"PARAMETER NUM(: %d) IS ERROR!!  ut_flash write [address] [length] ([value])\n", argc);
		return CMD_RET_FAILURE;
	}
	
	ret = check_oversize(addr,length);
	if(ret != 0)
	{
		os_printf(LM_CMD,LL_ERR,"oversize, flash write error ,ret =  %d\n", ret);
		return CMD_RET_FAILURE;
	}
	
	for(i=0; i<F_SIZE; i++){
		sfTest[i] = i;}

	
	if(buff_write != 0)
	{
		memcpy(sfTest,&buff_write,sizeof(buff_write));
	}

	while(length)
	{
	    offset = MIN(F_SIZE, length);
		ret = hal_flash_write(addr+write_data, (unsigned char *)&sfTest, offset);
		if(ret!=0)
		{
			os_printf(LM_CMD,LL_ERR,"flash write error ,ret =  %d\n", ret);
			return CMD_RET_FAILURE;
		}
		length -= offset;
		write_data += offset;
	}
	
	os_printf(LM_CMD,LL_INFO,"sf_program,addr = 0x%x,  length = 0x%x\n", addr,write_data);

	return CMD_RET_SUCCESS;

}
CLI_SUBCMD(ut_flash, write, utest_flash_write, "unit test flash write", "ut_flash write [address] [length]");

#ifdef CONFIG_FLASH_TH

static int utest_flash_pro(int argc, char *argv[])
{
	unsigned int addr;

	if (argc >= 2)
	{
		addr = strtoul(argv[1], NULL, 0);
	}
	else
	{
		os_printf(LM_CMD,LL_ERR,"PARAMETER NUM(: %d) IS ERROR!!  ut_flash write [address] [length] ([value])\n", argc);
		return CMD_RET_FAILURE;
	}
	
	extern unsigned int spiflash_ZB_wr_protect(unsigned int addr);
	
	spiflash_ZB_wr_protect( addr);
	
	#ifdef CONFIG_FLASH_TH
	if ((Flash_ID & 0xFF) == 0xEB)
	{
		spiflash_TH_wr_protect( addr);
		return CMD_RET_SUCCESS;
	}
	#endif
	
	#ifdef CONFIG_FLASH_GD
	if ((Flash_ID & 0xFFFFFF) == 0x1640C8)
	{
		spiflash_GD_wr_protect( addr);
		return CMD_RET_SUCCESS;
	}
	#endif
	
	#ifdef CONFIG_FLASH_BOYA
	if ((Flash_ID & 0xFFFFFF) == 0x164068)
	{
		spiflash_BOYA_wr_protect( addr);
		return CMD_RET_SUCCESS;
	}
	#endif
	
	os_printf(LM_CMD,LL_ERR,"flash protect is unsupported, Flash_ID = 0x%x\n", Flash_ID);
	//spiflash_TH_wr_protect( addr);
	return CMD_RET_SUCCESS;

}
CLI_SUBCMD(ut_flash, pro, utest_flash_pro, "unit test flash pro", "ut_flash pro [address] [length]");

#endif
static int utest_flash_read(int argc, char *argv[])
{
	unsigned int addr;
	unsigned int length;
	unsigned int sf        = 1;
	unsigned int offset    = 0;
    unsigned int read_data = 0;
	int ret;
	unsigned char sf_Test[F_SIZE]  =  {0};

	if (argc == 3)
	{
		addr = strtoul(argv[1], NULL, 0);
		length = strtoul(argv[2], NULL, 0);
	}
	else
	{
		os_printf(LM_CMD,LL_ERR,"PARAMETER NUM(: %d) IS ERROR!!  ut_flash read [address] [length]\n", argc);
		return CMD_RET_FAILURE;
	}
	
	ret = check_oversize(addr,length);
	if(ret != 0)
	{
		os_printf(LM_CMD,LL_ERR,"oversize, sf_read error, ret = %d\n", ret);
		return CMD_RET_FAILURE;
	}

	os_printf(LM_CMD,LL_INFO,"\r\n");

	while(length)
	{
	    offset = MIN(F_SIZE, length);
		ret = hal_flash_read(addr + read_data, (unsigned char *)sf_Test, offset);
		if(ret != 0)
		{
			os_printf(LM_CMD,LL_ERR,"sf_read error, ret = %d\n", ret);
			return CMD_RET_FAILURE;
		}
		length -=offset;
		read_data += offset;
		
		for(sf = 1; sf <= offset; sf++)
		{
			os_printf(LM_CMD,LL_INFO,"%02x", sf_Test[sf-1]);
			if (sf % 4 == 0)
			{
				os_printf(LM_CMD,LL_INFO," ");
			}
		}
		os_printf(LM_CMD,LL_INFO,"\r\n");
	}
	os_printf(LM_CMD,LL_INFO,"sf_read,addr = 0x%x, length = 0x%x\n", addr,read_data);
	return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_flash, read, utest_flash_read, "unit test flash read", "ut_flash read [address] [length]");

#ifdef CONFIG_FLASH_TH
	static int utest_flash_FT(int argc, char *argv[])
	{
		int ret;
		ret = spiflash_TH_FT();

		os_printf(LM_CMD,LL_INFO,"FT ret = %d\n", ret);
		return CMD_RET_SUCCESS;
	}
	CLI_SUBCMD(ut_flash, ft, utest_flash_FT, "unit test flash FT", "ut_flash FT");
#endif
static int utest_flash_cs_stress(int argc, char *argv[])
{

	unsigned int i,j = 0,ret;
	unsigned int num = 0;
	unsigned char sfTest[256] = {0};
	unsigned char sf_Test_2[256]  =  {0};
//	unsigned char sf_Test_1[256]  =	{0};

	for(i = 0; i < 256; i++)
	{
		sfTest[i] = 0;
	}
	os_printf(LM_CMD,LL_INFO,"erase all flash memory\r\n");
	hal_flash_erase(0x15000,0x1000);
	
	os_printf(LM_CMD,LL_INFO,"write all flash memory\r\n");
	
	ret = hal_flash_write(0x15000, (unsigned char *)sfTest, 256);
	if(ret != 0)
	{
		os_printf(LM_CMD,LL_INFO,"drv_spiflash_write, ret = %d, add = 0x%x\n", ret, 0 + j*F_SIZE);
		return -1;
	}
	
	while(1)
	{		
		hal_flash_read(0x15000, (unsigned char *)sf_Test_2, 256);
		if(memcmp(sfTest, sf_Test_2,256))
		{ 
			os_printf(LM_CMD,LL_INFO,"memcpy error. num = 0x%x)\r\n", num);
			return -1 ;
		}
		num++;
		if((num%0x500000) == 0)
		{
			os_printf(LM_CMD,LL_INFO,"0x%x ok\r\n", num);
		}
	}
	return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_flash, cs_stress, utest_flash_cs_stress, "unit test flash FT", "ut_flash FT");

#if 0
unsigned char sf_Test_[F_SIZE]  __attribute__((section(".dma.data")));

static int utest_flash_read_dma(int argc, char *argv[])
{
	unsigned int addr;
	unsigned int length;
	unsigned int sf        = 1;
	unsigned int offset    = 0;
    unsigned int read_data = 0;
	int ret;
	
	if (argc == 3)
	{
		addr = strtoul(argv[1], NULL, 0);
		length = strtoul(argv[2], NULL, 0);
	}
	else
	{
		os_printf(LM_CMD,LL_ERR,"PARAMETER NUM(: %d) IS ERROR!!  ut_flash read [address] [length]\n", argc);
		return CMD_RET_FAILURE;
	}
	
	ret = check_oversize(addr,length);
	if(ret != 0)
	{
		os_printf(LM_CMD,LL_ERR,"oversize, sf_read error, ret = %d\n", ret);
		return CMD_RET_FAILURE;
	}

	os_printf(LM_CMD,LL_INFO,"\r\n");
	//os_printf(LM_CMD,LL_INFO,"entry   \r\n");

	while(length)
	{
	    offset = MIN(F_SIZE, length);
		ret = drv_spiflash_read_dma(addr + read_data, (unsigned char *)sf_Test_, offset);

		if(ret != 0)
		{
			os_printf(LM_CMD,LL_ERR,"sf_read error, ret = %d\n", ret);
			return CMD_RET_FAILURE;
		}
		length -=offset;
		read_data += offset;
		
		for(sf = 1; sf <= offset; sf++)
		{
			os_printf(LM_CMD,LL_INFO,"%02x", sf_Test_[sf-1]);
			if (sf % 4 == 0)
			{
				os_printf(LM_CMD,LL_INFO," ");
			}
		}
		os_printf(LM_CMD,LL_INFO,"\r\n");
	}
	os_printf(LM_CMD,LL_INFO,"sf_read,addr = 0x%x, length = 0x%x\n", addr,read_data);
	return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_flash, read_dma, utest_flash_read_dma, "unit test flash read", "ut_flash read [address] [length]");


static int utest_flash_write_dma(int argc, char *argv[])
{
	int i, ret = 0;
	unsigned int addr;
	unsigned int length;
	unsigned int buff_write;
	unsigned int write_data = 0;
	unsigned int offset = 0;

	if (argc >= 3)
	{
		addr = strtoul(argv[1], NULL, 0);
		length = strtoul(argv[2], NULL, 0);
		buff_write = strtoul(argv[3], NULL, 0);
	}
	else
	{
		os_printf(LM_CMD,LL_ERR,"PARAMETER NUM(: %d) IS ERROR!!  ut_flash write [address] [length] ([value])\n", argc);
		return CMD_RET_FAILURE;
	}
	
	ret = check_oversize(addr,length);
	if(ret != 0)
	{
		os_printf(LM_CMD,LL_ERR,"oversize, flash write error ,ret =  %d\n", ret);
		return CMD_RET_FAILURE;
	}
	
	for(i=0; i<F_SIZE; i++){
		sf_Test_[i] = i;}

	
	if(buff_write != 0)
	{
		memcpy(sf_Test_,&buff_write,sizeof(buff_write));
	}

	while(length)
	{
	    offset = MIN(F_SIZE, length);
		ret = drv_spiflash_write_dma(addr+write_data, (unsigned char *)&sf_Test_, offset);
		if(ret!=0)
		{
			os_printf(LM_CMD,LL_ERR,"flash write error ,ret =  %d\n", ret);
			return CMD_RET_FAILURE;
		}
		length -= offset;
		write_data += offset;
	}
	
	os_printf(LM_CMD,LL_INFO,"sf_program,addr = 0x%x,  length = 0x%x\n", addr,write_data);

	return CMD_RET_SUCCESS;

}
CLI_SUBCMD(ut_flash, write_dma, utest_flash_write_dma, "unit test flash write", "ut_flash write [address] [length]");

#endif


static int utest_flash_otp_read(int argc, char *argv[])
{
	unsigned int addr;
	unsigned int length;
	unsigned int offset = 0;
	unsigned int ret = 0, i=0;
	unsigned int read_data = 0;
	unsigned char sfTest[F_SIZE] = {0};
	
	if (argc == 3)
	{
		addr = strtoul(argv[1], NULL, 0);
		length = strtoul(argv[2], NULL, 0);
	}
	else
	{
		os_printf(LM_CMD,LL_ERR,"PARAMETER NUM(: %d) IS ERROR!!  ut_flash otpread [address] [length]\n", argc);
		return CMD_RET_FAILURE;
	}

	ret = check_otp_oversize(addr,length);
	if(ret != 0)
	{
		os_printf(LM_CMD,LL_ERR,"oversize, sf_otp_read error, ret = %d\n", ret);
		return CMD_RET_FAILURE;
	}

	
	memset( sfTest, 0, F_SIZE );
	os_printf(LM_CMD,LL_INFO,"\r\n");

	
	while(length)
	{
	    offset = MIN(F_SIZE, length);
		ret = hal_flash_otp_read(addr+ read_data, (unsigned char*)(&sfTest[0]), offset);
		if(ret != 0)
		{
			os_printf(LM_CMD,LL_ERR,"sf_otp_read error, ret = %d\n", ret);
			return CMD_RET_FAILURE;
		}
		
		for(i=0; i<offset; i++)
		{
			os_printf(LM_CMD,LL_INFO,"[%3x]=0x%02x ",(addr+ read_data+ i),sfTest[i]);
			if((((i+1) % 8) == 0) || (i+1 == offset))
			{
				os_printf(LM_CMD,LL_INFO,"\n");
			}
		}

		length -=offset;
		read_data += offset;
		os_printf(LM_CMD,LL_INFO,"\r\n");
	}

	
	
	
	os_printf(LM_CMD,LL_INFO,"sf_otp read, addr = 0x%x,length = 0x%x\n",addr,read_data);
	return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_flash, otpread, utest_flash_otp_read, "unit test flash otpread", "ut_flash otpread [address] [length]");


static int utest_flash_otp_write(int argc, char *argv[])
{
 	unsigned int addr;
	unsigned int length;
	unsigned int buff_write;
	unsigned int offset    = 0;
	unsigned int write_data = 0;
	unsigned int ret = 0, i=0;
	unsigned char sfTest[F_SIZE] = {0};
	
	if (argc >= 3)
	{
		addr = strtoul(argv[1], NULL, 0);
		length = strtoul(argv[2], NULL, 0);
		buff_write = strtoul(argv[3], NULL, 0);
	}
	else
	{
		os_printf(LM_CMD,LL_ERR,"PARAMETER NUM(: %d) IS ERROR!!  ut_flash otpwrite [address] [length]\n", argc);
		return CMD_RET_FAILURE;
	}
	ret = check_otp_oversize(addr,length);
	if(ret != 0)
	{
		os_printf(LM_CMD,LL_ERR,"oversize, sf_otp_write error,  ret = %d\n", ret);
		return CMD_RET_FAILURE;
	}


	for(i=0;i<F_SIZE;++i)
	{
		sfTest[i] = i;
	}
	
	for(i=256;i<F_SIZE;++i)
	{
		sfTest[i] = 0x1ff-i;
	}

	for(i=512;i<F_SIZE;++i)
	{
		sfTest[i] = 0x280+i;
	}

	for(i=768;i<F_SIZE;++i)
	{
		sfTest[i] = 0x380-i;
	}
	
	if(buff_write != 0)
	{
		memcpy(sfTest,&buff_write,sizeof(buff_write));
	}
	
	while(length)
	{
	    offset = MIN(F_SIZE, length);
		ret = hal_flash_otp_write(addr+write_data, (unsigned char *)&sfTest, offset);	
		if(ret!=0)
		{
			os_printf(LM_CMD,LL_ERR,"sf_otp_write error, ret = %d\n", ret);
			return CMD_RET_FAILURE;
		}
		length -= offset;
		write_data += offset;
	}
	
	os_printf(LM_CMD,LL_INFO,"sf_otp_write, addr = 0x%x,length = 0x%x\n",addr,write_data);
	return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_flash, otpwrite, utest_flash_otp_write, "unit test flash otpwrite", "ut_flash otpwrite [address] [length]([value])");

#if 1
static int utest_flash_otp_erase(int argc, char *argv[])
{
	unsigned int i = 0;
	//unsigned int length = F_SIZE;
 	unsigned int addr = strtoul(argv[1], NULL, 0);
	unsigned int length = strtoul(argv[2], NULL, 0);
	unsigned int offset    = 0;
	unsigned int write_data = 0;
	unsigned int ret = 0;
	unsigned char sfTest[F_SIZE] = {0};
	//ret = hal_flash_otp_erase(addr);
	//ret = spiFlash_OTP_Se(addr);
	//ret = p_flash_dev->flash_otp_ops.flash_otp_erase(p_flash_dev, addr);
	for(i=0;i<F_SIZE;++i)
	{
		sfTest[i] = 0xff;
	}

	while(length)
	{
	    offset = MIN(F_SIZE, length);
		ret = hal_flash_otp_write(addr+write_data, (unsigned char *)&sfTest, offset);	
		if(ret!=0)
		{
			os_printf(LM_CMD,LL_ERR,"sf_otp_write error, ret = %d\n", ret);
			return CMD_RET_FAILURE;
		}
		length -= offset;
		write_data += offset;
	}
	
	os_printf(LM_APP, LL_INFO, "sf_otp_erase, ret = %d\n", ret);
	if(ret != 0)	
		return CMD_RET_FAILURE;

	return CMD_RET_SUCCESS;
}

CLI_SUBCMD(ut_flash, otperase, utest_flash_otp_erase, "unit test flash otperase", "ut_flash otperase [address] )");
#endif


static int utest_flash_otp_read_aes(int argc, char *argv[])
{
	unsigned int addr;
	unsigned int length;
	unsigned int offset = 0;
	unsigned int ret = 0, i=0;
	unsigned int read_data = 0;
	unsigned char sfTest[F_SIZE] = {0};
	
	if (argc == 3)
	{
		addr = strtoul(argv[1], NULL, 0);
		length = strtoul(argv[2], NULL, 0);
	}
	else
	{
		os_printf(LM_CMD,LL_ERR,"PARAMETER NUM(: %d) IS ERROR!!  ut_flash otpread_aes [address] [length]\n", argc);
		return CMD_RET_FAILURE;
	}

	ret = check_otp_oversize(addr,length);
	if(ret != 0)
	{
		os_printf(LM_CMD,LL_ERR,"oversize, sf_otp_read_aes error, ret = %d\n", ret);
		return CMD_RET_FAILURE;
	}

	
	memset( sfTest, 0, F_SIZE );
	os_printf(LM_CMD,LL_INFO,"\r\n");

	
	while(length)
	{
	    offset = MIN(F_SIZE, length);
		ret = hal_flash_otp_read_aes(addr+ read_data, (unsigned char*)(&sfTest[0]), offset);
		if(ret != 0)
		{
			os_printf(LM_CMD,LL_ERR,"sf_otp_read_aes error, ret = %d\n", ret);
			return CMD_RET_FAILURE;
		}
		
		for(i=0; i<offset; i++)
		{
			os_printf(LM_CMD,LL_INFO,"[%3x]=0x%02x ",(addr+ read_data+ i),sfTest[i]);
			if((((i+1) % 8) == 0) || (i+1 == offset))
			{
				os_printf(LM_CMD,LL_INFO,"\n");
			}
		}

		length -=offset;
		read_data += offset;
		os_printf(LM_CMD,LL_INFO,"\r\n");
	}

	os_printf(LM_CMD,LL_INFO,"sf_otp otpread_aes, addr = 0x%x,length = 0x%x\n",addr,read_data);
	return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_flash, otpread_aes, utest_flash_otp_read_aes, "unit test flash otpread_aes", "ut_flash otpread_aes [address] [length]");

static int utest_flash_otp_write_aes(int argc, char *argv[])
{
 	unsigned int addr;
	unsigned int length;
	unsigned int buff_write;
	unsigned int offset    = 0;
	unsigned int write_data = 0;
	unsigned int ret = 0, i=0;
	unsigned char sfTest[F_SIZE] = {0};
	
	if (argc >= 3)
	{
		addr = strtoul(argv[1], NULL, 0);
		length = strtoul(argv[2], NULL, 0);
		buff_write = strtoul(argv[3], NULL, 0);
	}
	else
	{
		os_printf(LM_CMD,LL_ERR,"PARAMETER NUM(: %d) IS ERROR!!  ut_flash otpwrite_aes [address] [length]\n", argc);
		return CMD_RET_FAILURE;
	}
	ret = check_otp_oversize(addr,length);
	if(ret != 0)
	{
		os_printf(LM_CMD,LL_ERR,"oversize, sf_otp_write_aes error,  ret = %d\n", ret);
		return CMD_RET_FAILURE;
	}


	for(i=0;i<F_SIZE;++i)
	{
		sfTest[i] = i;
	}
	
	if(buff_write != 0)
	{
		memcpy(sfTest,&buff_write,sizeof(buff_write));
	}
	
	while(length)
	{
	    offset = MIN(F_SIZE, length);
		ret = hal_flash_otp_write_aes(addr+write_data, (unsigned char *)&sfTest, offset);	
		if(ret!=0)
		{
			os_printf(LM_CMD,LL_ERR,"sf_otp_write_aes error, ret = %d\n", ret);
			return CMD_RET_FAILURE;
		}
		length -= offset;
		write_data += offset;
	}
	
	os_printf(LM_CMD,LL_INFO,"sf_otp_write_aes, addr = 0x%x,length = 0x%x\n",addr,write_data);
	return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_flash, otpwrite_aes, utest_flash_otp_write_aes, "unit test flash otpwrite_aes", "ut_flash otpwrite_aes [address] [length]([value])");

static int cmd_random_flash_write(int argc, char *argv[])
{
    unsigned int data;
    int i;
    
    if (argc >= 2)
    {
        unsigned int addr = strtoul(argv[1], NULL, 0);
        unsigned int size = strtoul(argv[2], NULL, 0);
        unsigned int random_addr = (addr+os_random()%size)&0xFFFFFFFC;
    
        // 读取当前地址数据
        drv_spiflash_read(random_addr, (unsigned char*)&data, 4);
        os_printf(LM_OS, LL_INFO, "read[0x%x] 0x%x\n", random_addr, data);
        
        // 查找第一个为1的bit并将其清零
        for (i = 0; i < 32; i++) {
            if (data & (1U << i)) {
                data &= ~(1U << i);
                break;
            }
        }
        if (i == 32) {
            os_printf(LM_OS, LL_ERR, "No bit to clear\n");
            return CMD_RET_FAILURE;
        }
        else
        {
            os_printf(LM_OS, LL_INFO, "Clear bit %d, data = 0x%x\n", i, data);
        }
    
        os_printf(LM_OS, LL_INFO, "write[0x%x] 0x%x\n", random_addr, data); 
        drv_spiflash_write(random_addr, (unsigned char*)&data, 4);
        
        // 读取当前地址数据
        memset((void*)&data, 0, 4);
        drv_spiflash_read(random_addr, (unsigned char*)&data, 4);
        os_printf(LM_OS, LL_INFO, "read again[0x%x] 0x%x\n", random_addr, data);

        return CMD_RET_SUCCESS;
    }
    else
    {
        os_printf(LM_OS, LL_INFO, "cmd need start addr and size\r\n");
        return CMD_RET_FAILURE;
    }
}

CLI_SUBCMD(ut_flash, flash_random_write, cmd_random_flash_write, "flash random write 1bit", "ut_flash flash_random_write addr size");

CLI_CMD(ut_flash, NULL, "unit test flash", "test_flash");


