# Git Release Notes 工具（V1.13.1）

统一使用 Python 入口，为两个 tag 或 commit ID 的 `A..B` 范围生成 Release Notes。基线 A 不包含在结果中；两端必须有祖先关系，不能省略一端后自动猜测。

成功结果位于本目录 `release/<版本>_<批次>/`，每批八个文件，新增研发用 `JIRA_Status_Review_<版本>.md`。JIRA 概要含 `【重要】` 的关联条目自动移到版本概述之后的“重要变更”，后续分类不重复；没有自动或人工重要条目时保持原章节结构。产品名为空、空白或“待填写”时，标题直接采用 `<版本> Release Notes`。

## Merge / Revert 自动整理（V1.13.1）

无需增加参数，CLI 与网页均自动生效，适用于所有 JIRA 项目。升级后重启网页服务并重新生成；现有有效 runtime 无需重装。

| Git 历史 | 客户正文处理 |
| --- | --- |
| 普通提交及对应的纯 Merge 流程记录 | 隐藏 Merge，保留实际提交；不按同一 JIRA 编号合并不同功能 |
| 原提交和完整 Revert 均在 A..B 内 | 证明完整抵消后隐藏两笔 |
| 原提交 → Revert → 再次 Revert | 保留恢复后的原始变更，隐藏后两笔；再次回退则全部抵消 |
| 原提交在基线之前、本次只有 Revert | 保留撤销变化并提示复核 |
| 冲突解决、额外合并代码、部分回退、Merge 的回退或复杂重引入 | 保留待复核，不能按标题或 JIRA 强行删除 |

`release_history.py` 根据父提交识别真实 Merge。不同文件的合并比较树差异；双方修改同一普通文本文件时，用旧版 Git 也支持的 `git merge-file -p` 计算三方结果，并与实际合并结果逐字节比较。结果不一致、存在冲突、自定义合并驱动、同文件二进制歧义、单文件超过 1 MiB、八爪鱼合并或取证失败时保留复核。仅在临时目录操作，不修改 SDK 工作区、索引或 Git 对象库。

Revert 必须有明确完整目标 SHA，目标是范围内祖先，且两笔均为单父提交。路径集合相同、文件模式和完整 blob ID 变化互逆时直接抵消；同一普通文本文件夹杂其他独立修改时，再分别计算“撤销原改动”和“重新应用原改动”的三方结果，两向均无冲突且与实际结果逐字节一致才抵消。部分回退、夹带额外修改或无法取证时保留。不按标题相同、JIRA 相同或时间相邻配对；正常再次 cherry-pick 的新提交仍单独保留；同一个目标被多笔 Revert 引用时，相关回退链保守保留复核。

历史筛选先于重要条目提升和 JIRA 状态筛选。人工条目仅对应已隐藏提交时一并移除；人工条目同时包含有效和已抵消提交时会要求先拆项，避免保留过期文字。人工条目仅附带纯 Merge 时移除 Merge 映射，保留有效提交。输入 JSON 不会被改写；已隐藏的重要 ID 不会再次显示。

两份客户正文隐藏可确认的记录，`CHANGELOG.generated.md`、`release-data.json` 和 `raw-commits.txt` 保留相关完整工程历史。`coverage-review.md` 记录每笔原始标题、隐藏/待复核原因、关联 SHA 和中文证明方法；未抵消的 Revert 会说明目标缺失、路径不同、三方冲突、实际结果不符等具体原因。manifest 保存 `historyAnalysis`、`classification.historyEvidence`、`customerCoverage.historyExclusions` 和移除的客户 ID。页面及正文区分原始提交数、隐藏纯 Merge 数、抵消提交数（每对计 2 条）、有效候选提交数以及状态筛选后的客户条目数。JIRA 查询、真实事项计数和状态研发清单仍覆盖整个原始范围。

历史证据纳入快照摘要及源码指纹；重放时重新核对本地 Git 历史，JIRA 完全离线。升级前的旧快照需重新在线生成。成功结果仍为八文件。

### 同一 JIRA 为什么可能分到不同章节

按每笔提交表达的实际变化分类，JIRA 编号用来关联事项，不把同一事项的所有提交强制放进同一章节。已配置的需求、任务、子任务类型仅在提交无法明确分类时兜底；人工分类及既有缺陷/优化直接映射优先级保持原规则。

| 同一需求下的提交示例 | 分类及依据 |
| --- | --- |
| 新增源码实现 UART 驱动 / add pinmux cfg for uart | 新增功能：明确新增功能对象 |
| update build script / add api-guide | 构建、测试与产品适配变化：明确构建脚本或文档 |
| adapt customer api | 描述不明确时，采用已配置 JIRA 需求类型的新增功能兜底，并提示复核 |

V1.13.1 收紧构建适配规则，不再仅因“源码、目录、pinmux、adapt API”等词判定为构建适配。明确的旧源码删除、命令代码迁移、宏维护仍可归该类；同一条描述包含多类动作时继续提示复核。没有足够依据的 Merge/Revert 不用 JIRA 类型强行归类。

具体依据见本批次 `coverage-review.md` 的“分类 / 依据来源”“命中规则”“复核原因”；网页可从“其他生成文件”下载。客户正文仍保持简短条目格式。

### V1.13.1 跨组件 / API 变更

无跨组件、固件公共 API 或 CLI 参数变更。修改局限于本模块历史证明、分类规则及覆盖报告；历史记录增加 `detailCode`，规则版本为 `effective-history-v2` / `customer-classification-v3`。保留逐提交分类及工程原始记录，避免按 JIRA 强制统一类别或按标题删除 Revert。内部快照的规则/源码指纹会变化，旧快照需在线重新生成；runtime、Node 适配器及八文件输出约定不变。验证使用临时 Git 仓库覆盖同文件完整/部分回退、附加修改、冲突、分类、完整生成及离线重放；回退时一并还原本次 Python 修改并重新生成归档。

### V1.13 跨组件 / API 变更（历史）

无 SDK 固件组件、公共 API、CLI 参数或外部接口变更，修改仅在 Release_Tools。新增本地历史分析模块并扩展 manifest/网页结果统计，现有字段继续保留；不采用全局 `--no-merges`、按 JIRA 去重或删除原始记录。Node 适配器通过现有插件扩展点保全被上游过滤的历史关联记录，vendor 和 runtime 不变。验证覆盖真实 Git 图、完整工程覆盖、两份正文、人工映射、状态筛选、重要条目和离线快照；需要回退时整组还原本轮工具文件并重新生成，不手改归档。

## 推荐：打开本机页面，只填两个版本

在 **`dbg_tools/Release_Tools` 目录**执行，Ubuntu 使用 `python3`：

```text
python generate_release_notes.py --prepare-runtime
python release_notes_web.py
```

第一条仅首次准备依赖时需要。第二条启动时提示输入缺失的 JIRA 用户名和隐藏密码；终端已有 `ECW_JIRA_USER`、`ECW_JIRA_PASSWORD` 时直接使用。凭据只保存在服务进程内存中，关闭后释放。已有 runtime 校验有效时可直接执行第二条，无需重新安装依赖。

浏览器访问终端显示的 `http://127.0.0.1:8765/`，填写“基线版本”和“目标版本”（tag 或 commit ID），选择发布说明范围，点击“生成 Release Notes”。范围默认“全部记录”，也可选“仅包含指定 JIRA 状态的记录”。完成后可预览正文，通过两个主下载按钮分别下载 Release Notes 和完整工程 CHANGELOG。仍需本机已有两个 Git 对象和完整历史。

