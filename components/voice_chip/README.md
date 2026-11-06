# components/voice_chip

## 目录职责

`components/voice_chip/` 提供共享的语音芯片抽象层，负责把板级音频器件能力整理成统一接口，当前覆盖 ES8311、GX8006 和 CI1302 的播放、采集及芯片扩展能力。

## 什么时候从这里进入

- 需要定位音量、输入增益、打开关闭、播放和采集运行态等公共音频器件问题。
- 需要判断问题是在共享语音芯片抽象层，还是在产品音频流程、板级引脚或 I2S 初始化层。
- 需要扩展新的语音芯片后端，或复用已有 ES8311 运行时能力。

## 关键内容

- `core/voice_chip_core.c`：统一接口分发和通用调用封装。
- `core/voice_chip_service.c`：面向应用的统一服务层，通过操作表间接调用当前芯片后端。
- `chips/es8311/`：ES8311 设备实现、运行时、ringbuffer 和 service 封装。
- `chips/gx8006/`：GX8006 设备实现、smartbot 协议栈和 service 封装。
- `chips/ci1302/`：CI1302 UART/PCM 协议、适配、service、CLI 和测试源码。
- `Kconfig`：`VOICE_CHIP` 总开关和非 optional `VOICE_CHIP_BACKEND` 单选；启用总开关且未指定成员时默认 ES8311。
- `chips/*/Make.defs`：每个后端独立归档为 `libpscomponents_voicechip_<chip>.a`；公共 core 仍进入 `libcomponents.a`。
- `include/components/voice_chip/`：对外抽象接口与运行时头文件。

## 与构建/配置的关系

- 由 `CONFIG_VOICE_CHIP` 控制启用；`CONFIG_VOICE_CHIP_ES8311`、`CONFIG_VOICE_CHIP_GX8006`、`CONFIG_CI1302` 通过 `VOICE_CHIP_BACKEND` choice 单选。
- 应用 Kconfig 只应 `select VOICE_CHIP`，不能 `select` choice 成员；Board/示例 defconfig 可直接写后端成员。
- ES8311/GX8006 在 SDK 中以对应预编译库交付并移除私有源码；CI1302 保留源码，独立库只作为内部构建和 SDK 回编产物，不复制到 SDK 交付 `libs`。
- Coze 与 JoyInside 已经直接依赖 `voice_chip_runtime.h` 和共享头文件，因此这里更适合放公共器件行为，不适合夹带产品状态机。
- 如果问题涉及 I2S、GPIO、板级电源时序或实际接线，通常还要结合板级目录一起看。

## 推荐下一步

- Coze 产品入口：看 `../../apps/ai_sys/coze/README.md`
- JoyInside 产品入口：看 `../../apps/ai_sys/joyinside/README.md`
- Coze 板级入口：看 `../../Boards/ecr6600/coze/README.md`
- JoyInside 板级入口：看 `../../Boards/ecr6600/joyinside/README.md`
- 组件总览：回到 `../README.md`
