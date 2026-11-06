# Voice Chip Core 接口使用说明

本文依据当前源码分析 core 文件职责、公开接口、返回值和后端差异。
实际路径为 `components/voice_chip/core/`，不在 `chips/core/` 下。

| 文件 | 职责 |
| --- | --- |
| `voice_chip_core.c` | 校验设备对象并分派底层 ops，不持有全局设备、不管理音频任务 |
| `voice_chip_service.c` | 持有单个全局服务指针，执行懒注册、APP 接口分派及 ES8311 公共配置转换 |
| `Make.defs` | `CONFIG_VOICE_CHIP=y` 时编译上述两个 C 文件并添加公共头文件路径 |
| `README.md` | 本接口使用说明 |

公共头文件位于 `include/components/voice_chip/voice_chip.h` 和
`include/components/voice_chip/voice_chip_service.h`，均支持 C++ 的 `extern "C"`。

以下两个实现文件对外提供的接口及推荐调用方式：

CI1302 后端支持可选 `voice_chip_service_end_stream()`：全部 PCM 写入后标记 EOF，
异步等待播放结束回调；它不等同于 clear/cancel。未实现该操作的后端返回 -1，原行为不变。
CI1302 的录音回调收到独立分配的 PCM，调用者必须 `os_free()`；该约定与原始 CI1302
驱动的借用式回调不同。完整示例见 `examples/ci1302_example/README.md`。

- `voice_chip_core.c`：面向具体 `voice_chip_device_t` 的底层设备操作封装。
- `voice_chip_service.c`：面向应用的全局语音服务接口，将调用转发给当前注册的后端。

对应公共头文件为：

```c
#include "voice_chip.h"
#include "voice_chip_service.h"
```

## 1. 接口层次与选择

应用通常使用 `voice_chip_service_*()`。它提供统一的初始化、录音、播放、音量、状态和本地音频接口，应用不需要保存具体芯片对象。

芯片驱动、板级适配或需要访问芯片原始音量、输入增益时，使用 `voice_chip_*()`，并显式传入由后端创建和绑定的 `voice_chip_device_t`。

两层关系如下：

```text
应用
  -> voice_chip_service_*()
       -> 当前 voice_chip_service_t.ops
            -> ES8311/GX8006 service 后端
                 -> voice_chip_*() 或后端协议/运行时
```

## 2. 通用约定

- 命令类接口通常以 `0` 表示请求成功，`-1` 表示参数无效、未注册或后端不支持；返回值由后端原样传递。查询类成功时返回音量、状态或资源 ID，不能按“非 0 即失败”处理。ES8311 的录音启动返回 1 表示被阻塞，档位操作的正值则表示有效业务结果。具体见对应表格。
- 公共包装层主要检查对象及函数指针，不统一校验音量范围、音频格式、缓冲区长度等业务参数；device 层几个生命周期入口对空 ops 的检查存在限制，见第 9 节。
- 很多操作是异步的。接口返回 `0` 往往只表示命令已提交或数据已入队，不表示硬件已经开始/结束录音或播放。实际状态应通过回调、`voice_chip_service_get_status()` 或后端事件确认。
- 后端未实现某个 `int` 接口时返回 `-1`；能力查询返回 `NULL`；布尔查询通常返回 `false`；`voice_chip_service_set_output_volume_local()` 是 `void`，不支持时静默忽略。
- 本模块没有统一加锁。服务注册、注销和生命周期切换应由应用在受控上下文中完成，不应与正在执行的音频调用并发进行。

## 3. 底层设备接口 `voice_chip_core.c`

### 3.1 设备对象

后端通过 `voice_chip_device_t` 描述一个语音芯片：

```c
struct voice_chip_device {
    const char *name;
    void *priv;
    const voice_chip_ops_t *ops;
    const voice_chip_caps_t *caps;
};
```

`ops` 由后端实现。调用公共包装接口前，必须先使用对应后端的 bind/config 接口初始化设备对象。

### 3.2 生命周期与控制

| 接口 | 用途 | 注意事项 |
| --- | --- | --- |
| `voice_chip_open(chip)` | 打开并初始化芯片设备 | `chip`、`chip->ops` 和 `open` 必须有效 |
| `voice_chip_close(chip)` | 关闭设备 | 是否释放板级资源由后端决定 |
| `voice_chip_reset(chip)` | 复位设备 | 复位后的配置保持情况由后端决定 |
| `voice_chip_flush(chip)` | 刷新待处理数据 | 具体是等待发送还是清空缓存由后端定义 |
| `voice_chip_abort_all(chip)` | 中止全部音频操作 | 使用前检查 `VOICE_CHIP_CAP_ABORT_ALL` |

### 3.3 音量与输入增益

