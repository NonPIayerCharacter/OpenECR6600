/**
 * @file ota.c
 * @brief OTA (Over-The-Air) firmware update implementation
 * @details This file contains the implementation of OTA firmware update functionality including
 *          download state management, flash operations, and partition handling.
 * @author your_name
 * @date today
 * @version V0.1
 * @Copyright © 2025 BEIJING ESWIN COMPUTING TECHINOLOGY CO., LTD and its affiliates ("ESWIN").
 *            All rights reserved. Any modification, reproduction, adaption, translation,
 *            distribution is prohibited without consent.
 * @par History 1:
 *      Date:
 *      Version:
 *      Author:
 *      Modification:
 *
 * @par History 2:
 */

/*--------------------------------------------------------------------------
 *                                 Include files
 *--------------------------------------------------------------------------*/
#include <stddef.h>
#include <string.h>
#include "flash.h"
#include "easyflash.h"
#include "oshal.h"
#include "ota.h"
#include "hal_system.h"
#include "eswin_sdk_version.h"
#include "efuse.h"
#include "hash.h"
#include "chip_memmap.h"
#if defined(CONFIG_MBEDTLS)
#include "mbedtls/sha256.h"
#endif

/*--------------------------------------------------------------------------
 *                                 Local Macros
 *--------------------------------------------------------------------------*/
/**
 * @brief Macro for checking return values in OTA operations
 * @details If the return value is non-zero, prints error message, updates status,
 *          releases memory and returns error code.
 */
#define DOWNLOAD_RET_CHECK_RETURN(ret) \
    {                                  \
        if ((ret) != 0)                \
        {                              \
            os_printf(LM_APP, LL_ERR, "%s[%d]ret:%d\n", __FUNCTION__, __LINE__, ret); \
            if (g_ota_download) g_ota_download->dw_status = ret; \
            download_mem_release(&g_ota_download); \
            return (-DOWNLOAD_INIT_ERR); \
        } \
    }

/** OTA partition size definitions */
#define OTA_STAT_PART_SIZE     (4 * 1024)
#define OTA_BACKUP_PART_SIZE   (28 * 1024)
/** OTA size alignment */
#define OTA_SIZE_ALIGN_4K(x)   ((x + 0xFFF) & (~0xFFF))

/** OTA AB update flags */
#define OTA_AB_UPDATAFLAG      (0x80)
#define OTA_MOVE_CZ_A2B        (0x40)
#define OTA_AB_SELECT_B        (0x10)
#define OTA_AB_DIRETC_START    (0x08)
#define OTA_AB_TRY_COUNT       (0x07)

/*--------------------------------------------------------------------------
 *                                 Local Types structs menus
 *--------------------------------------------------------------------------*/
/**
 * @brief Enumeration of download states
 */
typedef enum {
    DOWNLOAD_IDLE_ST,
    DOWNLOAD_HEAD_ST,
    DOWNLOAD_BOOT_ST,
    DOWNLOAD_IMGE_ST,
    DOWNLOAD_IMGA_ST,
    DOWNLOAD_IMGB_ST,
    DOWNLOAD_FULL_ST,
} download_state_e;

/**
 * @brief Enumeration of download partitions
 */
typedef enum {
    DOWNLOAD_BOOT_PART,
    DOWNLOAD_IMGA_PART,
    DOWNLOAD_IMGB_PART,
    DOWNLOAD_OTAS_PART,
    DOWNLOAD_FULL_PART,
} download_part_e;

/**
 * @brief Enumeration of download error codes
 */
typedef enum {
    DOWNLOAD_NONE_ERR,
    DOWNLOAD_METH_ERR,
    DOWNLOAD_INIT_ERR,
    DOWNLOAD_INITD_ERR,
    DOWNLOAD_NOMEM_ERR,
    DOWNLOAD_PART_ERR,
    DOWNLOAD_MAGIC_ERR,
    DOWNLOAD_SIZE_ERR,
    DOWNLOAD_CRC_ERR,
    DOWNLOAD_STAT_ERR,
} download_err_e;

/**
 * @brief OTA state structure
 */
typedef struct {
    unsigned char magic[3];
    unsigned char len;
    unsigned int  crc;
    unsigned int  patch_addr;
    unsigned int  patch_size;
    unsigned char update_flag;
    unsigned char update_state;
    unsigned char updata_ab;
    unsigned char reserved;
    unsigned int  ctrl;
    unsigned int  diff;
    unsigned int  exd;
    unsigned int  startpos;
    unsigned int  movecnt;
    unsigned int  movenum;
} ota_state_t;

/**
 * @brief Structure for download operation context
 * @details Contains all state and data needed for OTA download operation
 */
typedef struct {
    ota_package_head_t dw_head;
    download_state_e   dw_state;
    download_method_e  dw_lmethod;
    download_method_e  dw_rmethod;
    int dw_status;
    unsigned char *dw_buff;
    unsigned int   dw_buff_offt;
    unsigned int   dw_buff_size;
    unsigned int   dw_skip;
    unsigned int   dw_addr;
    unsigned int   dw_offt;
    unsigned int   dw_dlen;
    unsigned int   dw_all_dlen;
    unsigned int   dw_percent;
    unsigned char  dw_active_part;
    unsigned int   dw_paddr[DOWNLOAD_FULL_PART];
    unsigned int   dw_pdlen[DOWNLOAD_FULL_PART];
} download_oper_t;

/*--------------------------------------------------------------------------
 *                                 Global Variables
 *--------------------------------------------------------------------------*/
/** Global OTA download operation handle */
download_oper_t *g_ota_download;

/*--------------------------------------------------------------------------
 *                                 Local Function Prototypes
 *--------------------------------------------------------------------------*/
static void download_mem_release(download_oper_t **handle);
static int download_packet_data(download_oper_t *handle, unsigned char *data, unsigned int len);
static void download_packet_head_dump(download_oper_t *handle);
static void download_operate_dump(download_oper_t *handle);
static int download_get_lmethod(download_oper_t *handle);
static int download_get_active_part(download_oper_t *handle);
static int download_partition_init(download_oper_t *handle);
static int download_packet_flash(download_oper_t *handle, unsigned char *data, unsigned int len);
static int download_packet_flush(download_oper_t *handle);
static int download_package_head(download_oper_t *handle, unsigned char *data, unsigned int len);
static int download_get_firmware_size(unsigned int firmwareaddr);
static int download_packet_head_check(download_oper_t *handle);
static int download_packet_complete(download_oper_t *handle, unsigned char *data, unsigned int len);
static int download_package_boot(download_oper_t *handle, unsigned char *data, unsigned int len);
static int download_package_imge(download_oper_t *handle, unsigned char *data, unsigned int len);
static int download_mem_init(download_oper_t **handle);
static int download_packet_done(download_oper_t *handle, ota_state_t *state);
static int ota_state_part_read(ota_state_t *state);
static int ota_state_part_update(const ota_state_t *state);
static int ota_validate_image_part(unsigned int addr, unsigned int part_len);

