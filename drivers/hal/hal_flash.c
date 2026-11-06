
#include "flash.h"
#include "ota.h"
#include <string.h>

/* ROM function declaration */
extern int partion_info_get(char *key, unsigned int *addr, unsigned int *length);

int hal_flash_read(unsigned int addr, unsigned char * buff, unsigned int len)
{
	int ret;
	ret = drv_spiflash_read(addr, buff, len);
	return ret;
}

int hal_flash_write(unsigned int addr, unsigned char * buff, unsigned int len)
{
	int ret;
	ret = drv_spiflash_write(addr, buff, len);
	return ret;
}

int hal_flash_erase(unsigned int addr, unsigned int len)
{
	int ret;
	ret = drv_spiflash_erase(addr, len);
	return ret;
}

int hal_flash_otp_read(unsigned int addr, unsigned char * buff, unsigned int len)
{
	int ret;
	ret = drv_spiflash_OTP_Read(addr, len, buff);
	return ret;
}

int hal_flash_otp_write(unsigned int addr, unsigned char * buff, unsigned int len)
{
	int ret;
	ret = drv_spiflash_OTP_Write( addr, len, (unsigned char *) buff);
	return ret;
}

#if 0
int hal_flash_otp_erase(unsigned int addr)
{
	int ret;
	ret = drv_spiflash_OTP_Erase(addr);
	return ret;
}
#endif


int hal_flash_otp_read_aes(unsigned int addr, unsigned char * buff, unsigned int len)
{
	int ret;
	ret = drv_OTP_AES_Read(addr, len, buff);
	return ret;
}

int hal_flash_otp_write_aes(unsigned int addr, unsigned char * buff, unsigned int len)
{
	int ret;
	ret = drv_OTP_AES_Write( addr, len, (unsigned char *) buff);
	return ret;
}


int hal_flash_otp_lock(unsigned char otp_block)
{
	int ret;
	ret = drv_spiflash_OTP_Lock(otp_block);
	return ret;
}

int hal_flash_get_tail_space(char* partition_name, unsigned int *addr_out, unsigned int *size_out)
{
    if (!partition_name || !addr_out || !size_out) {
        return -1;
    }

    /* 1. 获取分区信息 */
    unsigned int partition_addr = 0;
    unsigned int partition_length = 0;

    if (partion_info_get(partition_name, &partition_addr, &partition_length) != 0) {
        return -1;
    }

    /* 2. 读取固件头信息 */
    image_headinfo_t image_head;
    memset(&image_head, 0, sizeof(image_headinfo_t));

    drv_spiflash_read(partition_addr, (unsigned char*)&image_head, sizeof(image_headinfo_t));

    /* 3. 计算固件实际占用大小 */
    unsigned int firmware_end = partition_addr +
                            sizeof(image_headinfo_t) +
                            image_head.text_size +
                            image_head.xip_size +
                            image_head.data_size;

    /* 4. 4KB对齐 */
    firmware_end = (firmware_end + 0xFFF) & 0xFFFFF000;

    /* 5. 分区末尾地址（4KB对齐） */
    unsigned int partition_end = (partition_addr + partition_length) & 0xFFFFF000;

    /* 6. 计算可用空间 */
    if (partition_end <= firmware_end) {
        return -1;  /* 无可用空间 */
    }

    *addr_out = firmware_end;
    *size_out = partition_end - firmware_end;

    return 0;
}



