/*
 * This file is part of the EasyFlash Library.
 *
 * Copyright (c) 2019, Armink, <armink.ztl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Function: Environment variables operating interface. This is the Next Generation version.
 * Created on: 2019-02-02
 */

#include <string.h>
#include <stdlib.h>

#include <easyflash.h>
#include "flash.h"
#include "chip_memmap.h"




//#if defined(EF_USING_ENV) && !defined(EF_ENV_USING_LEGACY_MODE)
#if defined(CONFIG_NV)

#ifndef EF_WRITE_GRAN
#error "Please configure flash write granularity (in ef_cfg.h)"
#endif

#if EF_WRITE_GRAN != 1 && EF_WRITE_GRAN != 8 && EF_WRITE_GRAN != 32 && EF_WRITE_GRAN != 64
#error "the write gran can be only setting as 1, 8, 32 and 64"
#endif


//#define SUPPORT_NV_PARTION	1
#define min(x, y)               (((x) < (y))? (x) : (y))


/* the ENV max name length must less then it */
#define EF_ENV_NAME_MAX                          32
/* magic word(`E`, `F`, `4`, `0`) */
#define SECTOR_MAGIC_WORD                        0x30344645

/* magic word(`E`, `F`, `4`, `1`) */
#define SECTOR_MAGIC_WORD_NV                     0x31344645

/* the using status sector table length */
#ifndef USING_SECTOR_TABLE_LEN
#define USING_SECTOR_TABLE_LEN                   4
#endif

/* the string ENV value buffer size for legacy ef_get_env() function */
#ifndef EF_STR_ENV_VALUE_MAX_SIZE
#define EF_STR_ENV_VALUE_MAX_SIZE                1024
#endif

/* the sector remain threshold before full status */
#ifndef EF_SEC_REMAIN_THRESHOLD
#define EF_SEC_REMAIN_THRESHOLD                  (ENV_HDR_DATA_SIZE + EF_ENV_NAME_MAX)
#endif

/* the total remain empty sector threshold before GC */
#ifndef EF_GC_EMPTY_SEC_THRESHOLD
#define EF_GC_EMPTY_SEC_THRESHOLD                1
#endif

/* the sector is not combined value */
#define SECTOR_NOT_COMBINED                      0xFFFFFFFF
/* the next address is get failed */
#define FAILED_ADDR                              0xFFFFFFFF

/* Return the most contiguous size aligned at specified width. RT_ALIGN(13, 4)
 * would return 16.
 */
#define EF_ALIGN(size, align)                    (((size) + (align) - 1) & ~((align) - 1))
/* align by write granularity */
#define EF_WG_ALIGN(size)                        (EF_ALIGN(size, (EF_WRITE_GRAN + 7)/8))
/**
 * Return the down number of aligned at specified width. RT_ALIGN_DOWN(13, 4)
 * would return 12.
 */
#define EF_ALIGN_DOWN(size, align)               ((size) & ~((align) - 1))
/* align down by write granularity */
#define EF_WG_ALIGN_DOWN(size)                   (EF_ALIGN_DOWN(size, (EF_WRITE_GRAN + 7)/8))

#if (EF_WRITE_GRAN == 1)
#define STATUS_TABLE_SIZE(status_number)         ((status_number * EF_WRITE_GRAN + 7)/8)
#else
#define STATUS_TABLE_SIZE(status_number)         (((status_number - 1) * EF_WRITE_GRAN + 7)/8)
#endif

#define STORE_STATUS_TABLE_SIZE                  STATUS_TABLE_SIZE(SECTOR_STORE_STATUS_NUM)
#define DIRTY_STATUS_TABLE_SIZE                  STATUS_TABLE_SIZE(SECTOR_DIRTY_STATUS_NUM)
#define ENV_STATUS_TABLE_SIZE                    STATUS_TABLE_SIZE(ENV_STATUS_NUM)

#define SECTOR_SIZE                              EF_ERASE_MIN_SIZE
#define SECTOR_NUM                               (ENV_AREA_SIZE / (EF_ERASE_MIN_SIZE))

#if (SECTOR_NUM < 2)
#error "The sector number must lager then or equal to 2"
#endif

#if (EF_GC_EMPTY_SEC_THRESHOLD == 0 || EF_GC_EMPTY_SEC_THRESHOLD >= SECTOR_NUM)
#error "There is at least one empty sector for GC."
#endif

#define SECTOR_HDR_DATA_SIZE                     (EF_WG_ALIGN(sizeof(struct sector_hdr_data)))
#define SECTOR_DIRTY_OFFSET                      ((unsigned long)(&((struct sector_hdr_data *)0)->status_table.dirty))
#define ENV_HDR_DATA_SIZE                        (EF_WG_ALIGN(sizeof(struct env_hdr_data)))
#define ENV_LEN_OFFSET                           ((unsigned long)(&((struct env_hdr_data *)0)->len))
#define ENV_NAME_LEN_OFFSET                      ((unsigned long)(&((struct env_hdr_data *)0)->name_len))

#define VER_NUM_ENV_NAME                         "__ver_num__"

enum sector_store_status {
    SECTOR_STORE_UNUSED,
    SECTOR_STORE_EMPTY,
    SECTOR_STORE_USING,
    SECTOR_STORE_FULL,
    SECTOR_STORE_STATUS_NUM,
};
typedef enum sector_store_status sector_store_status_t;

enum sector_dirty_status {
    SECTOR_DIRTY_UNUSED,
    SECTOR_DIRTY_FALSE,
    SECTOR_DIRTY_TRUE,
    SECTOR_DIRTY_GC,
    SECTOR_DIRTY_STATUS_NUM,
};
typedef enum sector_dirty_status sector_dirty_status_t;

enum env_status {
    ENV_UNUSED,
    ENV_PRE_WRITE,
    ENV_WRITE,
    ENV_PRE_DELETE,
    ENV_DELETED,
    ENV_ERR_HDR,
    ENV_STATUS_NUM,
};
typedef enum env_status env_status_t;

struct sector_hdr_data {
    struct {
        uint8_t store[STORE_STATUS_TABLE_SIZE];  /**< sector store status @see sector_store_status_t */
        uint8_t dirty[DIRTY_STATUS_TABLE_SIZE];  /**< sector dirty status @see sector_dirty_status_t */
    } status_table;
    uint32_t magic;                              /**< magic word(`E`, `F`, `4`, `0`) */
    uint32_t combined;                           /**< the combined next sector number, 0xFFFFFFFF: not combined */
    uint32_t reserved;
};
typedef struct sector_hdr_data *sector_hdr_data_t;

struct sector_meta_data {
    bool check_ok;                               /**< sector header check is OK */
    struct {
        sector_store_status_t store;             /**< sector store status @see sector_store_status_t */
        sector_dirty_status_t dirty;             /**< sector dirty status @see sector_dirty_status_t */
    } status;
    uint32_t addr;                               /**< sector start address */
    uint32_t magic;                              /**< magic word(`E`, `F`, `4`, `0`) */
    uint32_t combined;                           /**< the combined next sector number, 0xFFFFFFFF: not combined */
    signed int remain;                               /**< remain size */
    uint32_t empty_env;                          /**< the next empty ENV node start address */
};
typedef struct sector_meta_data *sector_meta_data_t;

struct env_hdr_data {
    uint8_t status_table[ENV_STATUS_TABLE_SIZE]; /**< ENV node status, @see node_status_t */
    uint32_t len;                                /**< ENV node total length (header + name + value), must align by EF_WRITE_GRAN */
    uint32_t crc32;                              /**< ENV node crc32(name_len + data_len + name + value) */
    uint8_t name_len;                            /**< name length */
    uint32_t value_len;                          /**< value length */
};
typedef struct env_hdr_data *env_hdr_data_t;

struct env_meta_data {
    bool crc_is_ok;                              /**< ENV node CRC32 check is OK */
    env_status_t status;                         /**< ENV node status, @see node_status_t */
    uint8_t name_len;                            /**< name length */
    uint32_t len;                                /**< ENV node total length (header + name + value), must align by EF_WRITE_GRAN */
    uint32_t value_len;                          /**< value length */
    char name[EF_ENV_NAME_MAX];                  /**< name */
    struct {
        uint32_t start;                          /**< ENV node start address */
        uint32_t value;                          /**< value start address */
    } addr;
};
typedef struct env_meta_data *env_meta_data_t;

static EfErrCode gc_collect(void);

/* ENV start address in flash */
static unsigned int  env_start_addr = 0;
/* default ENV set, must be initialized by user */
static ef_env const *default_env_set;
/* default ENV set size, must be initialized by user */
static signed int default_env_set_size = 0;
/* initialize OK flag */
static bool init_ok = false;
/* request a GC check */
static bool gc_request = false;


static unsigned int  env_total_len = 0x3000;


#define PARTION_BASE 0x0
#define PARTION_SIZE 0x1000
#define PARTION_PASSER_OFFSET		0x400

