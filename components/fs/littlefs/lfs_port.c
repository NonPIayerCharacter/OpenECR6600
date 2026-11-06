
#include "easyflash.h"
#include "lfs_port.h"
#include "flash.h"

#define  FLASH_BLOCK_SIZE       4096
#define  FLASH_BLOCK_ADDR(x)    (s_lfs_base + (x)*FLASH_BLOCK_SIZE)

static os_mutex_handle_t s_lfs_mutex;
static unsigned int s_lfs_base;
lfs_t lfs;
static int s_lfs_mounted;
static unsigned int s_lfs_size;

int lfs_spiflash_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size);
int lfs_spiflash_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size);
int lfs_spiflash_erase(const struct lfs_config *c, lfs_block_t block);
int lfs_spiflash_sync(const struct lfs_config *c);

int lfs_spiflash_lock(const struct lfs_config *c);
int lfs_spiflash_unlock(const struct lfs_config *c);

struct lfs_config cfg = 
{
    // block device operations
    .read  = lfs_spiflash_read,
    .prog  = lfs_spiflash_prog,
    .erase = lfs_spiflash_erase,
    .sync  = lfs_spiflash_sync,

	.lock  = lfs_spiflash_lock,
	.unlock= lfs_spiflash_unlock,

    // block device configuration
    .read_size = 32,
    .prog_size = 32,
    .block_size = FLASH_BLOCK_SIZE,
    .block_count = 64,
    /* 4KB blocks permit up to 512-byte inline files with this cache size. */
    .cache_size = 512,
    .lookahead_size = 32,
    .block_cycles = 500,
};


int lfs_spiflash_lock(const struct lfs_config *c)
{
	return os_mutex_lock(s_lfs_mutex, 0xFFFFFFFF);
}


int lfs_spiflash_unlock(const struct lfs_config *c)
{
	return os_mutex_unlock(s_lfs_mutex);
}


int lfs_spiflash_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
	return drv_spiflash_read_apb(FLASH_BLOCK_ADDR(block) + off, buffer, size)
        == FLASH_RET_SUCCESS ? 0 : LFS_ERR_IO;
}


int lfs_spiflash_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
	return drv_spiflash_write(FLASH_BLOCK_ADDR(block) + off, buffer, size)
        == FLASH_RET_SUCCESS ? 0 : LFS_ERR_IO;
}


int lfs_spiflash_erase(const struct lfs_config *c, lfs_block_t block)
{
	return drv_spiflash_erase(FLASH_BLOCK_ADDR(block), FLASH_BLOCK_SIZE)
        == FLASH_RET_SUCCESS ? 0 : LFS_ERR_IO;
}


int lfs_spiflash_sync(const struct lfs_config *c)
{
	return 0;
}


void *lfs_init(void)
{
    unsigned int size;
    int ret;

    if (s_lfs_mounted)
    {
        return &lfs;
    }

#ifdef CONFIG_MATTER_CUSTOMER_LFS
    const char *partition = PARTION_NAME_NV_CUSTOMER;
#else
    const char *partition = "fs";
#endif
    if (partion_info_get((char *)partition, &s_lfs_base, &size) ||
        s_lfs_base % FLASH_BLOCK_SIZE || size % FLASH_BLOCK_SIZE ||
        size / FLASH_BLOCK_SIZE < 2)
    {
        os_printf(LM_APP, LL_ERR, "littlefs: missing or invalid fs partition\r\n");
        return NULL;
    }
    cfg.block_count = size / FLASH_BLOCK_SIZE;
    s_lfs_size = size;

    if (!s_lfs_mutex) { s_lfs_mutex = os_mutex_create(); }
    if (!s_lfs_mutex)
    {
        return NULL;
    }

    ret = lfs_mount(&lfs, &cfg);
    if (ret == LFS_ERR_CORRUPT)
    {
#ifdef CONFIG_MATTER_CUSTOMER_LFS
        os_printf(LM_APP, LL_ERR, "littlefs: mount failed (%d), clearing CUSTOMER NV\r\n", ret);
        ret = drv_spiflash_erase(s_lfs_base, s_lfs_size) == FLASH_RET_SUCCESS ? 0 : LFS_ERR_IO;
        if (!ret)
#endif
        ret = lfs_format(&lfs, &cfg);
        if (!ret)
        {
            ret = lfs_mount(&lfs, &cfg);
        }
    }
    if (ret)
    {
        os_printf(LM_APP, LL_ERR, "littlefs: mount/format failed: %d\r\n", ret);
        os_mutex_destroy(s_lfs_mutex);
        s_lfs_mutex = NULL;
        return NULL;
    }

    s_lfs_mounted = 1;
    return &lfs;
}

#ifdef CONFIG_MATTER_CUSTOMER_LFS
/* Caller serializes all CUSTOMER operations and ensures no open files remain. */
int lfs_customer_remount(void)
{
    int ret;
    if (!s_lfs_mutex) { return LFS_ERR_IO; }
    ret = s_lfs_mounted ? lfs_unmount(&lfs) : 0;
    s_lfs_mounted = 0;
    if (!ret) { ret = lfs_mount(&lfs, &cfg); }
    if (!ret) { s_lfs_mounted = 1; }
    return ret;
}

int lfs_customer_reset(void)
{
    int ret;
    if (!s_lfs_mutex && !lfs_init()) { return LFS_ERR_IO; }
    ret = s_lfs_mounted ? lfs_unmount(&lfs) : 0;
    s_lfs_mounted = 0;
    if (!ret) {
        ret = drv_spiflash_erase(s_lfs_base, s_lfs_size) == FLASH_RET_SUCCESS ? 0 : LFS_ERR_IO;
    }
    if (!ret) { ret = lfs_format(&lfs, &cfg); }
    if (!ret) { ret = lfs_mount(&lfs, &cfg); }
    if (!ret) { s_lfs_mounted = 1; }
    return ret;
}
#endif
