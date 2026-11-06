# OTA 固件升级组件

> 空中升级,HTTP/MQTT/本地传输,A-B双分区,SHA256校验,差分/压缩/全量升级,回滚保护,安全启动

## 快速定位

**核心文件:**
- `ota.c:59-67` - download_state_e状态机(IDLE/HEAD/BOOT/IMGE/IMGA/IMGB/FULL)
- `ota.c:72-76` - download_method_e升级方式(COMZ压缩/DIFF差分/DUAL双分区)
- `ota.c:81-87` - download_part_e分区枚举(BOOT/IMGA/IMGB/OTAS/FULL)
- `ota.c:251` - download_partition_init() 分区初始化,擦除Flash
- `ota.c:876` - ota_write() 写入固件数据到Flash
- `http_ota.c:160` - HTTP client回调调用ota_write()
- `mqtt_ota.c` - MQTT订阅主题接收固件
- `local_ota.c` - 串口/SD卡本地升级

**关键宏:**
- `ota.c:42` - DOWNLOAD_RET_CHECK_RETURN() 统一错误处理+释放内存

## 核心API

**OTA控制:**
- `ota_init()` - 开始OTA(初始化分区/擦除Flash)
- `ota_write(data, len)` - 写入固件数据到Flash
- `ota_done(reset)` - 结束OTA(校验+更新OTA状态,按参数决定是否重启)
- `ota_confirm_update()` - 确认当前运行A-B分区有效,确认前会校验当前运行分区完整性
- `ota_is_ab_update_pending()` - 判断A-B升级是否处于已切换但未确认状态

**分区管理:**
- `download_partition_init()` - 初始化备用分区(擦除Flash)
- `ota_get_active_part()` - 获取当前活动分区(IMGA/IMGB)
- `change_active_part_for_ab()` - A-B分区切换/测试入口

**传输方式:**
- `http_client_download_file(url, reboot)` - HTTP/HTTPS下载固件，成功后按参数决定是否重启
- `mqtt_ota_init()` - MQTT订阅固件主题
- `local_ota_start()` - 串口/SD卡本地升级

**校验:**
- `ota_verify_crc32()` - CRC32校验
- `ota_verify_sha256()` - SHA256哈希校验
- `ota_verify_signature()` - 数字签名验证(可选)

## A-B双分区机制

```
Flash布局:
0x0      - Bootloader
0x8000   - Partition Table
0xD000   - OTA Data (运行分区标记)
0x10000  - IMGA (App分区A)
0x210000 - IMGB (App分区B)

升级流程:
1. 当前运行IMGA → 下载到IMGB
2. 校验通过 → 更新OTA Data → 重启
3. Bootloader从IMGB启动
4. 应用调用ota_confirm_update()
5. 异常时Bootloader自动回滚IMGA
```

**待确认状态:**
- `ota_is_ab_update_pending()` 用于判断A-B升级是否处于 trial/pending 状态: OTA状态中已设置 `OTA_AB_UPDATAFLAG`,但未设置 `OTA_AB_DIRETC_START`。
- pending 表示系统已经切到待确认分区运行,但当前版本尚未 `ota_confirm_update()` 确认。
- 处于 pending 时不应再次启动新的 OTA,用户可执行确认或走明确的回滚/控制路径。
- pending 检查应放在 HTTP OTA、服务 OTA、手动 OTA 的启动入口,不要放进每个分片都会调用的 `ota_write()` 路径,避免反复读 OTA state 分区并刷屏打印。

## AI理解要点

1. **状态机驱动**: download_state_e管理升级流程,IDLE→HEAD→IMGA/IMGB→FULL `ota.c:59-67`

2. **三种方式**: COMZ压缩/DIFF差分/DUAL双分区,根据method字段选择 `ota.c:72-76`

3. **A-B双分区**: IMGA/IMGB备份机制,OTA Data存储当前分区,Bootloader自动回滚 `Flash布局`

4. **分区初始化**: download_partition_init()擦除备用分区 `ota.c:251`

5. **ota_write流程**: drv_flash_write()写入,状态机更新offset `ota.c:876`

6. **HTTP传输**: 基于http_client,事件驱动ON_DATA回调 `http_ota.c:160`

7. **MQTT推送**: 订阅主题接收固件片段,适合云平台主动升级 `mqtt_ota.c`

8. **多重校验**: CRC32+SHA256+签名验证,防篡改/防降级 `ota_end()`

9. **本地升级**: local_ota.c支持串口/SD卡,调试/产线使用 `local_ota.c`

10. **EasyFlash集成**: OTA Data存储在EasyFlash分区 `ota.c:27`