#define PARTION_INDEX_MAX			11

#define PARTION_LMA_ANDES1		0x4000
#define PARTION_LMA_ANDES2		0x80000
#define PARTION_LMA_NV_USR		0xFB000
#define PARTION_LMA_NV_BACKUP	0xFE000
#define PARTION_LMA_NV_AMT		0xFF000

#define KEY_MAX_LEN		16

struct _partion_info
{
	char  key[KEY_MAX_LEN];
	unsigned int base;
	unsigned int length;
};

struct _partion_dev
{
	unsigned int num;
	unsigned int index_cfg;
	unsigned int index_nv_customer;
	unsigned int index_nv_develop;
	unsigned int index_nv_amt;
	
	struct _partion_info partion[PARTION_INDEX_MAX];
};
typedef struct _partion_dev  partion_dev;

static partion_dev partionDev;

//int  partion_info_get(char * key, unsigned int *addr, unsigned int * length);



static signed int set_status(uint8_t status_table[], signed int status_num, signed int status_index)
{
    signed int byte_index = ~0UL;
    /*
     * | write garn |       status0       |       status1       |      status2         |
     * ---------------------------------------------------------------------------------
     * |    1bit    | 0xFF                | 0x7F                |  0x3F                |
     * |    8bit    | 0xFFFF              | 0x00FF              |  0x0000              |
     * |   32bit    | 0xFFFFFFFF FFFFFFFF | 0x00FFFFFF FFFFFFFF |  0x00FFFFFF 00FFFFFF |
     */
    memset(status_table, 0xFF, STATUS_TABLE_SIZE(status_num));
    if (status_index > 0) {
#if (EF_WRITE_GRAN == 1)
        byte_index = (status_index - 1) / 8;
        status_table[byte_index] &= ~(0x80 >> ((status_index - 1) % 8));
#else
        byte_index = (status_index - 1) * (EF_WRITE_GRAN / 8);
        status_table[byte_index] = 0x00;
#endif /* EF_WRITE_GRAN == 1 */
    }

    return byte_index;
}

static signed int get_status(uint8_t status_table[], signed int status_num)
{
    signed int i = 0, status_num_bak = --status_num;

    while (status_num --) {
        /* get the first 0 position from end address to start address */
#if (EF_WRITE_GRAN == 1)
        if ((status_table[status_num / 8] & (0x80 >> (status_num % 8))) == 0x00) {
            break;
        }
#else /*  (EF_WRITE_GRAN == 8) ||  (EF_WRITE_GRAN == 32) ||  (EF_WRITE_GRAN == 64) */
        if (status_table[status_num * EF_WRITE_GRAN / 8] == 0x00) {
            break;
        }
#endif /* EF_WRITE_GRAN == 1 */
        i++;
    }

    return status_num_bak - i;
}

static EfErrCode write_status(uint32_t addr, uint8_t status_table[], signed int status_num, signed int status_index)
{
    EfErrCode result = EF_NO_ERR;
    signed int byte_index;

    EF_ASSERT(status_index < status_num);
    EF_ASSERT(status_table);

    /* set the status first */
    byte_index = set_status(status_table, status_num, status_index);

    /* the first status table value is all 1, so no need to write flash */
    if (byte_index == ~0UL) {
        return EF_NO_ERR;
    }
#if (EF_WRITE_GRAN == 1)
    result = ef_port_write(addr + byte_index, (uint32_t *)&status_table[byte_index], 1);
#else /*  (EF_WRITE_GRAN == 8) ||  (EF_WRITE_GRAN == 32) ||  (EF_WRITE_GRAN == 64) */
    /* write the status by write granularity
     * some flash (like stm32 onchip) NOT supported repeated write before erase */
    result = ef_port_write(addr + byte_index, (uint32_t *) &status_table[byte_index], EF_WRITE_GRAN / 8);
#endif /* EF_WRITE_GRAN == 1 */

    return result;
}

static signed int read_status(uint32_t addr, uint8_t status_table[], signed int total_num)
{
    EF_ASSERT(status_table);

	if(ef_port_read(addr, (uint32_t *) status_table, STATUS_TABLE_SIZE(total_num)) != EF_NO_ERR)
	{
		  return EF_READ_ERR;
	}

    return get_status(status_table, total_num);
}

static uint32_t get_next_env_addr(sector_meta_data_t sector, env_meta_data_t pre_env)
{
    uint8_t status_table[ENV_STATUS_TABLE_SIZE];
    uint32_t addr = FAILED_ADDR;

    if (sector->status.store == SECTOR_STORE_EMPTY) {
        return FAILED_ADDR;
    }

    if (pre_env->addr.start == FAILED_ADDR) {
        /* the first ENV address */
		if(sector->addr == PARTION_BASE)
		{
	        addr = sector->addr + PARTION_PASSER_OFFSET;
		}
		else
		{
			addr = sector->addr + SECTOR_HDR_DATA_SIZE;
		}
    } else {
        if (pre_env->addr.start <= sector->addr + SECTOR_SIZE) {
            /* next ENV address */
            addr = pre_env->addr.start + pre_env->len;
			if ((addr + SECTOR_HDR_DATA_SIZE)  > (sector->addr + SECTOR_SIZE) || pre_env->len == 0){
                //TODO 扇区连续模式
                return FAILED_ADDR;
            }
        } else {
            /* no ENV */
            return FAILED_ADDR;
        }
    }
    /* check ENV status, it's ENV_UNUSED when not using */
    if (read_status(addr, status_table, ENV_STATUS_NUM) != ENV_UNUSED) {
        return addr;
    } else {
        /* no ENV */
        return FAILED_ADDR;
    }
}

static EfErrCode read_env(env_meta_data_t env)
{
    struct env_hdr_data env_hdr;
    uint8_t buf[32];
    uint32_t calc_crc32 = 0, crc_data_len, env_name_addr;
    EfErrCode result = EF_NO_ERR;
    signed int len, size;
    /* read ENV header raw data */
	if( ef_port_read(env->addr.start, (uint32_t *)&env_hdr, sizeof(struct env_hdr_data)) != EF_NO_ERR)
	{
		  return EF_READ_ERR;
	}
    env->status = (env_status_t) get_status(env_hdr.status_table, ENV_STATUS_NUM);
    env->len = env_hdr.len;

    if (env->len == ~0UL || env->len > env_total_len) {
        /* the ENV length was not write, so reserved the meta data for current ENV */
        env->len = ENV_HDR_DATA_SIZE;
        if (env->status != ENV_ERR_HDR) {
            env->status = ENV_ERR_HDR;
            EF_DEBUG("Error: The ENV @0x%08X length has an error.\n", env->addr.start);
            write_status(env->addr.start, env_hdr.status_table, ENV_STATUS_NUM, ENV_ERR_HDR);
        }
        env->crc_is_ok = false;
        return EF_READ_ERR;
    } else if (env->len > SECTOR_SIZE - SECTOR_HDR_DATA_SIZE && env->len < env_total_len) {
        //TODO 扇区连续模式，或者写入长度没有写入完整
        EF_ASSERT(0);
    }

    /* CRC32 data len(header.name_len + header.value_len + name + value) */
    crc_data_len = env->len - ENV_NAME_LEN_OFFSET;
    /* calculate the CRC32 value */
    for (len = 0, size = 0; len < crc_data_len; len += size) {
        if (len + sizeof(buf) < crc_data_len) {
            size = sizeof(buf);
        } else {
            size = crc_data_len - len;
        }

		if( ef_port_read(env->addr.start + ENV_NAME_LEN_OFFSET + len, (uint32_t *) buf, EF_WG_ALIGN(size)) != EF_NO_ERR)
		{
			  return EF_READ_ERR;
		}
        calc_crc32 = ef_calc_crc32(calc_crc32, buf, size);
    }
    /* check CRC32 */
    if (calc_crc32 != env_hdr.crc32) {
        env->crc_is_ok = false;
        result = EF_READ_ERR;
    } else {
        env->crc_is_ok = true;
        /* the name is behind aligned ENV header */
        env_name_addr = env->addr.start + ENV_HDR_DATA_SIZE;
		if(ef_port_read(env_name_addr, (uint32_t *) env->name, EF_WG_ALIGN(env_hdr.name_len)) != EF_NO_ERR)
		{
			  return EF_READ_ERR;
		}
        /* the value is behind aligned name */
        env->addr.value = env_name_addr + EF_WG_ALIGN(env_hdr.name_len);
        env->value_len = env_hdr.value_len;
        env->name_len = env_hdr.name_len;
    }

    return result;
}

