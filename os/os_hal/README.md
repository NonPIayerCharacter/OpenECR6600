# os/os_hal

## 目录职责

`os/os_hal/` 提供统一的 OS 抽象实现，目标是让上层尽量不直接依赖具体 RTOS。

## 什么时候从这里进入

- 需要理解上层通用 OS API 最终是如何落到具体 RTOS 的。
- 需要扩展或调整通用 OS 行为。
- 需要排查“同一套上层 API，在不同 OS 下如何适配”的问题。

## 关键文件

- `os_hal_freertos.c`：当前可见的 FreeRTOS 抽象实现
- `Make.defs`：编译入口

## 与构建/配置的关系

- `os_hal` 是上层统一入口。
- 对外头文件通常位于 `../../include/os/`。
- 若某个上层模块直接引用 FreeRTOS 原生接口，应先评估是否可以收敛到 `os_hal`。

## 推荐下一步

- OS 总览：回到 `../README.md`
- 对外接口头：看 `../../include/README.md`
