# Boards/ecr6600/common

## 目录职责

`Boards/ecr6600/common/` 保存 ECR6600 各板型共享的板级支撑代码和资源。

## 什么时候从这里进入

- 需要看芯片启动、时钟、引脚复用、链接脚本。
- 需要排查多个板型共用的板级问题。
- 需要确认公共 `common_bin` 或 `psm` 资源的来源与切换逻辑。

## 关键内容

- 启动与芯片初始化：
  `chip_entry.S`、`chip_startup.c`、`chip_clk_ctrl.c`、`chip_pin_mux.c`、`chip_config.c`
- 版本信息：
  `sdk_version.c`、`release_version.c`
- 链接脚本：
  `ld_ram.script.S`、`ld_xip.script.S` 及变体脚本
- `common_bin/`：公共二进制资源目录
- `psm/`：与 PSM 相关的公共板级配置
- `include/`、`cpp/`：公共头文件和辅助实现

## 与构建/配置的关系

- 具体板型目录会复用这里的公共源码、公共二进制和链接脚本。
- 根 `Makefile` 中的 `setup_environment` 会根据配置切换 `common_bin` 中的资源。
- 这里的改动通常会影响多个板型，不建议只按单产品视角修改。

## 推荐下一步

- 板型选择：回到 `../README.md`
- 协议栈或组件问题：看 `../../../PS/README.md` 或 `../../../components/README.md`