static EfErrCode read_sector_meta_data(uint32_t addr, sector_meta_data_t sector, bool traversal)
{
    EfErrCode result = EF_NO_ERR;
    struct sector_hdr_data sec_hdr;

    EF_ASSERT(addr % SECTOR_SIZE == 0);
    EF_ASSERT(sector);

    /* read sector header raw data */
	if(ef_port_read(addr, (uint32_t *)&sec_hdr, sizeof(struct sector_hdr_data)) != EF_NO_ERR)
	{
		return EF_READ_ERR;
	}


    sector->addr = addr;
    sector->magic = sec_hdr.magic;
    /* check magic word */
    if ((sector->addr == 0x00) && (sector->magic != SECTOR_MAGIC_WORD)) {
        sector->check_ok = false;
        return EF_ENV_INIT_FAILED;
    }
	
    if ((sector->addr > 0x1000) && (sector->magic != SECTOR_MAGIC_WORD_NV) && (sector->magic != SECTOR_MAGIC_WORD)) {
        sector->check_ok = false;
        return EF_ENV_INIT_FAILED;
    }

    sector->check_ok = true;
    /* get other sector meta data */
    sector->combined = sec_hdr.combined;
    sector->status.store = (sector_store_status_t) get_status(sec_hdr.status_table.store, SECTOR_STORE_STATUS_NUM);
    sector->status.dirty = (sector_dirty_status_t) get_status(sec_hdr.status_table.dirty, SECTOR_DIRTY_STATUS_NUM);
    /* traversal all ENV and calculate the remain space size */
    if (traversal) {
        sector->remain = 0;
        sector->empty_env = sector->addr + SECTOR_HDR_DATA_SIZE;
        if (sector->status.store == SECTOR_STORE_EMPTY) {
            sector->remain = SECTOR_SIZE - SECTOR_HDR_DATA_SIZE;
        } else if (sector->status.store == SECTOR_STORE_USING) {
            struct env_meta_data env_meta;
            sector->remain = SECTOR_SIZE - SECTOR_HDR_DATA_SIZE;
            env_meta.addr.start = FAILED_ADDR;
            while ((env_meta.addr.start = get_next_env_addr(sector, &env_meta)) != FAILED_ADDR) {
                read_env(&env_meta);
                if (!env_meta.crc_is_ok) {
                    if (env_meta.status != ENV_PRE_WRITE && env_meta.status!= ENV_ERR_HDR) {
                        EF_INFO("Error: The ENV (@0x%08X) CRC32 check failed!\n", env_meta.addr.start);
                        sector->remain = 0;
                        result = EF_READ_ERR;
                        break;
                    }
                }
                sector->empty_env += env_meta.len;
                sector->remain -= env_meta.len;
            }
        }
    }

    return result;
}

static uint32_t get_next_sector_addr(sector_meta_data_t pre_sec)
{
    uint32_t next_addr;

    if (pre_sec->addr == FAILED_ADDR) {
        return env_start_addr;
    } else {
        /* check ENV sector combined */
        if (pre_sec->combined == SECTOR_NOT_COMBINED) {
            next_addr = pre_sec->addr + SECTOR_SIZE;
        } else {
            next_addr = pre_sec->addr + pre_sec->combined * SECTOR_SIZE;
        }
        /* check range */
        if (next_addr < env_start_addr + env_total_len) {
            return next_addr;
        } else {
            /* no sector */
            return FAILED_ADDR;
        }
    }
}

static void env_iterator(env_meta_data_t env, void *arg1, void *arg2,
        bool (*callback)(env_meta_data_t env, void *arg1, void *arg2))
{
    struct sector_meta_data sector;
    uint32_t sec_addr;
	int read_result = 0;

    sector.addr = FAILED_ADDR;
    /* search all sectors */
    while ((sec_addr = get_next_sector_addr(&sector)) != FAILED_ADDR) {
        if ((read_result = read_sector_meta_data(sec_addr, &sector, false)) != EF_NO_ERR) {
			EF_INFO("ERROR: The SECTOR=%x information read error:%d\n",sec_addr,read_result);
            init_ok = false;
            return ;
           // continue;
        }
        if (callback == NULL) {
            continue;
        }
        /* sector has ENV */
        if (sector.status.store == SECTOR_STORE_USING || sector.status.store == SECTOR_STORE_FULL) {
            env->addr.start = FAILED_ADDR;
            /* search all ENV */
            while ((env->addr.start = get_next_env_addr(&sector, env)) != FAILED_ADDR) {
                read_env(env);
                /* iterator is interrupted when callback return true */
                if (callback(env, arg1, arg2)) {
                    return;
                }
            }
        }
    }
}

static bool find_env_cb(env_meta_data_t env, void *arg1, void *arg2)
{
    const char *key = arg1;
    bool *find_ok = arg2;
    signed int key_len = strlen(key);

     if (key_len != env->name_len) {
	 	 return false;
     }
    /* check ENV */
    if (env->crc_is_ok && env->status == ENV_WRITE && !strncmp(env->name, key, key_len)) {
        *find_ok = true;
        return true;
    }
    return false;
}

static bool find_env(const char *key, env_meta_data_t env)
{
    bool find_ok = false;

    env_iterator(env, (void *)key, &find_ok, find_env_cb);

    return find_ok;
}

static bool ef_is_str(uint8_t *value, signed int len)
{
#define __is_print(ch)       ((unsigned int)((ch) - ' ') < 127u - ' ')
    signed int i;

    for (i = 0; i < len; i++) {
        if (!__is_print(value[i])) {
            return false;
        }
    }
    return true;
}

static signed int get_env(const char *key, void *value_buf, signed int buf_len, signed int *value_len)
{
    struct env_meta_data env;
    signed int read_len = 0;

    if (find_env(key, &env)) {
        if (value_len) {
            *value_len = env.value_len;
        }
        if (buf_len > env.value_len) {
            read_len = env.value_len;
        } else {
            read_len = buf_len;
        }
	
		if(ef_port_read(env.addr.value, (uint32_t *) value_buf, read_len) != EF_NO_ERR)
		{
			  return EF_READ_ERR;
		}
    }

    return read_len;
}

/**
 * Get a blob ENV value by key name.
 *
 * @param key ENV name
 * @param value_buf ENV blob buffer
 * @param buf_len ENV blob buffer length
 * @param value_len return the length of the value saved on the flash, 0: NOT found
 *
 * @return the actually get size on successful
 */

//#if SUPPORT_NV_PARTION
signed int ef_get_env_blob_partion(int partion_index, const char *key, void *value_buf, signed int buf_len, signed int *value_len)
{
    signed int read_len = 0;

    if (!init_ok) {
        EF_INFO("ENV isn't initialize OK.\n");
        return 0;
    }

    /* lock the ENV cache */
    ef_port_env_lock();

   	env_start_addr = partionDev.partion[partion_index].base;
   	env_total_len  = partionDev.partion[partion_index].length;

    read_len = get_env(key, value_buf, buf_len, value_len);

    /* unlock the ENV cache */
    ef_port_env_unlock();

    return read_len;
}


signed int customer_get_env_blob(const char *key, void *value_buf, signed int buf_len, signed int *value_len)
{
	if(partionDev.index_nv_customer == 0)
	{
        EF_INFO("partition table no customer partition.\n");

		return 0xffffffff;
	}

    return ef_get_env_blob_partion(partionDev.index_nv_customer, key, value_buf, buf_len, value_len);
}


signed int develop_get_env_blob(const char *key, void *value_buf, signed int buf_len, signed int *value_len)
{
	if(partionDev.index_nv_develop == 0)
	{
        EF_INFO("partition table no develop partition.\n");

		return 0xffffffff;
	}
    return ef_get_env_blob_partion(partionDev.index_nv_develop, key, value_buf, buf_len, value_len);
}


signed int amt_get_env_blob(const char *key, void *value_buf, signed int buf_len, signed int *value_len)
{
	if(partionDev.index_nv_amt == 0)
	{
        EF_INFO("partition table no amt partition.\n");

		return 0xffffffff;
	}
    return ef_get_env_blob_partion(partionDev.index_nv_amt, key, value_buf, buf_len, value_len);
}


signed int obk_get_env_blob(const char *key, void *value_buf, signed int buf_len, signed int *value_len)
{
	if(partionDev.index_cfg == 0)
	{
        EF_INFO("partition table no cfg partition.\n");

		return 0xffffffff;
	}
    return ef_get_env_blob_partion(partionDev.index_cfg, key, value_buf, buf_len, value_len);
}

/**
 * Get an ENV value by key name.
 *
 * @note this function is NOT supported reentrant
 * @note this function is DEPRECATED
 *
 * @param key ENV name
 *
 * @return value
 */
char *ef_get_env(const char *key)
{
    static char value[EF_STR_ENV_VALUE_MAX_SIZE + 1];
    signed int get_size;

    if ((get_size = customer_get_env_blob(key, value, EF_STR_ENV_VALUE_MAX_SIZE, NULL)) > 0) {
        /* the return value must be string */
        if (ef_is_str((uint8_t *)value, get_size)) {
            value[get_size] = '\0';
            return value;
        } else {
            EF_INFO("Warning: The ENV value isn't string. Could not be returned\n");
            return NULL;
        }
    }

    return NULL;
}

