# components/wifi_ctrl

## 目录职责

`components/wifi_ctrl/` 是系统侧 Wi-Fi 控制与事件汇聚层，承担网络初始化、连接流程、事件分发、系统接口封装，以及上层组件到 `PS/wifi/` 协议栈之间的桥接。

## 什么时候从这里进入

- 需要定位连网、断网、事件分发、系统网络接口、Sniffer 或快速连接问题。
- 需要判断问题是在公共 Wi-Fi 控制层，还是在 `PS/wifi/` 协议栈内部。
- 需要看上层配网组件为什么能触发底层动作，或者底层事件如何回到系统层。

## 常见误入

- 如果你已经确认问题落在 Wi-Fi 协议栈内部，例如 MAC、UMAC、RF 或平台实现，请继续去 `../../PS/wifi/README.md`。
- 如果你在找板级 `defconfig`、板型资源或构建脚本，请先去 `../../Boards/README.md`。
- 如果你在找产品侧配网交互、业务重连策略或云逻辑，请先去产品目录或 `../wifi_netcfg/README.md`。

## 关键内容

- `system_wifi.c`、`system_network.c`：系统 Wi-Fi 与网络管理入口。
- `event_loop.c`、`event_default_handlers.c`：事件循环与默认处理。
- `system_lwip.c`：与 `lwip` 的系统侧接入。
- `wifi_conn.c`、`wifi_config.c`：连接和配置流程。
- `wifi_sniffer.c`、`wifi_ps.c`：抓包监听与省电相关能力。
- `include/components/wifi_crtl/`：公共头文件与系统事件定义。

## 与构建/配置的关系

- 由 `CONFIG_SYSTEM_WIFI_CTRL` 控制，默认启用。
- `Kconfig` 提供日志等级、接收缓冲区、AMPDU 和控制层日志开关；`FAST_CONNECT` 会影响连接行为。
- `Make.defs` 直接依赖 `lwip`、`wpa` 与 `PS/wifi/ecr6600/modules`，说明这里是组件层和协议栈层的关键边界。
- `wifi_netcfg`、`smartconfig` 等公共组件一般从这里进入 Wi-Fi 能力，而不是直接操作 `PS/wifi/` 内部实现。

## 检索锚点

- 高频配置键：`CONFIG_SYSTEM_WIFI_CTRL`、`FAST_CONNECT`、`WIFI_CTRL_LOG_LEVEL`
- 核心实现：`system_wifi.c`、`system_network.c`、`event_loop.c`、`wifi_conn.c`
- 对外接口：`system_wifi.h`、`system_event.h`、`system_network.h`
- 下钻边界：`PS/wifi/ecr6600/`

## 推荐下一步

- 配网组件：看 `../wifi_netcfg/README.md`
- SmartConfig：看 `../smartconfig/README.md`
- Wi-Fi 协议栈总览：看 `../../PS/wifi/README.md`
- ECR6600 Wi-Fi 实现：看 `../../PS/wifi/ecr6600/README.md`
- 组件总览：回到 `../README.md`
