# components/websocket_client

## 目录职责

`components/websocket_client/` 提供仓库统一的 WebSocket 客户端能力，负责握手、帧收发以及 `ws://` / `wss://` 连接管理，不负责产品侧会话协议和业务消息解释。

## 什么时候从这里进入

- 需要定位 WebSocket 握手失败、帧发送接收异常、断线重连或 `wss` 加密链路问题。
- 需要判断问题出在共享客户端组件还是产品云适配逻辑。
- 需要查看 WebSocket 与 `http_client`、`mbedtls`、`trs-tls` 的耦合边界。

## 关键内容

- `websocket_client.c`：客户端主实现，包含握手和数据帧处理。
- `include/websocket_client.h`：对外接口。
- `websocket_example.c`：最小示例与调试参考。
- `Kconfig`、`Make.defs`：编译开关和依赖关系。

## 与构建/配置的关系

- 由 `CONFIG_WEB_SOCKET_CLIENT` 控制启用，并选择 `TRANSPORT_TCP` 与 `URLPARSER`。
- `Make.defs` 同时引入 `mbedtls`、`trs-tls` 与 `http_client/libhttp`，说明 `wss` 与 HTTP 握手能力都依赖共享基础层。
- 如果问题只发生在 Coze 或 JoyInside 的某个云协议流程，优先继续看产品目录，而不是在这里改业务状态机。

## 推荐下一步

- TLS 基础层：看 `../mbedtls/README.md`
- HTTP Client：看 `../http_client/README.md`
- Coze 云接入：看 `../../apps/ai_sys/coze/README.md`
- JoyInside 云接入：看 `../../apps/ai_sys/joyinside/README.md`
- 组件总览：回到 `../README.md`