/*--------------------------------------------------------------------------
 *                                 Function Definitions
 *--------------------------------------------------------------------------*/

/**
 * @brief Dump packet head information
 * @param handle Pointer to download operation context
 */
static void download_packet_head_dump(download_oper_t *handle)
{
    os_printf(LM_APP, LL_ERR, "magic:%c%c%c%c%c%c%c\n", handle->dw_head.magic[0], handle->dw_head.magic[1],
              handle->dw_head.magic[2], handle->dw_head.magic[3], handle->dw_head.magic[4],
              handle->dw_head.magic[5], handle->dw_head.magic[6]);
    os_printf(LM_APP, LL_ERR, "version:0x%x\n", handle->dw_head.version);
    os_printf(LM_APP, LL_ERR, "packsize:0x%x\n", handle->dw_head.package_size);
    os_printf(LM_APP, LL_ERR, "packcrc:0x%x\n", handle->dw_head.package_crc);
    os_printf(LM_APP, LL_ERR, "bootsize:0x%x\n", handle->dw_head.boot_size);
    os_printf(LM_APP, LL_ERR, "firmsize:0x%x\n", handle->dw_head.firmware_size);
    os_printf(LM_APP, LL_ERR, "firmnewsize:0x%x\n", handle->dw_head.firmware_new_size);
    os_printf(LM_APP, LL_ERR, "firmcrc:0x%x\n", handle->dw_head.firmware_crc);
    os_printf(LM_APP, LL_ERR, "firmnewcrc:0x%x\n", handle->dw_head.firmware_new_crc);
    os_printf(LM_APP, LL_ERR, "srcversion:%s\n", handle->dw_head.source_version);
    os_printf(LM_APP, LL_ERR, "dstversion:%s\n", handle->dw_head.target_version);
}

/**
 * @brief Dump complete download operation information
 * @param handle Pointer to download operation context
 */
static void download_operate_dump(download_oper_t *handle)
{
    int i;
    os_printf(LM_APP, LL_ERR, "##############OTA DOWNLOAD##################\n");
    download_packet_head_dump(handle);
    os_printf(LM_APP, LL_ERR, "state:%d\n", handle->dw_state);
    os_printf(LM_APP, LL_ERR, "lmethod:%d\n", handle->dw_lmethod);
    os_printf(LM_APP, LL_ERR, "rmethod:%d\n", handle->dw_rmethod);
    os_printf(LM_APP, LL_ERR, "status:%d\n", handle->dw_status);
    os_printf(LM_APP, LL_ERR, "buffoft:%d\n", handle->dw_buff_offt);
    os_printf(LM_APP, LL_ERR, "buffsize:%d\n", handle->dw_buff_size);
    os_printf(LM_APP, LL_ERR, "dw_skip:0x%x\n", handle->dw_skip);
    os_printf(LM_APP, LL_ERR, "dw_addr:0x%x\n", handle->dw_addr);
    os_printf(LM_APP, LL_ERR, "dw_offt:0x%x\n", handle->dw_offt);
    os_printf(LM_APP, LL_ERR, "dw_dlen:0x%x\n", handle->dw_dlen);
    os_printf(LM_APP, LL_ERR, "dw_all_dlen:0x%x\n", handle->dw_all_dlen);
    os_printf(LM_APP, LL_ERR, "active:0x%x\n", handle->dw_active_part);
    for (i = 0; i < DOWNLOAD_FULL_PART; i++) {
        os_printf(LM_APP, LL_ERR, "paddr:0x%x\n", handle->dw_paddr[i]);
        os_printf(LM_APP, LL_ERR, "pdlen:0x%x\n", handle->dw_pdlen[i]);
    }
    os_printf(LM_APP, LL_ERR, "############################################\n");
}

/**
 * @brief Get local update method
 * @param handle Pointer to download operation context
 * @return Error code
 */
static int download_get_lmethod(download_oper_t *handle)
{
    int ret;
    image_headinfo_t image_head;
    unsigned int	 start_addr = handle->dw_paddr[DOWNLOAD_IMGA_PART];

    if (handle->dw_active_part == ACTIVE_OTA_PARTB) {
        handle->dw_lmethod = DOWNLOAD_DUAL_METHOD;
    } else {
        ret = drv_spiflash_read(start_addr, (unsigned char *)&image_head, sizeof(image_headinfo_t));
        DOWNLOAD_RET_CHECK_RETURN(ret);
        handle->dw_lmethod = (image_head.update_method >> 4) / 4;
    }

    return DOWNLOAD_NONE_ERR;
}

/**
 * @brief Get active partition information
 * @param handle Pointer to download operation context
 * @return Error code
 */
static int download_get_active_part(download_oper_t *handle)
{
    handle->dw_active_part = ACTIVE_OTA_PARTA;
    if (ACTIVE_OTA_PARTB == ota_get_active_part()) {
        handle->dw_active_part = ACTIVE_OTA_PARTB;
    }

    return DOWNLOAD_NONE_ERR;
}

/**
 * @brief Initialize partition information
 * @param handle Pointer to download operation context
 * @return Error code
 */
static int download_partition_init(download_oper_t *handle)
{
    unsigned int get_addr;
    unsigned int get_len;
    int			 ret;

    handle->dw_status = partion_info_get(PARTION_NAME_BOOT, &get_addr, &get_len);
    DOWNLOAD_RET_CHECK_RETURN(handle->dw_status);
    handle->dw_paddr[DOWNLOAD_BOOT_PART] = get_addr;
    handle->dw_pdlen[DOWNLOAD_BOOT_PART] = get_len;

    ret = partion_info_get(PARTION_NAME_CPU, &get_addr, &get_len);
    DOWNLOAD_RET_CHECK_RETURN(ret);
    handle->dw_paddr[DOWNLOAD_IMGA_PART] = get_addr;
    handle->dw_pdlen[DOWNLOAD_IMGA_PART] = get_len / 2;
    handle->dw_paddr[DOWNLOAD_IMGB_PART] = get_addr + get_len / 2;
    handle->dw_pdlen[DOWNLOAD_IMGB_PART] = get_len / 2;

    ret = partion_info_get(PARTION_NAME_OTA_STATUS, &get_addr, &get_len);
    DOWNLOAD_RET_CHECK_RETURN(ret);
    handle->dw_paddr[DOWNLOAD_OTAS_PART] = get_addr;
    handle->dw_pdlen[DOWNLOAD_OTAS_PART] = get_len;

    return DOWNLOAD_NONE_ERR;
}

/**
 * @brief Write packet data to flash
 * @param handle Pointer to download operation context
 * @param data Data to write
 * @param len Length of data
 * @return Error code
 */
