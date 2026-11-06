/* Matter CUSTOMER NV compatibility layer. One encoded key per file. */
#include <string.h>
#include <limits.h>
#include "customer_lfs.h"
#include "lfs_port.h"
#include "hal_system.h"

#define KEY_LIMIT ((LFS_NAME_MAX - 1) / 2)
#define TXN_FILE ".nv-txn"
static lfs_t *s_fs;
/* LFS locks individual calls; this lock covers the entire KV transaction. */
static os_mutex_handle_t s_mutex;
/* Protected by s_mutex. Once set, only a reboot may resume CUSTOMER access. */
static int s_reset_pending;

static int key_path(const char *key, char *path)
{
    static const char hex[] = "0123456789abcdef";
    size_t i, length;
    if (!key || !(length = strlen(key)) || length > KEY_LIMIT) { return -1; }
    path[0] = 'k';
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)key[i];
        path[1 + 2 * i] = hex[c >> 4];
        path[2 + 2 * i] = hex[c & 15];
    }
    path[1 + 2 * length] = 0;
    return 0;
}

static EfErrCode map_error(int err)
{
    if (err >= 0) { return EF_NO_ERR; }
    if (err == LFS_ERR_NOENT) { return EF_ENV_NAME_ERR; }
    if (err == LFS_ERR_NOSPC) { return EF_ENV_FULL; }
    return EF_WRITE_ERR;
}

/* Only confirmed corruption enables destructive recovery for this reference design.
 * The failing operation still reports failure; it is never acknowledged as saved.
 * Call only after every file/directory handle has been closed. */
static int recover(int err)
{
    if (err == LFS_ERR_IO) {
        EF_INFO("CUSTOMER LFS IO error: remounting without clearing data.\n");
        s_fs = lfs_customer_remount() == 0 ? (lfs_t *)lfs_init() : NULL;
    } else if (err == LFS_ERR_CORRUPT) {
        EF_INFO("CUSTOMER LFS error %d: clearing all configuration.\n", err);
        s_reset_pending = 1;
        s_fs = lfs_customer_reset() == 0 ? (lfs_t *)lfs_init() : NULL;
        return 1;
    }
    return 0;
}

/* Recover under the transaction lock; release it before reset handlers use NV. */
static void finish(int err)
{
    int reboot_required = recover(err);
    os_mutex_unlock(s_mutex);
    if (reboot_required) { hal_system_reset(RST_TYPE_SOFTWARE_REBOOT); }
}

EfErrCode customer_lfs_init(void)
{
    int err;
    EfErrCode result;
    os_mutex_handle_t mutex;
    /* Single-core RTOS: publish exactly one mutex even if init callers race.
     * No filesystem IO or blocking mutex acquisition with scheduling suspended. */
    os_scheduler_lock();
    if (!s_mutex) { s_mutex = os_mutex_create(); }
    mutex = s_mutex;
    os_scheduler_unlock();
    if (!mutex) { return EF_ENV_INIT_FAILED; }
    if (os_mutex_lock(s_mutex, 0xffffffff)) { return EF_ENV_INIT_FAILED; }
    if (s_reset_pending) { os_mutex_unlock(s_mutex); return EF_ENV_INIT_FAILED; }
    if (s_fs) { os_mutex_unlock(s_mutex); return EF_NO_ERR; }
    s_fs = (lfs_t *)lfs_init();
    if (!s_fs) { os_mutex_unlock(s_mutex); return EF_ENV_INIT_FAILED; }
    /* A power loss before rename can leave an uncommitted temporary file. */
    err = lfs_remove(s_fs, TXN_FILE);
    if (err == LFS_ERR_NOENT) { err = 0; }
    result = err ? EF_ENV_INIT_FAILED : EF_NO_ERR;
    finish(err);
    return result;
}

signed int customer_lfs_get(const char *key, void *value, signed int size, signed int *length)
{
    char path[LFS_NAME_MAX + 1];
    lfs_file_t file;
    lfs_soff_t stored;
    int err, close_err;
    if (length) { *length = 0; }
    if (size < 0 || (!value && size) || key_path(key, path)) { return EF_READ_ERR; }
    if (!s_mutex) { return EF_ENV_INIT_FAILED; }
    if (os_mutex_lock(s_mutex, 0xffffffff)) { return EF_ENV_INIT_FAILED; }
    if (s_reset_pending || !s_fs) { os_mutex_unlock(s_mutex); return EF_ENV_INIT_FAILED; }
    err = lfs_file_open(s_fs, &file, path, LFS_O_RDONLY);
    if (!err) {
        stored = lfs_file_size(s_fs, &file);
        if (stored < 0) { err = (int)stored; }
        else if (stored > INT_MAX) { err = LFS_ERR_FBIG; }
        else {
            if (length) { *length = (int)stored; }
            if (size > stored) { size = (int)stored; }
            err = size ? (int)lfs_file_read(s_fs, &file, value, size) : 0;
            if (err >= 0 && err != size) { err = LFS_ERR_IO; }
        }
        close_err = lfs_file_close(s_fs, &file);
        if (err >= 0 && close_err) { err = close_err; }
    }
    finish(err);
    if (err == LFS_ERR_NOENT) { return 0; }
    return err < 0 ? EF_READ_ERR : err;
}

