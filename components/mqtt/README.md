# components/mqtt

## 目录职责

`components/mqtt/` 提供仓库统一的 MQTT 客户端实现，负责连接管理、报文编解码、收发线程和离线消息队列，不承载具体产品的云业务状态机。

## 什么时候从这里进入

- 需要定位 MQTT 连接、订阅、发布、重连、KeepAlive 或 outbox 积压问题。
- 需要判断问题出在共享 MQTT 组件还是产品侧云适配逻辑。
- 需要启用 MQTT over TLS、WS/WSS 或 MQTT 5 能力。

## 常见误入

- 如果你在改云鉴权、主题组织、业务消息语义或产品状态机，请先去对应产品目录，而不是直接改这里。
- 如果你在追 TLS 握手、证书、随机熵或底层加密问题，请先去 `../mbedtls/README.md`。
- 如果你在追 WebSocket 握手而不是 MQTT 协议本身，请先去 `../websocket_client/README.md`。

## 关键内容

- `mqtt_client.c`：客户端主流程、连接状态和事件分发。
- `mqtt_msg.c`：MQTT 报文构造与解析。
- `mqtt_outbox.c`：待发送消息缓存与重试队列。
- `mqtt5_client.c`、`mqtt5_msg.c`：MQTT 5 扩展能力。
- `platform_tr6600.c`：平台内存与运行时封装。
- `include/`：对外接口和私有头文件。

## 与构建/配置的关系

- 由 `CONFIG_MQTT` 控制启用，并选择 `TRANSPORT_TCP`、`MBEDTLS`、`URLPARSER`。
- `MQTT_SSL`、`MQTT_ENABLE_WS`、`MQTT_ENABLE_WSS`、`MQTT_PROTOCOL_5` 决定传输形态和协议版本。
- Coze 与 JoyInside 的云接入都以这里作为共享 MQTT 基础层，产品鉴权、主题组织和业务状态机不应下沉到本目录。

## 检索锚点

- 高频配置键：`CONFIG_MQTT`、`MQTT_SSL`、`MQTT_ENABLE_WS`、`MQTT_ENABLE_WSS`、`MQTT_PROTOCOL_5`
- 核心实现：`mqtt_client.c`、`mqtt_outbox.c`、`mqtt5_client.c`
- 对外接口：`include/mqtt_client.h`
- 关键类型：`trs_mqtt_client_handle_t`
- 产品包装层关键词：`gizwits_mqtt`、`fanlin_mqtt`

## 推荐下一步

- TLS 基础层：看 `../mbedtls/README.md`
- WebSocket 传输：看 `../websocket_client/README.md`
- Coze 云接入：看 `../../apps/ai_sys/coze/README.md`
- JoyInside 云接入：看 `../../apps/ai_sys/joyinside/README.md`
- 组件总览：回到 `../README.md`
