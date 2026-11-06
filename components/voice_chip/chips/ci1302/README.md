# CI1302 驱动 API 与平台接入说明

本目录从 apps/ai_sys/ci1302 迁入统一 voice_chip 框架。新应用使用公共
`voice_chip_service.h` 的服务接口，示例见
[`ci1302_example`](../../../../examples/ci1302_example/README.md)。
底层 [`ci1302_audio.h`](audio_adapt/ci1302_audio.h)、[`ci1302.h`](ci1302.h)
保留供后端和独立诊断使用，以下第 4～6 节说明其契约；不要与已启动的 service 混用。

平台继续管理原有缓存、队列、编解码、网络及状态机。实现仍依赖 ECR6600 UART/DMA
和 oshal；不是可直接运行于任意 MCU/操作系统的库。

## 1. 能力和前提

| 项目 | 当前约定 |
| --- | --- |
| 录音 / 播放 | 裸 PCM，16 kHz、signed 16-bit little-endian、单声道 |
| 数据速率 | 32000 字节/秒；PCM 分块长度应为偶数 |
| 会话 | 单芯片、一个录音消费者、一个播放生产者；先按非同时录放方式接入 |
| 支持 | 初始化、录音、PCM 播放、结束、取消、音量、状态、事件、诊断 |
| 不负责 | 格式识别、WAV 去头、重采样、Opus/MP3 解码、云端封装 |
| 暂未适配 | 芯片提示音、高级业务模式、硬件休眠、OTA |

CI1302 必须预先烧录匹配上述格式和协议的固件。6600 的配置不会修改 CI1302 固件。
SDK 存在 OTA 代码不代表本驱动提供 OTA；当前未实现升级发送流程。

## 2. 目录与构建

```text
ci1302/
├── ci1302.c / ci1302.h              芯片控制、RX 任务、事件、统计
├── porting/ci1302_uart.c / .h       ECR6600 UART/DMA 移植
├── protocol/ci1302_protocol.c / .h  协议编码、流式解析
├── audio_adapt/
│   └── ci1302_audio.c / .h          底层同步 PCM 端点
├── voice_ci1302_service.c           统一服务后端
├── cli/ci1302_cli.c                 诊断和本地测试
├── ci1302_sample_pcm.c / .h         CLI 示例资源
├── tests/                          主机侧测试
├── Make.defs
└── README.md
```

启用 `VOICE_CHIP` 后，在 `VOICE_CHIP_BACKEND` choice 中选择 `CONFIG_CI1302=y`；该成员自动选择
`UART_DMA_LOOPLIST_RX`，默认关闭。后端配置直接定义在 components/voice_chip/Kconfig，SDK 裁剪
ES8311/GX8006 私有目录后仍可解析；底层和 service 源码由本目录 `Make.defs` 加入独立归档
`libpscomponents_voicechip_ci1302.a`。该归档只用于源码工程和 SDK 回编，SDK 交付 `libs` 不复制。
原 apps 目录及编译入口已移除。CI1302 与其他芯片后端互斥选择。

`CONFIG_CI1302_CLI` 默认关闭，开启才编译旧 CLI、示例资源及其 32000 字节测试缓存。
常规 service 示例只保留自己的 32000 字节缓存。公共头位于 include/components/voice_chip。
不再构建旧 ci1302_tomcat 同名 sbot 兼容层，也不再提供旧 I2S 宏替换头。

其他构建系统需加入 ci1302.c、protocol/ci1302_protocol.c、porting/ci1302_uart.c、
audio_adapt/ci1302_audio.c、voice_ci1302_service.c 以及 voice_chip core。
源文件搜索路径按芯片目录、protocol、porting、audio_adapt 和公共 include 配置。

## 3. 串口和资源

配置位置：[`porting/ci1302_uart.c`](porting/ci1302_uart.c)。

