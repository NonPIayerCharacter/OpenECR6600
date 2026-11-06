# os

## 目录职责

`os/` 保存操作系统实现和 OS 适配层，是应用代码与底层 RTOS 之间的桥梁。

## 什么时候从这里进入

- 需要看任务、队列、定时器、同步原语等 OS 行为。
- 需要确认上层应该直接依赖 FreeRTOS 还是通过抽象层访问。
- 需要排查 RTOS 适配问题或 PSRAM 相关 OS 支撑。

## 关键内容

- `freertos/`：FreeRTOS 相关实现与内核代码
- `os_hal/`：统一 OS 抽象层
- `os_psram/`：PSRAM 相关 OS 支撑

## 与构建/配置的关系

- 根 `Kconfig` 和板级配置决定实际启用的 OS 实现。
- 上层代码一般应优先通过 `os_hal` 访问通用 OS 能力，以降低与具体 RTOS 的耦合。

## 推荐下一步

- 统一抽象层：看 `os_hal/README.md`
- 对外头文件：看 `../include/README.md`