static EfErrCode write_env_hdr(uint32_t addr, env_hdr_data_t env_hdr) {
    EfErrCode result = EF_NO_ERR;
    /* write the status will by write granularity */
    result = write_status(addr, env_hdr->status_table, ENV_STATUS_NUM, ENV_PRE_WRITE);
    if (result != EF_NO_ERR) {
        return result;
    }
    /* write other header data */
    result = ef_port_write(addr + ENV_LEN_OFFSET, &env_hdr->len, sizeof(struct env_hdr_data) - ENV_LEN_OFFSET);

    return result;
}

static EfErrCode format_sector(uint32_t addr, uint32_t combined_value)
{
    EfErrCode result = EF_NO_ERR;
    struct sector_hdr_data sec_hdr;

    EF_ASSERT(addr % SECTOR_SIZE == 0);

    result = ef_port_erase(addr, SECTOR_SIZE);
    if (result == EF_NO_ERR) {
        /* initialize the header data */
        memset(&sec_hdr, 0xFF, sizeof(struct sector_hdr_data));
        set_status(sec_hdr.status_table.store, SECTOR_STORE_STATUS_NUM, SECTOR_STORE_EMPTY);
        set_status(sec_hdr.status_table.dirty, SECTOR_DIRTY_STATUS_NUM, SECTOR_DIRTY_FALSE);
        sec_hdr.magic = SECTOR_MAGIC_WORD_NV;
        sec_hdr.combined = combined_value;
        sec_hdr.reserved = 0xFFFFFFFF;
        /* save the header */
        result = ef_port_write(addr, (uint32_t *)&sec_hdr, sizeof(struct sector_hdr_data));
    }

    return result;
}

static EfErrCode update_sec_status(sector_meta_data_t sector, signed int new_env_len, bool *is_full)
{
    uint8_t status_table[STORE_STATUS_TABLE_SIZE];
    EfErrCode result = EF_NO_ERR;
    /* change the current sector status */
    if (sector->status.store == SECTOR_STORE_EMPTY) {
        /* change the sector status to using */
        result = write_status(sector->addr, status_table, SECTOR_STORE_STATUS_NUM, SECTOR_STORE_USING);
    } else if (sector->status.store == SECTOR_STORE_USING) {
        /* check remain size */
        if (sector->remain < EF_SEC_REMAIN_THRESHOLD || sector->remain - new_env_len < EF_SEC_REMAIN_THRESHOLD) {
            /* change the sector status to full */
            result = write_status(sector->addr, status_table, SECTOR_STORE_STATUS_NUM, SECTOR_STORE_FULL);
            if (is_full) {
                *is_full = true;
            }
        } else if (is_full) {
            *is_full = false;
        }
    }

    return result;
}

static EfErrCode sector_iterator(sector_meta_data_t sector, sector_store_status_t status, void *arg1, void *arg2,
        bool (*callback)(sector_meta_data_t sector, void *arg1, void *arg2), bool traversal_env) {
    uint32_t sec_addr;
    EfErrCode result = EF_NO_ERR;

    /* search all sectors */
    sector->addr = FAILED_ADDR;
    while ((sec_addr = get_next_sector_addr(sector)) != FAILED_ADDR) {
        result = read_sector_meta_data(sec_addr, sector, false);
		if(result == EF_READ_ERR)
		{
			return result;
		}
        if (status == SECTOR_STORE_UNUSED || status == sector->status.store) {
            if (traversal_env) {
                result = read_sector_meta_data(sec_addr, sector, traversal_env);
				if(result == EF_READ_ERR)
				{
					return result;
				}
            }
            /* iterator is interrupted when callback return true */
            if (callback && callback(sector, arg1, arg2)) {
                return result;
            }
        }
    }
	return result;
}

static bool sector_statistics_cb(sector_meta_data_t sector, void *arg1, void *arg2)
{
    signed int *empty_sector = arg1, *using_sector = arg2;

    if (sector->check_ok && sector->status.store == SECTOR_STORE_EMPTY) {
        (*empty_sector)++;
    } else if (sector->check_ok && sector->status.store == SECTOR_STORE_USING) {
        (*using_sector)++;
    }

    return false;
}

static bool alloc_env_cb(sector_meta_data_t sector, void *arg1, void *arg2)
{
    signed int *env_size = arg1;
    uint32_t *empty_env = arg2;

    /* 1. sector has space
     * 2. the NO dirty sector
     * 3. the dirty sector only when the gc_request is false */
    if (sector->check_ok && sector->remain > *env_size
            && ((sector->status.dirty == SECTOR_DIRTY_FALSE)
                    || (sector->status.dirty == SECTOR_DIRTY_TRUE && !gc_request))) {
        *empty_env = sector->empty_env;
        return true;
    }

    return false;
}

static uint32_t alloc_env(sector_meta_data_t sector, signed int env_size)
{
    uint32_t empty_env = FAILED_ADDR;
    signed int empty_sector = 0, using_sector = 0;

    /* sector status statistics */
    sector_iterator(sector, SECTOR_STORE_UNUSED, &empty_sector, &using_sector, sector_statistics_cb, false);
    if (using_sector > 0) {
        /* alloc the ENV from the using status sector first */
        sector_iterator(sector, SECTOR_STORE_USING, &env_size, &empty_env, alloc_env_cb, true);
    }
    if (empty_sector > 0 && empty_env == FAILED_ADDR) {
        if (empty_sector > EF_GC_EMPTY_SEC_THRESHOLD || gc_request) {
            sector_iterator(sector, SECTOR_STORE_EMPTY, &env_size, &empty_env, alloc_env_cb, true);
        } else {
            /* no space for new ENV now will GC and retry */
           // EF_DEBUG("Trigger a GC check after alloc ENV failed.\n");
            gc_request = true;
        }
    }

    return empty_env;
}

static EfErrCode del_env(const char *key, env_meta_data_t old_env, bool complete_del) {
    EfErrCode result = EF_NO_ERR;
    uint32_t dirty_status_addr;

#if (ENV_STATUS_TABLE_SIZE >= DIRTY_STATUS_TABLE_SIZE)
    uint8_t status_table[ENV_STATUS_TABLE_SIZE];
#else
    uint8_t status_table[DIRTY_STATUS_TABLE_SIZE];
#endif

    /* need find ENV */
    if (!old_env) {
        struct env_meta_data env;
        /* find ENV */
        if (find_env(key, &env)) {
            old_env = &env;
        } else {
            EF_DEBUG("Not found '%s' in ENV.\n", key);
            return EF_ENV_NAME_ERR;
        }
    }
    /* change and save the new status */
    if (!complete_del) {
        result = write_status(old_env->addr.start, status_table, ENV_STATUS_NUM, ENV_PRE_DELETE);
    } else {
        result = write_status(old_env->addr.start, status_table, ENV_STATUS_NUM, ENV_DELETED);
    }

    dirty_status_addr = EF_ALIGN_DOWN(old_env->addr.start, SECTOR_SIZE) + SECTOR_DIRTY_OFFSET;
    /* read and change the sector dirty status */
    if (result == EF_NO_ERR
            && read_status(dirty_status_addr, status_table, SECTOR_DIRTY_STATUS_NUM) == SECTOR_DIRTY_FALSE) {
        result = write_status(dirty_status_addr, status_table, SECTOR_DIRTY_STATUS_NUM, SECTOR_DIRTY_TRUE);
    }

    return result;
}

/*
 * move the ENV to new space
 */
extern char * os_strncpy(char *dest, const char *src, signed int n);

static EfErrCode move_env(env_meta_data_t env)
{
    EfErrCode result = EF_NO_ERR;
    uint8_t status_table[ENV_STATUS_TABLE_SIZE];
    uint32_t env_addr;
    struct sector_meta_data sector;

    /* prepare to delete the current ENV */
    if (env->status == ENV_WRITE) {
        del_env(NULL, env, false);
    }

    if ((env_addr = alloc_env(&sector, env->len)) != FAILED_ADDR) {
        struct env_meta_data env_bak;
        char name[EF_ENV_NAME_MAX + 1] = { 0 };
        //os_strncpy(name, env->name, env->name_len);
        strlcpy(name, env->name, env->name_len+1);
        /* check the ENV is already create success */
        if (find_env(name, &env_bak)) {
            /* already create success, don't need to duplicate */
            result = EF_NO_ERR;
            goto __exit;
        }
    } else {
        return EF_ENV_FULL;
    }
    /* start move the ENV */
    {
        uint8_t buf[32];
        signed int len, size, env_len = env->len;

        write_status(env_addr, status_table, ENV_STATUS_NUM, ENV_PRE_WRITE);
        env_len -= ENV_LEN_OFFSET;
        for (len = 0, size = 0; len < env_len; len += size) {
            if (len + sizeof(buf) < env_len) {
                size = sizeof(buf);
            } else {
                size = env_len - len;
            }
		
			if( ef_port_read(env->addr.start + ENV_LEN_OFFSET + len, (uint32_t *) buf, EF_WG_ALIGN(size)) != EF_NO_ERR)
			{
				  return EF_READ_ERR;
			}
            result = ef_port_write(env_addr + ENV_LEN_OFFSET + len, (uint32_t *) buf, size);
        }
        write_status(env_addr, status_table, ENV_STATUS_NUM, ENV_WRITE);
    }
    /* update the new ENV sector status */
    update_sec_status(&sector, env->len, NULL);

   // EF_DEBUG("Moved the ENV (%.*s) from 0x%08X to 0x%08X.\n", env->name_len, env->name, env->addr.start, env_addr);

__exit:
    del_env(NULL, env, true);

    return result;
}