页面主统计显示 **Git 提交数、关联真实 JIRA 数、未提交状态 JIRA 数**；下方显示纳入/筛除条目数、状态未知数及无编号提交数。点击“查看未提交状态 JIRA”展开概要、当前状态、当前处理人、类型、JIRA 链接、关联提交及研发清单下载；默认折叠，刷新页面或开始新任务后恢复折叠，同一任务更新时保留用户的展开选择。状态未知和无编号提交另列。原“查看复核报告”入口移除，coverage-review.md 仍在内部归档和“其他生成文件”中。

下载的 `JIRA_Status_Review_<版本>.md` 使用纯文本 JIRA 编号，不再生成编号链接，并增加“当前处理人”列（显示名及可用账号）。处理人来自本批查询的 assignee；未分配、未取得及别名查询不一致分别明确标注。页面和 MD 使用同一快照，不代表查看时的实时处理人；更新名单需重新在线生成。

同一时刻只生成一批；刷新页面恢复最近任务及所选范围，关闭浏览器不会取消生成。终端按 Ctrl+C 关闭服务，会先等待当前批次结束。页面历史仅保留本次服务最近八个任务，重启后历史任务仍在磁盘 `release/` 中。升级脚本后请重启页面服务并刷新浏览器；现有有效 runtime 无需重新准备。

默认配置按工具所在目录查找：`jira-config.local.json` 优先，其次 `examples/jira-config.example.json`；错误的本地配置会报错，不自动换用示例。可通过 `python release_notes_web.py --jira-config <配置路径> --runtime-root <依赖目录> --port 8766` 覆盖启动设置；这些设置一次启动后用于所有任务。配置或 CA 文件更改后需重启服务。仅监听本机，不需要 npm 前端安装，也不提供远程团队登录。

## 项目配置与跨工程部署

CLI 和网页默认读取工具目录的 `release-project.json`，内容如下；ECW 现有生成行为保持原默认值：

```json
{
  "schemaVersion": 1,
  "defaultScope": "ECW6630、ECW6700",
  "webTitle": "ECW SDK",
  "fromPlaceholder": "ECW6700SDK_V1.0.0B01P21",
  "toPlaceholder": "ECW6700SDK_V1.0.0B02P03",
  "acceptedJiraStatuses": ["等待确认", "已解决", "已关闭"]
}
```

`defaultScope` 用于自动生成及人工 scope 留空时的默认适用芯片；`webTitle` 用于网页标题和页头，不替换客户文档的版本标题；两个 Placeholder 仅提示输入，不限制实际 tag 名称。`acceptedJiraStatuses` 同时控制网页提示、状态计数、指定状态筛选、Release Notes 和 JIRA 状态 MD 的说明；按去除首尾空白后的名称精确匹配。配置必须完整、合法，缺失或错误时直接报错，不回退到其他项目默认值。

换工程时，在具备完整版本历史的目标 Git 根目录下放置完整 `dbg_tools/Release_Tools`，保留 `vendor`、`src`、`templates`、`web`、基线文件和隐藏配置。只需按目标工程调整以下配置，无需修改 Python/HTML：

| 配置 | 调整内容 |
| --- | --- |
| `release-project.json` | 适用芯片、网页名称、示例标签、JIRA 指定状态 |
| 实际生效的 JIRA 配置 | 项目 key、经核实的类型映射、实例及认证环境变量设置；本地配置优先规则仍有效 |
| `.auto-changelog` | issuePattern 加入目标 JIRA 前缀；例如正则 `(?:ECR6600|ECR6600A)-\d+`，写入 JSON 时反斜杠须转义 |

已提供 `examples/release-project.ecr.json`，可以复制为默认文件，也可在两个入口使用 `--project-config PATH` 指定；相对路径按启动命令的工作目录解析。例如在工具目录启动：

```text
python release_notes_web.py --project-config examples/release-project.ecr.json --jira-config examples/jira-config.ecr.example.json
```

CLI 同样支持这些参数，不需要为了覆盖芯片而提供完整人工 `--content`。ECR 项目展示示例使用 ECR6600 芯片和现有三种状态作为起点；JIRA 示例配置 ECR6600/ECR6600A 两个项目，V1.12.2 已按企业 JIRA 项目元数据补齐类型映射，规则见下表。已有专用配置时，将对应 projects 内容合并到实际生效的配置，保留认证、CA、预算和经过确认的自定义映射。`.auto-changelog` 的编号规则仍需单独调整。`E:/project/AI/git/ECR6600` 核查时只有一个导入提交和一个基线标签，不能替代原项目版本历史。

项目配置在网页启动时冻结，变更后需重启服务并刷新页面；每批 manifest 保存 `projectConfig`，配置文件参与输入变化检查，其有效内容参与快照校验。同源码、同项目配置可以离线重放；修改芯片、状态或网页配置后需重新在线生成，旧源码快照同样需重生成。现有有效 runtime 可复用，输出仍为八文件。

### ECR 类型分类规则（V1.12.2）

上一版 ECR 示例只启用了查询项目，issueTypeMap/fallbackIssueTypeMap 均为空，导致需求、改进及缺陷的 JIRA 类型不能参与分类。本次于 2026-10-16 只读查询企业 JIRA 的两个项目元数据后补齐配置；沿用现有分类算法，不按类型名称模糊匹配。

| JIRA 类型 ID / 名称 | 项目 | 发布分类 |
| --- | --- | --- |
| 10100 / 改进 | ECR6600、ECR6600A | 功能优化 |
| 10102 / 缺陷；10603 / 客户问题 | ECR6600、ECR6600A | 问题修复 |
| 11100 / IC缺陷 | ECR6600A | 问题修复 |
| 10604 / 需求；10007 / 任务；10304 / 子任务 | ECR6600、ECR6600A | 无法明确分类时兜底到新增功能 |

需求/任务/子任务不覆盖提交中明确的修复、优化或构建适配分类。未被历史规则证明可隐藏的 Merge/Revert、多事项分类冲突、未知类型仍要求复核。ECR6600 的 10006“问题”描述为“跟踪事件的根本原因。由 Jira Service Desk 创建。”，不等同软件缺陷，本次未强行映射。

**更新配置后重启网页并重新在线生成。** 如果使用 jira-config.local.json 或其他 --jira-config 路径，必须同步其中两个项目的映射；只更新示例不会覆盖它们。旧 manifest 快照包含旧类型映射，离线重放会保留旧分类，不能用它验证新配置。现有有效 runtime 无需重新准备。

### V1.12.2 跨组件 / API 变更

无跨组件、固件或公共 API 变更；生产变更仅为 ECR JIRA 示例配置。缺口由空映射引起，采用项目级已核实 ID 映射，避免修改全局分类优先级或按名称猜测类型。ECW 配置、HTTP/CLI 参数、manifest schema 及八文件结构保持原契约。验证涵盖空映射复现、两个项目已知类型、明确提交/人工优先、冲突边界、完整生成/状态筛选及快照；回退恢复本次两个项目映射并重启服务，历史批次保留。真实元数据及验证记录见方案 20.28。

### ECR 工程出现“JIRA 查询不完整”

该错误表示至少一个事项未成功取得，或概要被截断；不能仅凭这句话认定是项目、权限或网络问题。V1.12.1 会在终端及页面直接显示生效配置路径、具体原因码、数量、代表编号和处理建议，coverage-review.md 保留全部失败编号、HTTP 状态和尝试次数。失败时保留原始提交与 manifest，不生成不完整的客户正文。