| 接口 | 用途 |
| --- | --- |
| `voice_chip_set_output_volume_raw(chip, volume)` | 设置后端原始音量值 |
| `voice_chip_set_output_volume_percent(chip, percent)` | 按百分比设置输出音量，通常期望 `0..100` |
| `voice_chip_get_output_volume_percent(chip)` | 获取百分比音量；失败返回负值 |
| `voice_chip_volume_step_up(chip)` | 音量增加一级 |
| `voice_chip_volume_step_down(chip)` | 音量降低一级 |
| `voice_chip_volume_step_cycle(chip)` | 按后端定义的档位循环调整 |
| `voice_chip_set_input_gain(chip, gain)` | 设置麦克风输入增益，范围和单位由后端定义 |

调用音量接口前建议先读取能力位，避免依赖某个具体芯片才有的操作：

```c
const voice_chip_caps_t *caps = voice_chip_get_caps(chip);

if (caps && (caps->capability_flags & VOICE_CHIP_CAP_OUTPUT_VOLUME_PERCENT)) {
    if (voice_chip_set_output_volume_percent(chip, 60) != 0) {
        /* 处理设置失败 */
    }
}
```

### 3.4 能力查询

`voice_chip_get_caps(chip)` 返回由后端持有的只读 `voice_chip_caps_t` 指针。调用方不得修改或释放该对象。接口不支持或设备无效时返回 `NULL`。

主要能力位：

| 能力位 | 含义 |
| --- | --- |
| `VOICE_CHIP_CAP_OUTPUT_VOLUME_RAW` | 支持原始音量值 |
| `VOICE_CHIP_CAP_OUTPUT_VOLUME_PERCENT` | 支持百分比音量 |
| `VOICE_CHIP_CAP_OUTPUT_VOLUME_STEP` | 支持逐级增减 |
| `VOICE_CHIP_CAP_OUTPUT_VOLUME_CYCLE` | 支持档位循环 |
| `VOICE_CHIP_CAP_INPUT_GAIN` | 支持输入增益 |
| `VOICE_CHIP_CAP_OUTPUT_MUTE` | 支持输出静音 |
| `VOICE_CHIP_CAP_ABORT_ALL` | 支持中止全部操作 |
| `VOICE_CHIP_CAP_PERSISTED_VOLUME` | 支持音量持久化 |
| `VOICE_CHIP_CAP_PCM_PLAYBACK` | 支持 PCM 播放 |
| `VOICE_CHIP_CAP_PCM_CAPTURE` | 支持 PCM 采集 |

`transport_flags` 表示后端使用的传输通道，可包含 `I2C`、`I2S`、`UART`。`playback` 和 `capture` 给出后端声明的采样率、位宽和声道数。

## 4. 服务接口 `voice_chip_service.c`

### 4.1 后端注册与生命周期

| 接口 | 用途 |
| --- | --- |
| `voice_chip_service_register(service)` | 注册全局后端；参数及 `service->ops` 必须有效 |
| `voice_chip_service_unregister(service)` | 注销后端；传 `NULL` 可清除当前后端 |
| `voice_chip_service_current()` | 获取当前后端，未注册时返回 `NULL` |
| `voice_chip_service_init()` | 调用当前后端的 `init` |
| `voice_chip_service_start()` | `voice_chip_service_init()` 的别名 |
| `voice_chip_service_deinit()` | 调用当前后端的 `deinit` |

同一时刻只允许一个 service。重复注册同一对象成功；已有其他对象时再注册会失败。`unregister()` 只清除全局指针，不会自动执行 `deinit()`、关闭设备或释放资源，正常顺序应为先停止业务、再 `deinit()`、最后注销。

启用 `CONFIG_VOICE_CHIP_GX8006` 或 `CONFIG_CI1302` 时，经过内部
`voice_chip_service_get_ops()` 的调用在未注册服务时会调用
`voice_chip_service_register_default_backend()` 懒注册。懒注册不等于启动。
`current()`、注册/注销、状态判定辅助函数不会触发懒注册；`configure()` 也不走此流程。
ES8311 不进行无参数懒注册，APP 应先调用 `voice_chip_service_configure(&cfg)`。
`voice_chip_service_register_default_backend()` 是后端提供的链接钩子，不是普通 APP 初始化入口。

`unregister(NULL)` 无条件清除服务指针；传入非当前服务对象返回 -1。
注销后 GX8006/CI1302 下次操作会再次触发懒注册，不能用注销作为永久禁用开关。
后端可能有自己的 started/registered 标记，core 注销不会重置这些标记；不保证
所有后端都支持注销后重新配置或热切换。ES8311 当前未实现 deinit。

推荐初始化顺序：

