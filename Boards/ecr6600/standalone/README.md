# Boards/ecr6600/standalone

## 目录职责

`Boards/ecr6600/standalone/` 是通用基线板型入口，适合作为不绑定特定产品云逻辑时的基础构建入口。

## 关键文件

- `defconfig`
- `makefile`
- `Make.defs`
- `main.c`
- `ref_bin/`
- `build.bat`
- `eclipse_make.sh`
- `make_ota_ab.sh`、`make_ota_comp.sh`、`make_ota_diff.sh`

## 关键事实

- 板级 `makefile` 是最简转发型入口，直接把构建请求传给根 `Makefile`
- `main.c` 走的是通用启动路径
- `main.c` 中可选包含 `CONFIG_APP_EXAMPLE_BUILD` 对应的示例入口
- `ref_bin/` 保存该基线板型配套的基础镜像资源，首次烧录时通常需要和应用镜像一起使用

## 什么时候从这里进入

- 需要验证基础固件能力而不是特定产品逻辑。
- 需要以最接近基线的平台版本做功能验证。

## 环境前提

- 推荐在 `Ubuntu 20/22` 或 `Cygwin` shell 中构建。
- 如果交叉编译器不在 `PATH`，可通过 `make GCC_PATH=xxx/bin/ ...` 指定。
- 如果 Ubuntu 下出现 `Lack of 32-bit support`，先安装 `libc6-i386`。

## 常见操作

首次推荐直接在当前目录执行：

```sh
make defconfig
make menuconfig
make all
```

也可以在仓库根目录显式指定当前板级路径：

```sh
make BOARD_DIR=$PWD/Boards/ecr6600/standalone APP_NAME=$PWD/Boards/ecr6600/standalone all
```

典型输出位置：

- `build/standalone/`：编译产物目录
- `build/standalone/standalone.elf`：调试 ELF
- `build/standalone/ECR6600F_standalone_cpu_0x7000.bin`：应用镜像
- `generated/.config`：当前板级配置

## 烧录提示

`standalone` 更适合作为首次 bring-up 的基线板型，但首次完整烧录时通常不只是一个应用 bin。

当前目录 `ref_bin/` 下通常会带着配套的基础镜像，例如：

- `*Partition*.bin`
- `*NV*.bin`
- `*ota_status*.bin`

建议使用与你当前 `standalone` 目录配套的 `ref_bin/`，再加上 `build/standalone/` 下新编出来的应用镜像一起下载，不要混用其他产品板型的基础 bin。

## 串口自测

首次上电后，建议先做最基础的启动检查：

1. 串口先按 `UART0 / 115200` 打开。
2. 观察是否打印 `SDK version`、`reboot reason`、`Flash_FT`。
3. 若出现 `flash init error`、`partition error`、`easyflash error`，优先检查下载内容和板型是否匹配。

如果需要验证示例入口是否接入成功，可继续观察是否出现 `[EXAMPLE] app_example_main enter`。

## 基础 CLI 测试入口

`standalone` 是第一次上手最合适的 CLI 验证入口，因为它不绑定特定产品逻辑，同时 `defconfig` 默认已经打开了常见 Wi-Fi 命令和一批驱动单测命令。

推荐按下面顺序测试：

1. `help`
   先确认 CLI 菜单能正常打印。
2. `wifi_scan`
   先确认 Wi-Fi 扫描链路和基础射频工作正常。
3. `wifi_sta <ssid> [password]`
   用最小方式验证 STA 联网。
4. `wifi_get_info`
   联网后确认当前 IP、SSID 等信息。
5. `ut_rtc get`
   低风险，适合作为驱动 CLI 是否可用的起点。
6. `ut_timer create`
7. `ut_timer set 0 1000`
8. `ut_timer start 0`
9. `ut_timer stop 0`
   这一组命令可以验证基础定时器路径。
10. `ut_adc vbat`
11. `ut_adc temp_test`
12. `ut_trng trng`
13. `ut_flash status`

如果上面的命令都能正常响应，再按需要进入：

- `../../../examples/static_ip_example/README.md`：验证静态 IP。
- `../../../examples/wifi_netcfg_multiap_example/README.md`：验证 SmartConfig、多 AP、凭据保存。

## 首次测试时不建议直接跑的命令

下面这些命令更适合明确知道板级连线和存储布局之后再跑：

- `ut_flash erase`、`ut_flash write`、`ut_flash write_dma`、`ut_flash otpwrite`、`ut_flash otperase`
- `ut_efuse write`
- `ut_gpio interrupt*`、`ut_pwm *`、`ut_uart *`、`SPI` 类依赖外设接线的测试
- `ut_wdt isr` 或会主动触发复位的看门狗测试

原因不是这些命令无效，而是它们要么会修改板上非易失数据，要么高度依赖你手上这块板子的实际引脚和外设连接，第一次上手时很容易把“硬件接线问题”误判成“SDK 问题”。

## 推荐下一步

- 公共板级层：看 `../common/README.md`
- 示例入口：看 `../../../examples/README.md`
