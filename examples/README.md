# examples

## 目录职责

`examples/` 保存示例工程，是快速验证某项能力或学习最小用法的入口。

## 什么时候从这里进入

- 需要快速跑通某个功能。
- 需要找最小可工作的示例，而不是完整产品工程。

## 关键内容

- `static_ip_example/`：静态 IP 示例
- `gx8006_example/`：统一语音服务的 GX8006 录放示例
- `ci1302_example/`：统一语音服务的 CI1302 PCM 示例，1 秒录音、回放、示例音和测试音
- `wifi_netcfg_multiap_example/`：多 AP 配网示例，目录内已有 README

## 与构建/配置的关系

- 示例目录通常带有自己的 `defconfig`、`makefile` 和必要资源。
- 示例更适合做功能验证，不应直接等同于量产产品板级目录。

## 第一次上手推荐顺序

如果你只是想确认“板子编出来以后能不能做基础测试”，建议按下面顺序进入：

1. 先完成 `../Boards/ecr6600/standalone/README.md` 的启动自测。
2. 再看 `static_ip_example/README.md`，先验证 `wifi_sta` 和 IP 配置是否正常。
3. 之后再看 `wifi_netcfg_multiap_example/README.md`，验证 SmartConfig、多 AP、凭据保存和重连。

这样做的原因是：

- `static_ip_example` 的依赖更少，失败时更容易判断是 Wi-Fi 基础问题、路由器配置问题，还是示例逻辑问题。
- `wifi_netcfg_multiap_example` 更接近真实配网流程，但它引入了凭据管理、SmartConfig 桥接和状态流转，适合放在第二步。
- `apps/ai_sys/coze/` 里的 BLE 配网属于产品级路径，不适合作为通用“第一次功能自测”的第一站。

## 推荐下一步

- 静态 IP 示例：看 `static_ip_example/README.md`
- 多 AP 配网示例：看 `wifi_netcfg_multiap_example/README.md`
- 配网公共组件：看 `../components/wifi_netcfg/README.md`
