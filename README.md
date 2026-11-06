# ECR6600 Firmware Workspace

本仓库是 ECR6600 平台型固件工作区，包含板级配置、应用域代码、公共组件、协议栈、操作系统适配层、构建脚本和专题文档。

## 适用范围

- 适用于当前 `ECR6600_Freertos-refs_heads_ECR6600F_V2.1.0B02P05` 这一类 ECR6600F 固件工作区 clone，以及目录结构保持一致的同系内部 clone。
- 主要面向 ECR6600 板级、产品应用、公共组件和 Wi-Fi/BLE 协议栈的维护、联调和排障。
- 主入口文档就是当前的 `README.md`，内容以中文为主；`README_CN.md` 仅作为兼容命名入口保留。

## 第一次上手 5 步

如果你是第一次接触这份仓库，建议先按下面顺序走完，不要一上来就钻进产品目录：

1. 先确认宿主环境和工具链。
   推荐 `Ubuntu 20/22`；Windows 通常通过 `Cygwin` 进入，交叉编译器前缀是 `nds32le-elf-`。
2. 先编 `standalone` 基线板型。

```sh
cd Boards/ecr6600/standalone
make defconfig
make menuconfig
make all
```

3. 准备首次烧录文件。
   通常至少需要同一板级目录的 `ref_bin/` 基础镜像，再加上 `build/standalone/` 下新编出来的应用镜像；当前源码没有统一的 `make flash`。
4. 上电后先看基础启动日志。
   串口先按 `UART0 / 115200` 打开，重点检查 `SDK version`、`reboot reason`、`Flash_FT`，并排除 `flash init error`、`partition error`、`easyflash error`。
5. 再做最小功能自测。
   先跑 `help`、`wifi_scan`、`wifi_sta <ssid> [password]`；之后进入 `examples/static_ip_example/README.md` 做最小联网验证，再进入 `examples/wifi_netcfg_multiap_example/README.md` 做配网和凭据保存测试。只有在你确实要跑产品级 BLE 配网和云逻辑时，再进入 `apps/ai_sys/coze/README.md`。

## 构建模式速览

这份仓库的主构建模式是：

- 根 `Makefile` 统一调度顶层目录。
- 板级目录保存 `defconfig`、局部 `makefile` 和板型相关资源。
- 构建输出写入 `build/<board-name>/`。
- 板级生成配置写入 `Boards/<chip>/<board>/generated/`。

## 编译前准备

首次在新机器上构建前，建议先确认下面几项：

- 推荐宿主环境是 `Ubuntu 20/22`；Windows 路径通常通过 `Cygwin` 进入，`Boards/ecr6600/*/build.bat` 只是拉起 `mintty`，不是一键编译脚本。
- 交叉编译器前缀是 `nds32le-elf-`，可通过环境变量或 `make GCC_PATH=xxx/bin/ ...` 指定工具链路径。
- Ubuntu 下如果出现 `Lack of 32-bit support`，需要安装 `libc6-i386`。
- `menuconfig` 会按 Ubuntu 版本或 `Cygwin` 分支调用不同的 `kconfig_tool` 可执行文件，因此首次构建建议优先在 README 推荐的环境中验证。

常见示例：

```sh
make GCC_PATH=/opt/nds32le-elf-mculib-v3s/bin/ BOARD_DIR=$PWD/Boards/ecr6600/standalone APP_NAME=$PWD/Boards/ecr6600/standalone all
```

## 入口怎么选

如果你是第一次接触这份仓库，建议先判断自己属于哪一类需求，再决定从哪个目录进入：

- 只想先把 SDK 基础能力编起来、看启动日志、排除工具链和下载问题：先走 `Boards/ecr6600/standalone/`
- 需要编译具体产品版本，例如 `coze`、`joyinside`、`tomcat`：走对应的 `Boards/ecr6600/<product>/`
- 需要先验证最小联网链路，不涉及 BLE 配网和产品状态机：先走 `examples/static_ip_example/`
- 需要继续验证 Wi-Fi 配网链路：再走 `examples/wifi_netcfg_multiap_example/`
- 需要排查产品级 BLE 配网、云逻辑、音频和状态机联动：再进入 `apps/ai_sys/coze/`

推荐的新手最短路径是先完成 `standalone` 的编译和启动自测，再进入 `static_ip_example`，最后按需要进入 `wifi_netcfg_multiap_example` 或具体产品板型。

如果你已经明确要编某个产品板型，只需要把上面的 `standalone` 目录替换成对应的 `Boards/ecr6600/<product>/`。

也可以在仓库根目录显式指定板级路径，例如：

```sh
make BOARD_DIR=$PWD/Boards/ecr6600/standalone APP_NAME=$PWD/Boards/ecr6600/standalone all
```