static int download_packet_flash(download_oper_t *handle, unsigned char *data, unsigned int len)
{
    unsigned int writelen;
    int			 ret;

    writelen = handle->dw_buff_size - handle->dw_buff_offt;
    if (writelen > len) {
        memcpy(handle->dw_buff + handle->dw_buff_offt, data, len);
        handle->dw_buff_offt += len;
    } else {
        memcpy(handle->dw_buff + handle->dw_buff_offt, data, writelen);
        ret = drv_spiflash_erase(handle->dw_addr + handle->dw_offt, handle->dw_buff_size);
        DOWNLOAD_RET_CHECK_RETURN(ret);
        ret = drv_spiflash_write(handle->dw_addr + handle->dw_offt, handle->dw_buff, handle->dw_buff_size);
        DOWNLOAD_RET_CHECK_RETURN(ret);
        handle->dw_buff_offt = 0;
        handle->dw_offt		+= handle->dw_buff_size;
        memset(handle->dw_buff, 0xFF, handle->dw_buff_size);
        if (len - writelen) {
            return download_packet_flash(handle, data + writelen, len - writelen);
        }
    }

    return DOWNLOAD_NONE_ERR;
}

/**
 * @brief Flush remaining data to flash
 * @param handle Pointer to download operation context
 * @return Error code
 */
static int download_packet_flush(download_oper_t *handle)
{
    int ret;

    if (handle->dw_buff_offt != 0) {
        ret = drv_spiflash_erase(handle->dw_addr + handle->dw_offt, handle->dw_buff_size);
        DOWNLOAD_RET_CHECK_RETURN(ret);
        ret = drv_spiflash_write(handle->dw_addr + handle->dw_offt, handle->dw_buff, handle->dw_buff_size);
        DOWNLOAD_RET_CHECK_RETURN(ret);

        handle->dw_offt		+= handle->dw_buff_offt;
        handle->dw_buff_offt = 0;
        memset(handle->dw_buff, 0xFF, handle->dw_buff_size);
    }

    return DOWNLOAD_NONE_ERR;
}

/**
 * @brief Process package head data
 * @param handle Pointer to download operation context
 * @param data Data to process
 * @param len Length of data
 * @return Error code
 */
static int download_package_head(download_oper_t *handle, unsigned char *data, unsigned int len)
{
    unsigned int   writelen = 0;
    unsigned int   leftlen	= 0;
    unsigned char *phead	= (unsigned char *)&handle->dw_head;

    DOWNLOAD_RET_CHECK_RETURN(handle->dw_state != DOWNLOAD_HEAD_ST);
    leftlen	 = handle->dw_dlen - handle->dw_offt - handle->dw_buff_offt;
    writelen = leftlen > len ? len : leftlen;
    memcpy(phead + handle->dw_offt, data, writelen);
    handle->dw_offt += writelen;

    if (len > writelen || leftlen == writelen) {
        return download_packet_data(handle, data + writelen, len - writelen);
    }

    return DOWNLOAD_NONE_ERR;
}

static unsigned int image_get_payload_size(const image_headinfo_t *image_head)
{
    return image_head->xip_size + image_head->text_size + image_head->data_size;
}

/**
 * @brief Get current firmware size
 * @param handle Pointer to download operation context
 * @return Firmware size
 */
static int download_get_firmware_size(unsigned int firmwareaddr)
{
    image_headinfo_t image_head;
    int ret;

    ret = drv_spiflash_read(firmwareaddr, (unsigned char *)&image_head, sizeof(image_headinfo_t));
    DOWNLOAD_RET_CHECK_RETURN(ret);

    return image_get_payload_size(&image_head) + sizeof(image_headinfo_t);
}

/**
 * @brief Check packet head validity
 * @param handle Pointer to download operation context
 * @return Error code
 */
static int download_packet_head_check(download_oper_t *handle)
{
    unsigned int filesize;
    unsigned int firmwaresize;

    if (memcmp(handle->dw_head.magic, "FotaPKG", strlen("FotaPKG")) != 0) {
        DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_MAGIC_ERR);
    }

    if (handle->dw_rmethod == DOWNLOAD_DIFF_METHOD && handle->dw_lmethod == DOWNLOAD_DUAL_METHOD) {
        DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_METH_ERR);
    }

    firmwaresize = handle->dw_head.firmware_size;
    if (handle->dw_head.firmware_size == 0) {
        firmwaresize = download_get_firmware_size(handle->dw_paddr[DOWNLOAD_IMGA_PART]);
    }

    firmwaresize = firmwaresize > handle->dw_head.firmware_new_size ?
                   firmwaresize : handle->dw_head.firmware_new_size;
    firmwaresize = OTA_SIZE_ALIGN_4K(firmwaresize);
    filesize	 = OTA_SIZE_ALIGN_4K(handle->dw_head.package_size);

    if (handle->dw_rmethod != DOWNLOAD_DUAL_METHOD && handle->dw_lmethod != DOWNLOAD_DUAL_METHOD) {
        if (firmwaresize + filesize > handle->dw_pdlen[DOWNLOAD_IMGA_PART] + handle->dw_pdlen[DOWNLOAD_IMGB_PART]) {
            DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_SIZE_ERR);
        }

        if (handle->dw_rmethod == DOWNLOAD_DIFF_METHOD) {
            if (firmwaresize + filesize + OTA_STAT_PART_SIZE + OTA_BACKUP_PART_SIZE >
                handle->dw_pdlen[DOWNLOAD_IMGA_PART] + handle->dw_pdlen[DOWNLOAD_IMGB_PART]) {
                DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_SIZE_ERR);
            }
        }
    } else {
        if (handle->dw_rmethod != DOWNLOAD_DUAL_METHOD) {
            firmwaresize = handle->dw_head.package_size;
        } else {
            firmwaresize = (handle->dw_active_part == ACTIVE_OTA_PARTA) ? handle->dw_head.firmware_new_size :
                handle->dw_head.firmware_size;
            if (firmwaresize == 0) {
                DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_SIZE_ERR);
            }
        }
        if (firmwaresize > handle->dw_pdlen[DOWNLOAD_IMGA_PART]) {
            DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_SIZE_ERR);
        }
    }

    return DOWNLOAD_NONE_ERR;
}

void download_packet_part_get(download_method_e method, unsigned int *addr, unsigned int *len)
{
    unsigned int cpuaddr, cpulen;
    int activepart = ota_get_active_part();
    int firmwaresize;

    partion_info_get(PARTION_NAME_CPU, &cpuaddr, &cpulen);
    if (method == DOWNLOAD_DUAL_METHOD) {
        if (activepart == ACTIVE_OTA_PARTB) {
            *addr = cpuaddr;
            *len = cpulen/2;
        } else {
            *addr = cpuaddr + cpulen/2;
            *len = cpulen/2;
        }
        return;
    }

    firmwaresize = OTA_SIZE_ALIGN_4K(download_get_firmware_size(cpuaddr));
    if (method == DOWNLOAD_COMZ_METHOD) {
        *addr = cpuaddr + firmwaresize;
        *len = cpulen - firmwaresize;
        return;
    }

    if (method == DOWNLOAD_DIFF_METHOD) {
        *addr = cpuaddr + firmwaresize + OTA_STAT_PART_SIZE;
        *len = cpulen - firmwaresize - OTA_STAT_PART_SIZE;
        return;
    }
}

