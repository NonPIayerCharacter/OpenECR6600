# components/wifi_mesh

## 目录职责

`components/wifi_mesh/` 提供仓库共享的 Wi-Fi Mesh 组网能力，负责 Mesh 配置、邻居和父节点选择、Beacon 数据封装、加密、状态机和存储。

## 什么时候从这里进入

- 需要定位 Mesh 建网、入网、父节点切换、Beacon 数据或投票策略问题。
- 需要判断问题在共享 Mesh 组件，还是在底层 Wi-Fi 协议栈支持层。
- 需要查看 Mesh 配置和运行时状态如何与系统 Wi-Fi 控制层联动。

## 关键内容

- `mesh_main.c`：Mesh 主上下文与状态管理入口。
- `mesh_config.c`、`mesh_storage.c`：配置装载与持久化。
- `mesh_vote.c`、`mesh_table.c`：候选节点评估、表管理和切换策略。
- `mesh_rx.c`、`mesh_tx.c`：Vendor IE 和 Beacon 数据收发。
- `mesh_aes.c`：Mesh 加密封装。
- `wifi_mesh.h`：对外接口。

## 与构建/配置的关系

- 由 `CONFIG_WIFI_MESH` 控制启用，并选择 `LWIP_NAPT` 与 `IPV6`。
- `Make.defs` 直接依赖 `wifi_crtl`、`lwip`、`wpa`，说明它建立在公共 Wi-Fi 控制层之上。
- `mesh_vote.c` 还会复用 `PS/wifi/ecr6600/modules/wifi_mesh/mesh_calc_rssi.c` 这类平台侧能力，因此 Mesh 问题往往要同时评估组件层与协议栈层。
- 如果只是普通 STA 或 AP 联网问题，不建议从这里开始排查。

## 推荐下一步

- Wi-Fi 控制组件：看 `../wifi_ctrl/README.md`
- ECR6600 Wi-Fi 实现：看 `../../PS/wifi/ecr6600/README.md`
- Wi-Fi 协议栈总览：看 `../../PS/wifi/README.md`
- 组件总览：回到 `../README.md`