```c
unsigned int waited_ms;

/* 1. 完成 GPIO、I2S/UART 等板级初始化。 */
/* 2. ES8311 先 configure；GX8006/CI1302 可使用懒注册。 */

if (voice_chip_service_set_record_callbacks(on_record_start,
                                            on_record_data,
                                            on_record_stop) != 0) {
    return -1;
}

if (voice_chip_service_start() != 0) {
    return -1;
}

/* 仅对实现了 is_ready 的异步后端执行带超时等待。 */
for (waited_ms = 0; waited_ms < READY_TIMEOUT_MS; waited_ms += 10) {
    if (voice_chip_service_is_ready()) {
        break;
    }
    os_msleep(10);
}

if (!voice_chip_service_is_ready()) {
    return -1;
}
```

注意：并非所有后端都实现 `is_ready`。该接口缺失时也返回 `false`，因此通用代码应先结合产品所选后端决定是否等待，且必须设置超时。

### 4.2 录音

| 接口 | 用途 |
| --- | --- |
| `voice_chip_service_set_record_callbacks(start, data, stop)` | 注册录音开始、数据和停止回调 |
| `voice_chip_service_start_capture()` | 请求开始录音 |
| `voice_chip_service_stop_capture()` | 请求停止录音 |

回调类型：

```c
typedef void (*voice_chip_service_record_start_cb_t)(void);
typedef void (*voice_chip_service_record_data_cb_t)(uint8_t *data, uint16_t size);
typedef void (*voice_chip_service_record_stop_cb_t)(void);
```

典型用法：

```c
static void on_record_start(void)
{
    /* 清空本次会话计数，通知上层开始接收。 */
}

static void on_record_data(uint8_t *data, uint16_t size)
{
    /* ES8311 示例：在回调内消费，或复制到应用自己的缓存。 */
    upload_audio(data, size);
    /* 若选用 GX8006 或 CI1302 service，此处还须 os_free(data)。 */
}

static void on_record_stop(void)
{
    /* 提交剩余数据并结束本次会话。 */
}
```

公共层只转发回调，不统一规定数据所有权：

| 后端 | 录音 data 回调的所有权 |
| --- | --- |
| ES8311 | 借用 runtime 缓存，不可释放；需要延后处理应复制，或通过配置的 upload 帧交接协议处理 |
| GX8006 | smartbot 传入分配的帧数据，APP 处理后负责 `os_free(data)`；可先转交 APP 队列，沿用原 smartbot 所有权协议 |
| CI1302 service | 每帧由 service 独立分配，APP 负责 `os_free(data)`；可转交 APP 队列后释放 |

回调可能运行在音频任务或协议任务上下文，不应长时间阻塞。
ES8311 data 回调与 upload 回调可以同时存在：前者接收 PCM 指针，后者接收
帧句柄，两者不是同一种参数协议。参考 ES8311 示例的同步复制加同步归还方式。

开始/停止接口只代表请求结果。调用方应以 start/stop 回调或状态查询确认真实状态，避免仅设置一个“requested”标志后就认为已经采集到数据。

### 4.3 流式播放

支持的统一格式枚举为：

| 枚举 | 值 | 格式 |
| --- | ---: | --- |
| `AUDIO_FORMAT_PCM` | 0 | PCM |
| `AUDIO_FORMAT_SPEEX` | 1 | Speex |
| `AUDIO_FORMAT_OPUS` | 2 | Opus |
| `AUDIO_FORMAT_MP3` | 3 | MP3 |
| `AUDIO_FORMAT_NSUP` | 255 | 不支持/未指定 |

格式枚举存在不代表当前后端支持该格式，应结合能力信息和后端说明确认。

| 接口 | 用途 |
| --- | --- |
| `voice_chip_service_play_stream(data, len, format)` | 按默认语义提交一段音频数据 |
| `voice_chip_service_play_stream_ex(data, len, format, semantic)` | 提交数据并指定播放语义 |
| `voice_chip_service_clear_stream()` | 清除流式播放缓存 |
| `voice_chip_service_cancel_playback()` | 取消当前播放 |
| `voice_chip_service_end_stream()` | 显式标记流结束，保留已入队音频并异步排空；当前 CI1302 支持，其他后端返回 -1 |
| `voice_chip_service_set_playback_callbacks(start, finished)` | 注册播放开始和完成回调，当前 GX8006/CI1302 支持 |

`semantic` 可取：

- `VOICE_PLAYBACK_STREAM_DEFAULT`：普通连续流。
- `VOICE_PLAYBACK_LOCAL_PROMPT_SHORT`：短本地提示音，后端可据此调整播放状态机。

