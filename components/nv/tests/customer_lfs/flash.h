#define FLASH_RET_SUCCESS 0
int drv_spiflash_read_apb(unsigned int, unsigned char *, unsigned int);
int drv_spiflash_write(unsigned int, const unsigned char *, unsigned int);
int drv_spiflash_erase(unsigned int, unsigned int);