| 原因码 | 处理方式 |
| --- | --- |
| project_not_configured | 在**实际生效的 JIRA 配置**的 projects 中加入 ECR6600 和 ECR6600A；仅改 release-project.json 或 .auto-changelog 不会启用查询 |
| auth_failed / auth_aborted | 修正认证或解除 JIRA 验证码限制，再重试 |
| forbidden / not_found_or_not_visible | 用同一账号确认事项编号和浏览权限 |
| budget_exhausted / timeout | 核对网络及请求耗时，再调整 totalBudgetSeconds / timeoutSeconds |
| tls_error / network_error | 核对 CA、网络、DNS 与代理 |

配置选择顺序是显式 `--jira-config`、工具目录 `jira-config.local.json`、默认 ECW 示例。修改示例后如果仍使用本地配置，或网页未重启，改动不会按预期生效。项目 key 使用 ECR6600/ECR6600A；tag 中的 ECR6600F 是产品版本名，不作为 JIRA 项目 key。

在工具目录使用明确的两个配置生成：

```text
python generate_release_notes.py --from ECR6600F_1.0.1 --to ECR6600F_1.0.9 --project-config examples/release-project.ecr.json --jira-config examples/jira-config.ecr.example.json --jira-prompt
```

若旧失败报告显示其他原因，应按原因处理；新增项目配置不能解决账号权限、错误编号或网络超时。V1.12.1 的空映射示例只能打通查询；V1.12.2 已补齐上述类型映射。当前 Windows 验证不代表已访问 Ubuntu 的失败批次。

### V1.12.1 跨组件 / API 变更

无固件、SDK 公共 API 或外部组件变更。仅给生成器内部完整性检查增加可选配置路径参数，扩充既有错误说明和失败报告，并新增 ECR JIRA 配置示例。选择保留“查询完整后生成”边界，避免以忽略失败事项或关闭 JIRA 的方式丢失分类、重要标记和状态依据。HTTP 字段、manifest schema、成功批次八文件结构均不变，网页沿用已有错误展示。验证双项目配置缺失/恢复、权限及超时等原因、完整生成与快照；回退恢复生成器及配套说明并移除新增示例/测试，旧批次保留。源码指纹变化后旧快照仍按既有规则重新在线生成；runtime 无需重装。

## 当前流程与结果怎么看

1. Python 冻结两个 tag/commit 的范围并读取全部提交；默认查询 Jira，结合提交描述和事项类型生成分类候选，依据事项概要的 `【重要】` 标记确定展示位置。
2. Node 适配器复用 auto-changelog 的解析和模板函数，使用 `engineering.zh.hbs` 生成工程记录，并检查提交覆盖。
3. Python 组织客户章节、重要变更及单行条目，生成 Release Notes 和 `github-release-body_<版本>.md`；Release Notes 的文件名按下表区分模式。

| 文件 | 作用 |
| --- | --- |
| `Release_Notes_<版本>.md` / `Release_Notes_<版本>_accepted.md` | 分别对应全部记录 / 指定状态版；每批只生成所选模式的一份客户发布说明 |
| `github-release-body_<版本>.md` | 同一批内容的发布正文 |
| `coverage-review.md` | 先看“优先复核”，处理问题标题、复合变更、Git/Jira 冲突，再核对提交覆盖 |
| `JIRA_Status_Review_<版本>.md` | 研发状态清单：范围、查询时间、模式、统计、未提交/未知状态事项、无编号提交及筛除原因 |
| `generation-manifest.json` | 客户条目、分类依据、Jira 查询结果、有效日期/芯片及统计快照 |
| `CHANGELOG.generated.md` / `release-data.json` | auto-changelog 生成的工程记录 / 工程结构化数据 |
| `raw-commits.txt` | 冻结范围内的原始提交清单 |

发布日期默认取**本次生成机器的系统本地日期**（`YYYY-MM-DD`），适用芯片读取项目配置的 `defaultScope`（随工具提供的默认值为 **ECW6630、ECW6700**）；两份正文使用同一日期。人工 `releaseDate` / `scope` 非空时优先，空白时采用默认值。芯片名称不等于所有板型、配置均已验证，具体条件仍由项目确认。

客户正文没有内容的章节保留标题，正文留空，不输出“待填写”或空分类占位提示；重要条目移出后的空分类同样留白。人工章节字段为空、空白或仅为“待填写”时留空，实际填写的说明保留。Release Notes 与 GitHub 正文采用同一规则；概述中的统计、筛选后零条目的说明继续保留。V1.11.5 删除两份文档标题下的固定生成提示，标题后直接展示发布日期；网页成功状态显示“生成完成 / 已生成”。

终端和客户发布说明同时显示 JIRA 兜底数量、需人工复核数量。**待分类为 0 不代表无需复核，也不代表测试通过。** 例如原 P21→P22 样例的 16 条记录中，7 条按 Jira 兜底、10 条需复核。

只想突出自动生成的条目时，用 `--important AUTO_<完整SHA>`。`--content` 是完整人工内容替换，独立非空 `--important-file` 只生成其中的人工条目；它们不是增量补充，缺口会在结果中提示。示例 `p21-p22-content.json` 已包含 15 条客户记录、覆盖全部 16 条提交，并将独立变化分开。

## 状态筛选与计数

指定状态精确匹配项目配置的 `acceptedJiraStatuses`，默认 **等待确认、已解决、已关闭**（去除首尾空白）；不根据 resolution、类型或英文相似名称猜测。全部模式保留原候选集合；筛选模式要求条目中所有提交均带 JIRA 编号，且所有关联事项均属于指定状态。任一事项不满足、状态未知或提交无编号，整个客户条目排除；人工合并条目不自动截断，重要标记不绕过筛选。查询失败仍按现有失败策略处理。

Git 提交按 SHA 去重；关联 JIRA 按成功查询的 issueId 去重，同一事项多个编号或多次合入只计一次。统计和研发清单覆盖整个 A..B，不随发布说明筛选缩小。别名查询返回状态不一致时视为未知，不纳入筛选版。“未提交状态”是原“非指定状态”的展示名称，指不属于配置指定状态的已查询事项，不表示 Git 提交未合入。状态未知与未提交状态分别计数。旧 distinctIssueKeyCount 仍为主题中不同编号数，正文明确称“关联编号”，不等同于真实事项数。

筛选同时作用于 Release Notes 和 GitHub 正文；`CHANGELOG.generated.md`、raw 与 release-data 仍为完整范围。筛除仅影响文档，不会移除目标版本代码。概述注明模式和纳入/排除数量；结果为零时显示“本次范围没有符合指定 JIRA 状态的客户变更”。状态取查询时快照，不能解释为目标版本发布时的历史状态或测试通过结论。

CLI 默认全部；生成筛选版时增加：

```text
python generate_release_notes.py --from <基线tag或commit> --to <目标tag或commit> --jira-prompt --jira-status-scope accepted
```

同源码快照也可使用 `--jira-status-scope accepted`，不重新查询 JIRA。`--no-jira` 只支持全部模式。人工排除、状态筛除和未说明遗漏分别记账，不能互相抵消；人工输入原有映射冲突在筛选前校验。

## 直接生成 Jira 辅助分类的 Release Notes

已准备 runtime 后，在 **`dbg_tools/Release_Tools` 目录**执行：

```text
python generate_release_notes.py --from ECW6700SDK_V1.0.0B01P21 --to ECW6700SDK_V1.0.0B02P03 --jira-config examples/jira-config.example.json --jira-prompt
```