/**
 * @brief Complete current download phase and transition to next
 * @param handle Pointer to download operation context
 * @param data Remaining data
 * @param len Length of remaining data
 * @return Error code
 */
static int download_packet_complete(download_oper_t *handle, unsigned char *data, unsigned int len)
{
    int ret;

    if (handle->dw_state == DOWNLOAD_HEAD_ST) {
        handle->dw_rmethod = (handle->dw_head.version >> 4) / 4;
        ret = download_packet_head_check(handle);
        DOWNLOAD_RET_CHECK_RETURN(ret);
        if (handle->dw_rmethod == DOWNLOAD_DUAL_METHOD) {
            handle->dw_state = DOWNLOAD_BOOT_ST;
            if (handle->dw_head.boot_size != 0) {
                handle->dw_addr = handle->dw_paddr[DOWNLOAD_BOOT_PART];
                handle->dw_dlen = handle->dw_head.boot_size;
                handle->dw_offt = 0;
            }
        } else {
            memcpy(handle->dw_buff, &handle->dw_head, sizeof(handle->dw_head));
            handle->dw_buff_offt += sizeof(handle->dw_head);

            handle->dw_dlen	 = handle->dw_head.package_size;
            handle->dw_offt	 = 0;
            handle->dw_state = DOWNLOAD_IMGE_ST;

            if (handle->dw_rmethod == DOWNLOAD_COMZ_METHOD && handle->dw_active_part == ACTIVE_OTA_PARTB) {
                handle->dw_addr = handle->dw_paddr[DOWNLOAD_IMGA_PART];
            } else {
                unsigned int dwaddr, dwlen;
                download_packet_part_get(handle->dw_rmethod, &dwaddr, &dwlen);
                handle->dw_addr = dwaddr;
            }
        }

        if (len > 0) {
            ret = download_packet_data(handle, data, len);
            DOWNLOAD_RET_CHECK_RETURN(ret);
        }

        return DOWNLOAD_NONE_ERR;
    }

    if (handle->dw_state == DOWNLOAD_BOOT_ST) {
        handle->dw_addr	 = handle->dw_paddr[DOWNLOAD_IMGA_PART];
        handle->dw_dlen	 = handle->dw_head.firmware_size;
        handle->dw_offt	 = 0;
        handle->dw_state = DOWNLOAD_IMGA_ST;
        handle->dw_skip = (handle->dw_active_part == ACTIVE_OTA_PARTA) ? 1 : 0;

        if (len > 0) {
            ret = download_packet_data(handle, data, len);
            DOWNLOAD_RET_CHECK_RETURN(ret);
        }

        return DOWNLOAD_NONE_ERR;
    }

    if (handle->dw_state == DOWNLOAD_IMGA_ST) {
        handle->dw_addr	 = handle->dw_paddr[DOWNLOAD_IMGB_PART];
        handle->dw_dlen	 = handle->dw_head.firmware_new_size;
        handle->dw_offt	 = 0;
        handle->dw_state = DOWNLOAD_IMGB_ST;
        handle->dw_skip = (handle->dw_active_part == ACTIVE_OTA_PARTB) ? 1 : 0;

        if (len > 0) {
            ret = download_packet_data(handle, data, len);
            DOWNLOAD_RET_CHECK_RETURN(ret);
        }

        return DOWNLOAD_NONE_ERR;
    }

    if ((handle->dw_state == DOWNLOAD_IMGE_ST || handle->dw_state == DOWNLOAD_IMGB_ST)) {
        return DOWNLOAD_NONE_ERR;
    }
    DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_METH_ERR);
}

/**
 * @brief Process boot partition data
 * @param handle Pointer to download operation context
 * @param data Data to process
 * @param len Length of data
 * @return Error code
 */
static int download_package_boot(download_oper_t *handle, unsigned char *data, unsigned int len)
{
    unsigned int writelen = 0;
    unsigned int leftlen  = 0;
    int			 ret;

    DOWNLOAD_RET_CHECK_RETURN(handle->dw_state != DOWNLOAD_BOOT_ST);
    DOWNLOAD_RET_CHECK_RETURN(handle->dw_dlen == 0);

    leftlen	 = handle->dw_dlen - handle->dw_offt - handle->dw_buff_offt;
    writelen = leftlen > len ? len : leftlen;
    ret		 = download_packet_flash(handle, data, writelen);
    DOWNLOAD_RET_CHECK_RETURN(ret);

    if (writelen == leftlen) {
        ret = download_packet_flush(handle);
        DOWNLOAD_RET_CHECK_RETURN(ret);
    }

    if (len > writelen) {
        return download_packet_data(handle, data + writelen, len - writelen);
    }

    return DOWNLOAD_NONE_ERR;
}

/**
 * @brief Process image data
 * @param handle Pointer to download operation context
 * @param data Data to process
 * @param len Length of data
 * @return Error code
 */
static int download_package_imge(download_oper_t *handle, unsigned char *data, unsigned int len)
{
    unsigned int writelen = 0;
    unsigned int leftlen  = 0;
    int			 ret;

    leftlen	 = handle->dw_dlen - handle->dw_offt - handle->dw_buff_offt;
    writelen = leftlen > len ? len : leftlen;

    if (handle->dw_skip == 0) {
        ret = download_packet_flash(handle, data, writelen);
        DOWNLOAD_RET_CHECK_RETURN(ret);

        if (writelen == leftlen) {
            ret = download_packet_flush(handle);
            DOWNLOAD_RET_CHECK_RETURN(ret);
        }

        if (len > writelen) {
            return download_packet_data(handle, data + writelen, len - writelen);
        }
    } else {
        handle->dw_offt += writelen;
        if (len > writelen) {
            handle->dw_skip = 0;
            ret = download_packet_data(handle, data + writelen, len - writelen);
            DOWNLOAD_RET_CHECK_RETURN(ret);
        }
    }

    return DOWNLOAD_NONE_ERR;
}

/**
 * @brief Process incoming packet data
 * @param handle Pointer to download operation context
 * @param data Data to process
 * @param len Length of data
 * @return Error code
 */