| 参数 | 当前值 |
| --- | --- |
| 波特率 | `CONFIG_CI1302_UART_BAUDRATE`，默认 921600；Kconfig 范围 115200～2000000 |
| UART | 固定 `E_UART_NUM_1`，open/close/read/write 四处一致 |
| 引脚 | `chip_uart1_pinmux_cfg(0)`：GPIO2 TX、GPIO1 RX |
| 格式 / 流控 | 8N1，无 CTS/RTS |
| 发送 / 接收 | DMA / DMA 循环链表 |
| UART 接收缓存 | 1024 字节 |

配置允许的波特率未逐一验证，目前没有运行时修改 UART/波特率的 API。
换 UART 时须一起修改四个串口操作和 pinmux，并检查其他外设资源冲突。
已验证接线为 GPIO2 TX → CI1302 PA3 RX、GPIO1 RX ← CI1302 PA2 TX，两端共地，
当前固件使用 3.3 V 推挽接口。CI1302 UART0 的下载/日志用途由其固件和板卡决定。

协议头为 16 字节，最大 payload 为 4096 字节。同步适配另有 4096 字节接收暂存区，
用于异步 UART 到同步读取的转换，不替代平台缓存。暂存区和 TX 缓冲使用 `.dma.data` 段；
其他芯片须核对段映射、DMA 对齐和缓存一致性。CLI 原有 32000 字节录音缓存只是测试限制，
正式录音 API 没有 1 秒时长限制。

## 4. 推荐 API：同步 PCM 端点

在调度器启动后的单一初始化任务中调用 `ci1302_audio_init()`，再启动平台已有录放任务。
初始化的幂等判断不等于并发初始化保护。

| API | 返回值 | 行为 |
| --- | --- | --- |
| `ci1302_audio_init()` | 0 成功，-1 失败 | 创建适配锁、初始化驱动并绑定事件槽；不等待芯片 ready |
| `ci1302_audio_capture_begin()` | 0 本地命令发送成功，-1 失败 | 初始化、清空接收暂存与溢出计数，发送开始录音 |
| `ci1302_audio_capture_stop()` | void | 停止接收新 PCM，尝试发送停止录音；不等待确认 |
| `ci1302_audio_read(data, length)` | 0 完整且无溢出，-1 失败 | data 为调用者可写空间，length 为字节数；不是返回读取字节数 |
| `ci1302_audio_write(data, length)` | 0 本块被驱动消费，-1 失败 | 非空、非零、偶数字节 PCM；首次写入启动流式播放 |
| `ci1302_audio_drain()` | void | 标记当前流结束，在芯片拉取时发送 EOF；不等待播完 |
| `ci1302_audio_cancel()` | void | 使当前写入失效，尝试发送立即停止；不清理平台队列 |
| `ci1302_audio_status()` | 状态位组合 | 0 空闲、1 录音、2 播放、3 两个标志同时置位 |
| `ci1302_audio_rx_overflow()` | uint32_t 字节数 | 当前录音因适配暂存满而丢弃的字节数 |
| `ci1302_audio_on_event(cb, arg)` | void | 设置一个上层事件回调；后一次覆盖前一次，NULL 注销 |

状态 0 也可能是尚未初始化，不表示 ready；状态 3 不是全双工能力已验证的证明。

### 4.1 录音

开始后持续读取现有平台需要的小块 PCM，例如 512 字节。不要重复调用 begin 轮询，
它会清空当前会话数据。有效缓冲参数下，读取不足会把尾部填零并返回 -1，不提供部分读取字节数；
平台应丢弃失败块或按自己的错误策略处理，不能把补零视为成功录音。

溢出计数直到下一次 begin 才清零；溢出后即使后续读满也返回 -1。
stop 后暂存可能还有旧数据，下次 begin 会清空，不能跨会话拼接。

### 4.2 播放与数据所有权

逐块 write，驱动按芯片请求拆包。不要一次提交整个长音频，以免触发等待超时。
write 期间调用者必须保持源缓冲地址和内容有效，返回后可释放或复用。
返回 0 表示本块已被消费，不保证芯片执行成功、声音播完或最后一包物理发送完成；
后续错误仍可能通过事件上报。