Ubuntu 将 `python` 换为 `python3`。按提示输入用户名和隐藏密码即可；本机必须已有两个 tag 及完整历史。脚本先从全部提交主题提取 Jira 单号并去重查询，再结合提交动作、变更对象和事项类型分类，最终生成八个文件。结果查看终端给出的 `batch` 目录。

自动化任务通过环境配置注入 `ECW_JIRA_USER`、`ECW_JIRA_PASSWORD`，命令去掉 `--jira-prompt`。账号密码不写入脚本。需要调整映射时复制示例为 `jira-config.local.json`；本地配置已加入忽略规则。**默认在线查询 Jira**，可省略 `--jira-config`，按上述优先级查找配置；`--jira-prompt` 可以独立使用。没有凭据或查询失败时明确报错，不自动退回本地生成。

默认支持 ECW6700、ECW6630、ECW6280。2026-10-15 已通过 JIRA 项目只读接口核实三个项目的类型 ID 和名称；当前示例映射如下：

| 项目 | issueTypeMap（直接分类） | fallbackIssueTypeMap（仅待分类兜底） |
| --- | --- | --- |
| ECW6700、ECW6630、ECW6280 | `11100/IC缺陷`、`11101/TS缺陷`、`10603/客户问题` → fixes；`10100/改进` → improvements | `10604/需求`、`10007/任务`、`10304/子任务` → features |
| ECW6630 额外类型 | 无新增直接映射 | `10512/用户需求`、`10509/System Requirement` → features |

三个项目的需求、任务/子任务均仅在待分类时兜底，保留明确的 Git 修复/优化/适配分类和人工分类；兜底条目仍标记需复核。Risk、测试用例不强制映射；ECW6630 的 `10006/问题` 在 JIRA 中定义为“跟踪事件的根本原因”，不能仅凭类型认定代码修复，因此也不强制映射。`13003/客户送样单` 不作为缺陷映射。

V1.11.4 将 V1.11.3 的 ECW6280 任务/子任务兜底扩展到 ECW6700、ECW6630，三个项目统一归入“新增功能”兜底。例如 ECW6280-36 的普通提交 `add ad9613 PWDN always ok`、`add MCP4811 for CFO` 可归入“新增功能”。这是项目认可的兜底展示规则，不认定所有任务都是新功能；对应 Merge/Revert 提交仍按原规则核对实际效果，多事项分类冲突继续待分类。该事项核实时处于“处理中”，全部记录版可展示，指定状态版仍排除。

自定义配置需分别在 `projects.ECW6700.fallbackIssueTypeMap`、`projects.ECW6630.fallbackIssueTypeMap`、`projects.ECW6280.fallbackIssueTypeMap` 中补齐 `"10007": "features"`、`"10304": "features"`，保留已有 `10604` 等映射。重启页面服务并重新在线生成即可应用，无需重装 runtime；历史快照继续使用其中保存的配置。

JIRA 编号从范围内提交标题提取，不按 tag 名称限制项目；例如 ECW6700SDK tag 间的 ECW6280 提交同样查询、分类、识别重要标记并参与状态筛选。`.auto-changelog` 的 issuePattern 已同时覆盖三个前缀，ECW6280 在工程 JSON/Markdown 中也可识别为关联事项。类型不在映射中时继续依赖明确的 Git 描述；证据不足则保留待分类。

使用自己的 `jira-config.local.json` 或 `--jira-config` 文件时，请按上表合并缺少的映射，保留项目自定义设置。仅更新示例不会自动改写另一个配置文件。默认本地配置优先于示例，配置修改后需重启网页服务。

明确新增分区/命令归新增，故障规避归修复，API 文档、构建脚本、目录维护归适配。已有明确分类及人工内容继续优先；剩余待分类条目若所有关联 Jira 均取得有效且一致的映射，则按 Jira 归类。冲突、复合、问题标题或模糊同步的原因保留在 `coverage-review.md`，标记 `jira_category_fallback` 和 `needsReview=true`；原文及未验证状态不变。类型无法映射、多 Jira 类型冲突、发布字段无效及 merge/revert 净效果不明时仍保留待分类。

范围内事项查询存在 failed/skipped、总状态非 complete 或 summary 截断时，本批失败，不生成客户正文；`raw-commits.txt`、failed manifest 和 `coverage-review.md` 保留诊断信息。配置/认证预检失败更早发生，可能仅有 manifest。没有 JIRA 编号的提交仍保留，按本地规则分类并计入统计；不伪造编号，也不扩展 Git 范围。

未加 fallbackIssueTypeMap 的旧配置继续有效，但不能使用对应需求类型的新增兜底。V1.11.2 更新 `.auto-changelog` 和示例配置即可使用三个项目的映射，无需重新准备 runtime。重新在线生成应用新映射；`--jira-snapshot` 使用历史批次保存的映射，更新示例不会改变旧快照的分类依据。

同范围、同工具源码重放已有查询结果，完全离线且不需要凭据：

```text
python generate_release_notes.py --from ECW6700SDK_V1.0.0B01P21 --to ECW6700SDK_V1.0.0B02P03 --jira-snapshot release/<原批次>/generation-manifest.json
```

`--jira-config`、`--jira-snapshot`、`--no-jira` 三者互斥。快照按所保存的概要重新识别重要项，不联网补查；不完整快照同样不能生成成功。当前分类规则为 `customer-classification-v2`；V1.11 修改了生成源码并加入 release_status.py 指纹，旧批次需重新在线生成，新批次支持同源码离线重放。发布日期仍按本次系统日期填写，因此跨日重放的日期会变化；需要固定日期时在完整人工 content 中明确填写。原 `--content`、`--important` 等编辑参数需自行再次传入，不会从历史批次自动恢复。

仅排查本地生成链路时，可显式使用 `--no-jira`；该模式不查询 JIRA、不自动识别重要事项，不能代替日常 JIRA 辅助生成。网页固定使用在线模式。

## Jira 独立查询与诊断

先在 `dbg_tools/Release_Tools` 目录执行以下命令，按提示输入用户名和密码；密码隐藏输入，不保存在文件中：

```text
python jira_client.py --config examples/jira-config.example.json --issue ECW6700-1434 --prompt
```

Ubuntu 使用 `python3`。仅需 Python 3.10+，查询本身不需要 Git、Node/npm 或 runtime。可重复 `--issue` 查询多个事项，同一编号只查询一次。其他工作目录下请使用脚本和配置的相应路径。

示例使用 `https://jira.eswincomputing.com`，已核对该地址返回 Jira Server 8.5.1，HTTPS 和 Basic 用户名/密码方式的三项只读查询成功。服务返回的 HTTP baseUrl 不会触发降级或重定向。配置中只有环境变量名，未保存账号密码。自动化运行时通过执行环境提供 `ECW_JIRA_USER`、`ECW_JIRA_PASSWORD`，去掉 `--prompt`；没有密码命令行参数。

成功输出 JSON：`status=complete`、`counts.fetched` 为已取到的不同事项数，`results.<key>.fields.issueType` 已含 `id` 和 `name`。`unmapped_issue_type` 表示该类型未配置发布分类，**不是查询失败**。客户端只取数；最终分类由生成入口处理。

退出码：0=全部取数成功；2=部分/全部事项未取得（查看各项 reasonCode）；1=配置/内部错误；130=交互中断。401/CAPTCHA 会停止后续认证尝试；403/404 保留失败记录并继续其他事项。TLS 校验始终启用，不跟随登录跳转，不支持明文 HTTP。JSON 只写标准输出，不自动创建额外 release 文件。

