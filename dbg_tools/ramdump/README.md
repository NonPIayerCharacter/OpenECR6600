# dbg_tools/ramdump

## 目录职责

`dbg_tools/ramdump/` 保存 ramdump 导出工具、Trace32 脚本和使用资料，是故障分析入口之一。

## 当前内容

- `dump_tool/dumptool.exe`：ramdump 工具
- `t32_scripts/`：Trace32 脚本和菜单
- `Ramdump工具使用指南.docx`：使用说明

## 什么时候从这里进入

- 需要导出或解析 ramdump。
- 需要配合 Trace32 做任务、堆或内存分析。

## 推荐下一步

- 现有说明：看 `Ramdump工具使用指南.docx`
- 如果要做源码级 crash 分析，可结合 `ecr6600-crash-analysis` 工作流
