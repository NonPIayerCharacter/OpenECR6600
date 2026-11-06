/**
 * @file ota.h
 * @brief OTA (Over-The-Air) update component header
 * @details This header contains definitions and structures for OTA update functionality
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

#ifndef __OTA_H__
#define __OTA_H__

/*--------------------------------------------------------------------------
 *                                 Include files
 *--------------------------------------------------------------------------*/
#include <stdbool.h>
#if defined(CONFIG_LWIP)
#include "lwip/sockets.h"
#endif

/**
 * @brief Enumeration of download methods
 */
typedef enum {
    DOWNLOAD_COMZ_METHOD,
    DOWNLOAD_DIFF_METHOD,
    DOWNLOAD_DUAL_METHOD,
} download_method_e;

/**
 * @brief OTA package header stored at the beginning of every OTA package.
 */
typedef struct {
    unsigned char magic[7];
    unsigned char version;
    unsigned int  package_size;
    unsigned int  package_crc;
    unsigned int  boot_size;
    unsigned int  firmware_size;
    unsigned int  firmware_new_size;
    unsigned int  firmware_crc;
    unsigned int  firmware_new_crc;
    unsigned char reserved[12];
    unsigned char source_version[32];
    unsigned char target_version[32];
} ota_package_head_t;

/*--------------------------------------------------------------------------
 *                                 Macros
 *--------------------------------------------------------------------------*/

/** OTA partition query result: current firmware does not run from an A-B OTA partition. */
#define ACTIVE_OTA_NOT_AB      (0)

/** OTA partition query result: current firmware runs from OTA partition A. */
#define ACTIVE_OTA_PARTA       (0x1)

/** OTA partition query result: current firmware runs from OTA partition B. */
#define ACTIVE_OTA_PARTB       (0x2)

/** Image header update method: compressed OTA package. */
#define OTA_UPDATE_METHOD_CZ   (0x22)

/** Image header update method: differential OTA package. */
#define OTA_UPDATE_METHOD_DI   (0x42)

/** Image header update method: A-B OTA package. */
#define OTA_UPDATE_METHOD_AB   (0x82)

/** Image header format version 3: board_name[8] is valid. */
#define IMAGE_HEADER_VERSION_V3 (3)

/** Image header format version 4: text_dynload_load_addr and text_dynload_size are valid. */
#define IMAGE_HEADER_VERSION_V4 (4)

/**
 * @brief Image header information structure
 */
typedef struct {
	unsigned int	image_magic;
	unsigned int	header_version;			/* image_header struct version V0 */
	unsigned int	image_ep;				/* Entry Point Address */
	unsigned int	text_load_addr;			/* download addr */
	unsigned int	text_size;				/* Image text Size */
	unsigned int	data_load_addr;			/* download addr */
	unsigned int	data_size;				/* Image Data Size */
	unsigned int	xip_size;				/* Image xip Size */
	unsigned char	signature[64];			/* ecdsa  (r,s) */
	unsigned char	release_version[12];	/* release_version */
	unsigned int	image_dcrc;				/* Image Data CRC Checksum        */
	unsigned int	update_method;			/* ota data, for details, see @ota_control */
	unsigned int	text_iram_load_addr; 	/* download addr */
	unsigned int	text_iram_size;			/* Image Data Size */
	unsigned int	flash_ecc_data_addr;	/* flash ecc data addr */
	unsigned char	reserved[124];
	unsigned int	image_hcrc;				/* Image Head CRC Checksum        */
} image_headinfo_t;

/*--------------------------------------------------------------------------
 *                                 Function Prototypes
 *--------------------------------------------------------------------------*/
/**
 * @brief Start an OTA download session.
 *
 * The function allocates the OTA download context, initializes the target
 * partition, reads the active partition, and reads the update method.
 *
 * @return 0 on success, or a non-zero OTA/download error code on failure.
 */
int ota_init(void);

/**
 * @brief Write one firmware fragment into the active OTA session.
 *
 * Call ota_init() before the first fragment. Fragments must be passed in the
 * firmware image order expected by the OTA package parser.
 *
 * @param[in] data Pointer to the firmware fragment buffer.
 * @param[in] len Length of the fragment in bytes.
 * @return 0 on success, or a non-zero OTA/download error code on failure.
 */