生成主脚本已接入客户端。内部接口仍为 `load_jira_config(path) → (config, auth_context)`、`fetch_issues(keys, config, auth_context) → report`；auth_context 不序列化。

离线自测（不使用真实账号、不访问网络）：

```text
python -B test/test_jira_client.py -v
python -B test/test_release_diagnostics.py -v
```

完整生成和分类回归（测试使用隔离 Git、模拟 Jira，串行执行）：

```text
python -B test/test_release_classification.py -v
python -B test/test_release_history.py -v
python -B test/test_release_notes.py --runtime-root <已准备的仓库外runtime-root>
python -B test/test_release_web.py
python -B test/test_release_status.py
node test/test_release_web_ui.js
```

历史测试见方案 20.16—20.29；V1.13.1 历史分析、分类及生成回归见 20.30。未重跑无关的外部 SDK 打包检查，V1.10 已记录的排除规则缺失仍待处理。原生 Ubuntu、真实浏览器视觉操作及目标版本范围完整联调未在本轮执行。

## 最常用的操作

在 SDK 根目录打开终端，首次准备运行环境：

```text
python dbg_tools/Release_Tools/generate_release_notes.py --prepare-runtime
```

日常仅指定两个 tag/commit 即可生成自动候选。例如，**已进入 `dbg_tools/Release_Tools` 目录时**：

```text
python generate_release_notes.py --from ECW6700SDK_V1.0.0B01P21 --to ECW6700SDK_V1.0.0B02P03 --jira-prompt
```

脚本会把范围内提交主题转换为客户条目，不再仅输出空分类；前提是本机 Git 中存在两端标签及完整历史。在 SDK 根目录运行时，脚本路径应写为 `dbg_tools/Release_Tools/generate_release_notes.py`。

若需使用已有人工样稿，以下命令在 **SDK 根目录**生成 P21→P22，将 RN1（新增吞吐配置）和 RN5（CIPCLOSE 修复）放入重要变更：

```text
python dbg_tools/Release_Tools/generate_release_notes.py --from ECW6700SDK_V1.0.0B01P21 --to ECW6700SDK_V1.0.0B01P22 --content dbg_tools/Release_Tools/examples/p21-p22-content.json --important RN1 --important RN5
```

终端打印 `batch`，打开该目录的 `Release_Notes_*.md` 即可查看客户发布说明。`github-release-body_*.md` 用于 GitHub 正文，其余五份为内部核对材料。生成不会发布文档、创建或移动标签，也不会自动认定产品测试通过。

只有未选人工重要条目且关联 JIRA 概要均无 `【重要】` 时，才不生成重要变更章节。去掉 `--important` 不会关闭自动识别。未提供 `--content` 且没有独立重要文件中的人工条目时，自动生成提交候选和提交数量概述，规则见下文；提供人工输入时保留人工内容路径。其余未带 `--jira-prompt` 的在线命令示例假定已配置认证环境变量。

Linux 使用相同脚本和参数，解释器通常写为 `python3`。**当前已在 Windows 原生环境验证；原生 Linux 和实际 SDK 打包验收尚未执行。** 不要求安装固件交叉编译器。

## 环境与目录

依赖 Python 3.10+、Git、Node.js；首次准备还需要 npm。V1.7 取消必须使用 Node v25.2.1/npm 11.6.2 的限制。V1.7.1 将预检、生成适配器和兼容入口统一改为 `require('fs')` 等新旧 Node 均支持的内置模块写法，消除 Node 12 不识别 `node:` 前缀的加载错误。npm 须能完成现有锁文件的 `npm ci --omit=dev --ignore-scripts` 安装，安装或运行验证失败不会写入有效回执。Python 使用标准库，Git 解析与工程模板仍复用固定 auto-changelog 2.6.1 源码。

`tool-baseline.json` 的 `nodeVersion`/`npmVersion` 保留为历史验证信息，不再作为全局版本门禁；源码和锁文件指纹仍须匹配。用户提供的 Node v26.10.0/npm 12.1.0 不会仅因不同于历史版本被拒绝，但该组合的实际安装/生成以及原生 Linux 运行仍需验证，不能把允许尝试等同于已经兼容性验收通过。

V1.7.1 已在 Windows Node v12.22.9/npm 8.5.1 完成固定依赖准备及 25 项回归，Node v25.2.1/npm 11.6.2 的 5 项定向回归通过。旧 Node 测试使用了本机受限环境所需的路径解析选项；详细条件和报告见[方案 18.8.5](../../ECW6700-1500/auto-changelog生成Git_Release_Notes需求与方案.md)，不代表已完成 Ubuntu 实机验证。

默认运行根目录为用户目录下 `.ecw-release-tools`。可用 `--runtime-root <仓库外目录>` 指定位置，准备和生成必须使用同一位置：

```text
python dbg_tools/Release_Tools/generate_release_notes.py --prepare-runtime --runtime-root E:/release-work/ECW6630-v16
python dbg_tools/Release_Tools/generate_release_notes.py --from ECW6700SDK_V1.0.0B01P21 --to ECW6700SDK_V1.0.0B01P22 --runtime-root E:/release-work/ECW6630-v16
```

运行目录为 `<runtime-root>/runtime/auto-changelog-2.6.1-local01/`，包含固定源码、node_modules、部署回执及 npm 缓存。使用固定锁文件安装运行依赖，禁止生命周期脚本；生成时不会自动安装 latest。两平台各自准备依赖，不能复制 Windows node_modules 到 Linux。

V1.5 回执没有平台标识，不能直接复用；请选一个新的 runtime-root 执行准备。V1.6 回执格式继续使用：实际 Node 精确版本、平台、架构，以及源码/依赖指纹必须与准备时一致；有效旧环境可以复用。npm 记录准备时的实际版本，日常生成不要求 npm 存在或与历史版本一致。失败部署或指纹不匹配会报错，不覆盖旧环境。运行根目录必须在 SDK 外，拒绝路径链接跳转。

升级 Node 后请使用新的 `--runtime-root` 重新准备，保留旧目录用于回退。例如，在 Ubuntu 的 `dbg_tools/Release_Tools` 目录使用新 Node 环境：

```bash
python3 generate_release_notes.py --prepare-runtime --runtime-root "$HOME/.ecw-release-tools-node26"
python3 generate_release_notes.py --from ECW6700SDK_V1.0.0B01P21 --to ECW6700SDK_V1.0.0B01P22 --content examples/p21-p22-content.json --important RN1 --important RN5 --runtime-root "$HOME/.ecw-release-tools-node26"
```

两条命令使用同一 Node 环境和 runtime-root；回执与每批次 `generation-manifest.json` 记录实际 Node/npm 版本。不新增环境安装、工具路径选择参数或运行目录层级。

脚本根据自身位置定位 SDK，不依赖当前工作目录；在其他目录使用脚本完整路径即可。用户输入文件的相对路径按调用者当前目录解析。运行和测试串行进行，不需要启动额外服务。

## Ubuntu 提示 Cannot find module 'node:fs'

此错误来自旧版 Node 不支持 `node:` 前缀，并非缺少需要通过 npm 安装的 fs 包。V1.7.1 已同步修复 Python 预检和 `src` 下的生成/兼容入口；请完整同步 Release_Tools 的更新，不能只替换 Python 文件。

该错误发生在创建运行目录之前，更新后可在 Release_Tools 目录直接重试原命令：