如果后端没有实现 `play_stream_ex`，service 会回退到 `play_stream`，此时 `semantic` 被忽略。后端必须至少实现 `play_stream` 才能使用这两个入口。

```c
int ret = voice_chip_service_play_stream_ex(frame,
                                             frame_len,
                                             AUDIO_FORMAT_OPUS,
                                             VOICE_PLAYBACK_STREAM_DEFAULT);
if (ret != 0) {
    /* 队列满或后端拒绝时，按产品策略重试或终止。 */
}
```

公共层不保证后端是否复制 `data`。除非具体后端明确说明已经接管或复制，否则应确保缓冲区在后端消费完成前保持有效。

### 4.4 音量、电源状态和运行状态

| 接口 | 用途 |
| --- | --- |
| `voice_chip_service_set_output_volume(volume)` | 设置统一输出音量；当前后端通常按 `0..100` 处理 |
| `voice_chip_service_get_output_volume()` | 获取当前输出音量，失败返回负值 |
| `voice_chip_service_set_idle()` | 通知后端进入空闲状态 |
| `voice_chip_service_set_active()` | 通知后端进入活动状态 |
| `voice_chip_service_get_status()` | 获取录音/播放组合状态，失败返回负值 |
| `voice_chip_service_is_ready()` | 查询异步后端是否就绪 |
| `voice_chip_service_get_caps()` | 获取当前后端能力，只读，不得释放 |

状态枚举：

| 状态 | 含义 |
| --- | --- |
| `VOICE_CHIP_SERVICE_STATUS_IDLE` | 未录音且未播放 |
| `VOICE_CHIP_SERVICE_STATUS_CAPTURING` | 正在录音 |
| `VOICE_CHIP_SERVICE_STATUS_PLAYING` | 正在播放 |
| `VOICE_CHIP_SERVICE_STATUS_CAPTURING_AND_PLAYING` | 同时录音和播放 |

使用 `voice_chip_service_status_is_capturing(status)` 和 `voice_chip_service_status_is_playing(status)` 判断组合状态，避免遗漏“同时录音和播放”。先确认 `status >= 0`，因为错误值也使用 `int` 返回。

### 4.5 本地音频和资源接口

以下是可选后端能力，当前主要由 GX8006 实现：

| 接口 | 用途 |
| --- | --- |
| `voice_chip_service_set_prompt_player(cb)` | 注册产品侧提示音播放回调，参数为音频 ID 和时长 |
| `voice_chip_service_set_audio_res_provider(cb)` | 注册 `audio_id` 到 `struct audio_res` 的资源查询函数 |
| `voice_chip_service_set_powerup_audio_id(audio_id)` | 设置上电提示音 ID |
| `voice_chip_service_play_local(audio_id)` | 将本地资源加入播放队列 |
| `voice_chip_service_clear_audio_queue()` | 清空本地音频队列 |
| `voice_chip_service_is_local_playback_busy()` | 查询本地音频是否忙 |

资源提供器返回的对象及其中的数据必须在后端使用期间保持有效。`play_local()` 返回 `0` 只表示资源已成功入队，不表示播放已经结束。

### 4.6 OPUS、VAD 和 GX8006 扩展

这些入口放在统一 service 操作表中，但不是所有后端都支持。调用前应检查返回值，不能把“不支持”当成配置成功。

| 接口 | 用途 |
| --- | --- |
| `voice_chip_service_set_opus_mode(mode)` | 设置录音 OPUS 输出模式：`RAW` 或 `OGG` |
| `voice_chip_service_set_vad_threshold(value)` | 将 VAD 开关/阈值参数提交给后端；GX8006 的具体取值遵循 smartbot 固件协议 |
| `voice_chip_service_set_custom_opus_resource(addr, len, framesize)` | 设置自定义 OPUS 资源；内存必须长期有效 |
| `voice_chip_service_get_custom_opus_resource(base, end, framesize)` | 读取已设置的自定义 OPUS 资源范围 |
| `voice_chip_service_get_custom_opus_audio_id()` | 获取自定义 OPUS 对应的本地音频 ID |
| `voice_chip_service_sync_speaker_config()` | 将当前扬声器配置同步到后端 |
| `voice_chip_service_set_output_volume_local(volume)` | 仅更新后端本地音量状态，不返回执行结果 |
| `voice_chip_service_commit_output_volume()` | 提交后端当前音量配置，具体同步/持久化行为由后端决定 |
| `voice_chip_service_reset_output_volume()` | 请求后端恢复音量，具体默认值由后端决定 |

GX8006 需要固定录音模式时，可参考如下顺序。命令通常经 UART/协议队列异步发送，配置命令之间是否需要等待 ACK 或适当延时，应按产品协议流程处理：