## 输出与烧录边界

关键输出位置：

- `build/<board-name>/`：编译产物目录。
- `Boards/ecr6600/<board>/generated/.config`：当前板级配置。
- `Boards/ecr6600/<board>/generated/config.h`：Kconfig 导出的配置头。

这份仓库能明确完成“编译并产出镜像”，但当前源码中没有统一的 `make flash` 入口。实际烧录通常需要配合你手上的 ECR6600 下载工具，把板级基础镜像和新编译出的应用镜像一起下载到板子。

首次完整烧录时，通常至少要关注两类文件：

- 板级 `ref_bin/` 中的基础镜像，例如分区表、NV、OTA 状态等
- `build/<board-name>/` 下新编出来的应用镜像，例如 `ECR6600F_<board>_cpu_0x7000.bin`

建议优先使用同一板级目录自带的 `ref_bin/`，不要混用不同板型的基础 bin。

## 第一次启动后的自测路线

如果你是第一次把镜像真正跑起来，建议按下面顺序做，不要一上来就进产品目录：

1. 串口先按 `UART0 / 115200` 打开，确认是否能看到启动日志。
2. 重点检查是否出现 `SDK version`、`reboot reason`、`Flash_FT` 等基础日志。
3. 若看到 `flash init error`、`partition error`、`easyflash error`，先排查板型、基础 bin 和下载内容是否匹配。
4. 先输入 `help`，确认 CLI 正常工作。
5. 再输入 `wifi_scan`，确认 Wi-Fi 命令和射频基础路径正常。
6. 使用 `wifi_sta <ssid> [password]` 做最小 STA 联网。
7. 需要验证静态 IP 时，进入 `examples/static_ip_example/README.md` 按 `static_ip set/get/dhcp` 流程测试。
8. 需要验证配网、凭据保存、重连时，再进入 `examples/wifi_netcfg_multiap_example/README.md`。
9. 需要做驱动基础单测时，优先跑安全命令：`ut_rtc get`、`ut_timer create`、`ut_timer set 0 1000`、`ut_timer start 0`、`ut_timer stop 0`、`ut_adc vbat`、`ut_trng trng`、`ut_flash status`。

第一次上手不建议直接执行 `ut_flash erase`、`ut_flash write`、`ut_efuse write`、未知 GPIO/PWM/SPI 映射测试，避免误改板上关键数据或因接线不明造成误判。

## 常见问法快速定位

| 常见问法 | 第一入口 | 说明 |
| --- | --- | --- |
| 这个工程怎么编译？ | `Boards/README.md` | 第一次上手优先 `Boards/ecr6600/standalone/` |
| 这个工程怎么烧录、下载、刷机？ | `Boards/ecr6600/standalone/README.md` | 当前仓库说明的是镜像组成和板级 `ref_bin/`，不是统一下载工具封装 |
| 串口日志怎么看？ | `Boards/ecr6600/standalone/README.md` | 先按 `UART0 / 115200` 看启动日志 |
| Wi-Fi 能不能先连起来？ | `examples/static_ip_example/README.md` | 优先走 `wifi_scan`、`wifi_sta`、`static_ip` |
| 怎么做配网、保存凭据、多 AP 测试？ | `examples/wifi_netcfg_multiap_example/README.md` | 走 `sc_test` 和 `smartcfg` |
| 怎么做 BLE 配网测试？ | `apps/ai_sys/coze/README.md` | 这是产品级路径，通常依赖手机端协议和产品状态机 |
| 怎么做驱动基础测试？ | `drivers/README.md` | 第一次上手先跑低风险 `ut_*` 命令 |
| 我该从哪里找代码？ | `apps/README.md`、`components/README.md`、`PS/README.md` | 先按任务索引选大类，再进子目录 README |

## 顶层目录导航

- `apps/`：应用域和客户/产品功能入口。先看 `apps/README.md`。
- `Boards/`：板级目录、`defconfig`、板级 `makefile`、板型资源。先看 `Boards/README.md`。
- `components/`：通用中间件和功能组件。先看 `components/README.md`。
- `dbg_tools/`：调试、构建辅助、Matter 联调和 ramdump 工具集合。先看 `dbg_tools/README.md`。
- `drivers/`：外设驱动和 HAL 实现。先看 `drivers/README.md`。
- `include/`：对外导出的公共头文件入口。先看 `include/README.md`。
- `libs/`：预编译库和历史库说明。先看 `libs/README.md`。
- `os/`：RTOS 与 OS 适配层。先看 `os/README.md`。
- `PS/`：Wi-Fi/BLE 协议栈源码。先看 `PS/README.md`。
- `scripts/`：构建工具、Kconfig 工具、OTA 工具。先看 `scripts/README.md`。
- `examples/`：示例工程入口。先看 `examples/README.md`。
- `doc/`：专题资料和用户手册。先看 `doc/README.md`。
- `arch/`：CPU 架构相关实现。先看 `arch/README.md`。
- `readme.txt`：历史根说明，保留为旧入口，不再作为主导航文件。

