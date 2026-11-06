# 测试交接文档

## 基本信息

| 项目 | 内容 |
| --- | --- |
| 需求 | ECW6700-1500，按 auto-changelog 方案实施 Git Release Notes |
| 类型 | 新需求：Windows PC 端发布工具 |
| 日期 / 状态 | 2026-09-23；本地实现与研发试点完成，供测试复核；未正式发布 |
| 授权 | 用户“请实施上述MD方案文档”；基于方案 V1.4，实施记录更新为 V1.5 |
| 仓库 | `C:\Users\ESWIN\Downloads\ECW6630`，HEAD `0dc369b01b746eecf7af86f33fa1aa6012883bd6` |
| 交付范围 | 本目录新增文件及原需求方案的实施状态同步；未提交 Git |
| 工具基线 | `auto-changelog-2.6.1-local01`，逐文件指纹见 `tool-baseline.json` |
| 环境 | Windows 10、Windows PowerShell 5.1、Node v25.2.1、npm 11.6.2、Git 2.29.1.windows.1 |
| 固件 / 板卡 | 不适用；未编译、烧录或执行硬件测试 |

## 类型判断依据

用户要求实现既有方案，增加独立发布工具目录和生成能力；不修改固件功能，不属于芯片缺陷修复。

## 需求目的 / 问题背景

固定两个已有标签，从 Git 历史生成可追溯工程记录，并按人工确认的内容输出简明客户说明。集中维护在 `dbg_tools/Release_Tools`，依赖与产物置于仓库外，避免接入固件默认构建。

## 变更摘要

提供固定源码与锁文件、部署入口、生成入口、工程模板、客户输入样例和串行集成测试。客户记录按新增、优化、修复、适配分类，一行一个变化、事项编号置于句末，不展示条目提交 SHA；内部保留完整映射和版本级 SHA。

## 关键改动内容

`prepare-runtime.ps1` 校验源码指纹、Node/npm 版本并在仓库外执行固定依赖安装；有效运行副本可复用，不覆盖旧快照。

`generate-release-notes.ps1` 从自身位置确定 SDK 根目录，检查两端标签、祖先关系、排序、浅克隆及同名分支，校验 JSON/Markdown 的 SHA 覆盖，记录 manifest，末尾再次检查输入和运行依赖是否变化。所有生成结果均为候选草稿。

本机出现 Git 成功退出但管道 stdout 偶发为空。`src/process.js` 使用文件捕获并等待退出；`src/run-auto-changelog.js` 在私有进程内适配上游 `utils.cmd`，传播 Git 错误，不修改原始 vendor 文件。正常缺少 remote 的查询仍受支持。

## 影响范围

| 维度 | 说明 |
| --- | --- |
| 模块 | 仅本发布工具与方案文档 |
| API | 新增本地部署/生成 CLI；SDK 公共 API、固件接口未修改 |
| 配置 | 本目录 `.auto-changelog`；根 package.json 的同名配置会触发覆盖歧义错误 |
| 数据 | 内部 JSON/manifest 仅供工具追溯；不作为跨系统稳定协议 |
| 固件 | 无固件产物变化，Kconfig、Make.defs、defconfig 未修改 |
| 打包 | 现有 SDK 源码复制可能包含维护工具；本次未实施 D12 裁剪 |
| 兼容性 | 固定试点 Node/npm；更换工具内部结构或运行时版本需要重新验证 |

## 研发已验证

1. 原 auto-changelog 的 52 个文件与 vendor 副本逐字节一致；新增锁文件共形成 53 文件工具基线，维护目录无 `node_modules`。
2. 2 个 PowerShell 文件通过 AST 语法检查；项目 JS 已运行验证。
3. 最终串行集成测试 **22/22 PASS**。命令：`node dbg_tools/Release_Tools/test/run-tests.js <ReleaseWorkRoot>`。结果：`C:\Users\ESWIN\AppData\Local\Temp\ECW6630-release-work\tests\193fd416-4ec2-4fd1-83ff-c247c1548ebc\test-results.json`。
4. 真实 P21→P22 试点包含 16 个原始提交，JSON/工程模板覆盖 16/16，无需排除；从方案 9.3 输入生成 12 条客户候选，新增与优化的同事项条目保留独立映射。
5. 真实批次：`C:\Users\ESWIN\AppData\Local\Temp\ECW6630-release-work\output\11bafb07-bfc6-47c6-a249-7c7371c596be`。manifest 为 `draft`，`customerApproved=false`，耗时 4159 ms；记录的包装器 RSS 为 59531264 字节，**不是整个进程树峰值内存**。
6. 从仓库外目录调用相同脚本得到批次 `67de2e1c-64d0-4111-ae15-10edf914cb90`；工程 Markdown、JSON、raw、客户 MD、GitHub 正文五份文件与首批次逐字节一致，两批次 manifest 产物哈希有效。再次部署已有副本返回 `reused=true`，未重复安装。

集成测试覆盖：三类集合、超过默认条数、混合标签、中文路径与内容、无 remote/自建 URL、重跑保留人工内容、单行客户条目、同事项拆分、缺标签、非祖先、排序矛盾、空范围、浅克隆、package 覆盖、同名分支、revert、工具隐藏提交及人工理由、Git 非零退出、输出路径/junction、模板漏项、源码/锁/运行依赖漂移，以及生成期间标签/配置变化。

