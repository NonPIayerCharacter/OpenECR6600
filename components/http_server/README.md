# components/http_server

## 目录职责

`components/http_server/` 提供仓库统一的嵌入式 HTTP Server 能力，负责请求解析、会话管理、URI 路由和可选的 WebSocket Server 支持。

## 什么时候从这里进入

- 需要定位本地 Web 配置页、HTTP 接口、URI 注册或请求收发异常。
- 需要排查问题是发生在共享 HTTP Server 框架，还是在具体页面和业务处理函数。
- 需要启用 WebSocket Server，或调整请求头和 URI 长度等服务端能力参数。

## 关键内容

- `src/httpd_main.c`：服务主循环、监听与任务控制。
- `src/httpd_parse.c`、`src/http_parser.c`：HTTP 报文解析。
- `src/httpd_sess.c`、`src/httpd_txrx.c`：会话和收发通道管理。
- `src/httpd_uri.c`：URI 注册与匹配。
- `src/httpd_ws.c`：WebSocket Server 握手和帧处理。
- `Kconfig`、`Make.defs`：编译开关和缓冲区配置。

## 与构建/配置的关系

- 由 `CONFIG_HTTPSERVER` 控制启用，可通过 `HTTPD_MAX_REQ_HDR_LEN`、`HTTPD_MAX_URI_LEN`、`HTTPD_WS_SUPPORT` 等选项调整能力边界。
- `components/web_config/` 与 `components/webserver_config/` 这类目录通常是它的调用方；如果 URI 业务逻辑有问题，优先去调用方看具体 handler。
- 如果只是设备联网或云连接问题，通常不应从这里开始排查。

## 推荐下一步

- WebSocket Client：看 `../websocket_client/README.md`
- 配网组件：看 `../wifi_netcfg/README.md`
- Wi-Fi 控制组件：看 `../wifi_ctrl/README.md`
- 组件总览：回到 `../README.md`