static uint32_t new_env(sector_meta_data_t sector, signed int env_size)
{
    bool already_gc = false;
    uint32_t empty_env = FAILED_ADDR;

__retry:

    if ((empty_env = alloc_env(sector, env_size)) == FAILED_ADDR && gc_request && !already_gc) {
       // EF_DEBUG("Warning: Alloc an ENV (size %d) failed when new ENV. Now will GC then retry.\n", env_size);
        gc_collect();
        already_gc = true;
        goto __retry;
    }
	if(gc_request == true)
	{
		gc_request = false;
	}
    return empty_env;
}

static uint32_t new_env_by_kv(sector_meta_data_t sector, signed int key_len, signed int buf_len)
{
    signed int env_len = ENV_HDR_DATA_SIZE + EF_WG_ALIGN(key_len) + EF_WG_ALIGN(buf_len);

    return new_env(sector, env_len);
}

static bool gc_check_cb(sector_meta_data_t sector, void *arg1, void *arg2)
{
    signed int *empty_sec = arg1;

    if (sector->check_ok) {
        *empty_sec = *empty_sec + 1;
    }

    return false;

}

static bool do_gc(sector_meta_data_t sector, void *arg1, void *arg2)
{
    struct env_meta_data env;

    if (sector->check_ok && (sector->status.dirty == SECTOR_DIRTY_TRUE || sector->status.dirty == SECTOR_DIRTY_GC)) {
        uint8_t status_table[DIRTY_STATUS_TABLE_SIZE];
        //TODO 重复写入安全性验证
        /* change the sector status to GC */
        write_status(sector->addr + SECTOR_DIRTY_OFFSET, status_table, SECTOR_DIRTY_STATUS_NUM, SECTOR_DIRTY_GC);
        /* search all ENV */
        env.addr.start = FAILED_ADDR;
        while ((env.addr.start = get_next_env_addr(sector, &env)) != FAILED_ADDR) {
            read_env(&env);
            if (env.crc_is_ok && (env.status == ENV_WRITE || env.status == ENV_PRE_DELETE)) {
                /* move the ENV to new space */
                if (move_env(&env) != EF_NO_ERR) {
                    EF_DEBUG("Error: Moved the ENV (%.*s) for GC failed.\n", env.name_len, env.name);
                }
            }
        }
        format_sector(sector->addr, SECTOR_NOT_COMBINED);
        //EF_DEBUG("Collect a sector @0x%08X\n", sector->addr);
    }

    return false;
}

/*
 * The GC will be triggered on the following scene:
 * 1. alloc an ENV when the flash not has enough space
 * 2. write an ENV then the flash not has enough space
 */
static EfErrCode gc_collect(void)
{
    EfErrCode result = EF_NO_ERR;
    struct sector_meta_data sector;
    signed int empty_sec = 0;

    /* GC check the empty sector number */
    sector_iterator(&sector, SECTOR_STORE_EMPTY, &empty_sec, NULL, gc_check_cb, false);

    /* do GC collect */
   // EF_DEBUG("The remain empty sector is %d, GC threshold is %d.\n", empty_sec, EF_GC_EMPTY_SEC_THRESHOLD);
    if (empty_sec <= EF_GC_EMPTY_SEC_THRESHOLD) {
        result = sector_iterator(&sector, SECTOR_STORE_UNUSED, NULL, NULL, do_gc, false);
		if( result != EF_NO_ERR)
		{
			return result;
		}
    }

    gc_request = false;
	return result;
}

static EfErrCode align_write(uint32_t addr, const uint32_t *buf, signed int size)
{
    EfErrCode result = EF_NO_ERR;
    signed int align_remain;

#if (EF_WRITE_GRAN / 8 > 0)
    uint8_t align_data[EF_WRITE_GRAN / 8];
    signed int align_data_size = sizeof(align_data);
#else
    /* For compatibility with C89 */
    uint8_t align_data_u8, *align_data = &align_data_u8;
    signed int align_data_size = 1;
#endif

    memset(align_data, 0xFF, align_data_size);
    result = ef_port_write(addr, buf, EF_WG_ALIGN_DOWN(size));

    align_remain = size - EF_WG_ALIGN_DOWN(size);
    if (result == EF_NO_ERR && align_remain) {
        memcpy(align_data, (uint8_t *)buf + EF_WG_ALIGN_DOWN(size), align_remain);
        result = ef_port_write(addr + EF_WG_ALIGN_DOWN(size), (uint32_t *) align_data, align_data_size);
    }

    return result;
}

static EfErrCode create_env_blob(sector_meta_data_t sector, const char *key, const void *value, signed int len)
{
    EfErrCode result = EF_NO_ERR;
    struct env_hdr_data env_hdr;
    bool is_full = false;
    uint32_t env_addr = sector->empty_env;

    if (strlen(key) > EF_ENV_NAME_MAX) {
        EF_INFO("Error: The ENV name length is more than %d\n", EF_ENV_NAME_MAX);
        return EF_ENV_NAME_ERR;
    }

    memset(&env_hdr, 0xFF, sizeof(struct env_hdr_data));
    env_hdr.name_len = strlen(key);
    env_hdr.value_len = len;
    env_hdr.len = ENV_HDR_DATA_SIZE + EF_WG_ALIGN(env_hdr.name_len) + EF_WG_ALIGN(env_hdr.value_len);

    if (env_hdr.len > SECTOR_SIZE - SECTOR_HDR_DATA_SIZE) {
        EF_INFO("Error: The ENV size is too big\n");
        return EF_ENV_FULL;
    }

    if (env_addr != FAILED_ADDR || (env_addr = new_env(sector, env_hdr.len)) != FAILED_ADDR) {
        signed int align_remain;
        /* update the sector status */
        if (result == EF_NO_ERR) {
            result = update_sec_status(sector, env_hdr.len, &is_full);
        }
        if (result == EF_NO_ERR) {
            uint8_t ff = 0xFF;
            /* start calculate CRC32 */
            env_hdr.crc32 = ef_calc_crc32(0, &env_hdr.name_len, ENV_HDR_DATA_SIZE - ENV_NAME_LEN_OFFSET);
            env_hdr.crc32 = ef_calc_crc32(env_hdr.crc32, key, env_hdr.name_len);
            align_remain = EF_WG_ALIGN(env_hdr.name_len) - env_hdr.name_len;
            while (align_remain--) {
                env_hdr.crc32 = ef_calc_crc32(env_hdr.crc32, &ff, 1);
            }
            env_hdr.crc32 = ef_calc_crc32(env_hdr.crc32, value, env_hdr.value_len);
            align_remain = EF_WG_ALIGN(env_hdr.value_len) - env_hdr.value_len;
            while (align_remain--) {
                env_hdr.crc32 = ef_calc_crc32(env_hdr.crc32, &ff, 1);
            }
            /* write ENV header data */
            result = write_env_hdr(env_addr, &env_hdr);
        }
        /* write key name */
        if (result == EF_NO_ERR) {
            result = align_write(env_addr + ENV_HDR_DATA_SIZE, (uint32_t *) key, env_hdr.name_len);
        }
        /* write value */
        if (result == EF_NO_ERR) {
            result = align_write(env_addr + ENV_HDR_DATA_SIZE + EF_WG_ALIGN(env_hdr.name_len), value,
                    env_hdr.value_len);
        }
        /* change the ENV status to ENV_WRITE */
        if (result == EF_NO_ERR) {
            result = write_status(env_addr, env_hdr.status_table, ENV_STATUS_NUM, ENV_WRITE);
        }
        /* trigger GC collect when current sector is full */
        if (result == EF_NO_ERR && is_full) {
           // EF_DEBUG("Trigger a GC check after created ENV.\n");
            gc_request = true;
        }
    } else {
        result = EF_ENV_FULL;
    }

    return result;
}

/**
 * Delete an ENV.
 *
 * @param key ENV name
 *
 * @return result
 */
