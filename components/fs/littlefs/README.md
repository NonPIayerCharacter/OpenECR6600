# ECR6600 littlefs

移植自 `6630/ECW6630/components/fs/littlefs`，littlefs 版本为 2.11，磁盘格式版本为 2.1。

## 启用与调用

1. 在目标工程 menuconfig 中启用 `LFS`（`CONFIG_LFS=y`），自动选择 `NV` 和其依赖的 `FLASH`。
2. 在实际烧录的分区表中提供名为 `fs` 的独立分区。起始地址和长度必须按 4096 字节对齐，长度至少为两个块；不能与固件、NV 或 OTA 区域重叠。本次未修改任何板级 defconfig 或分区表。
3. 在分区信息和 Flash 驱动初始化完成后，启动阶段单次调用 `lfs_init()`，随后再启动文件系统使用任务。包含公共头 `lfs_port.h`，返回值可作为 `lfs_t *` 使用；返回 NULL 表示初始化失败。
4. 文件操作使用 `lfs.h` 的标准接口。返回的是共享实例，请勿自行卸载；初始化入口不支持多个任务同时调用。挂载后的文件操作由 OS 互斥锁保护，公共头统一启用 `LFS_THREADSAFE`，调用方不要禁用此宏。

当挂载返回 `LFS_ERR_CORRUPT` 时会格式化整个 `fs` 分区后重新挂载，已有损坏数据也会被清除。Flash I/O 失败等其他错误直接返回失败，不自动格式化。

CLI 命令 `ut_lfs` 在文件 `test` 中保存计数，每次调用加一。新文件首次计数为 1；正常重启后再次调用应继续递增。该命令用于串行人工自测。

## 已执行验证

- 使用 `/opt/nds32le-elf-mculib-v3s/bin/nds32le-elf-gcc`，通过 SDK 的 `components/Makefile`，基于 `Boards/ecr6600/matter/generated` 现有配置并临时传入 `CONFIG_LFS=y`，成功编译 `lfs.c`、`lfs_port.c`、`lfs_util.c`、`lfs_test.c`。对象文件位于 `/tmp/ecr6600-littlefs-build`，未修改工程配置。
- 主机模拟 NOR Flash（校验擦除对齐、地址范围及只能从 1 写为 0），运行实际 littlefs 和移植层，启用 AddressSanitizer / UndefinedBehaviorSanitizer。通过初始化失败清理、I/O 失败不格式化、创建/写入/读取/重新挂载/删除验证。临时程序位于 `/tmp/ecr6600-lfs-test`。
- 完整固件链接、烧录、板上 `ut_lfs`、断电持久化和掉电恢复尚未验证；需在实际使用的板级工程启用并配置分区后执行。

默认块大小为 4096 字节，`cache_size` 为 512 字节，允许默认最多 512 字节的小文件内联。全局读写缓存合计 1024 字节，每个打开文件另需 512 字节缓存；读写最小粒度仍为 32 字节。