```c
if (voice_chip_service_set_opus_mode(VOICE_CHIP_SERVICE_OPUS_MODE_RAW) != 0) {
    return -1;
}

if (voice_chip_service_set_vad_threshold(0) != 0) {
    return -1;
}

if (voice_chip_service_start_capture() != 0) {
    (void)voice_chip_service_set_vad_threshold(1);
    return -1;
}

/* 等待录音回调并处理数据。 */

(void)voice_chip_service_stop_capture();
(void)voice_chip_service_set_vad_threshold(1);
```

## 5. 自定义 service 后端

新增后端时，定义长期有效的 `voice_chip_service_t` 和 `voice_chip_service_ops_t`，填入支持的操作后注册：

```c
static const voice_chip_service_ops_t my_ops = {
    .init = my_init,
    .deinit = my_deinit,
    .start_capture = my_start_capture,
    .stop_capture = my_stop_capture,
    .play_stream = my_play_stream,
    .get_status = my_get_status,
};

static const voice_chip_service_t my_service = {
    .name = "my_voice_chip",
    .ops = &my_ops,
    .priv = NULL,
};

int my_voice_service_register(void)
{
    return voice_chip_service_register(&my_service);
}
```

未支持的函数指针保持 `NULL`。应用必须允许可选操作返回不支持，后端对象和操作表不能使用生命周期短于注册周期的栈变量。

## 6. 常见问题检查

- `start_capture()` 返回成功但没有录音数据：确认后端已经 ready，并检查是否收到录音 start 回调，而不是只检查请求标志。
- `get_status()` 一直为 `IDLE`：请求可能仍在协议队列中，或硬件未确认状态；检查 UART/I2S、流控和后端日志。
- 播放返回失败：检查格式是否受支持、数据长度是否合法、本地/流式队列是否已满。
- 接口直接返回 `-1`：检查后端是否已注册，以及对应 `ops` 函数指针是否实现。
- `is_ready()` 返回 `false`：既可能是设备未就绪，也可能是当前后端没有实现该查询；等待逻辑必须设置超时。
- 切换后端：先停止录音和播放，完成 `deinit()`，再注销旧 service；不要在音频回调执行期间切换。

## 7. APP 配置、扩展控制和录音帧接口

Coze 使用公共 `voice_chip_service.h`，不再包含 ES8311 service/runtime 头文件。
APP 填写 `voice_chip_service_config_t`，调用 `voice_chip_service_configure(&cfg)`，
成功后调用 `voice_chip_service_start()`。当前配置入口支持编译选择的 ES8311 后端；
GX8006/CI1302 返回 -1，继续使用原有初始化或懒注册流程。配置入口不替换已经注册
的其他后端。配置中的字符串、存储回调上下文须在服务使用期间有效；ES8311
已注册后再次配置沿用原后端的幂等行为，不应用新的配置。

APP 仍负责板级 I2C 参数、音量默认值、存储策略、联网就绪/空闲时间回调以及
录音上传队列。core 将公共配置转换为后端配置，硬件和运行时实现保留在 chips 中。

| 公共接口 | 语义 |
| --- | --- |
| `voice_chip_service_set_output_volume_raw()` | 原始芯片音量，具体单位由后端定义 |
| `voice_chip_service_set_volume_bounds(max, min)` | 后端原始音量上下限 |
| `voice_chip_service_volume_step_up()` | 增加一档；ES8311 返回 0 表示已到边界，1 表示调整 |
| `voice_chip_service_volume_step_down()` | 降低一档；ES8311 返回 0 表示已到边界，1 表示调整 |
| `voice_chip_service_volume_step_cycle()` | ES8311 返回 1 为调整，2/3 为到达上限/下限 |
| `voice_chip_service_set_input_gain()` | 后端原始输入增益 |
| `voice_chip_service_get_capture_blocker_state()` | 查询录音阻塞状态，包含统一 service 状态；未实现此操作时输出清零且 can_start 为 false；实际后端报错时不得使用输出 |
| `voice_chip_service_capture_force_stop_pending()` | 强制停止是否待处理；不支持时 false |
| `voice_chip_service_reset_playback_cancel_latch()` | 显式开始合法新播放前解除取消锁存；Coze 仅在深睡前休眠提示音提交时调用 |

上述 int 接口未实现时返回 -1。百分比音量继续使用已有的
`voice_chip_service_set_output_volume()` / `voice_chip_service_get_output_volume()`。
Coze 的 int 百分比请求在转换为 uint8_t 前限幅到 0..100，保留 ES8311 原有行为。

录音上传回调的 `char *frame` 是不透明 `voice_chip_service_record_frame_t *` 句柄，
并非 PCM 起始地址；`length` 是 PCM 字节数。ES8311 保留现有回调返回值：0 表示
已接收帧，1 表示停止路径未接收该帧，-1 表示失败。队列仍传递同一个句柄：

