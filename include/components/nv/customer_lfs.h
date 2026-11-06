#ifndef CUSTOMER_LFS_H
#define CUSTOMER_LFS_H
#include "easyflash.h"
#ifdef __cplusplus
extern "C" {
#endif
/* First initialize at startup before NV users. Later retries are serialized. */
EfErrCode customer_lfs_init(void);
signed int customer_lfs_get(const char *key, void *value, signed int size, signed int *length);
EfErrCode customer_lfs_set(const char *key, const void *value, signed int size);
EfErrCode customer_lfs_del(const char *key);
/* Terminal reset: blocks CUSTOMER access, even on failure, until reboot.
 * Caller must reboot after this function returns. */
EfErrCode customer_lfs_reset(void);
void customer_lfs_print(void);
#ifdef __cplusplus
}
#endif
#endif
