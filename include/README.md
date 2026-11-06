# include

## 目录职责

`include/` 是仓库对外头文件的统一入口，方便从“接口视角”查找可用能力。

## 什么时候从这里进入

- 需要先找接口声明，再回溯到实现目录。
- 需要判断某个模块对外暴露了哪些头文件。

## 关键内容

- `ble/`：BLE 对外头文件
- `components/`：组件接口头文件
- `drivers/`：驱动接口头文件
- `os/`：OS 抽象接口
- `ota/`：OTA 接口
- `PS/`：协议栈相关接口
- `psm/`、`tx/`、`wifi/`：其他系统/连接相关接口

## 与构建/配置的关系

- `include/` 主要用于暴露接口，不代表实现位置。
- 查到接口后，通常还需要回到对应实现目录，例如 `components/`、`drivers/`、`PS/` 或 `os/`。

## 推荐下一步

- 公共组件实现：看 `../components/README.md`
- 驱动实现：看 `../drivers/README.md`
- OS 抽象：看 `../os/README.md`