```c
voice_chip_service_record_frame_t *frame = (voice_chip_service_record_frame_t *)buff;
uint8_t *data = voice_chip_service_record_frame_data(frame);
if (data && !voice_chip_service_record_frame_is_consumed(frame)) {
    /* 同步处理 data，长度来自上传回调/队列消息。 */
    (void)voice_chip_service_record_frame_consume(frame);
}
```

数据和句柄均由后端持有，不可 free，不可在消费完成后继续访问。
即使 APP 因断网或停止上传丢弃已入队的帧，也须标记消费完成。
不支持帧接口时 data 返回 NULL，is_consumed 返回 true，consume 返回 -1。
这些接口封装原有 buf/read_enable 操作，保持零拷贝，不改变运行时缓冲区调度或
增加线程同步；它们不保证快照查询和随后操作之间状态不变。

扩展 ops 成员追加在原成员之后，GX8006 源码和原操作分派保持不变。
公共头文件变更后须重新编译后端静态库，不能混用旧版 ops 表的预编译库。

### 7.1 公共配置字段

`voice_chip_service_config_t` 当前用于 ES8311。它不是全平台自动探测配置，
core 原样转换字段，不自动填默认值或初始化 I2S、GPIO、PA。

| 字段 | 使用约定 |
| --- | --- |
| `device_addr` | I2C 芯片地址，示例为 0x18 |
| `volume_min` / `volume_max` | 原始音量上下限，示例 0x87 / 0xc5；min 不得大于 max |
| `default_volume_index` | 默认档位索引，不是百分比；当前 ES8311 为 8 档，索引 0..7 |
| `i2c_speed` | SDK I2C 速度常量 |
| `i2c_address_bit` / `i2c_mode` / `i2c_dma` | SDK 地址位数、主从和 DMA 参数 |
| `scl_num` / `sda_num` | SDK I2C 引脚选择参数，不应猜测其枚举与 GPIO 编号相同 |
| `volume_store_key` | 音量存储键，需长期有效；NULL 可不使用持久化键 |
| `storage_ops.save_blob` | 存储回调，当前 ES8311 以字符串存储档位；成功返回 0 |
| `storage_ops.load_blob` | 读取回调，当前 ES8311 以返回值 >0 判定读到数据，value_len 提供实际长度；仅返回 0 不会触发恢复 |
| `storage_ops.ctx` | 存储回调上下文，须长期有效 |
| `runtime_hooks.ready_cb` | 就绪门控；返回 false 会阻止启动录音，NULL 不增加此门控；查询阻塞快照也可能调用它 |
| `runtime_hooks.idle_cb` | 接收 runtime 空闲时间通知，参数单位/时基遵循平台 `os_time_get()` |
| `record_upload_cb` | 接收不透明录音帧句柄和 PCM 字节数；不可以当作 PCM 数据指针 |

### 7.2 ES8311 初始化示例

下面片段在任务上下文中调用；PA、I2S 驱动和回调函数由产品代码提供。
I2C 参数按 ECR6600 SDK 选择，其他平台须使用其对应枚举。

```c
#include "voice_chip_service.h"
#include "i2c.h"

static int acknowledge_frame(char *buff, int length)
{
    (void)length;
    /* PCM 已由 data 回调同步处理，此处归还后端帧。 */
    return voice_chip_service_record_frame_consume(
        (voice_chip_service_record_frame_t *)buff);
}

static int init_audio(void)
{
    voice_chip_service_config_t cfg = {
        .device_addr = 0x18,
        .volume_min = 0x87, .volume_max = 0xc5,
        .default_volume_index = 6,
        .i2c_speed = I2C_Speed_100K,
        .i2c_address_bit = I2C_Address_7bit,
        .i2c_mode = I2C_Mode_Master,
        .i2c_dma = I2C_DMA_Disable,
        .scl_num = I2C_SCL_USED_GPIO2,
        .sda_num = I2C_SDA_USED_GPIO3,
        .record_upload_cb = acknowledge_frame,
    };
    int ret = voice_chip_service_configure(&cfg);
    if (ret != 0) return ret;
    /* 在这里完成产品侧 I2S 初始化，检查其返回值。 */
    ret = voice_chip_service_set_record_callbacks(on_record_start,
                                                 on_record_data,
                                                 on_record_stop);
    if (ret != 0) return ret;
    ret = voice_chip_service_start();
    if (ret != 0) return ret;
    return voice_chip_service_set_output_volume(60);
}
```

### 7.3 阻塞快照和帧操作

