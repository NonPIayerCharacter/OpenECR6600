# components/wifi_netcfg

## 目录职责

`components/wifi_netcfg/` 提供公共配网管理能力，是仓库内配网流程的组件层入口。

## 什么时候从这里进入

- 需要排查 Wi-Fi 配网状态流转。
- 需要定位配网管理器、适配层和端口接口的边界。
- 需要复用或扩展公共配网能力。

## 关键内容

- `wifi_netcfg_manager.c`：配网主流程管理
- `wifi_netcfg_adapter.c`：与外部事件/模块之间的适配
- `include/wifi_netcfg_manager.h`：管理器接口
- `include/wifi_netcfg_adapter.h`：适配层接口
- `include/wifi_netcfg_port.h`：端口相关接口

## 与构建/配置的关系

- 这里是公共组件层。
- 示例工程位于 `../../examples/wifi_netcfg_multiap_example/`。
- 应用层如需改配网行为，建议先确认是否应改在公共管理器，而不是各产品各改一份。

## 推荐下一步

- 示例入口：看 `../../examples/README.md`
- 协议栈下层：看 `../../PS/wifi/README.md`
