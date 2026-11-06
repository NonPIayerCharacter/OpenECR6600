# scripts

## 目录职责

`scripts/` 保存构建辅助脚本和工具配置，是根 `Makefile` 的工具层入口。

## 什么时候从这里进入

- 需要看 Kconfig 工具、构建脚本、OTA 工具或单独的辅助命令。
- 需要理解根 `Makefile` 调用了哪些脚本工具。

## 关键内容

- `Config.mk`：构建配置辅助
- `CMarcoDefine.mk`：宏定义相关辅助
- `bin/`：可执行工具集合

## 与构建/配置的关系

- 根 `Makefile` 中的 `menuconfig` 会调用 `scripts/bin/kconfig_tool/`。
- 这里偏工具层，不承载具体产品逻辑。

## 推荐下一步

- 工具集合：看 `bin/README.md`
- 根构建入口：回到 `../README.md`
