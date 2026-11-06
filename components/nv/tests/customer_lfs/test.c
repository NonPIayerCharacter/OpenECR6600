/* Host NOR simulation exercising the production adapter and littlefs port. */
#include <assert.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "customer_lfs.h"
#include "lfs_port.h"

#define BLOCK 4096
#define BASE BLOCK
#define CAPACITY (100 * 1024)
static unsigned char flash[BASE + CAPACITY + BLOCK];
static int missing, fail_io, erase_count, write_count, cut_at, mutations, reboots;
static void assert_blocked(void);
void hal_system_reset(int type) { (void)type; ++reboots; assert_blocked(); }
static void *kv_mutex;
static int mutex_creations;
static pthread_mutex_t scheduler_lock = PTHREAD_MUTEX_INITIALIZER;
int os_scheduler_lock(void) { return pthread_mutex_lock(&scheduler_lock); }
int os_scheduler_unlock(void) { return pthread_mutex_unlock(&scheduler_lock); }
static pthread_mutex_t gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond = PTHREAD_COND_INITIALIZER;
static int gate_enabled, gate_entered, gate_release, gate_waiters;
static jmp_buf power_cut;
extern struct lfs_config cfg;

void *os_mutex_create(void)
{
    pthread_mutex_t *p = malloc(sizeof(*p));
    assert(p && !pthread_mutex_init(p, NULL));
    ++mutex_creations;
    if (!kv_mutex) { kv_mutex = p; }
    return p;
}
int os_mutex_destroy(void *p) { int r = pthread_mutex_destroy(p); free(p); return r; }
int os_mutex_lock(void *p, size_t timeout)
{
    (void)timeout;
    pthread_mutex_lock(&gate_lock);
    if (p == kv_mutex && gate_enabled && gate_entered) {
        ++gate_waiters; pthread_cond_broadcast(&gate_cond);
    }
    pthread_mutex_unlock(&gate_lock);
    return pthread_mutex_lock(p);
}
int os_mutex_unlock(void *p) { return pthread_mutex_unlock(p); }
int partion_info_get(char *name, unsigned int *base, unsigned int *size)
{
    assert(!strcmp(name, PARTION_NAME_NV_CUSTOMER));
    *base = BASE; *size = CAPACITY;
    return missing;
}
static int fault(void) { if (fail_io) { if (fail_io > 0) { --fail_io; } return 1; } return 0; }
static void mutation_done(void) { if (cut_at && ++mutations == cut_at) { longjmp(power_cut, 1); } }
int drv_spiflash_read_apb(unsigned int address, unsigned char *buffer, unsigned int size)
{
    assert(address >= BASE && address + size <= BASE + CAPACITY);
    if (fault()) { return -1; }
    memcpy(buffer, flash + address, size); return 0;
}
int drv_spiflash_write(unsigned int address, const unsigned char *buffer, unsigned int size)
{
    assert(address >= BASE && address + size <= BASE + CAPACITY);
    if (fault()) { return -1; }
    for (unsigned int i = 0; i < size; ++i) {
        assert((flash[address+i] & buffer[i]) == buffer[i]);
        flash[address+i] &= buffer[i];
    }
    ++write_count; mutation_done(); return 0;
}
int drv_spiflash_erase(unsigned int address, unsigned int size)
{
    assert(address >= BASE && address + size <= BASE + CAPACITY);
    assert(!(address % BLOCK) && !(size % BLOCK));
    pthread_mutex_lock(&gate_lock);
    if (gate_enabled && !gate_entered) {
        gate_entered = 1; pthread_cond_broadcast(&gate_cond);
        while (!gate_release) { pthread_cond_wait(&gate_cond, &gate_lock); }
    }
    pthread_mutex_unlock(&gate_lock);
    if (fault()) { return -1; }
    memset(flash + address, 255, size); ++erase_count; mutation_done(); return 0;
}
static void check(const char *key, const void *data, int size)
{
    unsigned char buffer[4096]; int length = -1;
    assert(size <= (int)sizeof(buffer));
    int result = customer_lfs_get(key, buffer, sizeof(buffer), &length);
    if (result != size) { fprintf(stderr, "read %s: got %d, expected %d\n", key, result, size); }
    assert(result == size);
    assert(length == size && !memcmp(buffer, data, size));
}
static void *writer(void *arg)
{
    char key[20]; unsigned int value;
    snprintf(key, sizeof(key), "f/%lu/thread", (unsigned long)arg);
    for (value = 0; value < 100; ++value) {
        assert(customer_lfs_init() == EF_NO_ERR);
        assert(customer_lfs_set(key, &value, sizeof(value)) == EF_NO_ERR);
        check(key, &value, sizeof(value));
    }
    return NULL;
}
static void assert_blocked(void)
{
    char value; int length = -1;
    int writes = write_count, erases = erase_count;
    assert(customer_lfs_init() == EF_ENV_INIT_FAILED);
    assert(customer_lfs_get("saved", &value, 1, &length) == EF_ENV_INIT_FAILED && !length);
    assert(customer_lfs_set("saved", "old", 3) == EF_ENV_INIT_FAILED);
    assert(customer_lfs_del("saved") == EF_ENV_INIT_FAILED);
    assert(customer_lfs_reset() == EF_ENV_INIT_FAILED);
    customer_lfs_print();
    assert(write_count == writes && erase_count == erases);
}
static void *queued_operation(void *arg)
{
    char value; int length;
    switch ((unsigned long)arg) {
    case 0: assert(customer_lfs_get("saved", &value, 1, &length) == EF_ENV_INIT_FAILED); break;
    case 1: assert(customer_lfs_set("saved", "old", 3) == EF_ENV_INIT_FAILED); break;
    case 2: assert(customer_lfs_del("saved") == EF_ENV_INIT_FAILED); break;
    case 3: assert(customer_lfs_init() == EF_ENV_INIT_FAILED); break;
    case 4: customer_lfs_print(); break;
    case 5: assert(customer_lfs_reset() == EF_ENV_INIT_FAILED); break;
    }
    return NULL;
}
static void *reset_thread(void *unused)
{
    (void)unused;
    assert(customer_lfs_reset() == EF_NO_ERR);
    return NULL;
}
static void reset_fence_tests(void)
{
    /* Fork provides a fresh process lifetime for each terminal reset test. */
    for (int mode = 0; mode < 3; ++mode) {
        pid_t pid = fork(); assert(pid >= 0);
        if (!pid) {
            if (mode == 0) {
                pthread_t resetter, queued[6];
                gate_enabled = 1;
                assert(!pthread_create(&resetter, NULL, reset_thread, NULL));
                pthread_mutex_lock(&gate_lock);
                while (!gate_entered) { pthread_cond_wait(&gate_cond, &gate_lock); }
                pthread_mutex_unlock(&gate_lock);
                for (unsigned long i=0; i<6; ++i) {
                    assert(!pthread_create(&queued[i], NULL, queued_operation, (void *)i));
                }
                pthread_mutex_lock(&gate_lock);
                while (gate_waiters < 6) { pthread_cond_wait(&gate_cond, &gate_lock); }
                gate_release = 1; pthread_cond_broadcast(&gate_cond);
                pthread_mutex_unlock(&gate_lock);
                assert(!pthread_join(resetter, NULL));
                for (int i=0; i<6; ++i) { assert(!pthread_join(queued[i], NULL)); }
                assert_blocked();
            } else if (mode == 1) {
                fail_io = -1;
                assert(customer_lfs_reset() == EF_ERASE_ERR);
                fail_io = 0;
                assert_blocked(); /* Even a failed reset cannot reopen the gate. */
            } else {
                assert(customer_lfs_set("saved", "data", 4) == EF_NO_ERR);
                memset(flash + BASE, 0, CAPACITY);
                ((lfs_t *)lfs_init())->rcache.block = (lfs_block_t)-1;
                char value;
                assert(customer_lfs_get("saved", &value, 1, NULL) == EF_READ_ERR);
                assert(reboots == 1);
                assert_blocked(); /* Includes access from the reboot callback. */
            }
            _exit(0);
        }
        int status; assert(waitpid(pid, &status, 0) == pid);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
}
/* Tests may reuse the process; production reset intentionally forbids this. */
static void clear_for_test(void)
{
    assert(!lfs_customer_reset());
    assert(customer_lfs_init() == EF_NO_ERR);
}
static void four_fabrics(void)
{
    const char *suffix[] = {"k/0", "g", "o", "n", "r", "m", "ac/0/0", "i"};
    const int sizes[] = {97, 23, 105, 257, 254, 16, 27, 250};
    const char *keys[] = {"LockUser", "Credential", "LockUserName", "CredentialData",
                          "UserCredentials", "WeekSchedules", "YearSchedules", "HolidaySched"};
    /* Conservative complete arrays, including status/alignment. */
    const int arrays[] = {280, 1600, 100, 800, 400, 600, 1200, 160};
    unsigned char data[2048]; char key[40];
    memset(data, 0x38, sizeof(data));
    clear_for_test();
    for (int f = 1; f <= 4; ++f) {
        for (int j = 0; j < 8; ++j) {
            snprintf(key, sizeof(key), "f/%x/%s", f, suffix[j]);
            assert(customer_lfs_set(key, data, sizes[j]) == EF_NO_ERR);
            check(key, data, sizes[j]);
        }
    }
    for (int j = 0; j < 8; ++j) { assert(customer_lfs_set(keys[j], data, arrays[j]) == EF_NO_ERR); }
    for (int j = 0; j < 20; ++j) {
        snprintf(key, sizeof(key), "g/config/%x", j);
        assert(customer_lfs_set(key, data, 32) == EF_NO_ERR);
    }
    for (int j = 0; j < 300; ++j) {
        data[0] = j;
        assert(customer_lfs_set("Credential", data, 1600) == EF_NO_ERR);
    }
    customer_lfs_print();
    lfs_ssize_t blocks = lfs_fs_size((lfs_t *)lfs_init());
    assert(blocks > 0 && blocks < CAPACITY/BLOCK);
    printf("4 Fabric + lock arrays + 20 globals, after 300 updates: %ld/%d allocated blocks\n",
           (long)blocks, CAPACITY/BLOCK);
    clear_for_test();
}
static int unlocked(const struct lfs_config *c) { (void)c; return 0; }
static void cuts(void)
{
    /* Each child starts from the same committed old state. Crash after every
     * program/erase callback, then mount using a fresh LFS instance. */
    for (int cut = 1; cut <= 24; ++cut) {
        pid_t pid = fork(); assert(pid >= 0);
        if (!pid) {
            lfs_t fresh; lfs_file_t file; char buffer[8] = {0};
            cut_at = cut; mutations = 0;
            if (!setjmp(power_cut)) { customer_lfs_set("atomic", "new", 3); }
            cut_at = 0;
            /* Simulated power loss discards RAM state, including held locks. */
            cfg.lock = unlocked; cfg.unlock = unlocked;
            assert(!lfs_mount(&fresh, &cfg));
            assert(!lfs_file_open(&fresh, &file, "k61746f6d6963", LFS_O_RDONLY));
            assert(lfs_file_read(&fresh, &file, buffer, sizeof(buffer)) == 3);
            assert(!memcmp(buffer, "old", 3) || !memcmp(buffer, "new", 3));
            assert(!lfs_file_close(&fresh, &file));
            assert(!lfs_unmount(&fresh));
            _exit(0);
        }
        int status; assert(waitpid(pid, &status, 0) == pid);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
}
static void *cold_init(void *unused)
{
    (void)unused;
    assert(customer_lfs_init() == EF_ENV_INIT_FAILED);
    return NULL;
}
static void first_init_race(void)
{
    pid_t pid = fork(); assert(pid >= 0);
    if (!pid) {
        pthread_t threads[8];
        missing = 1;
        for (int i=0; i<8; ++i) { assert(!pthread_create(&threads[i], NULL, cold_init, NULL)); }
        for (int i=0; i<8; ++i) { assert(!pthread_join(threads[i], NULL)); }
        assert(mutex_creations == 1);
        _exit(0);
    }
    int status; assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
int main(void)
{
    unsigned char data[2048]; char output[32]; int length, before;
    first_init_race();
    memset(flash, 0x5a, sizeof(flash)); /* Old NV/unknown format. */
    missing = 1; assert(customer_lfs_init() == EF_ENV_INIT_FAILED);
    missing = 0; fail_io = 1;
    assert(customer_lfs_init() == EF_ENV_INIT_FAILED && !erase_count);
    for (size_t i=0; i<sizeof(flash); ++i) { assert(flash[i] == 0x5a); }
    /* Once reads work, the old/unknown format is detected as CORRUPT. */
    assert(customer_lfs_init() == EF_NO_ERR && erase_count);
    for (int i=0; i<BASE; ++i) { assert(flash[i] == 0x5a); }
    for (size_t i=BASE+CAPACITY; i<sizeof(flash); ++i) { assert(flash[i] == 0x5a); }
    assert(customer_lfs_get("missing", output, sizeof(output), &length) == 0 && !length);
    assert(customer_lfs_set("f/1/ac/0/0", "abc", 3) == EF_NO_ERR);
    assert(customer_lfs_set("f_1_ac_0_0", "xyz", 3) == EF_NO_ERR);
    check("f/1/ac/0/0", "abc", 3); check("f_1_ac_0_0", "xyz", 3);
    before = write_count;
    assert(customer_lfs_set("f/1/ac/0/0", "abc", 3) == EF_NO_ERR);
    assert(write_count == before);
    assert(customer_lfs_get("f/1/ac/0/0", output, 1, &length) == 1 && length == 3 && output[0] == 'a');
    assert(customer_lfs_get("f/1/ac/0/0", NULL, 0, &length) == 0 && length == 3);
    assert(customer_lfs_set("empty", "", 0) == EF_NO_ERR);
    assert(customer_lfs_get("empty", output, sizeof(output), &length) == 0 && !length);
    assert(customer_lfs_set("delete", "a", 1) == EF_NO_ERR);
    assert(customer_lfs_set("delete", NULL, 0) == EF_NO_ERR);
    assert(customer_lfs_del("delete") == EF_ENV_NAME_ERR);
    lfs_t *fs = lfs_init();
    assert(!lfs_unmount(fs) && !lfs_mount(fs, &cfg));
    check("f/1/ac/0/0", "abc", 3);
    assert(customer_lfs_set("atomic", "old", 3) == EF_NO_ERR); cuts();
    pthread_t threads[3];
    for (unsigned long i=0; i<3; ++i) { assert(!pthread_create(&threads[i], NULL, writer, (void *)i)); }
    for (int i=0; i<3; ++i) { assert(!pthread_join(threads[i], NULL)); }
    for (int i=0; i<300; ++i) { memset(data, i, sizeof(data)); assert(customer_lfs_set("LockUser", data, sizeof(data)) == EF_NO_ERR); }
    check("LockUser", data, sizeof(data));
    /* Full filesystem must preserve the last committed value and not format. */
    before = erase_count; int full = 0;
    for (int i=0; i<200; ++i) {
        snprintf(output, sizeof(output), "fill/%d", i);
        int err = customer_lfs_set(output, data, sizeof(data));
        if (err == EF_ENV_FULL) { full = 1; break; }
        assert(err == EF_NO_ERR);
    }
    assert(full); check("atomic", "old", 3);
    unsigned char *large = calloc(128, 1024); assert(large);
    assert(customer_lfs_set("atomic", large, 128*1024) == EF_ENV_FULL);
    free(large); check("atomic", "old", 3);
    (void)before;
    clear_for_test();
    assert(customer_lfs_get("LockUser", output, sizeof(output), &length) == 0 && !length);
    assert(customer_lfs_set("saved", data, sizeof(data)) == EF_NO_ERR);
    before = erase_count;
    fail_io = 1;
    assert(customer_lfs_get("saved", output, sizeof(output), &length) == EF_READ_ERR);
    assert(erase_count == before && reboots == 0);
    check("saved", data, sizeof(data));
    fail_io = -1;
    assert(customer_lfs_get("saved", output, sizeof(output), &length) == EF_READ_ERR);
    assert(erase_count == before && reboots == 0);
    fail_io = 0;
    assert(customer_lfs_init() == EF_NO_ERR);
    check("saved", data, sizeof(data));
    four_fabrics();
    reset_fence_tests();
    puts("PASS: conversion, boundaries, key mapping, dedup, partial reads, remount, atomic cuts, concurrent updates, GC, full, reset fence, concurrent init, IO failures");
}
