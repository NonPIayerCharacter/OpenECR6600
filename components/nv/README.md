# components/nv

## 目录职责

`components/nv/` 是 NV 和 EasyFlash 持久化能力的组件入口。

## 什么时候从这里进入

- 需要定位配置存储、持久化、参数落盘或升级保留相关问题。
- 需要区分通用 NV 逻辑、端口适配和类型插件。

## 关键内容

- `src/`：EasyFlash 相关核心实现
- `port/`：平台适配层，例如 `ef_port.c`
- `plugins/`：类型和序列化插件，目录内已有局部 README
- `Kconfig`：NV 组件配置
- `Make.defs`：组件编译入口

## 与构建/配置的关系

- 组件由 `CONFIG_NV=y` 选通。
- `Make.defs` 会编译 `easyflash.c`、`ef_env.c`、`ef_iap.c`、`ef_log.c`、`ef_utils.c` 和端口层 `ef_port.c`。
- 板级和应用层的参数保存通常会依赖这里。

## 推荐下一步

- 平台配置：看 `../platform_api/README.md`
- 板级配置与生成：看 `../../Boards/README.md`
- 插件说明：看 `plugins/types/README.md`