## 任务索引

| 任务 | 第一入口 | 第二跳 | 关键配置/文件 |
| --- | --- | --- | --- |
| 编译某个板型或产品版本 | `Boards/README.md` | `Boards/ecr6600/README.md` | `defconfig`、`makefile`、`generated/.config` |
| 修改产品业务逻辑 | `apps/README.md` | `apps/ai_sys/README.md` | `Make.defs`、`*_main.c`、产品子目录 |
| 查共享状态机或公共产品能力 | `apps/ai_sys/common/README.md` | `apps/ai_sys/common/state_machine_v2/README.md` | 状态机动作、事件、共享 manager |
| 做第一次基础联网测试 | `examples/README.md` | `examples/static_ip_example/README.md` | `wifi_scan`、`wifi_sta`、`static_ip` |
| 做 Wi-Fi 配网/多 AP/凭据保存测试 | `examples/README.md` | `examples/wifi_netcfg_multiap_example/README.md` | `sc_test`、`smartcfg`、NV 凭据 |
| 做驱动基础单测 | `drivers/README.md` | `Boards/ecr6600/standalone/README.md` | `CONFIG_DRV_UNIT_TEST`、`ut_*` CLI |
| 做产品级 BLE 配网排查 | `apps/ai_sys/coze/README.md` | `apps/ai_sys/coze/ble_adpt/ble_adpt.md` 或 `apps/ai_sys/coze/wifi_adpt/wifi_adpt.md` | 手机端协议、产品状态机、BLE/Wi-Fi 适配 |
| 查 OTA | `components/README.md` | `components/ota/README.md` | 模块 `Kconfig`、板级打包脚本 |
| 查联网、配网、Wi-Fi 控制 | `components/README.md` | `components/wifi_ctrl/README.md` 或 `components/wifi_netcfg/README.md` | `CONFIG_SYSTEM_WIFI_CTRL`、配网示例 |
| 查 MQTT、WebSocket、HTTP 云接入基础层 | `components/README.md` | `components/mqtt/README.md`、`components/websocket_client/README.md`、`components/http_client/README.md` | `CONFIG_MQTT`、`MQTT_ENABLE_WS`、`MBEDTLS` |
| 查 Wi-Fi/BLE 协议栈内部实现 | `PS/README.md` | `PS/wifi/README.md` 或 `PS/ble/README.md` | `PS/wifi/ecr6600/Kconfig`、`rf/`、Host/Controller |
| 查板级资源或生成配置 | `Boards/README.md` | 具体板级目录 README | `generated/config.h`、局部 `Kconfig` |
| 查预编译库或链接问题 | `libs/README.md` | 对应调用方目录 README | 链接脚本、库名、依赖方向 |
| 查调试、Matter 联调、ramdump 或打包工具 | `dbg_tools/README.md` | 对应工具子目录 README | 工具脚本、产物路径、辅助配置 |

## 检索锚点

如果是人为搜索、AI 检索或脚本索引，建议优先使用这些稳定锚点：

- 构建锚点：`BOARD_DIR`、`APP_NAME`、`defconfig`、`generated/.config`、`generated/config.h`
- 装配锚点：`Make.defs`、`Kconfig`
- 高频产品或子域关键词：`coze`、`joyinside`、`tomcat`、`matter`
- 高频组件关键词：`wifi_netcfg`、`wifi_ctrl`、`mqtt`、`ota`、`platform_api`
- 调试与故障关键词：`ramdump`、`Matter`、`autobuild`

## 已有深层文档

这些目录已经有较完整的专题文档，建议从父级 README 跳转进入：

- `apps/aws/README.md`
- `apps/ai_sys/gx8006/README.md`
- `apps/ai_sys/common/state_machine_v2/README.md`
- `examples/static_ip_example/README.md`
- `examples/wifi_netcfg_multiap_example/README.md`
- `scripts/bin/trstool/README.txt`

## README 入口治理

- 标准入口覆盖清单见 [doc/readme_entry_coverage.md](./doc/readme_entry_coverage.md)。
- README 分层规则、维护约定和治理边界见 [doc/readme_governance.md](./doc/readme_governance.md)。
- 根 README 只保留“第一次上手路径、任务索引、检索锚点和关键下一跳”，避免继续膨胀成维护细则手册。