| 接口 | 参数及返回值 |
| --- | --- |
| `voice_chip_service_get_capture_blocker_state(state)` | state 为有效输出指针；NULL 返回 -1；先清零，再调用后端；成功 0，不支持 -1 |
| `voice_chip_service_record_frame_data(frame)` | 返回借用 PCM 指针；frame 为 NULL 或操作不支持返回 NULL |
| `voice_chip_service_record_frame_is_consumed(frame)` | 已消费为 true；NULL 或不支持同样为 true，不能据此推断硬件支持帧协议 |
| `voice_chip_service_record_frame_consume(frame)` | 标记消费，不释放内存；NULL 或不支持返回 -1，ES8311 成功返回 0 |

阻塞状态的 `force_stop`、`mic_running`、`speaker_running` 分别表示停止请求待处理、
麦克风运行、扬声器运行；`status` 为统一 service 状态。
`ready_cb_registered` / `ready_cb_ready` 表示就绪钩子是否存在以及结果。
`can_start` 综合表示采集启动条件是否满足，不代表录音已经开始。

```c
voice_chip_service_capture_blocker_state_t state;
if (voice_chip_service_get_capture_blocker_state(&state) == 0 && state.can_start) {
    int ret = voice_chip_service_start_capture();
    /* 即使快照可启动，仍须处理 ret 并等待录音开始确认。 */
    (void)ret;
}
```

帧接口通过当前服务分派，不携带后端身份；不能在帧仍在队列中时切换/注销后端。
consume 后 runtime 可以复用缓存，APP 不得保存 data 继续使用。
现有 runtime 的复用、取消及停止释放机制保持原样，core 没有新增引用计数、
帧租期保证或并发保护，异步队列需要沿用所选后端已有的时序约定。

## 8. 当前 service 后端支持矩阵

以下矩阵依据后端静态 ops 初始化表，表示有无分派入口，不表示硬件已就绪或
所有格式/参数都受支持。列为“有”的入口仍可能因运行状态返回失败。
所有表格中的接口均为完整公共函数名。

| 公共接口 | ES8311 | GX8006 | CI1302 |
| --- | --- | --- | --- |
| `voice_chip_service_register()` | core 实现 | core 实现 | core 实现 |
| `voice_chip_service_unregister()` | core 实现 | core 实现 | core 实现 |
| `voice_chip_service_current()` | core 实现 | core 实现 | core 实现 |
| `voice_chip_service_configure()` | 支持 | 不支持 | 不支持 |
| `voice_chip_service_set_output_volume_raw()` | 有 | 无 | 无 |
| `voice_chip_service_set_volume_bounds()` | 有 | 无 | 无 |
| `voice_chip_service_volume_step_up()` | 有 | 无 | 无 |
| `voice_chip_service_volume_step_down()` | 有 | 无 | 无 |
| `voice_chip_service_volume_step_cycle()` | 有 | 无 | 无 |
| `voice_chip_service_set_input_gain()` | 有 | 无 | 无 |
| `voice_chip_service_get_capture_blocker_state()` | 有 | 无 | 无 |
| `voice_chip_service_capture_force_stop_pending()` | 有 | 无 | 无 |
| `voice_chip_service_reset_playback_cancel_latch()` | 有 | 无 | 无 |
| `voice_chip_service_record_frame_data()` | 有 | 无 | 无 |
| `voice_chip_service_record_frame_is_consumed()` | 有 | 无 | 无 |
| `voice_chip_service_record_frame_consume()` | 有 | 无 | 无 |
| `voice_chip_service_start()` | 有 | 有 | 有 |
| `voice_chip_service_init()` | 有 | 有 | 有 |
| `voice_chip_service_deinit()` | 无 | 无 | 有 |
| `voice_chip_service_start_capture()` | 有 | 有 | 有 |
| `voice_chip_service_stop_capture()` | 有 | 有 | 有 |
| `voice_chip_service_end_stream()` | 无 | 无 | 有 |
| `voice_chip_service_play_stream()` | 有 | 有 | 有 |
| `voice_chip_service_play_stream_ex()` | 有 | 回退普通播放 | 回退普通播放 |
| `voice_chip_service_clear_stream()` | 有 | 有 | 有 |
| `voice_chip_service_cancel_playback()` | 有 | 有 | 有 |
| `voice_chip_service_set_output_volume()` | 有 | 有 | 有 |
| `voice_chip_service_get_output_volume()` | 有 | 有 | 有 |
| `voice_chip_service_set_idle()` | 有 | 有 | 有 |
| `voice_chip_service_set_active()` | 有 | 有 | 有 |
| `voice_chip_service_get_status()` | 有 | 有 | 有 |
| `voice_chip_service_is_ready()` | 无 | 有 | 有 |
| `voice_chip_service_set_record_callbacks()` | 有 | 有 | 有 |
| `voice_chip_service_get_caps()` | 有 | 有 | 有 |
| `voice_chip_service_status_is_capturing()` | core 实现 | core 实现 | core 实现 |
| `voice_chip_service_status_is_playing()` | core 实现 | core 实现 | core 实现 |
| `voice_chip_service_set_playback_callbacks()` | 无 | 有 | 有 |
| `voice_chip_service_set_prompt_player()` | 无 | 有 | 无 |
| `voice_chip_service_set_audio_res_provider()` | 无 | 有 | 无 |
| `voice_chip_service_set_powerup_audio_id()` | 无 | 有 | 无 |
| `voice_chip_service_play_local()` | 无 | 有 | 无 |
| `voice_chip_service_clear_audio_queue()` | 无 | 有 | 无 |
| `voice_chip_service_is_local_playback_busy()` | 无 | 有 | 无 |
| `voice_chip_service_get_custom_opus_audio_id()` | 无 | 有 | 无 |
| `voice_chip_service_get_custom_opus_resource()` | 无 | 有 | 无 |
| `voice_chip_service_set_custom_opus_resource()` | 无 | 有 | 无 |
| `voice_chip_service_set_opus_mode()` | 无 | 有 | 无 |
| `voice_chip_service_set_vad_threshold()` | 无 | 有 | 无 |
| `voice_chip_service_sync_speaker_config()` | 无 | 有 | 无 |
| `voice_chip_service_set_output_volume_local()` | 无 | 有 | 无 |
| `voice_chip_service_commit_output_volume()` | 无 | 有 | 无 |
| `voice_chip_service_reset_output_volume()` | 无 | 有 | 无 |
| `voice_chip_service_register_default_backend()` | 无此钩子 | 后端实现 | 后端实现 |