所有数据写完才 drain，它不是每包后的 flush。暂时缺数据不要 drain；平台按自己的
结束标志决定 EOF。流式播放正常结束通过 `PLAY_STOP` 或 playing 清零观察；
上层需要处理缺失完成通知的等待超时。cancel 不是正常结束，也不保证产生 `PLAY_STOP`。

取消时先停住旧生产者，再按平台原逻辑清理队列、网络和芯片会话，避免旧数据重新启动播放。
stop/cancel 为 void，不能返回底层发送失败，需结合状态和日志判断。

### 4.3 任务、等待与释放

- API 在任务上下文调用，不在 ISR、事件回调或 source 回调里重入录放控制。
- 由平台串行管理开始、结束、切换会话；内部锁不代表支持多个独立录放会话。
- 读等待、写消费等待各最多约 2000 次 `os_msleep(1)`，写入还可能等待上次 drain。
  加锁使用 `WAIT_FOREVER`，另有调度延迟，因此不能保证 API 严格在 2 秒内返回。
- 当前无 `ci1302_audio_deinit()`，适配的三个互斥锁按应用生命周期保留。
- 关闭时先停止生产者、停止录音/取消播放，等待读写任务退出 API，注销上层回调，
  再调用 `ci1302_deinit()`。不允许并发读写、回调和反初始化。
- 原始 deinit 不清除独立平台槽。销毁上下文前，在无并发回调时调用
  `ci1302_audio_on_event(NULL, NULL)` 和 `ci1302_set_platform_callback(NULL, NULL)`；
  重接入时重新调用 `ci1302_audio_init()`。

### 4.4 最小录放示例

以下函数在独立任务中执行，调用者提供已有的至少 32000 字节缓冲。
生产项目可以改为逐块交给原有队列，无需新增同样大小的缓存。调用前确保无其他录放会话。

```c
#include "ci1302_audio.h"
#include "oshal.h"

int local_record_replay(uint8_t *capture, size_t capacity)
{
    unsigned int waited;
    if (!capture || capacity < 32000) return -1;
    if (ci1302_audio_init() != 0 || ci1302_audio_status() != 0) return -1;
    for (waited = 0; !ci1302_is_ready() && waited < 10000; waited += 10)
        os_msleep(10);
    if (!ci1302_is_ready() || ci1302_set_volume(5) != 0) return -1;
    if (ci1302_audio_capture_begin() != 0) return -1;
    for (size_t offset = 0; offset < 32000; ) {
        unsigned int n = (32000 - offset > 512) ? 512 : 32000 - offset;
        if (ci1302_audio_read(capture + offset, n) != 0) {
            ci1302_audio_capture_stop();
            return -1;
        }
        offset += n;
    }
    ci1302_audio_capture_stop();
    for (waited = 0; ci1302_is_recording() && waited < 2000; waited += 10)
        os_msleep(10);
    if (ci1302_is_recording() || ci1302_audio_rx_overflow()) return -1;
    for (size_t offset = 0; offset < 32000; ) {
        unsigned int n = (32000 - offset > 2048) ? 2048 : 32000 - offset;
        if (ci1302_audio_write(capture + offset, n) != 0) return -1;
        offset += n;
    }
    ci1302_audio_drain();
    for (waited = 0; ci1302_is_playing() && waited < 5000; waited += 10)
        os_msleep(10);
    if (ci1302_is_playing()) {
        ci1302_audio_cancel();
        return -1;
    }
    return 0;
}
```

示例只展示基本调用顺序。生产项目还须记录并处理 ERROR 事件，不能把 playing 清零
一律当成成功播放；初始化也不等于芯片就绪。

## 5. 事件与注册入口

回调类型：

```c
void callback(ci1302_event_t event, const uint8_t *data,
              uint16_t length, void *user_data);
```

