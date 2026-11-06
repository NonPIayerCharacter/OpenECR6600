# scripts/bin

## 目录职责

`scripts/bin/` 保存构建和交付过程中使用的命令行工具。

## 什么时候从这里进入

- 需要确认某个构建动作到底依赖了哪个工具。
- 需要查 Kconfig、单板构建、OTA 打包或 TRS 工具。

## 关键内容

- `kconfig_tool/`：`menuconfig`、`defconfig`、`genconfig` 等 Kconfig 工具
- `makeone_tool/`：单独构建相关工具
- `ota_tool/`：OTA 相关工具
- `trstool/`：TRS 工具，目录内已有 `README.txt`

## 与构建/配置的关系

- 根 `Makefile` 会直接调用 `kconfig_tool`。
- 板级目录中的某些打包或发布流程会依赖 `ota_tool` 等工具。

## 推荐下一步

- Kconfig 构建链：回到 `../../README.md`
- Kconfig 工具：看 `kconfig_tool/README.md`
- OTA 工具：看 `ota_tool/README.md`
- Makeone 工具：看 `makeone_tool/README.md`
- TRS 工具说明：看 `trstool/README.txt`
