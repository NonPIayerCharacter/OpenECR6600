# components

## 目录职责

`components/` 保存可复用的功能组件和中间件，是应用层与底层协议栈/驱动之间最常见的公共能力层。

## 什么时候从这里进入

- 需要定位跨多个应用复用的功能模块。
- 需要判断某个能力应放在 `components` 还是 `apps`。
- 需要查网络、安全、OTA、音频、存储等公共能力实现。

## 常见误入

- 如果你在找客户逻辑、产品状态机、云业务流程或产品 UI，请先去 `../apps/README.md`。
- 如果你在找 Wi-Fi/BLE 协议栈内部实现，而不是共享控制层，请先去 `../PS/README.md`。
- 如果你在找具体板型的 `defconfig`、板级脚本或板级资源，请先去 `../Boards/README.md`。

## 关键内容

- 网络与连接：
  `lwip/`、`mqtt/`、`http_client/`、`http_server/`、`websocket_client/`、`transport_tcp/`、`wifi_ctrl/`、`wifi_mesh/`、`wifi_netcfg/`、`smartconfig/`、`sntp/`、`iperf3/`
- 安全与存储：
  `mbedtls/`、`nv/`、`ota/`、`platform_api/`、`fs/`
- 音频与媒体：
  `codec/`、`opus/`、`voice_chip/`
- 设备与协议：
  `matter/`、`modbus/`、`nfc/`
- 工具与服务：
  `cmd/`、`cjson/`、`url_parser/`、`web_config/`、`webserver_config/`、`health_monitor/`

## 与构建/配置的关系

- `components/` 通常通过根 `Make.defs`、板级 `defconfig` 和模块 Kconfig 被选择性编译。
- 如果同一个功能同时被多个应用使用，通常优先在这里落地，而不是复制到各个 `apps/` 子目录。

## 检索锚点

- 构建锚点：`Make.defs`、模块 `Kconfig`
- 高频配置键：`CONFIG_MQTT`、`CONFIG_MBEDTLS`、`CONFIG_HTTPSERVER`、`CONFIG_SYSTEM_WIFI_CTRL`、`CONFIG_WIFI_SMARTCONFIG`、`CONFIG_OTA`
- 高频入口组件：`wifi_ctrl`、`wifi_netcfg`、`mqtt`、`ota`、`platform_api`、`lwip`
- 公共头文件出口：`include/components/`

## 推荐下一步

- OTA：看 `ota/README.md`
- 平台配置：看 `platform_api/README.md`
- TLS 基础层：看 `mbedtls/README.md`
- MQTT：看 `mqtt/README.md`
- 配网：看 `wifi_netcfg/README.md`
- SmartConfig：看 `smartconfig/README.md`
- Matter：看 `matter/README.md`
- NV：看 `nv/README.md`
- 编解码：看 `codec/README.md`
- 语音芯片抽象：看 `voice_chip/README.md`
- lwIP：看 `lwip/README.md`
- HTTP Client：看 `http_client/README.md`
- HTTP Server：看 `http_server/README.md`
- WebSocket Client：看 `websocket_client/README.md`
- Wi-Fi 控制：看 `wifi_ctrl/README.md`
- Wi-Fi Mesh：看 `wifi_mesh/README.md`
- 应用调用方：看 `../apps/README.md`