| 事件 | 实际含义 |
| --- | --- |
| READY | 收到 0x0601，或首次成功执行 ACK 将 ready 置位；可能重复通知 |
| ERROR | 芯片错误、部分流式 TX 失败，或 deinit 对平台槽的关闭通知；不是统一错误码 |
| WAKE_UP / EXIT_WAKE_UP | 芯片唤醒通知，取决于固件 |
| RECORD_START / DATA / STOP | 对应 `CI1302_EVENT_RECORD_START/RECORD_DATA/RECORD_STOP`，来自芯片录音通知 |
| PLAY_START | 本地启动调用中发送，可能早于启动命令发送失败；不是芯片 ACK |
| PLAY_STOP | 芯片 0x0208 或 0x020D；本地 cancel 不合成此事件 |

普通芯片事件在 UART RX 任务执行，PLAY_START 在控制调用者上下文执行，关闭通知在 deinit
调用者上下文执行。`data` 可为 NULL，只在回调期间有效，不要保存或释放它。
异步处理需复制到平台已有缓冲/队列。回调中避免网络、Flash、阻塞队列、长时间编解码及
重入播放控制/反初始化，否则可能堵住 RX 或死锁。

| 注册入口 | 用途 |
| --- | --- |
| `ci1302_audio_on_event()` | 同步 PCM 端点的上层事件，推荐给新平台 |
| `ci1302_set_platform_callback()` | 原始驱动平台槽，同步适配内部占用；绕过同步层时才直接使用 |
| `ci1302_set_event_callback()` | 原始驱动普通槽，当前 CLI 录音使用，可能被 CLI 覆盖 |

每个入口只有一个回调，不是多订阅框架。同步适配先保存 PCM，再通知上层；原始驱动另调用
普通槽。使用同步端点时不要覆盖原始平台槽，否则会破坏录音。注册/注销和上下文生命周期
由平台保证，无并发回调时修改。回调槽隔离不代表 CLI 与业务硬件会话隔离。

## 6. 原始芯片 API（定制数据桥接）

同一播放会话不要混用同步 write 与 source API。

| API | 说明 |
| --- | --- |
| `ci1302_init/deinit()` | 初始化/关闭 UART 和 RX 任务；0 成功，非零失败；init 不等 ready |
| `ci1302_is_initialized/ready/recording/playing()` | 本地布尔状态，ready 不是持续链路健康检查 |
| `ci1302_set_volume(volume)` | 1～7；0 不是静音而是非法参数；返回 0 为本地发送成功 |
| `ci1302_get_volume()` | 本地缓存值，不查询芯片；发送失败也可能已更新缓存 |
| `ci1302_record_start/stop()` | 发送录音命令；同步读取路径应使用 capture_begin/stop 而非混用 |
| `ci1302_play_pcm_start(source, arg)` | 有限音频 source，短读视为最后一包 |
| `ci1302_play_pcm_stream_start(source, arg)` | 流式 source，短读仍为普通数据包 |
| `ci1302_play_stop()` | 发送停止并清本地播放状态；0 为本地发送成功 |
| `ci1302_play_tone(ms, volume)` | 1 kHz 测试音，1～60000 ms，音量 1～7 |
| `ci1302_reset()` | 清 ready 并发送软件复位，不是 OTA，也不自动清理平台会话 |
| `ci1302_send_command(cmd, fill, payload, len)` | 原始音频协议发送，payload 最大 4096 字节；不等待执行 ACK |
| `ci1302_set_trace(bool)` / `ci1302_set_raw_trace(bool)` | 帧日志 / 原始 RX 字节日志 |
| `ci1302_get_stats(ptr)` / `ci1302_reset_stats()` | 读取/清除统计，不清同步暂存或适配层 overflow |

source 类型为 `int source(uint8_t *buffer, uint32_t max_length, void *user_data)`，
由 UART RX 任务调用，向给定 buffer 填 PCM，不保留 buffer、不阻塞等待生产者。
返回数据须不超过 max_length，并保证样本完整。

