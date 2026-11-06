# components/smartconfig

## 目录职责

`components/smartconfig/` 提供基于空口报文解析的 SmartConfig 配网能力，负责广播和探测数据解析、通知回调以及配网辅助信息整理。

## 什么时候从这里进入

- 需要定位 SmartConfig 配网失败、空口数据解析异常或通知链路问题。
- 需要判断问题是在 SmartConfig 算法层，还是在 Wi-Fi 监听、事件上报或产品配网流程。
- 需要查看 SmartConfig 与 `wifi_netcfg` 的边界。

## 关键内容

- `smartconfig.c`：SmartConfig 主流程。
- `smartconfig_notify.c`：结果通知与事件上报。
- `zconfig_ieee80211.c`、`zconfig_utils.c`：802.11 报文解析与辅助工具。
- `awss_aplist.c`：AP 列表辅助处理。
- `smartconfig.h`：对外接口。

## 与构建/配置的关系

- 由 `CONFIG_WIFI_SMARTCONFIG` 控制启用。
- 本目录本身只编译解析与通知逻辑，实际监听能力仍依赖 `wifi_ctrl` 和更下层的 `PS/wifi/` 协议栈支持。
- 如果只是通用配网状态机或产品侧交互问题，优先看 `wifi_netcfg` 或应用目录，而不是直接改这里。

## 推荐下一步

- 配网组件：看 `../wifi_netcfg/README.md`
- Wi-Fi 控制组件：看 `../wifi_ctrl/README.md`
- Wi-Fi 协议栈总览：看 `../../PS/wifi/README.md`
- 组件总览：回到 `../README.md`
