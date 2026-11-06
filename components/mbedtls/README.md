# components/mbedtls

## 目录职责

`components/mbedtls/` 提供仓库统一的 TLS/DTLS 加密能力，是 `http_client`、`mqtt`、`websocket_client` 等上层网络组件使用的安全基础层。

## 什么时候从这里进入

- 需要定位 TLS 握手、证书校验、套接字加密传输或随机熵问题。
- 需要判断某个联网组件是卡在协议逻辑还是卡在加密层。
- 需要调整 TLS 1.2、TLS 1.3、DTLS、调试等级、客户端或服务端编译开关。

## 关键内容

- `mbedtls/`：上游 mbedTLS 源码与算法实现。
- `port/`：ECR6600 适配层，包含 `trs_config.h`、网络封装、内存和熵源适配。
- `Kconfig`：TLS 版本、DTLS、调试等级、客户端/服务端能力选择。
- `Make.defs`：统一配置头、包含路径和实际编译源列表。

## 与构建/配置的关系

- 由 `CONFIG_MBEDTLS` 控制启用，并依赖 `LWIP`。
- 编译时通过 `-DMBEDTLS_CONFIG_FILE` 指向 `port/include/mbedtls/trs_config.h`。
- `mqtt`、`websocket_client`、部分 HTTP/TLS 场景会直接依赖这里，不建议在应用目录重复实现证书或握手逻辑。

## 推荐下一步

- MQTT：看 `../mqtt/README.md`
- WebSocket Client：看 `../websocket_client/README.md`
- HTTP Client：看 `../http_client/README.md`
- 组件总览：回到 `../README.md`