EfErrCode ef_del_env_partion(int partion_index, const char *key)
{
    EfErrCode result = EF_NO_ERR;

    if (!init_ok) {
        EF_INFO("Error: ENV isn't initialize OK.\n");
        return EF_ENV_INIT_FAILED;
    }

    /* lock the ENV cache */
    ef_port_env_lock();

    env_start_addr = partionDev.partion[partion_index].base;
    env_total_len  = partionDev.partion[partion_index].length;

    if (env_total_len > EF_ERASE_MIN_SIZE) {
        result = del_env(key, NULL, true);
    } else {
        result = EF_WRITE_ERR;	
    }

    /* unlock the ENV cache */
    ef_port_env_unlock();

    return result;
}


EfErrCode customer_del_env(const char *key)
{
	if(partionDev.index_nv_customer == 0)
	{
        EF_INFO("partition table no customer partition.\n");

		return EF_WRITE_ERR;
	}
    return ef_del_env_partion(partionDev.index_nv_customer, key);
}

EfErrCode develop_del_env(const char *key)
{

	if(partionDev.index_nv_develop == 0)
	{
        EF_INFO("partition table no develop partition.\n");

		return EF_WRITE_ERR;
	}
	
    return ef_del_env_partion(partionDev.index_nv_develop, key);
}

EfErrCode amt_del_env(const char *key)
{

	if(partionDev.index_nv_amt == 0)
	{
        EF_INFO("partition table no amt partition.\n");

		return EF_WRITE_ERR;
	}
	
    return ef_del_env_partion(partionDev.index_nv_amt, key);
}



/**
 * The same to ef_del_env on this mode
 * It's compatibility with older versions (less then V4.0).
 *
 * @note this function is DEPRECATED
 *
 * @param key ENV name
 *
 * @return result
 */
EfErrCode ef_del_and_save_env(const char *key)
{
    return ef_del_env(key);
}

static EfErrCode set_env(const char *key, const void *value_buf, signed int buf_len)
{
    EfErrCode result = EF_NO_ERR;
    static struct env_meta_data env;
    static struct sector_meta_data sector;
    bool env_is_found = false;

    if (value_buf == NULL) {
        result = del_env(key, NULL, true);
    } else {
        /* make sure the flash has enough space */
        if (new_env_by_kv(&sector, strlen(key), buf_len) == FAILED_ADDR) {
			EF_INFO("ENV full!!!!!!!!!!!!!!!!!!!!!!! %d\n",sector.remain);
            return EF_ENV_FULL;
        }
        env_is_found = find_env(key, &env);
        /* prepare to delete the old ENV */
        if (env_is_found) {
            //wangc add: if values are equal, then do nothing
            if(env.value_len == buf_len && \
                memcmp((void *)(env.addr.value + MEM_BASE_XIP), value_buf, buf_len) == 0) {
                    return result;
            }
            result = del_env(key, &env, false);
        }
        /* create the new ENV */
        if (result == EF_NO_ERR) {
            result = create_env_blob(&sector, key, value_buf, buf_len);
        }
        /* delete the old ENV */
        if (env_is_found && result == EF_NO_ERR) {
            result = del_env(key, &env, true);
        }
        /* process the GC after set ENV */
        if (gc_request) {
            gc_collect();
        }
    }

    return result;
}

/**
 * Set a blob ENV. If it value is NULL, delete it.
 * If not find it in flash, then create it.
 *
 * @param key ENV name
 * @param value ENV value
 * @param len ENV value length
 *
 * @return result
 */
//#if SUPPORT_NV_PARTION
#if 1

EfErrCode ef_set_env_blob_partion(int partion_index, const char *key, const void *value_buf, signed int buf_len)
{
    EfErrCode result = EF_NO_ERR;


    if (!init_ok) {
        EF_INFO("ENV isn't initialize OK.\n");
        return EF_ENV_INIT_FAILED;
    }

    /* lock the ENV cache */
    ef_port_env_lock();

    env_start_addr = partionDev.partion[partion_index].base;
    env_total_len  = partionDev.partion[partion_index].length;
	//EF_INFO("env_start_addr = 0x%x.\n",env_start_addr);
	//EF_INFO("env_total_len = 0x%x.\n",env_total_len);

    if (env_total_len > EF_ERASE_MIN_SIZE) {
        result = set_env(key, value_buf, buf_len);
    } else {
        result = EF_WRITE_ERR;	
    }

    /* unlock the ENV cache */
    ef_port_env_unlock();

    return result;
}


EfErrCode customer_set_env_blob(const char *key, const void *value_buf, signed int buf_len)
{
	if(partionDev.index_nv_customer == 0)
	{
        EF_INFO("partition table no customer partition.\n");

		return EF_WRITE_ERR;
	}

	return ef_set_env_blob_partion(partionDev.index_nv_customer, key, value_buf, buf_len);
}


EfErrCode develop_set_env_blob(const char *key, const void *value_buf, signed int buf_len)
{
	if(partionDev.index_nv_develop == 0)
	{
        EF_INFO("partition table no backup partition.\n");

		return EF_WRITE_ERR;
	}
	
	return ef_set_env_blob_partion(partionDev.index_nv_develop, key, value_buf, buf_len);
}


EfErrCode amt_set_env_blob(const char *key, const void *value_buf, signed int buf_len)
{
	if(partionDev.index_nv_amt == 0)
	{
        EF_INFO("partition table no amt partition.\n");

		return EF_WRITE_ERR;
	}
	
	return ef_set_env_blob_partion(partionDev.index_nv_amt, key, value_buf, buf_len);
}


EfErrCode obk_set_env_blob(const char* key, const void* value_buf, signed int buf_len)
{
	if(partionDev.index_cfg == 0)
	{
		EF_INFO("partition table no cfg partition.\n");

		return EF_WRITE_ERR;
	}

	return ef_set_env_blob_partion(partionDev.index_cfg, key, value_buf, buf_len);
}


#else

EfErrCode ef_set_env_blob(const char *key, const void *value_buf, signed int buf_len)
{
    EfErrCode result = EF_NO_ERR;


    if (!init_ok) {
        EF_INFO("ENV isn't initialize OK.\n");
        return EF_ENV_INIT_FAILED;
    }

    /* lock the ENV cache */
    ef_port_env_lock();

    result = set_env(key, value_buf, buf_len);

    /* unlock the ENV cache */
    ef_port_env_unlock();

    return result;
}
#endif

/**
 * Set a string ENV. If it value is NULL, delete it.
 * If not find it in flash, then create it.
 *
 * @param key ENV name
 * @param value ENV value
 *
 * @return result
 */
EfErrCode ef_set_env(const char *key, const char *value)
{
    return customer_set_env_blob(key, value, strlen(value));
}

/**
 * The same to ef_set_env on this mode.
 * It's compatibility with older versions (less then V4.0).
 *
 * @note this function is DEPRECATED
 *
 * @param key ENV name
 * @param value ENV value
 *
 * @return result
 */
EfErrCode ef_set_and_save_env(const char *key, const char *value)
{
    return customer_set_env_blob(key, value, strlen(value));
}

/**
 * Save ENV to flash.
 *
 * @note this function is DEPRECATED
 */
EfErrCode ef_save_env(void)
{
    /* do nothing not cur mode */
    return EF_NO_ERR;
}

/**
 * ENV set default.
 *
 * @return result
 */
EfErrCode ef_env_set_default(void)
{
    EfErrCode result = EF_NO_ERR;
    uint32_t addr, i, value_len;
    struct sector_meta_data sector;

    EF_ASSERT(default_env_set);
    EF_ASSERT(default_env_set_size);

    /* lock the ENV cache */
    ef_port_env_lock();
    /* format all sectors */
//	partion_info_get(PARTION_NAME_NV_CUSTOMER, &env_start_addr, &env_total_len);

    for (addr = env_start_addr; addr < env_start_addr + env_total_len; addr += SECTOR_SIZE) {
        result = format_sector(addr, SECTOR_NOT_COMBINED);
        if (result != EF_NO_ERR) {
            goto __exit;
        }
    }
	
    /* create default ENV */
    for (i = 0; i < default_env_set_size; i++) {
        /* It seems to be a string when value length is 0.
         * This mechanism is for compatibility with older versions (less then V4.0). */
        if (default_env_set[i].value_len == 0) {
            value_len = strlen(default_env_set[i].value);
        } else {
            value_len = default_env_set[i].value_len;
        }
        sector.empty_env = FAILED_ADDR;
        create_env_blob(&sector, default_env_set[i].key, default_env_set[i].value, value_len);
        if (result != EF_NO_ERR) {
            goto __exit;
        }
    }
__exit:
    /* unlock the ENV cache */
    ef_port_env_unlock();

    return result;
}

