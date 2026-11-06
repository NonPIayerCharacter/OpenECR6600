# components/lwip

## 目录职责

`components/lwip/` 是 lwIP 网络协议栈的组件包装入口。

## 什么时候从这里进入

- 需要确认仓库如何集成 lwIP。
- 需要排查 IPv4/IPv6、NAPT、dhcpserver、mdns 等网络栈问题。
- 需要区分本地端口层和 lwIP 上游源码子树。

## 关键内容

- `contrib/port/`：本地移植和端口层
- `lwip-2.1.0/`：lwIP 上游源码子树，目录内已有上游 README
- `Kconfig`：lwIP 组件配置
- `Make.defs`：lwIP 编译聚合入口

## 与构建/配置的关系

- 组件由 `CONFIG_LWIP=y` 选通。
- `Make.defs` 会编译本地 `sys_arch.c` 和 lwIP 源码。
- `CONFIG_LWIP_NAPT`、`CONFIG_IPV6` 等配置会改变编译范围。

## 推荐下一步

- HTTP Client：看 `../http_client/README.md`
- 配网组件：看 `../wifi_netcfg/README.md`
- 协议栈底层：看 `../../PS/wifi/README.md`