ES8311 的 set_idle/set_active 当前为返回 0 的空操作，不表示执行了硬件休眠/唤醒。
ES8311 目前不提供 is_ready、播放回调和 end_stream；不能据此写无限等待循环。
GX8006 新增的原始音量/帧/阻塞快照 service 槽位未实现，仍通过原有音量、录音和
本地资源入口工作；device 层支持情况须另外查询其 ops，不能用本表替代。

## 9. 源码分析与当前限制

| 位置 | 当前实现及影响 |
| --- | --- |
| `voice_chip_core.c` 的 open/close/reset/flush/abort_all | 表达式先访问 chip->ops 的成员，再进入 call0 校验；chip 非 NULL 而 ops 为 NULL 时存在空指针解引用。使用者必须传入已绑定 ops 的有效对象；不能认为这些入口已完全保护空 ops |
| `voice_chip_get_caps()` | 调用 ops->get_caps，不直接回退到 chip->caps；只有 caps 字段而无 get_caps 槽位时仍返回 NULL |
| `voice_chip_service.c` 的全局指针 | 不加锁、不持有引用；对象及 ops 必须长期有效，生命周期操作不能与音频调用并发 |
| `voice_chip_service_play_stream_ex()` | 先要求 play_stream 存在，再使用 play_stream_ex；只实现扩展播放而未实现普通播放会返回 -1 |
| `voice_chip_service_configure()` | 编译条件仅 ES8311；通过 current->name 字符串拒绝不同名称后端，不是通用配置 ops；勿用同名自定义后端模拟选择 |
| `voice_chip_service_get_capture_blocker_state()` | 不支持时输出清零；如果后端实际执行后返回错误，则不保证输出仍全零，调用者应先检查返回值 |
| bool/void 可选接口 | false 或静默返回无法区分“不支持”和“当前为否”；需结合支持矩阵和产品配置 |
| 运行状态查询 | 无原子快照或操作预留，查询结果可能在调用启动前变化 |
| 参数校验 | 公共层不统一检查长度、格式、音量范围或资源指针，须按后端约定使用 |
| 扩展 ops ABI | 旧成员位置保留，但结构体已增长；需重新编译所有相关后端静态库，不支持混用旧表 |

本文记录实际行为，不修改上述实现。产品初始化、取消、重试和超时策略应在 APP
或板级适配中组织，core 不替代会话状态机。

## 10. 示例入口

- ES8311：`examples/es8311_example/es8311_example.c`，配置、录音、PCM 回放、短提示音、帧归还均使用 core。
- GX8006：`examples/gx8006_example/`，以现有初始化、录音和本地资源流程为准。
- CI1302：`examples/ci1302_example/README.md`，包含 PCM 回调所有权和流式 EOF 说明。
- Coze APP：`apps/ai_sys/coze/device/i2c_es8311.c` 负责产品配置，`cloud_adpt/coze_task.c` 负责业务队列与帧消费。
