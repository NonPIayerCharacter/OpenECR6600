# Boards

## 目录职责

`Boards/` 是板级构建入口目录，负责承载板型 `defconfig`、板级 `makefile`、板级资源和与具体板型绑定的入口代码。

## 什么时候从这里进入

- 需要编译某个板型或产品版本。
- 需要确认 `defconfig`、板级 Kconfig、板级资源和板级脚本。
- 需要定位构建产物与 `generated` 配置目录。

## 常见误入

- 如果你在改产品业务逻辑或客户功能，而不是板级装配，请先去 `../apps/README.md`。
- 如果你在找共享网络、OTA、平台接口或语音抽象，请先去 `../components/README.md`。
- 如果你已经确认问题落在 Wi-Fi/BLE 协议栈内部，而不是板级配置，请先去 `../PS/README.md`。

## 关键内容

- `ecr6600/`：当前仓库的主要板级集合。

## 新手如何选板级入口

如果你是第一次进入 `Boards/`，建议先按目标选择入口，而不是直接从某个产品目录开始：

- 先验证工具链、下载链路、启动日志：优先 `ecr6600/standalone/`
- 需要编译具体产品版本：进入对应的 `ecr6600/<product>/`
- 需要最小功能自测：先确认 `standalone` 可启动，再进入 `../examples/` 中对应示例

这样做的原因是：`standalone` 更接近公共基线，适合先排除环境、基础镜像和串口 bring-up 问题；产品板型再负责叠加具体业务配置。

## 与构建/配置的关系

- 推荐从具体板级目录进入构建，例如 `Boards/ecr6600/coze/`。
- 每个板级目录一般包含 `defconfig`、局部 `makefile`、`Kconfig`、必要脚本和资源。
- 构建时会在板级目录下生成 `generated/`，不要手工长期维护其中的生成文件。

## 检索锚点

- 板级配置：`defconfig`
- 构建与装配：`makefile`、局部 `Kconfig`
- 生成配置：`generated/.config`、`generated/config.h`
- 根构建变量：`BOARD_DIR`、`APP_NAME`
- 当前板族入口：`ecr6600/`

## 推荐下一步

- ECR6600 板型入口：看 `ecr6600/README.md`
- 根构建逻辑：回到 `../README.md`