int ota_write(unsigned char *data, unsigned int len);

/**
 * @brief Finish the active OTA session and optionally restart the system.
 *
 * The function verifies the downloaded image, writes OTA state, releases the
 * OTA download context, and requests a reset when @p reset is true.
 *
 * @param[in] reset true to restart after a successful OTA finish, false to
 *                  finish without requesting a restart.
 * @return 0 on success, or a non-zero OTA/download error code on failure.
 */
int ota_done(bool reset);

#ifdef CONFIG_CUSTOM_MIDEA
/**
 * @brief Invalidate any previously staged OTA update.
 *
 * The OTA state remains structurally valid, but update_flag and staged
 * metadata are cleared so the bootloader cannot start a stale update.
 *
 * @param[in] method OTA download method selected by the caller.
 * @return 0 on success, or a non-zero OTA/download error code on failure.
 */
int ota_invalidate_staged_update(download_method_e method);

/**
 * @brief Verify and persist an OTA update without activating it.
 *
 * This finishes the active OTA download session and stores the staged
 * metadata with update_flag cleared.
 *
 * @param[in] method OTA download method selected by the caller.
 * @return 0 on success, or a non-zero OTA/download error code on failure.
 */
int ota_done_staged(download_method_e method);

/**
 * @brief Verify a staged OTA update from flash.
 *
 * This can be called after an ordinary reboot, when the download context no
 * longer exists.
 *
 * @param[in] method OTA download method selected by the caller.
 * @return 0 on success, or a non-zero OTA/download error code on failure.
 */
int ota_verify_staged_update(download_method_e method);

/**
 * @brief Verify and activate a staged OTA update.
 *
 * On success update_flag is set to 1 and the OTA state CRC is updated. The
 * caller is responsible for requesting an OTA reset.
 *
 * @param[in] method OTA download method selected by the caller.
 * @return 0 on success, or a non-zero OTA/download error code on failure.
 */
int ota_activate_staged_update(download_method_e method);
#endif

/**
 * @brief Confirm the currently running image after an A-B update.
 *
 * Applications should call this after the new image has booted and passed
 * startup checks, such as connecting to the cloud service. The function
 * validates the active A-B partition before marking it as confirmed.
 *
 * @return 0 on success, or a non-zero OTA/download error code on failure.
 */
int ota_confirm_update(void);

/**
 * @brief Check whether an A-B update is booted but not confirmed.
 *
 * @return 1 when an A-B image is pending confirmation, otherwise 0.
 */
int ota_is_ab_update_pending(void);
/**
 * @brief Calculate the CRC value for a flash address range.
 *
 * @param[in] addr Start flash address.
 * @param[in] size Number of bytes to include in the CRC calculation.
 * @return Calculated CRC value, or a negative value on failure.
 */
int ota_get_flash_crc(unsigned int addr, unsigned int size);
/**
 * @brief Get the currently active OTA partition.
 *
 * @return ACTIVE_OTA_NOT_AB, ACTIVE_OTA_PARTA, or ACTIVE_OTA_PARTB.
 */
unsigned int ota_get_active_part(void);
/**
 * @brief Switch the active A-B OTA partition.
 *
 * @return 0 on success, otherwise an OTA action status code.
 */
unsigned int change_active_part_for_ab(void);
/**
 * @brief Release the active OTA download context and temporary resources.
 */
void ota_cleanup(void);

/**
 * @brief Update an image by hif service interface.
 *
 * @param[in] data Pointer to the image data fragment.
 * @param[in] len Length of the image data fragment in bytes.
 * @return 0 on success, or a non-zero OTA/download error code on failure.
 */
int ota_update_image(unsigned char *data, unsigned int len);

/**
 * @brief Get the target flash range for OTA package data.
 *
 * The target range is selected according to the download method, the active
 * A-B partition, and the space occupied by the current firmware image.
 *
 * @param[in] method OTA package download method.
 * @param[out] addr Start address of the target flash range.
 * @param[out] len Available length of the target flash range in bytes.
 * @note @p addr and @p len must not be NULL. Their values are updated only
 *       when @p method is a supported download method.
 */
void download_packet_part_get(download_method_e method, unsigned int *addr, unsigned int *len);

#endif /* __OTA_H__ */