static int download_packet_data(download_oper_t *handle, unsigned char *data, unsigned int len)
{
    int ret;

    if (handle->dw_dlen == 0 && handle->dw_state == DOWNLOAD_IDLE_ST) {
        handle->dw_dlen	 = sizeof(ota_package_head_t);
        handle->dw_state = DOWNLOAD_HEAD_ST;
    }

    if (handle->dw_head.package_size) {
        handle->dw_all_dlen += len;
        if (handle->dw_all_dlen > handle->dw_head.package_size) {
            handle->dw_all_dlen = handle->dw_head.package_size;
        }
        unsigned int now_percent = handle->dw_all_dlen * 100 / handle->dw_head.package_size;
        if (handle->dw_percent != now_percent) {
            handle->dw_percent = now_percent;
            os_printf(LM_APP, LL_ERR, "OTA download %d%%\n", handle->dw_percent);
        }
    } else {
        handle->dw_all_dlen = sizeof(ota_package_head_t);
    }

    if (handle->dw_offt == handle->dw_dlen) {
        ret = download_packet_complete(handle, data, len);
        DOWNLOAD_RET_CHECK_RETURN(ret);
    } else if (handle->dw_state == DOWNLOAD_HEAD_ST) {
        ret = download_package_head(handle, data, len);
        DOWNLOAD_RET_CHECK_RETURN(ret);
    } else if (handle->dw_state == DOWNLOAD_BOOT_ST) {
        ret = download_package_boot(handle, data, len);
        DOWNLOAD_RET_CHECK_RETURN(ret);
    } else if (handle->dw_state >= DOWNLOAD_IMGE_ST && handle->dw_state <= DOWNLOAD_IMGB_ST) {
        ret = download_package_imge(handle, data, len);
        DOWNLOAD_RET_CHECK_RETURN(ret);
    } else {
        DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_STAT_ERR);
    }

    return DOWNLOAD_NONE_ERR;
}

/**
 * @brief Initialize download memory context
 * @param handle Double pointer to download operation context
 * @return Error code
 */
static int download_mem_init(download_oper_t **handle)
{
    download_oper_t *ht = *handle;

    if (ht != NULL) {
        return DOWNLOAD_NONE_ERR;
    }

    ht = (download_oper_t *)os_malloc(sizeof(download_oper_t));
    if (ht == NULL) {
        DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_NOMEM_ERR);
    }
    memset(ht, 0, sizeof(download_oper_t));
    ht->dw_buff_size = 4096;

    ht->dw_buff = (unsigned char *)os_malloc(ht->dw_buff_size);
    if (ht->dw_buff == NULL) {
        DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_NOMEM_ERR);
    }

    memset(ht->dw_buff, 0, ht->dw_buff_size);
    *handle = ht;

    return DOWNLOAD_NONE_ERR;
}

/**
 * @brief Release download memory resources
 * @param handle Double pointer to download operation context
 */
static void download_mem_release(download_oper_t **handle)
{
    download_oper_t *ht = *handle;

#ifdef CONFIG_FLASH_TH
    spiFlash_TH_unprotect(0);
#endif

    if (ht == NULL) {
        return;
    }

    if (ht->dw_status != DOWNLOAD_NONE_ERR) {
        download_operate_dump(ht);
    }

    if (ht->dw_buff) {
        os_free(ht->dw_buff);
        ht->dw_buff = NULL;
    }

    os_free(ht);
    *handle = NULL;
}

/**
 * @brief Finalize download operation
 * @param handle Pointer to download operation context
 * @param state Pointer to OTA state structure
 * @return Error code
 */
static int download_packet_done(download_oper_t *handle, ota_state_t *state)
{
    unsigned int crc;

    if (handle->dw_active_part == ACTIVE_OTA_PARTB && handle->dw_rmethod != DOWNLOAD_COMZ_METHOD) {
        os_printf(LM_APP, LL_INFO, "OTA:* * * active part B * * *\n");
        crc = ota_get_flash_crc(handle->dw_paddr[DOWNLOAD_IMGA_PART], handle->dw_head.firmware_size);
        //        os_printf(LM_APP, LL_INFO, "OTA:handle->dw_paddr[DOWNLOAD_IMGA_PART]:0x%08x\n", handle->dw_paddr[DOWNLOAD_IMGA_PART]);
        //        os_printf(LM_APP, LL_INFO, "OTA:handle->dw_head.firmware_size:0x%08x\n", handle->dw_head.firmware_size);
        //        os_printf(LM_APP, LL_INFO, "OTA:handle->dw_head.firmware_crc:0x%08x\n", handle->dw_head.firmware_crc);
        //        os_printf(LM_APP, LL_INFO, "OTA:crc:0x%08x\n", crc);
        if (crc != handle->dw_head.firmware_crc) {
            os_printf(LM_APP, LL_ERR, "OTA:crc not correct. crc:0x%08x firmware_crc:0x%08x\n", crc,
                      handle->dw_head.firmware_crc);
            DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_CRC_ERR);
        }
        state->updata_ab = OTA_AB_UPDATAFLAG;
    } else {
        unsigned int len = offsetof(ota_package_head_t, boot_size);
        os_printf(LM_APP, LL_INFO, "OTA:* * * active part A * * *\n");
        if (handle->dw_rmethod == DOWNLOAD_DUAL_METHOD) {
            os_printf(LM_APP, LL_INFO, "OTA: dw_paddr:0x%x size:0x%x\r\n", handle->dw_paddr[DOWNLOAD_IMGB_PART],
                      handle->dw_head.firmware_new_size);
            crc = ota_get_flash_crc(handle->dw_paddr[DOWNLOAD_IMGB_PART], handle->dw_head.firmware_new_size);
            //            os_printf(LM_APP, LL_INFO, "OTA:handle->dw_head.firmware_crc:0x%08x\n", handle->dw_head.firmware_crc);
            //            os_printf(LM_APP, LL_INFO, "OTA:crc:0x%08x\n", crc);
            if (crc != handle->dw_head.firmware_new_crc) {
                os_printf(LM_APP, LL_ERR, "OTA:crc not correct. crc:0x%08x firmware_new_crc:0x%08x\n", crc,
                          handle->dw_head.firmware_new_crc);
                DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_CRC_ERR);
            }
            state->updata_ab = OTA_AB_UPDATAFLAG | OTA_AB_SELECT_B;
        } else {
            crc = ota_get_flash_crc(handle->dw_addr + len, handle->dw_head.package_size - len);
            if (crc != handle->dw_head.package_crc) {
                os_printf(LM_APP, LL_ERR, "OTA:crc not correct. crc:0x%08x package_crc:0x%08x\n", crc,
                          handle->dw_head.package_crc);
                DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_CRC_ERR);
            }

            if (handle->dw_active_part == ACTIVE_OTA_PARTB && handle->dw_rmethod == DOWNLOAD_COMZ_METHOD) {
                state->updata_ab |= OTA_MOVE_CZ_A2B;
            }
        }
    }

    return DOWNLOAD_NONE_ERR;
}

static int ota_state_crc_check(const ota_state_t *state)
{
    unsigned int len = offsetof(ota_state_t, patch_addr);
    unsigned int calcrc;

    if (memcmp(state->magic, "OTA", 3) != 0 || state->len != sizeof(ota_state_t)) {
        os_printf(LM_APP, LL_ERR, "OTA:state invalid magic:%c%c%c len:%d expect:%d\n",
                  state->magic[0], state->magic[1], state->magic[2],
                  state->len, sizeof(ota_state_t));
        return -DOWNLOAD_STAT_ERR;
    }

    calcrc = ef_calc_crc32(0, (char *)state + len, sizeof(ota_state_t) - len);
    if (calcrc != state->crc) {
        os_printf(LM_APP, LL_ERR, "OTA:state crc error cal:0x%x hdr:0x%x\n", calcrc, state->crc);
        return -DOWNLOAD_CRC_ERR;
    }

    return DOWNLOAD_NONE_ERR;
}

