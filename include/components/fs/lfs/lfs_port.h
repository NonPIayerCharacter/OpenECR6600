#ifndef LFS_PORT_H
#define LFS_PORT_H

#include "lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call once during startup before starting filesystem users.
 * Mount "fs", or existing "nv_customer" with CONFIG_MATTER_CUSTOMER_LFS.
 * Format only on corruption; IO failures return an error without clearing data.
 * Returns NULL on failure. Do not unmount the returned shared instance.
 */
void *lfs_init(void);
#ifdef CONFIG_MATTER_CUSTOMER_LFS
int lfs_customer_reset(void);
/* Rebuild in-memory state without formatting or erasing. */
int lfs_customer_remount(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