```text
python generate_release_notes.py --prepare-runtime
```

日志中的 `/usr/bin/node` 是该次实际调用的程序；若预期使用另一版本，须在同一终端核对 PATH 和 `node --version`。只有另报回执版本/平台不匹配时，才按前述方式使用新的 `--runtime-root` 重新准备，并在生成时使用同一路径。

## 参数

| 参数 | 含义 |
| --- | --- |
| `--from REF` / `--to REF` | 基线/目标，生成必填；各自可为 tag 或 commit |
| `--version TEXT` | 展示版本，不能改变 Git 范围；文件名会安全规范化 |
| `--content PATH` | 人工客户内容 JSON；显式提供时不混入自动候选，包括 entries 为空的情况 |
| `--project-config PATH` | 项目展示/状态配置；不传时读取工具目录 release-project.json，CLI 与网页均支持 |
| `--important ID` | 从人工或自动候选中选择重要条目，可重复；人工示例 `--important RN1 --important RN5` |
| `--important-file PATH` | 独立重要变更 JSON，与 --important 互斥 |
| `--coverage PATH` | 工具遗漏提交的人工确认 JSON |
| `--jira-config PATH` | 覆盖默认在线配置路径；不传时优先本地配置，其次工具内示例 |
| `--jira-prompt` | 在线生成交互输入缺失凭据，可省略配置路径；CI 用环境变量 |
| `--jira-snapshot PATH` | 同范围/源码的 v3 manifest 离线重放，并从概要恢复自动重要项 |
| `--no-jira` | 显式本地诊断；与 jira-config / jira-snapshot 互斥，不自动识别重要项 |
| `--jira-status-scope all/accepted` | 默认 all；accepted 仅纳入全部关联事项处于指定状态的条目，不改变工程范围 |
| `--runtime-root PATH` | 仓库外依赖根目录，不改变结果输出位置 |
| `--prepare-runtime` | 只准备依赖，不接受 from/to 等生成参数 |
| `--timeout SECONDS` | 单次子进程默认 120 秒，合法值 10—1800；安装为 300 秒 |
| `--help` | 查看帮助 |

两端支持 tag→tag、tag→commit、commit→tag 和 commit→commit：

```text
python dbg_tools/Release_Tools/generate_release_notes.py --from 0996e836d99fa1137d2ff6df54c387ffadf1c98b --to 33f65587c1ddd6e7fc0cf3a734f5a7b36b8b47e9 --version ECW6700SDK_V1.0.0B01P22
```

tag 按 `refs/tags/` 精确查找，允许附注/轻量标签；同名分支不会改变目标。commit 接受完整 SHA 或至少 7 位唯一缩写。有歧义时使用 `tag:<名称>` / `commit:<ID>`。不接受 HEAD、分支名或 `~`、`^` 等 revision 表达式。浅克隆、缺对象、非祖先或空范围失败；不会自动 fetch 或扩大范围。

目标为 tag 时默认用 tag 名作为版本；目标为 commit 时默认为 `commit-<12位SHA>`。`release-data.json` 的 diff 始终使用完整 SHA；commit 目标的 tag 字段为 null，不伪造标签。

## 无人工输入时的自动候选

V1.8 修复了“工程记录有提交、两份客户文档却只有占位提示”的问题。触发条件为：未传 `--content`，并且未传 `--important-file` 或该文件合法且 `entries` 为空。每个不同的完整提交 SHA 生成一个候选，保留主题原语言及事项编号；不同提交即使属于同一 Jira，也分别保留。

条目采用 `- 说明文字 (事项编号).` 的单行格式，事项编号用行内代码展示。SHA/提交链接从条目中去除，完整映射保留在内部材料；版本级追溯 SHA 继续保留。明确的新增、优化、修复动作及常用 Conventional Commit 前缀用于初步分类；识别到多类标记、合并/回退或无法判定的提交放入“待分类变更”，不按 Jira 编号直接判为修复。该章节仅在有条目时出现。主题可能包含内部维护或复合变化，发布前仍需人工筛选、改写并按独立变化拆分。

概述的独立统计段列出提交数、客户条目数、不同事项数、各类数量、Jira 兜底数及需复核数，不改写人工 summary；逐条验证记录为未验证。日期及芯片采用上述默认值，产品名称、具体板型/配置、升级条件、测试结论及交付物等仍待完善。

自动条目的稳定 ID 为 `AUTO_<完整提交SHA>`，可从 `generation-manifest.json` 的 `customerEntries` 查找，并用已有 `--important ID` 重跑，将选中条目只展示在“重要变更”中。ID 不进入客户条目正文。若需正式编辑内容，可按下面的人工输入格式整理后传入 `--content`。

**独立非空 `--important-file` 继续只使用其人工条目，不自动补入其他提交**；显式 `--content` 也始终优先。这样保留既有人工整理及按独立变化拆项的行为，避免自动内容与人工说明重复。重要文件若格式错误仍会失败，不回退为自动生成。

本次只改 Python 客户内容处理与说明，不涉及 Node/npm、vendor 或部署回执；现有有效 runtime 可直接复用，无需因本次修正重新执行 `--prepare-runtime`。

## 客户输入及重要变更

可从 `examples/p21-p22-content.json` 复制人工输入并维护。顶层 `previousSha`、`targetSha` 必须与本批完整 SHA 一致。文本字段为 product、releaseDate、scope、summary、upgrade、knownIssues、validation、artifacts、rollback；entries 保存分类条目。

每条记录包括：

```json
{
  "id": "RN5",
  "category": "fixes",
  "text": "修复使用 `CIPCLOSE=5` 时重复上报 `0,CLOSED` 的问题",
  "issues": ["ECW6700-1434"],
  "commits": ["97a5fec675f1a5517cdea116e2d0dbb21f29533c"],
  "validation": "未验证，待补充原问题复测记录"
}
```

category 为 features、improvements、fixes、adaptations 或 unclassified（待分类变更）。每条 text 为非空单行；允许行内代码，不带提交 SHA/提交链接。issues 可为空；commits 为范围内完整 SHA；validation 填写验证依据或待确认状态。

完整 content 还可提供 `customerExclusions: [{"hash":"范围内完整SHA","reason":"排除理由","reviewer":"确认人"}]`，用于整条提交不在客户交付范围的情况；不能与 entries 的提交映射重叠，重要文件不接受该字段。客户排除与 `--coverage` 工程遗漏确认相互独立。一个 SHA 拆为多个不同 ID、多个 SHA 合并为一条均允许；人工输入未说明的提交会标记 `incomplete` 并在客户概述提示，自动模式缺口则生成失败。

已有 content 时直接使用 `--important RN5`，无需复制文案。独立重要文件包含 `previousSha`、`targetSha` 和 `entries`，其中条目使用上述完整结构；即使没有 content 也可生成重要变更章节。JSON 使用 UTF-8，可带 BOM。两份输入若出现相同 ID，所有字段必须一致，否则报错。

自动重要性仅检查范围内提交主题所关联事项的 **summary**，精确匹配 `【重要】`，不使用 priority、描述或普通“重要”二字。一个条目关联的任一事项命中即可；同一事项的多条提交、同一提交拆成的多个人工条目分别提升。人工条目仍使用 RN 等原 ID，不新增 AUTO 副本；手工填入但不属于该提交的事项编号不会扩展查询范围。人工选中 ID 和自动命中 ID 取并集。

