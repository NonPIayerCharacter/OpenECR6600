# 测试交接文档

## 基本信息

- 关联需求：ECW6700-1500；方案 V1.6，第 18 章为契约、第 19 章为实际结果。
- 日期：2026-09-24；状态：Windows 研发验证完成，原生 Linux 与实际 SDK 归档待验证。
- 实施依据：用户“请实施更新之后MD文档方案”。维护目录为 dbg_tools/Release_Tools。
- 类型：需求/功能优化；无固件编译、烧录或硬件改动。
- 需求/测试/发布负责人：待项目指定；本文件未发送给外部测试组。

## 类型判断依据

用户新增跨平台 Python 入口、tag/commit 范围、七文件输出及重要变更章节，并要求同一条目不重复展示，属于 PC 发布工具能力扩展。

## 需求目的 / 问题背景

V1.5 仅支持 Windows 标签入口，产物保存在外部工作目录并带有额外日志/输入副本。V1.6 统一命令，输出迁入 release，重要条目单独前置以便优先复测。

## 变更摘要

Python 负责范围、环境、客户内容和归档；Node 保留固定 auto-changelog 解析/模板能力。旧 PowerShell/JS 命令转发到 Python，SDK 两条裁剪路径排除生成的 release 子树。

## 关键改动内容

- 两端分别接受 tag 或 commit ID，冻结完整 SHA 后取 A..B；祖先、完整历史和非空范围为前置条件。
- 成功目录恰好七文件，manifest v2 合并人工输入/重要性/遗漏决定及六份文件哈希；失败返回非零并标记 failed。
- --important 选择客户条目 ID，或 --important-file 提供完整结构化条目；重要变更只展示一次，同 Jira/SHA 的其他独立变化保留。
- Windows 使用作业对象清理本次进程树，Git 输出通过临时文件捕获；Linux 分支使用进程组，尚未实测。
- 依赖固定 Node v25.2.1、npm 11.6.2，Python 使用标准库；V1.5 runtime 回执需要迁移到新目录准备。

## 影响范围

| 范围 | 影响 |
| --- | --- |
| 模块/CLI | Release_Tools 新增 Python 统一入口，旧参数转发；ReleaseWorkRoot 只选择依赖目录 |
| 配置 | 继续显式加载原 .auto-changelog/模板；不修改 vendor、配置内容或锁文件 |
| 数据 | manifest 升级为 schemaVersion=2；工程 JSON 的 diff 为完整 SHA，commit 目标 tag 为 null |
| 客户文档 | 有重要条目时增加第 2 章、后续顺延；无输入沿用原结构和 summary/待填写 |
| SDK 打包 | ECW6700/ECW6700F 对 release 子树增加最小排除；实际包清单待验证 |
| 固件/API | 未修改公共 API、共享头、固件组件、Kconfig 或编译入口；无新固件产物 |

## 功能描述

版本概述后可插入与普通分类同级的“重要变更”。数据仍保存原类别及验证依据；展示按唯一条目 ID 分配，不按 Jira/提交粗暴合并。GitHub 正文同步处理，并保持 Known Issues 在 Changelog 前。

## 验收标准

1. tag→tag、tag→commit、commit→tag、commit→commit 均只覆盖冻结的 A..B。
2. 每成功批次七文件；不覆写既有批次，失败不能被识别为成功草稿。
3. 重要条目只出现一次；同 Jira/SHA 的其他条目和内部映射不丢失；无重要输入保持原机制。
4. 新平台/版本/runtime 不匹配或输入漂移时明确失败，不自动安装 latest。
5. SDK 实际产物不包含 release 任意批次，其他包内容保持原策略；该项尚未实测。

## 研发已验证

环境：Windows 原生、Python 3.14、Node v25.2.1、npm 11.6.2、Git 2.29.1.windows.1。运行根目录为 `C:/Users/ESWIN/AppData/Local/Temp/ecw1500-v16-runtime`，仅作为本轮验证环境。

```text
python -B dbg_tools/Release_Tools/test/test_release_notes.py --runtime-root C:/Users/ESWIN/AppData/Local/Temp/ecw1500-v16-runtime
```

结果：21/21 PASS，97.004 秒。报告：`C:/Users/ESWIN/AppData/Local/Temp/ecw-release-tests-oy3s5es6/test-results.json`。测试隔离仓库在该目录内，不修改 SDK 历史。覆盖四范围组合、重要性/冲突/无输入、无效文件、标签/输入/源码漂移、回执/依赖指纹、浅克隆、错误和超时、junction、兼容命令及 SDK 静态排除连线。

Windows 超时曾因 taskkill 未可靠终止进程而失败，修复为作业对象后，父进程及继承捕获句柄的后代清理用例通过。最终全量结果包含该修复。

实际 P21→P22：[本轮样例目录](release/ECW6700SDK_V1.0.0B01P22_20260924T015553Z_acec56df2d83/)，16 个原始提交、覆盖 16/16、12 条客户候选、RN1/RN5 为重要条目，生成耗时 4119 ms。目录恰好七文件，manifest 六份文件哈希核对通过，draft/customerApproved=false。6 个同输入隔离批次的六份结果哈希一致，重跑未覆盖旧批次。