static int ota_state_part_read(ota_state_t *state)
{
    unsigned int ota_part_addr, ota_part_len;
    int ret;

    ret = partion_info_get(PARTION_NAME_OTA_STATUS, &ota_part_addr, &ota_part_len);
    if (ret != 0) {
        os_printf(LM_APP, LL_ERR, "OTA:can not get %s info\n", PARTION_NAME_OTA_STATUS);
        return ret;
    }

    ret = drv_spiflash_read(ota_part_addr, (unsigned char *)state, sizeof(ota_state_t));
    if (ret == 0 && ota_state_crc_check(state) == DOWNLOAD_NONE_ERR) {
        return DOWNLOAD_NONE_ERR;
    }

    ret = drv_spiflash_read(ota_part_addr + ota_part_len / 2, (unsigned char *)state, sizeof(ota_state_t));
    if (ret == 0 && ota_state_crc_check(state) == DOWNLOAD_NONE_ERR) {
        return DOWNLOAD_NONE_ERR;
    }

    return -DOWNLOAD_CRC_ERR;
}

static int ota_state_part_update(const ota_state_t *state)
{
    unsigned int ota_part_addr, ota_part_len;
    int ret;

    ret = partion_info_get(PARTION_NAME_OTA_STATUS, &ota_part_addr, &ota_part_len);
    if (ret != 0) {
        os_printf(LM_APP, LL_ERR, "OTA:can not get %s info\n", PARTION_NAME_OTA_STATUS);
        return ret;
    }

    ret = drv_spiflash_erase(ota_part_addr, ota_part_len / 2);
    if (ret != 0) {
        os_printf(LM_APP, LL_ERR, "OTA:erase state primary failed addr:0x%x len:0x%x ret:%d\n",
                  ota_part_addr, ota_part_len / 2, ret);
        return ret;
    }
    ret = drv_spiflash_write(ota_part_addr, (const unsigned char *)state, sizeof(ota_state_t));
    if (ret != 0) {
        os_printf(LM_APP, LL_ERR, "OTA:write state primary failed addr:0x%x len:0x%x ret:%d\n",
                  ota_part_addr, sizeof(ota_state_t), ret);
        return ret;
    }
    ret = drv_spiflash_erase(ota_part_addr + ota_part_len / 2, ota_part_len / 2);
    if (ret != 0) {
        os_printf(LM_APP, LL_ERR, "OTA:erase state backup failed addr:0x%x len:0x%x ret:%d\n",
                  ota_part_addr + ota_part_len / 2, ota_part_len / 2, ret);
        return ret;
    }
    ret = drv_spiflash_write(ota_part_addr + ota_part_len / 2, (const unsigned char *)state, sizeof(ota_state_t));
    if (ret != 0) {
        os_printf(LM_APP, LL_ERR, "OTA:write state backup failed addr:0x%x len:0x%x ret:%d\n",
                  ota_part_addr + ota_part_len / 2, sizeof(ota_state_t), ret);
        return ret;
    }

    return DOWNLOAD_NONE_ERR;
}

int ota_is_ab_update_pending(void)
{
    ota_state_t state;

    if (ota_state_part_read(&state) != DOWNLOAD_NONE_ERR) {
        return 0;
    }

    if ((state.updata_ab & OTA_AB_UPDATAFLAG) && !(state.updata_ab & OTA_AB_DIRETC_START)) {
        return 1;
    }

    return 0;
}

static int ota_calc_image_sha256(unsigned int addr, unsigned int size, unsigned char hash[32])
{
    if (hash == NULL || size == 0) {
        return -DOWNLOAD_SIZE_ERR;
    }

#if defined(CONFIG_HASH)
    int ret;

    ret = drv_hash_sha256_ret((const unsigned char *)(MEM_BASE_XIP + addr), size, hash);
    if (ret != 0) {
        return ret;
    }

    return DOWNLOAD_NONE_ERR;
#else
#if defined(CONFIG_MBEDTLS)
    int ret;

    ret = mbedtls_sha256((const unsigned char *)(MEM_BASE_XIP + addr), size, hash, 0);
    if (ret != 0) {
        return ret;
    }

    return DOWNLOAD_NONE_ERR;
#else
    (void)addr;
    (void)hash;
    os_printf(LM_APP, LL_ERR, "OTA:image data check needs CONFIG_HASH or CONFIG_MBEDTLS\n");
    return -DOWNLOAD_CRC_ERR;
#endif
#endif
}

static int ota_validate_image_part(unsigned int addr, unsigned int part_len)
{
    unsigned int data_len, calcrc, hash_prefix;
    unsigned char hash[32];
    image_headinfo_t image_head;
    int ret;

    if (part_len <= sizeof(image_headinfo_t)) {
        return -DOWNLOAD_SIZE_ERR;
    }

    ret = drv_spiflash_read(addr, (unsigned char *)&image_head, sizeof(image_headinfo_t));
    if (ret != 0) {
        os_printf(LM_APP, LL_ERR, "OTA:read image head failed addr:0x%x ret:%d\n", addr, ret);
        return ret;
    }

    if (image_head.update_method != OTA_UPDATE_METHOD_AB) {
        os_printf(LM_APP, LL_ERR, "OTA:image method error addr:0x%x method:0x%x expect:0x%x\n", addr,
                  image_head.update_method,
                  OTA_UPDATE_METHOD_AB);
        return -DOWNLOAD_METH_ERR;
    }

    calcrc = ota_get_flash_crc(addr, offsetof(image_headinfo_t, image_hcrc));
    if (calcrc != image_head.image_hcrc) {
        os_printf(LM_APP, LL_ERR, "OTA:image hcrc error addr:0x%x cal:0x%x hdr:0x%x\n", addr, calcrc,
                  image_head.image_hcrc);
        return -DOWNLOAD_CRC_ERR;
    }

    data_len = image_get_payload_size(&image_head);
    if (data_len == 0 || data_len > part_len - sizeof(image_headinfo_t)) {
        os_printf(LM_APP, LL_ERR, "OTA:image size error addr:0x%x data:0x%x part:0x%x\n", addr, data_len, part_len);
        return -DOWNLOAD_SIZE_ERR;
    }

    if (image_head.image_dcrc == 0) {
        os_printf(LM_APP, LL_ERR, "OTA:image data check missing addr:0x%x\n", addr);
        return -DOWNLOAD_CRC_ERR;
    }

    ret = ota_calc_image_sha256(addr + sizeof(image_headinfo_t), data_len, hash);
    if (ret != DOWNLOAD_NONE_ERR) {
        os_printf(LM_APP, LL_ERR, "OTA:image sha256 calc failed addr:0x%x ret:%d\n", addr, ret);
        return ret;
    }
    hash_prefix = ((unsigned int)hash[0] << 24) | ((unsigned int)hash[1] << 16) |
                  ((unsigned int)hash[2] << 8) | hash[3];
    if (hash_prefix != image_head.image_dcrc) {
        os_printf(LM_APP, LL_ERR, "OTA:image dcrc error addr:0x%x cal:0x%x hdr:0x%x\n", addr, hash_prefix,
                  image_head.image_dcrc);
        return -DOWNLOAD_CRC_ERR;
    }

    os_printf(LM_APP, LL_INFO, "OTA:image check pass addr:0x%x\n", addr);

    return DOWNLOAD_NONE_ERR;
}