有重要条目时生成 `## 2. 重要变更`，下面新增、优化、修复等章节顺延。条目按 ID 只展示一次；同 Jira/SHA 的其他独立条目继续保留，不能靠 Jira 或文字相似度去重。某分类全部移出后保留标题，正文留空。自动与人工均未命中时不新增章节；空白/坏 JSON 文件是错误。重要性只改变展示位置，不改分类、文案或验证结论，也不删除普通变更。

人工模式的版本概述继续采用 summary，缺少时留空；自动模式使用提交数量及候选状态说明。重要说明不会自动改写概述、生成测试通过结论或推断分类。GitHub 正文同步增加 Important Changes，并从后续 Changelog 移出对应条目；Known Issues 仍先于 Changelog。

## 每批八个结果文件

```text
release/<版本>_<批次>/
  CHANGELOG.generated.md
  release-data.json
  raw-commits.txt
  generation-manifest.json
  coverage-review.md
  Release_Notes_<版本>.md
  github-release-body_<版本>.md
  JIRA_Status_Review_<版本>.md
```

上表为全部记录模式；指定状态模式仅将客户文件名改为 `Release_Notes_<版本>_accepted.md`，文件数仍为八个。两种模式使用独立批次目录，同一 tag 的两份 Release Notes 下载到同一目录时名称不同。历史产物不自动改名。

manifest v3 保留原始参数、范围、工具/环境/输入指纹、人工输入、重要 ID、覆盖决定和其余七份文件 SHA-256；包含 `jira` 查询快照、`classification` 分类依据/Git 补充证据、`customerCoverage`、`customerStatistics` 及实际 `renderedIds`。V1.9.1 增加 `customerMetadata` 保存实际日期/芯片，统计增加 `jiraFallbackEntryCount`；`customerContentSource` 沿用 git-subjects/manual，精确分类来源看 classification。运行环境回执仍为 v2，无需重装 runtime。

V1.10 增加 `importance`：规则 `jira-summary-important-v1`、标记 `【重要】`、各条目来源 manual/jira-summary 及命中事项 key；`importantIds` 仍为最终并集。终端另输出 `importantEntryCount`、`autoImportantEntryCount`、`noIssueCommitCount`。凭据和页面会话令牌不写入 manifest。

V1.11 增加 `jiraStatusReview`（整个范围的状态证据与数量）、`statusSelection`（模式、保留数量、被筛除条目及原因）、客户覆盖的 `statusFilteredCommits` 和统计 `statusFilteredCommitCount`。与原始 Jira 快照共同归档，页面与 MD 读取同一批数据。即使无未提交状态事项，仍生成内部状态清单并明确“无”。

`coverage-review.md` 分别展示工程/客户覆盖、类型映射、失败原因、待拆分项及人工排除。成功退出码 0 只表示八文件生成通过；终端同时报告 Jira fetched/failed/skipped、待分类数和客户缺口数，不能用于自动认定可以发布。

批次先写独立临时目录，校验完毕后改名；重跑产生新目录，不覆盖旧文档。成功批次的内部 JSON 仍是 `status=draft`、`customerApproved=false`，保留快照状态契约；网页显示“已生成”。失败返回非零并保留白名单内已生成的证据和 failed manifest，可能少于八份；不得拿失败批次结果发布。进程被强制终止留下的 `.pending_*` 不能视为成功批次，人工清理前应确认作业已停止。

人工修改输出后，旧 manifest 哈希不再代表修改后的文件，应另行记录修订与确认。release 默认不入 Git，内部批次不要整体发送给客户。

## 遗漏提交与失败恢复

若工具隐藏纯版本号等提交，先检查失败批次的 raw、JSON 和 coverage-review。人工覆盖文件采用以下结构，通过 --coverage 重跑；只允许本批确实遗漏的 SHA：

```json
{
  "previousSha": "基线完整SHA",
  "targetSha": "目标完整SHA",
  "decisions": [{"hash": "遗漏的完整SHA", "reason": "说明排除原因", "reviewer": "确认人"}]
}
```

范围外提交、未解释遗漏、模板漏项、Git 错误、输入/标签/源码/依赖漂移及超时均失败。修复输入后创建新批次，不通过移动已发布 tag 或全局忽略合并记录补救。

## 维护文件与兼容入口

- `generate_release_notes.py`：唯一编排，范围、运行环境、客户渲染及归档。
- `src/run-auto-changelog.js` / `src/process.js`：固定 SHA 解析适配、工程模板及文件捕获；不修改 vendor。
- `.auto-changelog` / `templates/engineering.zh.hbs`：规则与工程模板；不是缓存，缺少会失败。
- `tool-baseline.json` / `vendor/auto-changelog`：固定源码、锁文件和指纹；保留上游许可及文件。
- `generate-release-notes.ps1` / `prepare-runtime.ps1` / `src/release-tools.js`：旧参数转发 Python，不再维护第二套生成逻辑。

旧 PowerShell PreviousTag/TargetTag 对应 from/to；ReleaseWorkRoot 现在只选择依赖目录。结果改到 Release_Tools/release，旧 WorkRoot/output 批次不搬移、不删除。旧快照标识不符时明确失败。

## 验证

先准备 V1.6 runtime，再执行：

```text
python -B dbg_tools/Release_Tools/test/test_release_notes.py --runtime-root E:/release-work/ECW6630-v16
```

测试创建仓库外隔离 Git 样本并输出报告路径；覆盖范围组合、重要性/去重、八文件、坏输入、漂移、错误传播和兼容入口。旧 `node test/run-tests.js <runtime-root>` 转发同一测试。SDK 排除的自动检查仅为静态连线检查，不冒充实际归档验收。

## 跨组件 / API 与回退

固件公共 API、共享头、构建入口、Kconfig 和 vendor 源码不变。本项目增加 Python CLI 和 manifest v2，内部 JSON 不承诺跨系统兼容；外部消费者须识别 schemaVersion。

V1.8 无跨组件或公共 API 变更。修改目标仅为 Python 客户内容路径：继续仅依赖人工输入无法解决空文档，按主题强行推断全部语义会误导，因此采用保守候选及待分类条目。CLI 和七文件兼容；内部 manifest v2 增加来源字段及 unclassified 分类，读取方应允许未知字段和待分类值。验证覆盖自动/人工路径、重要条目去重及完整映射；回退仅恢复本轮 Python 改动及配套测试/文档，runtime 和历史批次无需迁移。

结果移入源码树后，`.gitignore` 不能阻止 SDK 复制。原方案要求在 `dbg_tools/autobuild/6700_build_sdk.sh` 的 ECW6700 裁剪函数及 ECW6700F 公共裁剪路径排除 `dbg_tools/Release_Tools/release`。**V1.10 本轮回归发现当前工作树缺少该规则，静态检查失败；本轮未修改外部打包脚本，不宣称排除已落实。** 需由打包维护者确认并验收。维护工具源码是否随 SDK 分发仍按项目策略决定。

## V1.12 跨组件 / API 变更

无固件、SDK 公共接口或外部组件改动。新增的项目加载模块由生成器、网页、状态模块共用，避免各处重复常量或为每个工程维护代码分支。CLI/网页启动新增可选 `--project-config`；原命令使用随工具提供的 ECW 配置。内部 metadata/build_review 函数只增加可选配置参数，旧调用继续读取默认配置。HTTP 提交字段和下载结构保留，配置值在服务端转义后填入页面，不由浏览器传入任意路径。manifest v3 增加 projectConfig，源码与项目配置指纹参与快照校验。

