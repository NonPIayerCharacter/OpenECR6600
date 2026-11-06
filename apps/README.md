# apps

## 目录职责

`apps/` 保存应用域代码、客户功能和产品级业务实现，是“产品逻辑”最常见的进入点。

## 什么时候从这里进入

- 需要看某个产品或客户功能实现。
- 需要定位应用层初始化、业务状态流转和产品适配。
- 需要判断某个功能属于“公共组件”还是“具体产品”。

## 常见误入

- 如果你在找板级 `defconfig`、构建脚本、产物目录或 `generated` 配置，请先去 `../Boards/README.md`。
- 如果你在找共享网络、OTA、音频抽象或平台接口，请先去 `../components/README.md`。
- 如果你在找 Wi-Fi/BLE 协议栈内部实现，而不是产品调用关系，请先去 `../PS/README.md`。

## 关键内容

- `ai_sys/`：AI/语音产品域，含共享能力和多个产品方向。
- `aws/`：AWS 相关应用、库和示例，目录内已有较完整 README。
- `tencent/`、`jd/`、`qthsdk/`：客户或平台接入目录。
- `ble_apps/`、`ble_profile/`：BLE 应用和配置示例。
- `lowpower/`：低功耗相关应用。
- `spi_service/`：SPI 服务类应用入口。
- `xsl/`：特定业务目录。

## 与构建/配置的关系

- `apps/` 通常不作为第一构建入口。
- 实际编译路径通常由 `Boards/ecr6600/<board>/defconfig` 和相应 `Make.defs` 决定。
- 修改应用逻辑时，建议同时确认对应板级目录和组件依赖是否匹配。

## 检索锚点

- 板级装配：`Boards/ecr6600/<board>/defconfig`
- 应用装配：`Make.defs`
- AI/语音主域：`ai_sys/`
- 常见产品入口名：`coze_main.c`、`joyinside_main.c`、`product_test.c`
- 共享产品能力入口：`ai_sys/common/state_machine_v2/`

## 推荐下一步

- AI/语音相关：看 `ai_sys/README.md`
- 板级构建路径：看 `../Boards/README.md`
- 公共中间件依赖：看 `../components/README.md`
- AWS 方向：看 `aws/README.md`
