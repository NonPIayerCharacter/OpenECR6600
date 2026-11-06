# scripts/bin/kconfig_tool

## 目录职责

`scripts/bin/kconfig_tool/` 保存 Kconfig 相关可执行工具，是 `make menuconfig`、配置生成和导出头文件的工具入口。

## 当前内容

- `genconfig`、`genconfig.exe`
- `menuconfig.exe`
- `menuconfig_ubuntu16_18`
- `menuconfig_ubuntu20_22`

## 什么时候从这里进入

- 需要确认 `menuconfig` 实际调用了哪个可执行文件。
- 需要排查配置生成、平台差异或主机环境兼容问题。

## 与构建/配置的关系

- 根 `Makefile` 的 `menuconfig` 会根据主机环境调用这里的不同工具。
- `genconfig` 用于从 `.config` 生成 `config.h`。

## 推荐下一步

- 根构建入口：看 `../../../README.md`
- 脚本总览：回到 `../README.md`