/* Return 1 for equal content, 0 for different/missing, negative on error. */
static int same_value(const char *path, const void *value, int size)
{
    lfs_file_t file;
    unsigned char buffer[64];
    const unsigned char *bytes = value;
    int err, result = 1, offset = 0, count;
    lfs_soff_t stored;
    err = lfs_file_open(s_fs, &file, path, LFS_O_RDONLY);
    if (err) { return err == LFS_ERR_NOENT ? 0 : err; }
    stored = lfs_file_size(s_fs, &file);
    if (stored < 0) { result = (int)stored; }
    else if (stored != size) { result = 0; }
    while (result == 1 && offset < size) {
        count = size - offset;
        if (count > (int)sizeof(buffer)) { count = sizeof(buffer); }
        err = lfs_file_read(s_fs, &file, buffer, count);
        if (err < 0) { result = err; }
        else if (err != count) { result = LFS_ERR_IO; }
        else if (memcmp(buffer, bytes + offset, count)) { result = 0; }
        offset += count;
    }
    err = lfs_file_close(s_fs, &file);
    return err && result >= 0 ? err : result;
}

EfErrCode customer_lfs_set(const char *key, const void *value, signed int size)
{
    char path[LFS_NAME_MAX + 1];
    lfs_file_t file;
    int err, close_err;
    if (!value) { return customer_lfs_del(key); }
    if (size < 0 || key_path(key, path)) { return EF_WRITE_ERR; }
    if (!s_mutex) { return EF_ENV_INIT_FAILED; }
    if (os_mutex_lock(s_mutex, 0xffffffff)) { return EF_ENV_INIT_FAILED; }
    if (s_reset_pending || !s_fs) { os_mutex_unlock(s_mutex); return EF_ENV_INIT_FAILED; }
    err = same_value(path, value, size);
    if (err == 1) { os_mutex_unlock(s_mutex); return EF_NO_ERR; }
    if (!err) {
        err = lfs_file_open(s_fs, &file, TXN_FILE, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
        if (!err) {
            err = size ? (int)lfs_file_write(s_fs, &file, value, size) : 0;
            if (err >= 0) { err = err == size ? 0 : LFS_ERR_IO; }
            close_err = lfs_file_close(s_fs, &file);
            if (!err) { err = close_err; }
            if (!err) { err = lfs_rename(s_fs, TXN_FILE, path); }
        }
    }
    if (err) { lfs_remove(s_fs, TXN_FILE); }
    finish(err);
    return map_error(err);
}

EfErrCode customer_lfs_del(const char *key)
{
    char path[LFS_NAME_MAX + 1];
    int err;
    if (key_path(key, path)) { return EF_ENV_NAME_ERR; }
    if (!s_mutex) { return EF_ENV_INIT_FAILED; }
    if (os_mutex_lock(s_mutex, 0xffffffff)) { return EF_ENV_INIT_FAILED; }
    if (s_reset_pending || !s_fs) { os_mutex_unlock(s_mutex); return EF_ENV_INIT_FAILED; }
    err = lfs_remove(s_fs, path);
    finish(err);
    return map_error(err);
}

EfErrCode customer_lfs_reset(void)
{
    int err;
    if (!s_mutex) { return EF_ENV_INIT_FAILED; }
    if (os_mutex_lock(s_mutex, 0xffffffff)) { return EF_ENV_INIT_FAILED; }
    if (s_reset_pending) { os_mutex_unlock(s_mutex); return EF_ENV_INIT_FAILED; }
    /* Fence waiting tasks before touching the filesystem, even if erase fails.
     * DoFactoryReset reboots after this call; no task may repopulate old NV. */
    s_reset_pending = 1;
    err = lfs_customer_reset();
    s_fs = NULL;
    os_mutex_unlock(s_mutex);
    return !err ? EF_NO_ERR : EF_ERASE_ERR;
}

void customer_lfs_print(void)
{
    lfs_dir_t dir;
    struct lfs_info info;
    char key[KEY_LIMIT + 1];
    unsigned int total = 0;
    int err, close_err;
    size_t i, length;
    if (!s_mutex) { return; }
    if (os_mutex_lock(s_mutex, 0xffffffff)) { return; }
    if (s_reset_pending || !s_fs) { os_mutex_unlock(s_mutex); return; }
    err = lfs_dir_open(s_fs, &dir, "/");
    if (!err) {
        while ((err = lfs_dir_read(s_fs, &dir, &info)) > 0) {
            if (info.type != LFS_TYPE_REG || info.name[0] != 'k') { continue; }
            length = strlen(info.name + 1);
            if ((length & 1) || length / 2 > KEY_LIMIT) { continue; }
            for (i = 0; i < length / 2; ++i) {
                char a = info.name[1 + 2*i], b = info.name[2 + 2*i];
                if (!((a >= '0' && a <= '9') || (a >= 'a' && a <= 'f')) ||
                    !((b >= '0' && b <= '9') || (b >= 'a' && b <= 'f'))) { break; }
                key[i] = ((a <= '9' ? a - '0' : a - 'a' + 10) << 4) |
                         (b <= '9' ? b - '0' : b - 'a' + 10);
            }
            if (i != length / 2) { continue; }
            key[i] = 0;
            EF_INFO("%s=blob %lu bytes\n", key, (unsigned long)info.size);
            total += info.size;
        }
        close_err = lfs_dir_close(s_fs, &dir);
        if (!err) { err = close_err; }
    }
    EF_INFO("CUSTOMER LFS: %u bytes of values (excluding filesystem overhead).\n", total);
    finish(err);
}