/**
 * @brief Calculate CRC of flash region
 * @param addr Starting address
 * @param size Size of region
 * @return Calculated CRC
 */
int ota_get_flash_crc(unsigned int addr, unsigned int size)
{
    unsigned int crc = 0;
    unsigned int len = size;
    unsigned int readLen;
    int alone = 1;
    int ret;

    ret = ota_init();
    if (ret == DOWNLOAD_INITD_ERR)
    {
        alone = 0;
    }
    else
    {
        alone = 1;
        DOWNLOAD_RET_CHECK_RETURN(ret);
    }

    while (len > 0)
    {
        memset(g_ota_download->dw_buff, 0, g_ota_download->dw_buff_size);
        readLen = (len > g_ota_download->dw_buff_size) ? g_ota_download->dw_buff_size : len;
        ret = drv_spiflash_read(addr + size - len, g_ota_download->dw_buff, readLen);
        DOWNLOAD_RET_CHECK_RETURN(ret);
        crc = ef_calc_crc32(crc, g_ota_download->dw_buff, readLen);
        len -= readLen;
    }

    if (alone == 1)
    {
        download_mem_release(&g_ota_download);
    }

    return crc;
}

/**
 * @brief 清理OTA状态
 *
 * 用于在OTA失败或需要重新开始时主动清理OTA状态。
 * 这是一个公开接口，可以被其他模块调用。
 */
void ota_cleanup(void)
{
    if (g_ota_download != NULL) {
        os_printf(LM_APP, LL_INFO, "Cleaning OTA state\n");
        download_mem_release(&g_ota_download);
        g_ota_download = NULL;
    }
}

/**
 * @brief Initialize OTA download context
 * @return Error code
 */
int ota_init(void)
{
    int ret;

    if (g_ota_download == NULL) {
        ret = download_mem_init(&g_ota_download);
        DOWNLOAD_RET_CHECK_RETURN(ret);
        ret = download_partition_init(g_ota_download);
        DOWNLOAD_RET_CHECK_RETURN(ret);
        ret = download_get_active_part(g_ota_download);
        DOWNLOAD_RET_CHECK_RETURN(ret);
        ret = download_get_lmethod(g_ota_download);
        DOWNLOAD_RET_CHECK_RETURN(ret);

#ifdef CONFIG_FLASH_TH
        spiFlash_TH_unprotect(1);
#endif

        return DOWNLOAD_NONE_ERR;
    }

    return DOWNLOAD_INITD_ERR;
}

/**
 * @brief Write data to OTA download
 * @param data Data to write
 * @param len Length of data
 * @return Error code
 */
int ota_write(unsigned char *data, unsigned int len)
{
    if (g_ota_download == NULL) {
        DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_INIT_ERR);
    }

    return download_packet_data(g_ota_download, data, len);
}

/**
 * @brief Finalize OTA download and optionally reset
 * @param reset Whether to reset after completion
 * @return Error code
 */
int ota_done(bool reset)
{
    ota_state_t	 state;
    unsigned int len = offsetof(ota_state_t, patch_addr);
    int			 ret;

    if (g_ota_download == NULL) {
        DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_INIT_ERR);
    }

    memset(&state, 0x0, sizeof(ota_state_t));
    memcpy(state.magic, "OTA", 3);
    state.update_flag = 1;
    state.len		  = sizeof(ota_state_t);
    state.patch_addr  = g_ota_download->dw_addr;
    state.patch_size  = g_ota_download->dw_head.package_size;

    DOWNLOAD_RET_CHECK_RETURN(g_ota_download->dw_dlen == 0);
    if (g_ota_download->dw_dlen != g_ota_download->dw_offt) {
        DOWNLOAD_RET_CHECK_RETURN(-DOWNLOAD_SIZE_ERR);
    }
    ret = download_packet_done(g_ota_download, &state);
    DOWNLOAD_RET_CHECK_RETURN(ret);

    state.crc = ef_calc_crc32(0, (char *)&state + len, sizeof(ota_state_t) - len);
    ret = ota_state_part_update(&state);
    DOWNLOAD_RET_CHECK_RETURN(ret);

    os_printf(LM_APP, LL_INFO, reset ? "ota done success, reset now\n" : "ota done success\n");
    os_msleep(100);  // for print log

    download_mem_release(&g_ota_download);

    if (reset) {
        hal_system_reset(RST_TYPE_OTA);  // 重启系统，不会返回
    }

    return DOWNLOAD_NONE_ERR;
}

/**
 * @brief Confirm OTA update and mark partition for boot
 * @return Error code
 */
int ota_confirm_update(void)
{
    ota_state_t	 state;
    unsigned int len = offsetof(ota_state_t, patch_addr);
    unsigned int part_addr, part_len;
    int			 ret;

    if (g_ota_download != NULL) {
        os_printf(LM_APP, LL_ERR, "OTA:ota_confirm_update called while OTA is active, state:%d status:%d\n",
                  g_ota_download->dw_state, g_ota_download->dw_status);
        return -DOWNLOAD_STAT_ERR;
    }

    ret = ota_init();
    if (ret != DOWNLOAD_NONE_ERR) {
        return ret;
    }

    ret = ota_state_part_read(&state);
    if (ret != DOWNLOAD_NONE_ERR) {
        os_printf(LM_APP, LL_INFO, "OTA:state error, run A\n");
        download_mem_release(&g_ota_download);
        return ret;
    }

    if (g_ota_download->dw_lmethod != DOWNLOAD_DUAL_METHOD) {
        os_printf(LM_APP, LL_INFO, "OTA:Not AB type, no confirm needed\n");
        download_mem_release(&g_ota_download);
        return DOWNLOAD_NONE_ERR;
    }

    if (state.updata_ab & OTA_AB_DIRETC_START) {
        os_printf(LM_APP, LL_ERR, "OTA:Already confirmed\n");
        download_mem_release(&g_ota_download);

        return DOWNLOAD_NONE_ERR;
    }

    if (g_ota_download->dw_active_part == ACTIVE_OTA_PARTB) {
        part_addr = g_ota_download->dw_paddr[DOWNLOAD_IMGB_PART];
        part_len = g_ota_download->dw_pdlen[DOWNLOAD_IMGB_PART];
    } else {
        part_addr = g_ota_download->dw_paddr[DOWNLOAD_IMGA_PART];
        part_len = g_ota_download->dw_pdlen[DOWNLOAD_IMGA_PART];
    }
    ret = ota_validate_image_part(part_addr, part_len);
    if (ret != DOWNLOAD_NONE_ERR) {
        os_printf(LM_APP, LL_ERR, "OTA:confirm denied, running part(%d) check failed\n",
                  g_ota_download->dw_active_part);
        download_mem_release(&g_ota_download);
        return ret;
    }

    if (g_ota_download->dw_active_part == ACTIVE_OTA_PARTB) {
        state.updata_ab = OTA_AB_UPDATAFLAG | OTA_AB_SELECT_B | OTA_AB_DIRETC_START;
    } else {
        state.updata_ab = OTA_AB_UPDATAFLAG | OTA_AB_DIRETC_START;
    }
    os_printf(LM_APP, LL_INFO, "OTA:confirm part(%d) state:0x%02x\n",
              g_ota_download->dw_active_part, state.updata_ab);

    state.crc = ef_calc_crc32(0, (char *)&state + len, sizeof(ota_state_t) - len);
    ret = ota_state_part_update(&state);
    if (ret != DOWNLOAD_NONE_ERR) {
        download_mem_release(&g_ota_download);
        return ret;
    }

    download_mem_release(&g_ota_download);

    return DOWNLOAD_NONE_ERR;
}