| source 返回值 | 有限 source | 流式 source |
| --- | --- | --- |
| 等于 max_length 的正值 | 普通包 | 普通包 |
| 小于 max_length 的正值 | 带数据结束包 | 普通包，可继续 |
| 0 | EOF | 明确 EOF |
| `CI1302_PCM_AGAIN`（-2） | 按结束处理，不支持暂缺 | 暂缺，保留请求并后续重试 |
| 其他负值 | 按结束处理 | 按结束处理，不自动形成独立错误事件 |

有限 source 在最后数据发送时清 playing，流式 source 等芯片完成才清 playing。
source 的上下文在正常结束或经过同步取消、停止访问前须一直有效。
统计为运行中快照，不是多任务原子事务；本地 TX 成功不等于芯片执行成功。

## 7. 统一服务接入（新应用推荐）

应用头文件：`voice_chip_service.h`。诊断头：`voice_ci1302_service.h`。

| 服务 API | CI1302 行为 |
| --- | --- |
| start / init | 自动注册 CI1302 backend、初始化 UART；不等待 ready |
| is_ready | 查询芯片 ready |
| set_record_callbacks | 在空闲时注册；data 回调接收独立分配的 PCM，必须 os_free |
| start_capture / stop_capture | 开始/停止 PCM；先注册消费者、确保没有播放 |
| play_stream(data, len, AUDIO_FORMAT_PCM) | 同步提交一块 PCM；不支持 Opus/MP3 |
| end_stream | 新增可选 EOF 操作，异步结束；等 set_playback_callbacks 注册的完成回调 |
| clear_stream / cancel_playback | 丢弃当前播放，不合成正常完成 |
| set_output_volume / get_output_volume | 1～100 映射七档；返回缓存量化值，0/静音不支持 |
| get_status / get_caps | 0/1/2/3 状态，UART + PCM 16k/16bit/mono 能力 |
| set_idle / set_active | 停止音频 / 初始化，无硬件电源或睡眠控制 |
| deinit | 调用前停住生产者并等待 worker 退出，关闭 UART、清回调 |
| play_local / set_opus_mode / VAD 等 | 未实现，按 core 约定返回不支持 |

`voice_ci1302_service_last_error()` 是后端错误锁存，开始新录音或新播放时重置，
不是芯片所有 ACK 的完整错误码解析。播放成功需结合完成回调和错误锁存。

**服务层数据回调与原始回调所有权不同**：service 按 GX8006 的惯例分配独立 PCM，
接收者释放；原始 ci1302 回调仍是借用。service 回调在 RX 任务执行，应快速复制/入队并返回，
不能阻塞或重入控制接口。单一应用任务管理初始化、回调注册和会话切换。

原平台缓存与业务代码未在本次迁移中更改，Tomcat/Coze/Joyinside 仍需按新后端显式选择及验证。
仅注册服务不代表 Ogg 上传、提示音或云端已兼容。

## 8. 构建与验证

从 examples/ci1302_example 构建；入口与 gx8006_example 相同，采用 standalone staging，
不修改正式 defconfig。先从 ~/.bashrc 查找编译链，再通过 GCC_PATH 指定 Andes bin 路径。
全局默认关闭 CI1302，示例 examples_config 显式开启。不要并行链接两个示例。

```text
ci1302voice status
ci1302voice volume 67
ci1302voice sample
ci1302voice tone
ci1302voice record
ci1302voice replay
```

先等待 ready=1，每次等 job completed 再执行下一条。录音保存 32000 字节，
回放完成 busy=0 status=0。详情见示例 README。旧 ci1302 CLI 可单独打开，但不能与 service 并发测试。

```sh
sh components/voice_chip/chips/ci1302/tests/run_host_tests.sh
```

覆盖协议、PCM、EOF、取消、TX 失败、录音溢出，以及 service 默认注册、能力、音量、
不支持格式、录音所有权、完成回调和重新初始化。
迁移前底层录放已上板验证，本次新 service 示例应重新进行硬件验收。