## sdk_diff Review

依据 `.ai/guides/sdk_diff_review_guide.md`，本次范围没有参与检查的 `.c/.h/.sh` 文件，基础 P0/P1 检查已跳过，未产生 P0/P1 命中；不能解释为所有代码风险已自动检查通过。

`review_patch_risks.py` 已对指定 JS/PowerShell 文件执行，该工具报告参与文件为 0，因此其零发现不作为工具代码正确性证据。当前助手另行检查了参数转义、路径落点、子进程退出、覆盖集合、输入冻结、失败状态及回退；以实际集成测试结果为主要研发证据。遵守项目规则，未组织并行代理或调用 OpenCode。

## 功能描述

输出工程 Markdown/JSON、原始提交、覆盖表、客户内容输入、客户 Release Notes、GitHub Release 正文及 manifest。无人工内容时保留明确待填写提示；提供 `ContentPath` 后渲染人工分类，仍标为草稿，不推断测试结果。

## 验收标准

自动范围必须准确、提交去向闭合、最终模板有对应 SHA/破坏性标记，输入发生变化必须失败，人工文档不能被重跑覆盖。客户条目无提交 SHA/链接，事项和拆分映射可追踪；最终客户验证/交付结论由项目确认。

## Demo / 操作步骤

在 SDK 根目录执行，`<ReleaseWorkRoot>` 替换为仓库外绝对路径：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\dbg_tools\Release_Tools\prepare-runtime.ps1 -ReleaseWorkRoot '<ReleaseWorkRoot>'
powershell -NoProfile -ExecutionPolicy Bypass -File .\dbg_tools\Release_Tools\generate-release-notes.ps1 -ReleaseWorkRoot '<ReleaseWorkRoot>' -PreviousTag 'ECW6700SDK_V1.0.0B01P21' -TargetTag 'ECW6700SDK_V1.0.0B01P22' -ContentPath '.\dbg_tools\Release_Tools\examples\p21-p22-content.json'
```

默认使用 `auto-changelog-2.6.1-local01`。已在 TEMP 下建立研发运行副本；该位置可被系统清理，正式使用应按项目归档策略部署到持久目录。执行策略参数只影响本次进程，不修改系统策略。

## 涉及 CLI 命令与说明

部署仅要求 `ReleaseWorkRoot`，可显式传 `ToolSnapshotId`。生成还需两端标签；可选 `ContentPath`、`CoveragePath` 和每次子进程 `TimeoutSeconds`。参数、字段格式、失败恢复及清理说明见 [README](README.md)。

## 测试建议

| 前置条件 | 操作 | 预期观察 |
| --- | --- | --- |
| 固定 Windows 环境、内存 ≥4096 MB | 部署后再次部署 | 第二次校验复用，不覆盖运行副本 |
| 完整试点历史 | 从仓库根目录和其他目录分别调用同一入口 | 处理同一 SDK，自动文件内容一致，批次目录不同 |
| 已运行普通批次 | 人工修改旧客户文件后重跑 | 旧内容保留，新文件在新批次 |
| 隔离 Git 样本 | 用缺失/同名/非祖先标签或浅克隆生成 | 非零退出，失败状态明确，无成功发布结论 |
| 人工输入含同事项新增和优化 | 提供 ContentPath 生成 | 两条独立记录，内部保留分别验证依据 |
| 模拟纯版本提交被隐藏 | 先直接运行，再逐条填写理由和确认人重跑 | 首次失败保留遗漏，确认后新批次成功为草稿 |
| 隔离配置/运行副本 | 删除模板、修改依赖或生成期间改标签 | 失败并记录原因，不覆盖上一成功批次 |
| 实际 SDK 打包环境及 D12 已决定 | 检查归档清单 | 无运行依赖/内部附件，维护工具包含或排除符合决定，原有交付文件完整 |

## 回归范围

普通标签、补丁、跨多版本范围、同事项多提交、单提交多条客户变化、三类破坏性记录、重跑/覆盖、中文、失败恢复、打包目录边界。运行工具升级或改变 Node/npm 后重跑固定样本；如客户要求首版/预览模式，应独立实现对应范围策略再验收。

## 风险与未覆盖项

- 实际 CI 平台、客户 Jira/Git 可访问性、SDK 归档清单（T26）未实测；维护目录可能随 SDK 交付，不能宣称已自动排除。
- 未运行上游完整开发测试套件，未验证最大历史跨度、进程树峰值内存及长时间任务超时后的所有 Windows 子进程场景；T20 只有当前样本的耗时/RSS记录。
- 首版无前驱、未打标签预览不在本入口范围，普通标签路径会明确失败，不伪造结果。
- 固件功能与性能测试未执行；示例候选和研发工具测试不代表 P22 功能已通过客户验收。
- Windows 文件捕获适配依赖当前上游私有 `utils.cmd` 结构；升级前须检验兼容性。直接绕过项目入口不能获得相同检查保证。
- 自动成功仍为草稿。发布内容、事项披露、交付物 SHA-256、已知问题与确认人需项目补齐。

## 信息来源

[需求与方案](../../ECW6700-1500/auto-changelog生成Git_Release_Notes需求与方案.md)、用户本轮实施授权、固定源码与锁文件、实际生成 manifest、串行测试报告及本目录 README。交接文档已落盘，未发送给其他人员。
