# dbg_tools

## 目录职责

`dbg_tools/` 保存调试、联调、辅助构建和交付工具，是“非源码工具入口”的聚合目录。

## 什么时候从这里进入

- 需要找调试 APK、自动构建脚本、Matter 联调工具、ramdump 工具或语音转换脚本。
- 需要定位某个工具属于研发辅助、测试联调还是问题分析。

## 关键内容

- `App/`：调试用 APK，如 SmartConfig 和 Wi-Fi Mesh
- `autobuild/`：SDK 自动构建、静态检查和 OTA 打包脚本
- `Dev_Tools/`：开发工具和打包说明
- `matter/`：Matter 联调 APK 和 chip-tool 相关工具
- `ramdump/`：ramdump 导出工具、Trace32 脚本和使用文档
- `yuyin/`：音频格式转换脚本
- `Burn_Tools.tar`：烧录相关工具包

## 与构建/配置的关系

- 这里通常不直接参与固件编译。
- 更适合做辅助开发、联调、问题分析和交付。

## 推荐下一步

- 调试 APK：看 `App/README.md`
- 自动构建：看 `autobuild/README.md`
- 开发工具：看 `Dev_Tools/README.md`
- Matter 联调：看 `matter/README.md`
- Ramdump：看 `ramdump/README.md`
- 语音工具：看 `yuyin/README.md`