Python AST、Node 语法、两份 PowerShell AST 通过，release 的 Git 忽略检查通过。没有执行上游完整 npm 开发测试或固件构建。

## sdk_diff Review

依据 `.ai/guides/sdk_diff_review_guide.md`，在生成本交接文件前对本轮修改相对开工备份进行审查。参与文件为 dbg_tools/autobuild/6700_build_sdk.sh，本轮新增 3 行；既有用户改动保留，未扩展成全文件审查。

基础结果：P0=0，P1=0，AI 建议通过。新增内容为公共排除数组的一项和 ECW6700 裁剪函数中的说明/调用，复用已有安全检查函数；没有新增未校验的 rm -rf 变量操作，文件为 LF。

Python、JS、PowerShell、MD 不参与该基础 C/H/SH 规则，另以源码自检、语法及行为测试验证。接口/内存风险脚本参与文件为 0，不能将其零发现视为这些语言的自动审查通过。未组织并行代理或 OpenCode 评审。

## Demo / 操作步骤

在 SDK 根目录执行；首次在持久仓库外路径准备：

```text
python dbg_tools/Release_Tools/generate_release_notes.py --prepare-runtime --runtime-root E:/release-work/ECW6630-v16
python dbg_tools/Release_Tools/generate_release_notes.py --from ECW6700SDK_V1.0.0B01P21 --to ECW6700SDK_V1.0.0B01P22 --runtime-root E:/release-work/ECW6630-v16 --content dbg_tools/Release_Tools/examples/p21-p22-content.json --important RN1 --important RN5
```

按打印的 batch 打开 Release_Notes 文件：第 2 章包含 RN1/RN5，后文无相同条目；RN2 保留在功能优化。去掉 important 参数重跑：不再有重要变更章节，原分类恢复，旧批次不变。产品文字仍是候选，不能作为产品测试通过证据。

## 涉及 CLI 命令与说明

主要参数为 --from、--to、--version、--content、--important/--important-file、--coverage、--runtime-root、--prepare-runtime、--timeout。完整含义及人工 JSON 示例见 [README](README.md)。重要性参数互斥，范围两端必填；--version 只改展示，不改变提交范围。

## 测试建议

| 前置条件 | 操作 | 预期观察 |
| --- | --- | --- |
| 完整 Git 仓库及已准备 runtime | 对同范围运行四种端点组合 | 原始提交集合一致，commit 目标不冒充 tag |
| 现有人工样例 | 选择 RN1/RN5，再运行不带重要参数的批次 | 分类迁移/恢复正确，正文不重复，内部验证映射完整 |
| 可编辑的独立输入 | 传冲突 ID、范围外 SHA、坏 JSON；生成中修改文件 | 非零退出/failed，未扩大范围或生成伪成功 |
| 原生 Linux 且固定依赖版本齐备 | 运行同一 Python 测试及样例，检查 npm .bin 链接与超时 | 与 Windows 语义一致，无 PowerShell 依赖；记录真实平台证据 |
| 项目支持的 SDK 打包环境 | 包含成功/失败/临时历史批次后，分别打包 ECW6700/ECW6700F | 包中无 Release_Tools/release，其他清单与基线对比符合原策略 |

## 回归范围

工程 commits/merges/fixes 三类集合、SHA/模板覆盖、客户单行条目、同事项独立分类、输入与工具指纹、Git 错误传播、兼容入口、失败批次和 SDK 两条裁剪路径。

## 跨组件 / API 及回退

生成目录进入源码树后，仅 .gitignore 无法阻止 SDK 复制，工具内部无法单独解决；因此采用方案已授权的两个打包入口最小排除。替代的全外部输出不符合新需求，整个维护目录排除会扩大分发范围，均未采用。影响限于发布附件路径，不改固件公共 API。

回退应恢复完整 V1.5 工具和匹配运行副本，先恢复仓库外输出，再评估撤销 SDK 排除；不移动/删除历史批次，不改变标签。验证回退需在对应工具与打包环境进行，当前未实际执行成套降级。

## 风险与未覆盖项

- 原生 Linux、Python 3.10 最低版本、实际 SDK 归档和成套回退未执行；静态检查不能替代这些验收。
- 最大历史跨度、进程树峰值资源、实际 CI/客户链接与自动发布未验证。
- 内部 runtime/测试报告位于 TEMP，可能被系统清理；正式使用应选择持久位置并归档报告。
- 重要条目去重依赖人工复用同一 ID，无法自动识别不同 ID 的语义重复；分类和验证结论需要人工确认。
- 无未处理的本轮基础 P0/P1 提示；未参与该规则的文件类型如上，不宣称测试组已验收或客户已批准。

## 信息来源

- 用户当前及此前补充要求；[方案第 18/19 章](../../ECW6700-1500/auto-changelog生成Git_Release_Notes需求与方案.md)。
- 本轮入口、私有适配器、隔离集成测试及 SDK 裁剪的实际修改。
- 上述测试报告、真实 P22 样例及 manifest；历史 V1.5 记录单独保留，不冒充本轮证据。
