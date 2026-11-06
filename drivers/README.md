# drivers

## 目录职责

`drivers/` 保存底层外设驱动和 HAL 层实现，是硬件访问路径的主要入口之一。

## 什么时候从这里进入

- 需要排查具体外设驱动问题。
- 需要区分通用组件问题和底层驱动问题。
- 需要看 HAL、驱动实现和驱动单测。

## 关键内容

- `drivers/`：具体驱动实现
- `hal/`：硬件抽象层
- `unit_test/`：驱动相关测试

## 与构建/配置的关系

- 板级目录和组件层通常依赖这里的底层能力。
- 如果问题已经定位到引脚、总线、寄存器或外设时序，优先进入这里。
- CLI 驱动单测通常由 `drivers/unit_test/` 提供，是否可用取决于板级 `defconfig` 是否打开 `CONFIG_DRV_UNIT_TEST` 及对应子项。

## 第一次上手怎么用这里

如果你是第一次把 ECR6600 工程跑起来，不建议一进来就逐个看驱动源码。更好的顺序是：

1. 先用 `Boards/ecr6600/standalone/` 把基础镜像跑起来。
2. 先确认 `help`、`wifi_scan`、`wifi_sta` 正常。
3. 再把 `drivers/` 当成“驱动 CLI 自测说明入口”和“驱动源码入口”。

## 适合新手先跑的低风险命令

下面这些命令更适合作为第一次驱动自测：

- `ut_rtc get` 或 `ut_rtc set <unix-seconds>`：验证 RTC 读写。
- `ut_timer create`、`ut_timer set 0 1000`、`ut_timer start 0`、`ut_timer stop 0`：验证基础定时器路径。
- `ut_trng trng`：验证随机数输出。
- `ut_adc vbat`、`ut_adc temp_test`：验证片上 ADC 电压/温度采样。
- `ut_flash status`：只读查询 flash 状态，不改内容。
- `ut_aes enc ...`、`ut_hash sha256`：在明确命令格式后，可做纯算法类验证。

这些命令的共同特点是：不依赖复杂外设接线，或者默认是只读/内存内验证，更适合拿来区分“CLI 不通”“驱动未使能”“板级硬件异常”这几类问题。

## 需要明确警告的高风险命令

第一次上手时，不建议直接运行下面这些命令：

- `ut_flash erase`、`ut_flash write`、`ut_flash write_dma`、`ut_flash flash_random_write`
- `ut_flash otpwrite`、`ut_flash otperase`
- `ut_efuse write`
- `ut_gpio write`、`ut_gpio interrupt*`
- `ut_pwm *`、`ut_uart *`
- 会主动触发复位或超时的 `ut_wdt` 测试

原因如下：

- 可能直接修改 flash / OTP / eFuse 等不可逆或关键存储内容。
- 可能依赖特定 GPIO、PWM、UART、SPI 接线，而 README 无法替代实际原理图。
- 某些失败现象是板级连接问题，不是驱动代码问题。

如果你的目标是“确认 SDK 没问题”，优先先做低风险命令；如果目标是“验证某个外设板卡”，请先把对应板级连接关系和测试回路说明补齐，再使用这些命令。

## 推荐下一步

- 板级绑定：看 `../Boards/README.md`
- 对外头文件：看 `../include/README.md`