/**
 * @brief Get currently active partition
 * @return Active partition identifier
 */
unsigned int ota_get_active_part()
{
    ota_state_t	 state;
    int			 ret;
    unsigned int cpu_part_addr, cpu_part_len;

    ret = partion_info_get(PARTION_NAME_CPU, &cpu_part_addr, &cpu_part_len);
    DOWNLOAD_RET_CHECK_RETURN(ret);

    ret = ota_state_part_read(&state);
    if (ret != DOWNLOAD_NONE_ERR) {
        image_headinfo_t image_head;
        ret = drv_spiflash_read(cpu_part_addr, (unsigned char *)&image_head, sizeof(image_headinfo_t));
        if (image_head.update_method == OTA_UPDATE_METHOD_AB) {
            return ACTIVE_OTA_PARTA;
        } else {
            return ACTIVE_OTA_NOT_AB;
        }
    }

    if (state.updata_ab & OTA_AB_UPDATAFLAG) {
        if (state.updata_ab & OTA_AB_SELECT_B) {
            return ACTIVE_OTA_PARTB;
        } else {
            return ACTIVE_OTA_PARTA;
        }
    } else {
        return ACTIVE_OTA_NOT_AB;
    }
}

/**
 * @brief Change active partition for AB update
 * @return Operation status
 */
unsigned int change_active_part_for_ab()
{
    unsigned int active_part;
    unsigned int standby_part;
    unsigned int standby_addr;
    unsigned int standby_len;
    unsigned int len = offsetof(ota_state_t, patch_addr);
    int ret;
    ota_state_t	 state;

    if (g_ota_download != NULL) {
        os_printf(LM_APP, LL_ERR, "OTA:change active denied during OTA, state:%d status:%d\n",
                  g_ota_download->dw_state, g_ota_download->dw_status);
        return DOWNLOAD_INIT_ERR;
    }

    ret = ota_init();
    if (ret != DOWNLOAD_NONE_ERR) {
        return ret;
    }

    active_part = g_ota_download->dw_active_part;
    if (g_ota_download->dw_lmethod != DOWNLOAD_DUAL_METHOD) {
        os_printf(LM_APP, LL_ERR, "Not AB UPDATE TYPE!\n");
        download_mem_release(&g_ota_download);

        return DOWNLOAD_PART_ERR;
    }

    ret = ota_state_part_read(&state);
    if (ret != DOWNLOAD_NONE_ERR) {
        os_printf(LM_APP, LL_ERR, "OTA:change active denied, state read failed ret:%d\n", ret);
        download_mem_release(&g_ota_download);

        return DOWNLOAD_PART_ERR;
    }

    if ((state.updata_ab & OTA_AB_UPDATAFLAG) && !(state.updata_ab & OTA_AB_DIRETC_START)) {
        os_printf(LM_APP, LL_ERR, "OTA:AB update pending confirm, reject change active part\n");
        download_mem_release(&g_ota_download);

        return DOWNLOAD_STAT_ERR;
    }

    standby_part = (active_part == ACTIVE_OTA_PARTB) ? ACTIVE_OTA_PARTA : ACTIVE_OTA_PARTB;
    if (standby_part == ACTIVE_OTA_PARTB) {
        standby_addr = g_ota_download->dw_paddr[DOWNLOAD_IMGB_PART];
        standby_len = g_ota_download->dw_pdlen[DOWNLOAD_IMGB_PART];
    } else {
        standby_addr = g_ota_download->dw_paddr[DOWNLOAD_IMGA_PART];
        standby_len = g_ota_download->dw_pdlen[DOWNLOAD_IMGA_PART];
    }
    ret = ota_validate_image_part(standby_addr, standby_len);
    if (ret != DOWNLOAD_NONE_ERR) {
        os_printf(LM_APP, LL_ERR, "OTA:change active denied, standby part(%d) check failed\n", standby_part);
        download_mem_release(&g_ota_download);
        return DOWNLOAD_PART_ERR;
    }

    memset(&state, 0x0, sizeof(ota_state_t));
    memcpy(state.magic, "OTA", 3);
    state.update_flag = 1;
    state.len		  = sizeof(ota_state_t);
    if (standby_part == ACTIVE_OTA_PARTA) {
        state.updata_ab = OTA_AB_UPDATAFLAG;
    } else {
        state.updata_ab = OTA_AB_UPDATAFLAG | OTA_AB_SELECT_B;
    }

    state.crc = ef_calc_crc32(0, (char *)&state + len, sizeof(ota_state_t) - len);
    ret = ota_state_part_update(&state);
    DOWNLOAD_RET_CHECK_RETURN(ret);

    download_mem_release(&g_ota_download);
    os_printf(LM_APP, LL_INFO, "Change part to %s ok, reset now\n",
              (standby_part == ACTIVE_OTA_PARTA) ? "part A" : "part B");
    os_msleep(100);  // for print log
    hal_system_reset(RST_TYPE_OTA);  // 重启系统，不会返回

    return DOWNLOAD_NONE_ERR;
}

/**
 * @brief Update image via HIF SVR interface
 * @param data Image data
 * @param len Length of data
 * @return Error code
 */
int ota_update_image(unsigned char *data, unsigned int len)
{
    ota_init();
    ota_write(data, len);

    if (g_ota_download->dw_dlen != 0 && g_ota_download->dw_dlen == g_ota_download->dw_offt) {
        if (g_ota_download->dw_rmethod == DOWNLOAD_DUAL_METHOD && g_ota_download->dw_state == DOWNLOAD_IMGA_ST) {
            return DOWNLOAD_NONE_ERR;
        }
        ota_done(1);
        download_mem_release(&g_ota_download);
    }

    return DOWNLOAD_NONE_ERR;
}