验证覆盖配置错误、默认及自定义芯片、人工值优先、自定义完成状态、页面转义/配置变更、ECR 模拟事项与重要项、全量/筛选及离线重放。回退需成套恢复生成器、网页、状态模块、页面及配套测试/文档并移除新增项目配置模块/文件；runtime 和历史批次保留。详见方案 20.26。

## V1.11.5 跨组件 / API 变更（历史）

无跨组件或公共 API 契约变更。修改目标为本模块生成器、网页服务/页面的展示文字及快照校验错误提示，原因是统一发布说明的专业展示。直接删除固定文首提示、更新成功文案即可；全局替换内部 draft 状态会影响 manifest 和快照读取，因此不采用。分类、复核统计、状态筛选、内部 JSON 状态与八文件结构继续沿用原规则。重启页面服务、刷新浏览器并重新在线生成，无需重装 runtime；历史文件不自动改写。由于生成源码指纹变化，旧源码快照按原校验规则需重新在线生成，新批次支持同源码离线重放。验证使用现有生成/下载/重放用例及页面 DOM 检查；回退时恢复本次展示文案及配套说明，重新生成所需文档。详见方案 20.25。

## V1.11.4 跨组件 / API 变更（历史）

无跨组件或公共 API 变更。仅在示例配置的 ECW6700、ECW6630 fallbackIssueTypeMap 各补充任务、子任务两个类型，使三项目展示策略一致；沿用现有扩展点，避免直接映射覆盖明确分类或修改全局算法扩大影响。分类优先级、状态筛选、CLI/HTTP、schema 及八文件结构兼容；自定义配置需合并上述键，旧快照保留历史映射。26 项分类/快照测试及 1 项三项目 Git/Node 集成测试通过，验证明确分类/人工分类优先、特殊提交边界及两种状态范围。回退时移除 ECW6700、ECW6630 本次新增的四个映射键并恢复配套测试/文档，重新在线生成，保留 ECW6280 原映射和历史批次。详见方案 20.24。

## V1.11.3 跨组件 / API 变更（历史）

无跨组件或公共 API 变更；只在示例配置的 ECW6280 fallbackIssueTypeMap 增加两个类型，复用原分类流程。直接映射会干扰明确的 Git 分类，修改全局算法会扩大影响，故采用项目级兜底。其他项目、状态筛选、文件结构及 schema 保留原行为；自定义配置需手动合并，历史快照不自动更新。验证覆盖实际提交标题、明确分类/人工分类优先、Merge/Revert、多事项冲突及全量/指定状态生成；回退时移除本次两个兜底键并恢复配套测试/文档，重新在线生成，历史产物保留。详见方案 20.23。

## V1.11.2 跨组件 / API 变更

无跨组件或公共 API 变更，仅补齐本模块 `.auto-changelog` 的 ECW6280 前缀及示例 JIRA 项目映射。现有配置足以实现，不新增按项目分支或修改分类算法；不能直接复制其他项目类型，先以项目元数据核实 ID 与语义。输出仍为八文件，CLI/HTTP、状态规则及 schema 保持原契约；工程 JSON 的 ECW6280 提交可从普通 commits 归入 fixes（模板展示为“关联事项”），并不意味着客户类型一定为问题修复。消费者应继续合并 commits/merges/fixes 获取完整提交集合。

25 项分类测试及 3 项隔离 Git/Node 集成检查覆盖三项目前缀、缺陷/需求映射、未映射边界、状态与重要项、覆盖完整性及快照；详细结果见方案 20.22。回退时恢复本轮两个配置和配套测试/文档，历史批次保留。JIRA 项目元数据为实际只读查询，集成生成中的事项响应为模拟数据。

## V1.11.1 跨组件 / API 变更

无跨组件、固件公共 API、CLI 或本机 HTTP 参数变更，修改均在 Release_Tools 内。直接使用原生折叠控件满足按需查看；全部记录文件名保留，筛选版增加 `_accepted`，比只修改下载名更能保证磁盘文件、下载和 manifest 一致。内部 `otherStatusIssueCount`、状态分组和筛选规则保持原契约。空章节留白仅改变正文显示，不改输入快照和覆盖检查。

处理人无法从原字段可靠推导，因此原 JIRA 只读 GET 的 fields 增加 assignee，归一化 fields 增加可选 `assignee: {name, displayName} | null`，不归档邮箱、头像等完整用户资料。快照校验同步允许并检查此字段，状态清单增加 assigneeName；schema 版本保持不变。缺失字段显示“未取得”，明确 null 显示“未分配”，查询警告优先于 null；别名处理人不一致显示“未知 / 不一致”。MD 改为纯文本编号，页面保留可点击编号。验证覆盖 Server/Cloud 账号、空值/异常、转义、别名和同源码重放，回退时需成套恢复客户端、快照校验及清单模块。

外部脚本若硬编码筛选版旧文件名，需改读 manifest 的 artifacts 或适配 `_accepted.md`；生成器白名单及页面下载同步使用实际文件名。旧源码快照按现有指纹校验规则需重新生成，runtime 可复用。验证覆盖两种命名、哈希、HTTP 下载、同源码重放、折叠行为及空章节；回退时恢复本次生成器、状态文案、页面、测试和文档，历史批次无需迁移。详见方案 20.21。

## V1.11 跨组件 / API 变更

无固件、SDK 公共 API、JIRA 客户端接口或 vendor 修改。Release_Tools 内增加 release_status.py，复用现有 JIRA 状态字段完成去重、筛选和研发清单；页面与 CLI 共用同一逻辑，避免浏览器筛选造成文件与统计不一致。

CLI 新增 --jira-status-scope；HTTP POST /api/jobs 增加可选 statusScope，缺省 all 保持旧请求语义。内部 customer_coverage 增加可选 filtered 参数，缺省空集合；状态筛除单独计数，不复用需人工确认人的 customerExclusions。manifest 仍 v3，附加状态审计字段；输出由七份增至八份，需同步更新文件白名单消费者。验证包括全量/筛选、别名去重、混合/未知状态、空结果、重要项、人工冲突、快照及页面数据绑定。回退时成套恢复 V1.10 生成器、分类/网页、测试和文档，撤下新增状态模块，保留既有批次及 runtime。详见方案 20.20。

## V1.10 接口变化（历史）

无固件或 SDK 公共 API 修改；实现局限 Release_Tools。内部 generate 增加可选凭据加载/进度回调，便于本机服务复用生成流程，原位置参数兼容；相比另建子进程或前端分类逻辑，不增加第二套实现。manifest v3 增加 importance 审计，既有字段和七文件保持；旧源码快照需重新在线生成，runtime 可复用。

CLI 默认改为在线，查询不完整不再输出成功客户正文；旧无凭据调用需注入环境凭据、使用 --jira-prompt 或显式 --no-jira 诊断。网页只提供本机单任务接口。验证覆盖默认模式、并集去重、失败诊断、重放和 HTTP 生成/下载；回退时成套恢复 V1.9.1 生成器、测试和文档并撤下新增网页文件，保留历史批次。详细目标、替代方案、兼容性与回退矩阵见方案 20.19。

适配器复用固定 vendor 的 parseReleases/compileTemplate 私有接口，升级需重新验证；直接修改 vendor 或维护第二套解析器会增加偏差。回退应恢复整套 V1.5 工具、匹配运行环境及外部输出策略，再决定是否撤销打包排除；保留既有批次，不回滚固件或 Git 历史。实际 SDK 包清单及原生 Linux 验证仍待执行，详见测试交接记录。