static bool print_env_cb(env_meta_data_t env, void *arg1, void *arg2)
{
    bool value_is_str = true, print_value = false;
    signed int *using_size = arg1;

    if (env->crc_is_ok) {
        /* calculate the total using flash size */
        *using_size += env->len;
        /* check ENV */
        if (env->status == ENV_WRITE) {
            ef_print("%.*s=", env->name_len, env->name);

            if (env->value_len < EF_STR_ENV_VALUE_MAX_SIZE ) {
                uint8_t buf[32];
                signed int len, size;
__reload:
                /* check the value is string */
                for (len = 0, size = 0; len < env->value_len; len += size) {
                    if (len + sizeof(buf) < env->value_len) {
                        size = sizeof(buf);
                    } else {
                        size = env->value_len - len;
                    }
					
					if( ef_port_read(env->addr.value + len, (uint32_t *) buf, EF_WG_ALIGN(size)) != EF_NO_ERR)
					{
						  return EF_READ_ERR;
					}
                    if (print_value) {
                        ef_print("%.*s", size, buf);
                    } else if (!ef_is_str(buf, size)) {
                        value_is_str = false;
                        break;
                    }
                }
            } else {
                value_is_str = false;
            }
            if (value_is_str && !print_value) {
                print_value = true;
                goto __reload;
            } else if (!value_is_str) {
                ef_print("blob @0x%08X %dbytes", env->addr.value, env->value_len);
            }
            ef_print("\n");
        }
    }

    return false;
}


/**
 * Print ENV.
 */

//#if SUPPORT_NV_PARTION
#if 1
void ef_print_env_partion(int partion_index)
{
    struct env_meta_data env;
    signed int using_size = 0;

    if (!init_ok) {
        EF_INFO("ENV isn't initialize OK.\n");
        return;
    }

    /* lock the ENV cache */
    ef_port_env_lock();

    env_start_addr = partionDev.partion[partion_index].base;
    env_total_len  = partionDev.partion[partion_index].length;

    env_iterator(&env, &using_size, NULL, print_env_cb);

    //ef_print("\nmode: next generation\n");
    ef_print("size: %lu/%lu bytes.\n", using_size + (env_total_len/SECTOR_SIZE - EF_GC_EMPTY_SEC_THRESHOLD) * SECTOR_HDR_DATA_SIZE,
            env_total_len - SECTOR_SIZE * EF_GC_EMPTY_SEC_THRESHOLD);

    /* unlock the ENV cache */
    ef_port_env_unlock();
}



static bool recovery_env_cb(env_meta_data_t env, void *arg1, void *arg2)
{   
    uint8_t buf[EF_STR_ENV_VALUE_MAX_SIZE];

    if (env->crc_is_ok && env->status == ENV_WRITE) {
		if(ef_port_read(env->addr.value, (uint32_t *) buf, env->value_len) != EF_NO_ERR)
		{
			ef_print("Read flash addr overflow, error\n");
			return false;
		}
		env_start_addr = partionDev.partion[partionDev.index_nv_customer].base;
		env_total_len  = partionDev.partion[partionDev.index_nv_customer].length;

		env->name[env->name_len] = '\0';

		if (set_env(env->name, buf, env->value_len) != EF_NO_ERR) {
		   ef_print("ef_backup_recovery error\n");
		}

		env_start_addr = partionDev.partion[partionDev.index_nv_develop].base;
		env_total_len  = partionDev.partion[partionDev.index_nv_develop].length;
    }
	
    return false;
}


void backup_recovery(void)
{
    struct env_meta_data env;
	
    if (!init_ok) 
	{
        EF_INFO("ENV isn't initialize OK.\n");
        return;
    }
	
	if(partionDev.index_nv_develop == 0)
	{
        EF_INFO("partition table no develop partition.\n");
		return ;
	}
	else
	{
    /* lock the ENV cache */
    ef_port_env_lock();

    env_start_addr = partionDev.partion[partionDev.index_nv_develop].base;
    env_total_len  = partionDev.partion[partionDev.index_nv_develop].length;

    env_iterator(&env, NULL, NULL, recovery_env_cb);

    /* unlock the ENV cache */
    ef_port_env_unlock();
	}

}


void customer_print_env(void)
{
	if(partionDev.index_nv_customer == 0)
	{
        EF_INFO("partition table no customer partition.\n");
	}
	else
	{
		ef_print_env_partion(partionDev.index_nv_customer);
	}

}

void develop_print_env(void)
{
	if(partionDev.index_nv_develop == 0)
	{
        EF_INFO("partition table no develop partition.\n");
	}
	else
	{
    	ef_print_env_partion(partionDev.index_nv_develop);
	}
}


void amt_print_env(void)
{
	if(partionDev.index_nv_amt == 0)
	{
        EF_INFO("partition table no amt partition.\n");
	}
	else
	{
    	ef_print_env_partion(partionDev.index_nv_amt);
	}
}


#else

void ef_print_env(void)
{
    struct env_meta_data env;
    signed int using_size = 0;

    if (!init_ok) {
        EF_INFO("ENV isn't initialize OK.\n");
        return;
    }

    /* lock the ENV cache */
    ef_port_env_lock();

    env_iterator(&env, &using_size, NULL, print_env_cb);

    //ef_print("\nmode: next generation\n");
    ef_print("size: %lu/%lu bytes.\n", using_size + (SECTOR_NUM - EF_GC_EMPTY_SEC_THRESHOLD) * SECTOR_HDR_DATA_SIZE,
            ENV_AREA_SIZE - SECTOR_SIZE * EF_GC_EMPTY_SEC_THRESHOLD);

    /* unlock the ENV cache */
    ef_port_env_unlock();
}
#endif


#ifdef EF_ENV_AUTO_UPDATE
/*
 * Auto update ENV to latest default when current EF_ENV_VER_NUM is changed.
 */
static void env_auto_update(void)
{
    signed int saved_ver_num, setting_ver_num = EF_ENV_VER_NUM;

    if (get_env(VER_NUM_ENV_NAME, &saved_ver_num, sizeof(signed int), NULL) > 0) {
        /* check version number */
        if (saved_ver_num != setting_ver_num) {
            struct env_meta_data env;
            signed int i, value_len;
            struct sector_meta_data sector;
            EF_DEBUG("Update the ENV from version %d to %d.\n", saved_ver_num, setting_ver_num);
            for (i = 0; i < default_env_set_size; i++) {
                /* add a new ENV when it's not found */
                if (!find_env(default_env_set[i].key, &env)) {
                    /* It seems to be a string when value length is 0.
                     * This mechanism is for compatibility with older versions (less then V4.0). */
                    if (default_env_set[i].value_len == 0) {
                        value_len = strlen(default_env_set[i].value);
                    } else {
                        value_len = default_env_set[i].value_len;
                    }
                    sector.empty_env = FAILED_ADDR;
                    create_env_blob(&sector, default_env_set[i].key, default_env_set[i].value, value_len);
                }
            }
        } else {
            /* version number not changed now return */
            return;
        }
    }

    set_env(VER_NUM_ENV_NAME, &setting_ver_num, sizeof(signed int));
}
#endif /* EF_ENV_AUTO_UPDATE */

static bool check_sec_hdr_cb(sector_meta_data_t sector, void *arg1, void *arg2)
{
    if (!sector->check_ok) {
        EF_INFO("Warning: Sector header check failed. Set it to default.\n");
        ef_port_env_unlock();
        ef_env_set_default();
        ef_port_env_lock();
        return true;
    } else if (sector->status.dirty == SECTOR_DIRTY_GC) {
        /* make sure the GC request flag to true */
        gc_request = true;
        /* resume the GC operate */
        gc_collect();
    }

    return false;
}

static bool check_and_recovery_env_cb(env_meta_data_t env, void *arg1, void *arg2)
{
    /* recovery the prepare deleted ENV */
    if (env->crc_is_ok && env->status == ENV_PRE_DELETE) {
        EF_INFO("Found an ENV (%.*s) which has changed value failed. Now will recovery it.\n", env->name_len, env->name);
        /* recovery the old ENV */
        if (move_env(env) == EF_NO_ERR) {
            EF_DEBUG("Recovery the ENV successful.\n");
        } else {
            EF_DEBUG("Warning: Moved an ENV (size %d) failed when recovery. Now will GC then retry.\n", env->len);
            return true;
        }
    } else if (env->status == ENV_PRE_WRITE) {
        uint8_t status_table[ENV_STATUS_TABLE_SIZE];
        /* the ENV has not write finish, change the status to error */
        //TODO 绘制异常处理的状态装换图
        write_status(env->addr.start, status_table, ENV_STATUS_NUM, ENV_ERR_HDR);
        return true;
    }

    return false;
}

/**
 * Check and load the flash ENV meta data.
 *
 * @return result
 */
EfErrCode ef_load_env(void)
{
    EfErrCode result = EF_NO_ERR;
    struct env_meta_data env;
    struct sector_meta_data sector;
	
    /* lock the ENV cache */
    ef_port_env_lock();

    /* check all sector header */
    result = sector_iterator(&sector, SECTOR_STORE_UNUSED, NULL, NULL, check_sec_hdr_cb, false);
	if(result == EF_READ_ERR)
	{
		EF_DEBUG("ERROR: sector_iterator result = %d.\n", result);
		ef_port_env_unlock();
		return result;
	}

__retry:
    /* check all ENV for recovery */
    env_iterator(&env, NULL, NULL, check_and_recovery_env_cb);
    if (gc_request) {
        gc_collect();
        goto __retry;
    }

    /* unlock the ENV cache */
    ef_port_env_unlock();

    return result;
}

/**
 * Flash ENV initialize.
 *
 * @param default_env default ENV set for user
 * @param default_env_size default ENV set size
 *
 * @return result
 */

#if  1

static EfErrCode ef_env_init_index(int  partion_index) {
    EfErrCode result = EF_NO_ERR;

    env_start_addr = partionDev.partion[partion_index].base;
    env_total_len  = partionDev.partion[partion_index].length;

    EF_ASSERT(env_total_len);
    EF_ASSERT(env_total_len % EF_ERASE_MIN_SIZE == 0);

    EF_DEBUG("partion start address is 0x%08X, size is %d bytes.\n",  env_start_addr, env_total_len);

    if (env_total_len > EF_ERASE_MIN_SIZE) 
	{
        result = ef_load_env();
		if(result == EF_READ_ERR){
			EF_DEBUG("ERROR: ef_env_init_index result = %d.\n", result);}
    }
	return result;
}

EfErrCode ef_env_init(ef_env const *default_env, signed int default_env_size) {
    EfErrCode result = EF_NO_ERR;

    EF_ASSERT(default_env);
    EF_ASSERT(EF_ERASE_MIN_SIZE);
    EF_ASSERT((EF_STR_ENV_VALUE_MAX_SIZE * 8) % EF_WRITE_GRAN == 0);

    if (init_ok) {
        return EF_NO_ERR;
    }

    default_env_set = default_env;
    default_env_set_size = default_env_size;

    if (partionDev.num == 0) {
        partion_info_get(PARTION_NAME_NV_CUSTOMER, &env_start_addr, &env_total_len);
        partionDev.index_nv_customer = 0;
        partionDev.partion[0].base = env_start_addr;
        partionDev.partion[0].length = env_total_len;		
        result = ef_load_env();
    } else {
        result = ef_env_init_index(partionDev.index_nv_customer);
		if(result != EF_READ_ERR)
		{
			if(partionDev.index_cfg != 0)
	        	ef_env_init_index(partionDev.index_cfg);
			if(partionDev.index_nv_develop != 0)
	        	ef_env_init_index(partionDev.index_nv_develop);
			if(partionDev.index_nv_amt != 0)
	        	ef_env_init_index(partionDev.index_nv_amt);
		}
    }

    if (result != EF_READ_ERR) {
        init_ok = true;
		result = EF_NO_ERR;
    }
	else
	{
		init_ok = false;
		EF_DEBUG("ERROR: ef_env_init, result = %d\n",result);
	}
    return result;
}

#define DATA_BEGIN	"0x"

int partion_parser(env_meta_data_t env)
{
	char buf[64] = {0};
	char * data, *end;

	struct _partion_info * pPartion = &partionDev.partion[partionDev.num++];

	strlcpy(pPartion->key, env->name, min(env->name_len+1, KEY_MAX_LEN));

	if( ef_port_read(env->addr.value, (uint32_t *)buf, env->value_len) != EF_NO_ERR)
	{
		  return EF_READ_ERR;
	}

	data = strstr(buf, DATA_BEGIN);
	if (data == NULL)
	{
		return -1;
	}
	
	pPartion->base = strtol(data, &end, 0);

	data = strstr(end, DATA_BEGIN);
	if (data == NULL)
	{
		return -1;
	}
	
	pPartion->length = strtol(data, NULL, 0);

	if(!strncmp(env->name, PARTION_NAME_CFG, strlen(PARTION_NAME_CFG)))
	{
		partionDev.index_cfg = partionDev.num - 1;
	}
	if (!strncmp(env->name, PARTION_NAME_NV_CUSTOMER, strlen(PARTION_NAME_NV_CUSTOMER)))
	{
		partionDev.index_nv_customer = partionDev.num-1;
	}
	else if (!strncmp(env->name, PARTION_NAME_NV_DEVELOP, strlen(PARTION_NAME_NV_DEVELOP)))
	{
		partionDev.index_nv_develop = partionDev.num-1;
	}
	else if (!strncmp(env->name, PARTION_NAME_NV_AMT, strlen(PARTION_NAME_NV_AMT)))
	{
		partionDev.index_nv_amt = partionDev.num-1;
	}

	return EF_NO_ERR;
}


int  partion_info_get(char *key, unsigned int *addr, unsigned int * length)
{
	int i = 0;
	unsigned int base = 0, len = 0;
	struct _partion_info * pPartion;

	if (partionDev.num != 0)
	{
		for (i=0; i<partionDev.num; i++)
		{
			pPartion = &partionDev.partion[i];
			if (!strncmp(key, pPartion->key, strlen(key)))
			{
				base = pPartion->base;
				len = pPartion->length;
				break;
			}
		}
	}

	if (partionDev.num == i)
	{
		return -1;
	}

	if (addr)
	{
		*addr = base;
	}

	if (length)
	{
		*length = len;
	}

	return EF_NO_ERR;
}


void partion_print_all(void)
{
	int i;
	struct _partion_info * pPartion;

	if (partionDev.num == 0)
	{
		EF_DEBUG("partionDev.num !!\n");
	}
		

	for (i=0; i<partionDev.num; i++)
	{
		pPartion = &partionDev.partion[i];
		EF_DEBUG("partion[%d] -%15s, start address is 0x%08X, size is %8d bytes.\n",  i, pPartion->key, pPartion->base, pPartion->length);
	}
}

void partion_get_string_part(char *buff, unsigned int len)
{
    int i;
    int offset = 0;
    struct _partion_info * pPartion;

    if (partionDev.num == 0)
    {
        snprintf(buff, len, "%s", "none");
        return;
    }

    for (i = 0; i < partionDev.num; i++)
    {
        pPartion = &partionDev.partion[i];
        offset += snprintf(buff + offset, len - offset, "%15s,0x%08X,%8d", pPartion->key, pPartion->base, pPartion->length);
        if (i != partionDev.num - 1)
        {
            offset += snprintf(buff + offset, len - offset, "%s", "\n");
        }
    }
}


int partion_init(void)
{
	struct sector_meta_data sector;
	struct env_meta_data env;

	if (read_sector_meta_data(PARTION_BASE, &sector, false) != EF_NO_ERR)
	{
		return EF_READ_ERR;
	}

	/* sector has ENV */
	if (sector.status.store == SECTOR_STORE_USING || sector.status.store == SECTOR_STORE_FULL)
	{
		env.addr.start = FAILED_ADDR;
		env_total_len = PARTION_SIZE;
		/* search all ENV */
		while ((env.addr.start = get_next_env_addr(&sector, &env)) != FAILED_ADDR)
		{
			if (read_env(&env) == EF_NO_ERR)
			{
				if (env.status != ENV_WRITE)
				{
					return EF_PARTITION_STATUS_ERR;
				}

				if (partion_parser (&env) )
				{
					EF_DEBUG("ENV parser error, %s\n", partionDev.partion[partionDev.num].key);
				}
			}
			else
			{
				return EF_READ_ERR;
			}
		}

		return EF_NO_ERR;
	}

	return EF_ENV_INIT_FAILED;
}

#else

EfErrCode ef_env_init(ef_env const *default_env, signed int default_env_size) {
    EfErrCode result = EF_NO_ERR;

    EF_ASSERT(default_env);
    EF_ASSERT(ENV_AREA_SIZE);
    EF_ASSERT(EF_ERASE_MIN_SIZE);
    /* must be aligned with erase_min_size */
    EF_ASSERT(ENV_AREA_SIZE % EF_ERASE_MIN_SIZE == 0);
    /* must be aligned with write granularity */
    EF_ASSERT((EF_STR_ENV_VALUE_MAX_SIZE * 8) % EF_WRITE_GRAN == 0);

    if (init_ok) {
        return EF_NO_ERR;
    }

    env_start_addr = EF_START_ADDR;
    env_total_len = ENV_AREA_SIZE;
	
    default_env_set = default_env;
    default_env_set_size = default_env_size;

    EF_DEBUG("ENV start address is 0x%08X, size is %d bytes.\n", EF_START_ADDR, ENV_AREA_SIZE);

    result = ef_load_env();

#ifdef EF_ENV_AUTO_UPDATE
    if (result == EF_NO_ERR) {
        env_auto_update();
    }
#endif

    if (result == EF_NO_ERR) {
        init_ok = true;
    }

    return result;
}



#endif


#endif /* defined(EF_USING_ENV) && !defined(EF_ENV_USING_LEGACY_MODE) */
