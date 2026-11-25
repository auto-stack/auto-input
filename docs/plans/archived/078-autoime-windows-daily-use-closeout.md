---
plan_id: PLAN-078
status: archived
feature_name: AutoIME Windows 日用收尾与候选学习统一
author: [Codex]
created_at: 2026-11-25
updated_at: 2026-11-25
plan_revision: 3
depends_on: [PLAN-071, PLAN-072, PLAN-073]
current_step: 7
total_steps: 9
supersedes_spec_components: [docs/specs/autoime/pinyin-quality.md, docs/specs/autoime/phrase-management.md]
new_spec_components: [docs/specs/autoime/windows-daily-use.md]
touched_goals: []
---

# PLAN-078 AutoIME Windows 日用收尾与候选学习统一

## 0. 变更摘要

归并安装版试用发现的四项问题。修复英文别名抢占常用中文首选，统一全拼、简拼、混输及补充词的个人词频，消除两个应用同时输入时的词库锁冲突，交付横排候选窗与可管理的个人英文术语。用同一个 Windows x64 离线包完成日用验收，形成后续独立建仓的评估记录。

## 1. 目标

| 台账条目 | 用户可观察的交付 | 验收 |
|---|---|---|
| AUTOIME-EN-TRIGGER | 空用户词库 `s` 首选「是」，没有 SQLite。`SQ` 可明确触发 SQLite。一般中文简拼优先，完整的 `sqlite`、`ros`、`autoos` 继续有效 | AC-01 |
| AUTOIME-LEARNING-SHARING | 同一用户在窗口 A 确认选词后，已打开的窗口 B 在下一次组合享受更新后的学习权重。补充词与普通词按文字和实际读音学习，全拼、简拼、混输共享收益，重启保留 | AC-02、AC-03 |
| AUTOIME-TERM-MANAGEMENT | 可用安装包内命令查看、添加、移除、导入、导出个人英文术语，保留大小写。已打开的应用下一次组合读取更新 | AC-04 |
| AUTOIME-CANDIDATE-LAYOUT | 候选默认横排，蓝色细边框与微蓝白灰底，点击/数字选词与实际显示一致 | AC-05 |

日用基线以 Windows 11 x64 独立离线安装、两个并行宿主稳定输入、升级保持用户数据为准。沿用 072 的新版 ACE AV 管理工具与原生按键链。074 继续负责图形管理，076 继续负责分段交互和造词，并消费本包的共享学习接口。两者均可在日用基线之后交付，其未交能力记入独立建仓评估。主题设置、自动抓取英文术语、AI 专名词库、云同步与 Linux 宿主按各自需求后续规划。

## 2. 架构方案

候选链依次处理按键与组合、候选准入、召回与排序、候选快照、宿主选择与确认、个人学习。默认公有词频、个人学习和显式术语配置各有明确的数据来源。同一用户数据配置的不同应用使用同一学习状态，每个编辑上下文拥有自己的组合与页版本。候选读取以当前组合为边界，下一次新组合读取已确认的学习，当前显示页按原快照接受选词。

首选架构为由当前用户运行的本地引擎进程，集中管理 librime 会话与可写用户词库。TSF DLL 保留系统回调、宿主编辑、焦点和候选窗，经本机 IPC 获取候选或确认提交。管理工具沿 072 事务入口与词库所有者协调重载。T-02 比较共享引擎与可行的轻量存储适配，选定能够证明跨进程学习、宿主生命周期、延迟与升级合同的路径。独立的进程驻留只是实现选项，用户可见行为由 AC-02/03 固定。

优先复用 librime 已验证的音节匹配和排序学习机制，把公有补充词接入同一学习身份。076 的最终整词确认、撤销学习也使用这份个人词库。普通输入的快速修复 T-01 独立于上述架构验证，可以先落地。

## 3. 技术栈与执行位置

librime 1.13.1、Rime YAML、C++17 TSF/Win32、Inno Setup、072 的新版 ACE AV 管理工具。计划主仓 auto-os，实施树 `D:/autostack/.wt/os-078/auto-os`，分支 `plan-078-dev`，执行和复审顺序使用同一树。源码范围以 `experimental/autoime/` 为主，核心输入不需要 AutoOS 桌面、AutoUI、AGE、新版 A2R 或旧 AutoVM/A2R。

先交付日用版本，再评估将 `experimental/autoime/` 演进为 `D:/autostack/auto-ime` 的独立 Git 仓。目录与远端的最终名字由迁移决策记录确认。若 AutoOS 需要固定装配版本，按现有独立 clone 和固定 gitlink 约定决定集成方式，输入法仍可独立安装。参见 [App 子模块工作流程](https://github.com/auto-stack/auto-os/blob/main/docs/design/strategy/app-submodule-workflow.md)。

## 4. 需求分析与背景调查

2026-11-25 用户要求将近期试用反馈和候选词系统改动合成一个修改计划，完成后再考虑独立建仓。本轮授权为规划。主检出基线 `863056ea9a29f9b382545a0c1de8cdfe2cf5f89c`，实际实现时记录启用方案、DLL、资源和二进制版本。

| 证据 | 结论与影响 |
|---|---|
| [试用反馈台账](../KNOWN-DEBT-AND-RISKS.md) | 空库 `s` 的首选是 SQLite。只关闭英文表前缀补全的隔离对照得到「是、说、上、时、谁」，完整英文码与 `nihao` 不回归。该对照尚未验证显式大写触发 |
| 同一台账的独立引擎进程回放 | A 学到「是」且同进程另一会话可见。B 访问 `autoime_user.userdb/LOCK` 失败，继续基础排序。重开读回结果。单进程持久化通过无法证明并行宿主共享 |
| 同一台账的跨拼法与补充词回放 | `lx` 选「李显」3 次后 `lixian` 第 2 升至第 1。默认 `sd` 选「是的」5 次未更新主词库，`script_translator@autoime_words` 关闭学习。保留 073 的空库频率收益还需接入个人学习 |
| `windows/ime/rime_engine.cpp`、`textservice.cpp`、`deployment.cpp` | 现由各宿主使用进程内 Rime。部署锁保护资源准备，未解决长期打开的用户词库互斥。`TakeCommit` 与 TSF 编辑成功属于不同阶段 |
| `windows/ime/candidate_window.cpp` | 当前按 Y 坐标算点击行。横排需同步布局、绘制、点击及高亮矩形，保留页版本和焦点保护 |
| [072 短语规范](../../specs/autoime/phrase-management.md)、[073 拼音规范](../../specs/autoime/pinyin-quality.md) | 显式短语是精确小写短码，已有验证和部署事务。073 已冻结 Shift、一般中文、项目术语和 `是的` 五种输入 |
| [074 图形管理](../074-auto-input-av-phrase-ui.md)、[076 分段选词](../076-auto-input-segment-selection-learning.md) | 本包提供术语配置、候选矩形和学习接口。后续两计划分别消费，避免同一文件并发修改 |

## 5. 详细设计

**候选准入。** 单字母输入不查英文别名，空库 `s` 首选「是」。T-01 首先采用已验证的关闭隐式英文前缀补全路径，保留完整历史码。完整交付允许显式声明区分大小写的触发码，例如 `SQ` 和 `SQLite`，保留 `sqlite` 为完整兼容码。`S`、`sq`、`Sq` 与 `sql` 都不隐式召回 SQLite。用户声明的英文术语触发码至少两个 ASCII 字母，大小写逐字符匹配。拼音候选沿原生顺序参与，精确声明的术语码可快速选取。保留 072 下用户主动定义的精确短码优先级，验收默认行为时使用空个人配置。T-01/05 用生产 KeyRouter 与引擎验证大写路径，不能只在 YAML 中登记当前小写 speller 无法收到的码。

**共享会话与资源。** 正常模式下同一用户数据根只有一个可写词库所有者。控制端与宿主绑定同一身份、数据根、协议和资源版本。每个客户端的编辑状态、组合代次与提交标识独立，迟到响应、非法页与失焦请求失效。同一组合按版本核对，已确认完成的学习对其他宿主的下一次组合可见。连接、启动、重载均遵守按键延迟上限。服务未就绪或退出时沿现有透明直通和组合取消语义恢复，稳定后新组合重新接入，每个宿主不会因降级重新打开可写词库。

T-02 用两个真实进程、同一临时数据根、双向学习和持久化做原型验证，验证所有者重启时宿主继续可用。评估 Win32 IPC 的身份与正常发行权限，确保连接限于预期用户和 data root。若采用独立进程，明确谁启动、何时退出、升级如何交接，以及旧 DLL 活跃占用词库时如何报告和恢复。部署/管理器获得所有者的协作，不再由多个工具并行加载可写词库。密码框、隐私模式与默认诊断遵守现有输入保护。

**个人词频。** 以词条文字及规范读音标识记录学习，完整的 `shi de` 从引擎音节与候选来源取得。不同输入串生成不同候选池时仍用同一词条权重。优先验证把 `autoime_words` 和适用的 `autoime_terms` 公有词条纳入主词库或同一学习域，保留 essay.txt、主词频与原用户库兼容。存在多个 translator 或简繁过滤时，消歧与去重不得丢掉正确读音，也不能把同一确认重复写入。若采用 Rime 原生学习，需核实其调用时机与 TSF 提交成功的一致性。T-02 明确引擎提交与宿主确认接口，T-04 验证最终确认一次、取消和失败不计入、重启还能读取。用实际学习记录和排名共同验证，避免「是的」原本第 1 导致测试看不出变化。

**英文术语管理。** 在 `autoime-config` 增加 `terms list/add/remove/import/export`。安装包内置表沿现有资源只读交付，每用户可写文件作为覆盖层，列表区分内置与个人来源。支持通过精确码快速添加 `AutoScape`，可用 `AS` 作为显式触发例，输入 `AutoScape` 多次本身不写入词库。个人项按触发码和提交文本管理，不影响 `custom_phrase.txt` 的格式和各命令的兼容性。数据格式、冲突优先级、合法同码、删除个人覆盖后显示内置值、导入校验和失败恢复由 T-05 冻结。写入与生效复用 072 的验证/发布，当前组合保留旧快照，下次读取新版本。

**候选窗。** 预编辑独占顶行，候选按标签从左到右，蓝色细边框、微蓝白灰底、独立的选中态。文字宽度决定每项的矩形，工作区过窄时允许同页换行。测量、绘制、命中和高亮共用候选矩形。保持非激活窗口、光标跟随、DPI、翻页及页版本检查。076 的分词按钮后续可使用同一布局机制和独立动作区。

**离线交付与独立建仓条件。** T-07 在同一离线包验证冷装、升级、回退、卸载重装、两个并行宿主、学习和术语兼容。记录源提交、打包材料和 SHA-256，核对旧 TSF 注册身份与用户数据目录保留。验收后列出独立仓拆分边界、保留的历史及许可证、独立构建与测试、现有 074/076 后续归属、AutoOS 集成方式和维护所有者。实际建仓、发布远端及父仓切换留待独立决策。

### 规范增量

| delta_id | add/modify/retire | docs/specs 目标 | 前后规则 | 原因 | acceptance IDs |
|---|---|---|---|---|---|
| SD-01 | modify | docs/specs/autoime/pinyin-quality.md | 增加单字母与显式英文码准入、共享个人词频及跨拼法、跨应用可见性 | 固定默认输入质量和学习行为 | AC-01–03 |
| SD-02 | modify | docs/specs/autoime/phrase-management.md | 扩展个人术语覆盖、读写事务和重载，保留显式短语规格 | 提供可重复验证的术语管理 | AC-04 |
| SD-03 | add | docs/specs/autoime/windows-daily-use.md | 明确横排候选窗、Windows 宿主隔离、版本交接、离线日用与迁移评估 | 形成独立发布前的日用基线 | AC-02、AC-05、AC-06 |

## 6. 测试设计

下列为计划用例。保留表中既有套件断言，针对有意变化的前缀行为单列新断言。引擎诊断用合成语料和隔离用户数据根。构建、词库准备和测试分开，新增自动用例按仓库约定逐条记录完整耗时并限制在 10 秒内。前期用自动回放，最后集中做真实宿主验收，人工与安装环节单列计时。

| Case | 执行入口与观测 | AC |
|---|---|---|
| V-01 | 基于 `windows/probe/rime_probe.cpp` 扩充空库断言。`s` 首选「是」，SQLite 全表缺席，`SQ`、`SQLite`、完整旧码命中；`S`、`sq`、`Sq`、`sql` 无 SQLite。验证大小写真实按键和原 071/073 语料 | AC-01 |
| V-02 | 由 T-02/03 增加双进程探针，先打开 A/B，再交替确认选词。观察新组合时的实际首选和同时打开词库的所有者数。另用真实记事本与浏览器文本框重放双向选词 | AC-02 |
| V-03 | 同一隔离词库复现 `lx`/`lix`/`lixian` 收益。用 073「是的」五种输入及实际学习记录区分默认高频与个人学习。验证补充词、普通词、不同实际读音、重复确认、取消和宿主提交失败 | AC-03 |
| V-04 | 双宿主已打开时关闭 A、强制测试环境的所有者中断并恢复、全关重开。从 B 完成新的确认并于新进程读回；保留旧用户库与边界状态 | AC-02、AC-03 |
| V-05 | 复用 `manager/tests/txn.ps1` 的隔离主目录和事务模式。真实已安装 `autoime-config terms` 添加、列出、验证有效码、删除、导出与重导入。坏文件、并发冲突或加载失败不破坏旧配置。新组合读回实际候选，072 的 `phrases` 用例仍过 | AC-04 |
| V-06 | 复用原生 fixture，添加候选矩形/标签映射及页版本反例。实际窗口验浅/深背景、长术语、窄窗口、边缘、多 DPI、翻页、数字/鼠标、点击预编辑或空白时保持焦点 | AC-05 |
| V-07 | `windows/installer/build.cmd` 打包。`windows/ime/tests/run-t04.ps1` 的部署/回退与延迟路径扩充为新运行拓扑。干净断网 Win11 x64 测冷装、正常使用、旧版升级及重装，双宿主新组合学习一致，核对工件 SHA | AC-06 |

**V-07 结果（2026-11-25，交付包 `9CC83C36…`，已安装 DLL `F89B06A3…`、引擎 `8E5A5692…`、部署工具 `DA465AE2…`，三者与包内逐字节一致）**：真实宿主（记事本/浏览器/DSH）打字/候选窗/双宿主检查于 20:30 在上一对构建（DLL `3BE9B85F…` + 17:16 引擎）上由用户确认通过；交付配对的 3 项复确认（`ros`→ROS 2、`SQ`+`1`→SQLite、任务栏橙色 A）于 2026-11-25 22:19 由用户在已安装包 `9CC83C36…`（DLL `F89B06A3…`、引擎 `8E5A5692…`）上确认通过。记录如下：——`ros`→ROS 2、`autoos`→AutoOS、`tsf`→TSF，`zlby`/`lxpy`/`hxck` 简拼命中，`shide`/`sd`→是的，`s`→首选「是」，`SQ`→SQLite 且 `Sq`/`sq` 不触发，`AS`→AutoScape（个人术语），Shift+`;`/`=` 符号与模式不切换，候选窗横排/2 px 圆角蓝框/`#E8F0FA` 淡蓝底、点击与数字选同一项，双宿主学习一致。反馈修复累计：候选框圆角与 2 px 边框、符号后光标、单候选数字选词（`SQ`+`1` 不再变 `SQ1`）、每用户语言配置补写后任务栏显示橙色 A（根因：任务栏徽标读 HKCU `CTF\\TIP` 语言配置，之前只有 HKLM 一份）。升级/重装保留用户数据与 `custom_phrase.txt` 哈希，`verify_registered` 退出 0。

`run-t04.ps1` 现有就绪态 P95 低于 10 ms 的检查继续保留。新增 IPC 或进程转发时，另以同机预热的实际按键到候选快照完整路径测试 P95 低于 10 ms，记录冷启和故障等待上限，区分渲染/屏幕延迟。准备依赖沿现有 `windows/ime/build.cmd`、`windows/probe/build.cmd` 和 `manager/scripts/prepare.ps1`。

## 7. 验收标准

| ID | owner_stage | 可观察结果 |
|---|---|---|
| AC-01 | work | V-01 通过。`s` 在空库的首选为「是」且无 SQLite，显式大小写规则、旧完整码、既有拼音回归通过 |
| AC-02 | work | V-02/04 通过。同用户数据根的两个常驻宿主双向共享个人学习，组合相互隔离，正常使用无词库锁失败，宿主/所有者退出可恢复 |
| AC-03 | work | V-03/04 通过。普通与补充词按同一文字和读音学习。一次成功提交产生一次有效学习，取消或失败不计入，换拼法与重启可读回 |
| AC-04 | work | V-05 通过。个人术语管理可从已安装命令完成，大小写与事务兼容，升级和失败保留个人配置 |
| AC-05 | work | V-06 通过。横排、边框与浅蓝底清晰可辨，点击/数字、页与候选布局一致 |
| AC-06 | work | V-07 通过，使用真实宿主交付离线安装包及身份记录，提交独立建仓评估材料并列出 074/076 迁移安排 |
| AC-07 | review | 专属实施树完成独立复审，复核 AC-01–06、SD-01–03，提交/学习和升级范围有真实证据 |
| AC-08 | merge | 同一冻结版本落地并归档，台账按证据关闭相应项，记录发布基线和仍待用户决定的独立建仓议题 |

## 8. 执行步骤

实施前在可用的任务面板登记以下 ID。T-01 可直接启动，T-02 的原型证据决定 T-03/04 的具体实现。T-06 可独立于词库架构推进，凡修改相同文件仍采用单一写入者。

- [x] T-01 修复 `s` 的默认首选

  owner_stage=work。修改 `shared/resources/autoime_pinyin.schema.yaml` 的别名准入，扩展 `shared/tests/` 与既有 probe，先关闭隐式英文前缀补全，通过 `s`、`sq`、完整别名与普通中文的定向回归。冻结 AC-01 第一阶段证据，显式大小写触发由 T-05 完成。

  [✅ 已完成] 资源 0.3.0：主方案与依赖方案 `autoime_en` 的 `enable_completion` 均改为 false（完整码准入）。probe 新增 `expect_absent`（单值或数组；`scan:true` 查整表）并把大写字母按生产 KeyRouter 的「大写 keysym + Shift mask」回放，新增语料 `shared/tests/en-admission.json`（17 例）。

  红绿证据（`D:/autostack/plan-artifacts/plan-078/t01/`）：修复前 17 例 4 失败——`s` 首选 SQLite（「是」第 2、整表第 2），`sq`/`sql` 整表含 SQLite，`autoscape` 有候选；修复后 17/17 通过：`s` 首选「是」且 SQLite 整表缺席（2752 条候选），`sq`（3339 条）、`sql`（3396 条）同样缺席，`sqlite`/`ros`/`autoos`/`tsf` 均第 1，`S`/`Sq`/`SQ`/`Sql` 无候选、无提交。

  回归与耗时：`cases.json` 13/13、`pinyin-quality.json` 61/61 全过（t12 学习持久化 5.5–6.0 s），三次运行退出码均为 0，总耗时 6.611 / 6.543 / 8.070 s；新增 17 例单例最长 50.8 ms，均 <10 s。构建 `windows/probe/build.cmd`（MSVC 19.43 + Ninja）成功，仅 1 条既有 C4456 警告（t11 局部同名）。commit `687f7a5`。

- [x] T-02 确定共享引擎与学习确认方式

  owner_stage=work。检查 `windows/ime/rime_engine.*`、`textservice.cpp`、`deployment.*`、`windows/probe/rime_api.h` 和锁定版本源码。提供双进程原型与计时、词库所有权、提交时机、数据兼容、恢复与版本交接的决策记录。验证 V-02/03/04 的关键路径，绑定 AC-02/03。跨宿主同步或学习确认的关键证据不成立时，修订实现方案并重验证。

  [✅ 已完成] 选定「每用户一个 `autoime_engine.exe` 所有者」架构，决策记录落在 [Design 07](../../design/07-auto-input-shared-engine.md)（方案比较、进程边界、生命周期、提交确认、数据兼容、版本交接、残余风险）。新增 `windows/engine/`：`protocol.h`（版本化帧）、`engine_client.*`（TSF 侧薄客户端，不加载 rime.dll）、`engine_server.cpp`（所有者：用户互斥单实例 + 管道 + SID 校验 + idle 退出前 finalize）；`RimeEngine` 增加 `DeleteCandidateOnPage` 供「宿主写入失败」补偿。协议新增 `Confirm`/`Forget`：确认成功只记账，失败按同一编码开临时会话 `delete_candidate_on_current_page` 删除刚学到的词条。

  双进程验证（`windows/probe/build/rime_shared_probe.exe`，spawn 真实所有者 + 子客户端，`D:/autostack/plan-artifacts/plan-078/t02/shared-probe-3.log`）：17 项全过、退出码 0、总 7.043 s。跨进程双向学习（所有者学「李显」×3 → 另一进程新会话第 1；另一进程学「师姐」第 2→1 → 所有者第 1）、另一进程输入时本组合保持 `ni` 与 5 个候选不变、杀所有者后客户端 ping 失败且不落盘、重启后重连且两处学习均保留、第二所有者退出码 8、引擎日志无 `LOCK`、`Forget` 后「李显」排名回到第 2。计时：冷启到 Hello（含首次部署）3445/3565/4487 ms；预热 key→候选快照 n=200 时 P50 2.421/3.655/2.650 ms、P95 3.657/5.044/3.146 ms（限 10 ms）。

  构建：`ime/build.cmd` 产出 `autoime_engine.exe`，`probe/build.cmd` 产出 `rime_shared_probe.exe`，均 /W4 无新增警告。commit `28421a7`。

## 对 T-03/T-04 的接口冻结

`Confirm` 只在宿主正文写入成功后调用；写入失败调用 `Forget`（编码为空时由所有者使用该会话最近一次确认的编码）。所有者不因客户端退出而丢弃已确认的学习；idle 退出与 `Shutdown` 都先 finalize 用户库。

- [x] T-03 接通宿主与共享词库

  owner_stage=work，依赖 T-02。实现已选的本地共享引擎路径、版本化快照、并行客户端状态隔离与部署协调。修改 `rime_engine.*`、`textservice.cpp`、`deployment.*` 和相应构建入口，运行 V-02/04，验证 AC-02。

  [✅ 已完成] `RimeEngine` 增加 remote 模式：`EnsureReady` 连接或拉起 `<module_dir>\autoime_engine.exe`（支持 `--instance` 隔离实例），会话/按键/上下文/状态/选项/选词/提交/清空全部经管道转发，快照按会话转换并保留页版本；客户端不加载 `rime.dll`、不打开用户目录。所有者丢失或 redeploy 使会话失效时客户端转 not-ready、恢复文档、按 2 s 上限重连并在下次组合重建会话（不会退化为第二个词库所有者）。`textservice.cpp` 在宿主正文写入成功后调用 `ConfirmCommit`、失败调用 `ForgetCommit`；部署协调：`autoime_deploy` 命中同用户目录的在跑所有者时转发重部署（`via=owner`），否则保留进程内路径（异构目录不串用）。

  验证（`D:/autostack/plan-artifacts/plan-078/t03/`）：
  - 夹具新例 `remote_engine_session`：spawn 真实所有者（隔离实例 + 临时用户目录），经生产适配器输入 `nihao`→首选「你好」→按页选 1→恰好一次提交「你好」→`ConfirmCommit`；再输 `s`→首选「是」且候选无 SQLite；用例 4.504 s（<10 s），27 例 347 检查 0 失败、退出码 0。
  - 共享引擎双进程套件重跑 17 项全过、689 检查、退出码 0、总 6.974 s（P50 2.422 ms / P95 2.922 ms）。
  - `run-t04.ps1` 部署/回退/升级/并发 32 检查 0 失败、25.290 s，退出码 0；手动核对：同目录在跑所有者时 `autoime_deploy` 输出 `"via":"owner"`、`ready=1`、944 ms 退出 0；不同用户目录仍走进程内并成功。

  构建：`ime/build.cmd`（DLL + engine + fixture + deploy）与 `probe/build.cmd` 均 /W4 无新增警告。commit `b4b6326`。

- [x] T-04 统一补充词与主词库学习

  owner_stage=work，依赖 T-03。修改 `shared/resources/autoime_pinyin.schema.yaml` 与关联词表或已选引擎模块。按 §5 的确认边界更新学习。给 076 留可复用入口和契约，用 V-03 及重开验证 AC-03。

  [✅ 已完成] 资源 0.3.1：`autoime_words` translator 由 `enable_user_dict: false` 改为 true 并显式 `user_dict: autoime_user`，与主拼音同一学习域（补充词「是的」选中后按同一文字/读音写入，主拼音、简拼与混输读取同一份权重；空库无学习条目时排名不变，PLAN-073 的「是的」五种输入保持第 1）。`autoime_terms` 保持不学习：项目术语只有精确长码且已按 `initial_quality: 1.0` 排首位，重复确认不应放大术语权重，076 的整词学习走同一 `autoime_user`。

  验证（`D:/autostack/plan-artifacts/plan-078/t04/`）：夹具新例 `supplement_learning_shared_db` 经真实共享引擎所有者确认「是的」10 次并 finalize，用户词库增长 1173–1179 字节；关闭学习时同一用例仅 135 字节日志波动且判失败（红）；随后一次取消组合不增长（-386 字节，日志轮转）。用例 6.70 s（<10 s）。夹具 28 例 353 检查 0 失败；probe 三套语料（en-admission 17、cases 13、pinyin-quality 61）退出码 0；共享引擎双进程 689 检查 0 失败；`run-t04.ps1` 32 检查 0 失败、22.373 s、退出码 0（资源哈希变化后的全新部署）。

  构建产物与资源哈希：`autoime_pinyin.schema.yaml` 5232 字节 / `457BAB89...`；commit `552cfbf`。

- [x] T-05 交付英文术语准入与管理

  owner_stage=work，依赖 T-01、T-03。修改 `shared/resources/autoime_en*`、`windows/ime/key_router.*`、`manager/src/main.at` 与相关事务入口。提供独立个人配置、组合间重载和确切大小写触发，验证 V-01/05，完成 AC-01/04。

  [✅ 已完成] 触发码：speller 字母表扩到 A–Z，`autoime_en.dict.yaml` 新增显式条目 `SQLite←SQ`、`SQLite←SQLite`（1200）；`SQ`/`SQLite` 逐字符命中 SQLite，`S`/`Sq`/`Sqlite`/`sqlITE`/`sq`/`sql` 不命中，小写完整码与单字母 `s`→「是」不回归。个人术语层：新依赖词库 `autoime_user_terms`（`dict` 模板 + 依赖 schema）由 `autoime-config terms` 独占维护，主方案 translator 先于内置别名查询；部署只在文件缺失时复制模板、不覆盖用户内容、不计入资源版本哈希。CLI 在 Auto 源实现（`manager/src/terms.at` + `main.at`）：2..32 ASCII 字母、逐字符区分大小写、权重缺省 1000、规范化写回、整批校验、退出码 0/2/3/4/6、部署失败恢复上一份文件；`list` 输出 `text\tcode\tweight\tpersonal|builtin` 并隐藏被个人覆盖的内置同码。

  验证（`D:/autostack/plan-artifacts/plan-078/t05/`）：
  - `shared/tests/en-admission.json` 20/20（新增 `SQ`/`SQLite` 命中、`Sqlite`/`sqlITE` 反例）；`cases.json` 13/13；`pinyin-quality.json` 61/61。
  - manager 套件 32/32（新增 6 例 `T-05-*`：add 缺省权重与合并 5.9 s、短码拒绝 1.8 s、personal/builtin 列表 2.1 s、导出 2.2 s、删除保留另一行 5.8 s、坏权重整批拒绝 2.0 s；均 <10 s）。
  - `run-t05.ps1` 整链（CLI 写入 → 部署编译 → 真实引擎）8/8，manager_adds 8.417 s、probe 4.248 s，退出码 0。
  - 回归：夹具 28 例 353 检查 0 失败；共享引擎 17/17；`run-t04.ps1` 32/32（新依赖 schema 的全新部署与回退）。
  - 规范：`docs/specs/autoime/phrase-management.md` 增「个人英文术语」合同，`pinyin-quality.md` 增「英文别名准入」「补充词的个人学习」。

  commit `bea76ca`。

- [x] T-06 交付横排候选窗

  owner_stage=work。修改 `windows/ime/candidate_window.*` 与现有界面 fixture，统一位置、绘制与点击，保留 076 后续操作区。完成 V-06、AC-05。

  [✅ 已完成] 新增 `windows/ime/candidate_layout.{h,cpp}`：纯几何布局（预编辑独占顶行、候选按标签从左到右、工作区过窄换行、超长术语按可用宽度裁剪），点击命中与页版本/页内索引准入校验同源；`candidate_window.*` 按同一批矩形测量、绘制与外框（蓝色细边 + 微蓝白灰底 `#E8F0FA`、选中项实心蓝底白字、文字按项矩形裁剪），并新增 `WM_PRINTCLIENT` 让渲染路径可被夹具回放；`textservice.cpp` 记录当前页候选数并用 `AcceptCandidateClick` 拒绝过期页/越界点击。

  验证（`D:/autostack/plan-artifacts/plan-078/t06/`）：`ime/build.cmd` 成功（/W4 无新增警告）；`autoime_fixture.exe` 26 例 324 检查 0 失败、退出码 0、总 3.279 s。新增 5 例单例耗时：`candidate_layout_horizontal` 0.023 ms、`candidate_layout_wrap_narrow` 0.008 ms、`candidate_click_version` 0.007 ms、`candidate_metrics_dpi` 0.007 ms、`candidate_window_render_click` 10.770 ms（真实窗口：`WM_PRINTCLIENT` 取像素断言边框/底色/选中/普通项，鼠标点第 4 项得到页内索引 3 与快照版本 42，点预编辑、留白与页外均不触发；光标在右下边缘时窗口仍限制在工作区内；弹出窗从不取得前台激活）。

  反例证明（同目录备份/还原 `candidate_layout.cpp`）：把换行条件改为永不换行 → `candidate_layout_wrap_narrow` 2 处失败；把命中判定退化为只比 Y 坐标 → 3 个用例共 7 处失败（含真实窗口点击得到索引 0）。commit `883cdbb`。

- [x] T-07 交付日用离线包与迁移评估

  owner_stage=work，依赖 T-03–06。在 `windows/installer/` 更新包清单、版本和交付说明，跑 V-07 并完成双宿主最终走查。准备 SD-01–03 和独立建仓评估。AC-06。

  [✅ 已完成] 打包、真实升级、迁移评估与真实宿主走查均已完成（V-07 结果见 §6）。
  - 包：`autoime-setup.iss` 版本 0.3.2，payload 增加 `autoime_engine.exe`（`restartreplace` 仅作回退；安装/升级在替换前先 `taskkill` 停止旧所有者，`run-t07` 校验已安装 DLL/引擎/部署工具与包内逐字节一致）；`build.cmd` 暂存引擎、`install.cmd` 复制、`uninstall.cmd` 先停所有者再清理引擎/管理器。**最终包**（交付基线）`windows/installer/dist/AutoIME-Setup-x64.exe`，SHA-256 `9CC83C3648255724DE08260825F0130055A3F7BFF91FD7399DD8198151D6A55D`（9,432,181 字节），已安装 `autoime_ime.dll` SHA-256 `F89B06A33EA4E0ABE461234FD3AD21928AA6B4F82D47A4CDB9748B24B5E6D09E`、`autoime_engine.exe` `8E5A5692…`、`autoime_deploy.exe` `DA465AE2…`（三者均与包内逐字节一致；`run-t07` 27/27）。该包在 `98C8E8E4…` 之后加入管道 DACL 修复（`8360829`：所有者以当前用户 SID 显式授予管道访问，使提升权限宿主启动的所有者也能被普通权限输入法连接），是用户确认的交付基线；`98C8E8E4…`/`D8ACF9C3…`/`A3A2FE89…`/`ADA0E251…` 均为过程迭代。
  - 真实升级（本机，管理员 UAC 授权，包 `134EEAA2…` 的 payload，即本计划代码 + 数据根隔离修复前）：0.2.0 安装 → 0.3.2 升级成功（exit 0，104 s，含首次部署），`verify_registered` 退出 0（`file=1`、profile 与 CLSID 不变）；`custom_phrase.txt` SHA-256 前后一致（`FB897BF7…`），`autoime_user.userdb` 11 个文件保留，资源更新为 0.3.2（新增 `autoime_user_terms.*`），版本标记 `r007a60567f42146c`。
  - **升级安装修复（实测教训）**：首次尝试在替换 `data/luna_pinyin.dict.yaml` 时因宿主仍映射该文件而中止并回滚，回滚后 `autoime_ime.dll` 缺失。改为 `rime.dll`/`data\*` 使用 `onlyifdoesntexist`（与锁定版本相同则跳过），并先停所有者再升级：第二次成功。该规则已写入 SD-03。
  - 数据根隔离修复（`8993c12`）：所有者实例按用户数据根派生（默认无后缀，其它根 `profile-<hash>`），部署工具同规则寻找；修复前隔离档案会连到默认所有者并读到默认词库/学习。
  - `windows/installer/run-t07.ps1`（23 检查，0 失败，最终包 17.6 s）：包哈希与体积、stage 必需文件、resources 与仓库逐文件一致、锁定清单校验、模拟安装目录全新部署、包内引擎召回 20/20、升级保留用户文件。
  - 迁移评估：[autoime-standalone-repo-assessment.md](../../design/strategy/auto-input-standalone-repo-assessment.md)；SD-03 已写入 `docs/specs/autoime/windows-daily-use.md`。
  - 自动化 `host-acceptance.ps1` 的按键注入在本机被其他窗口抢占前台（详见 §10），已按用户手动走查替代；脚本保留每键前台/焦点校验与 ABORT，用作回归工具，不再阻塞验收。
  - 已安装 payload 的宿主探测（`host-acceptance.ps1`，DLL `DB76341B…`）：TSF 服务在 PowerShell 宿主与记事本中均 `dll attach` → `AdviseKeyEventSink` → `activate`，共享引擎为隔离数据目录启动并 `remote_ready`（资源标记 `r007a60567f42146c`）；两窗口的**活动输入配置不是 AutoIME**（Win+Space 在多个已装输入法间轮转未命中），打字类断言无法自动通过。自动化还暴露出一个安全点：前台抢占失败时按键会落向前台应用，故不再重复自动化打字。**结论**：打字/候选窗/双宿主走查必须人工选择 AutoIME 后执行（见 §10）。
  - 最终包已安装（2026-11-25 18:15 首次、18:39、18:52 三次 UAC 批准后重装；最后一次包 SHA-256 `A3A2FE895335C35203F6FB6ADC200BAF0EA11400AFE731F79D2F552F1F551813`）：已安装 `autoime_ime.dll` 与仓库构建逐字节一致（`B45313309B99386F61C821A1E342D9A253B88EA3810FB989C29F7F853173C875`），`verify_registered` 退出 0，安装的 `enable-user-list.ps1` 与仓库一致。安装脚本现在把 AutoIME 放在当前用户 zh-CN 输入法顺序的首位（默认），小狼毫保留为第 2 项；实测新开记事本自动激活 AutoIME（host log `activate tid=47`），`autoime_user_terms` 编译失败已消除。
  - 个人术语表两项修复（`54e0f40`）：空表用不可达占位行保持可编译（管理者隐藏/保护/删空后重写），部署身份纳入术语文件字节使改动必然重编译；新增 manager 用例 `T-05-add-rejects-reserved-code`、`T-05-remove-last-keeps-placeholder`，manager 34/34、run-t05 通过，实测「先部署空表无编译错误 → terms add AS → AS→AutoScape 生效」。
  - 自动化安全化与再探测（`a43c94a`）：`host-acceptance.ps1` 改为每次打字前用 `AttachThreadInput` 强制并**验证**前台窗口，失败即中止（不再可能把按键打进无关应用），并新增 `s`→是、`SQ`→SQLite、`AS`→AutoScape 三个 078 用例与大小写保持输入。复跑结果：一次成功取得前台时，真实 TSF 窗口的 AutoIME 身份探针 **`ros`→`ROS 2` 通过**（证明已安装 DLL + 共享引擎的端到端链路可用）；但 Win+Space 在多输入法机器上不能稳定把活动配置切到 AutoIME，其余轮次在探针处失败。**打字验收因此是人工步骤**，不能无人值守自动化。

- [ ] T-08 复审日用验收

  owner_stage=review，依赖 T-07。复核发布身份、双宿主端到端、词频观察、学习与部署异常、窗口验收、SD-01–03，完成 AC-07。

- [ ] T-09 合入与归档

  owner_stage=merge，依赖 T-08。最终验证冻结构建，将代码、规范和离线包交付记录合入并归档。逐项关闭已有证据的反馈，完成 AC-08。

## 9. 复审记录

2026-11-25 建立 r1。依据为原台账实际回放和源码核对，本轮只完成计划、新编号和执行边界。Git Bash 取号进程在启动时返回 `-1073741502`，未写入文件。已在 main 独占 `.next-id`，复核活跃及归档编号，使用 `CreateNew` 分配 078，下一号为 079。073 的既有关闭证据保留，078 负责新反馈。产品构建和计划测试尚未执行。

计划检查 46 项通过，覆盖章节、任务和验收编号、链接、现有测试入口、取号及空白，耗时 0.284 s。四条反馈的台账入口已链接本计划，仍按 open 记录。暂存计划及取号文件时 Git 无法创建 `.git/index.lock`，结果保存在工作区，尚未提交。

stage: new | plan_id: PLAN-078 | plan_revision: 3 | outcome: pass | next: work/T-01、T-02 | tasks: T-01–09 | acceptance: AC-01–08

stage: work | plan_id: PLAN-078 | plan_revision: r1 | round: 1 | from: drafting | to: executing | status: archived | outcome: blocked | worktree: D:/autostack/.wt/os-078/auto-os | branch: plan-078-dev | commits: 687f7a5,883cdbb,28421a7,b4b6326,552cfbf,bea76ca,8245326,d4d9293 | tasks: T-01,T-02,T-03,T-04,T-05,T-06 完成；T-07 打包/评估完成、真实冷装待管理员 | evidence: t01/t02/t03/t04/t05/t07 目录 | blocker: V-07 真实冷装与双宿主走查需管理员权限（见 §10） | next: user

阶段小结（work，r1）：T-01–T-06 全部完成并有红绿证据；T-07 的离线包（0.3.2，9,418,814 字节，SHA-256 `D65ADEA0…F2F5`）、`run-t07.ps1`（23 检查 0 失败）与独立建仓评估已交付，SD-01/02/03 规范已更新。唯一未完成项是 V-07 的真实冷装/双宿主走查，需要管理员权限；解除条件写在 §10。T-08/T-09（review/merge）在 AC-06 关闭前不得开始。

T-02 负责实验后定下本地引擎所有者、TSF 确认边界以及旧版本交接。T-05 负责固定术语覆盖文件与事务字段。074、076 将来实施时由其 owner 核对 078 实际接口与合入版本。离线包验收通过后，再确定独立仓名字、地址、迁移时间及 AutoOS 的固定版本关系。


stage: work | plan_id: PLAN-078 | plan_revision: r1 | round: 2 | from: work | to: work | status: archived | outcome: waiting | worktree: D:/autostack/.wt/os-078/auto-os | branch: plan-078-dev | commit: 8993c12 | tasks: T-01–T-06 完成；T-07 打包/真实升级/迁移评估完成，最终包宿主走查待 UAC | evidence: D:/autostack/plan-artifacts/plan-078/final/ | blocker: 最终包重装 UAC 被取消（见本节末尾阻塞条） | next: user

第二轮：拿到一次管理员授权，完成真实升级并在过程中修掉两个真实缺陷。(1) 升级安装首次在替换被宿主映射的 data 文件时中止并回滚（回滚后 DLL 缺失）；改为锁定数据 onlyifdoesntexist + 先停所有者，第二次升级成功，用户数据（短语哈希、11 个 userdb 文件）保留，注册校验退出 0。(2) 修掉所有者单例只按用户而非按数据根隔离的问题：隔离/备用档案会连到默认所有者并读默认词库，现在按数据根派生 profile 实例，部署工具同规则。最终代码全套复跑：fixture 28/353、en-admission 20/20、cases 13/13、quality 61/61、共享引擎 17/17、run-t05 8/8（9.365 s + 4.069 s）、run-t04 32/32、run-t07 23/23、manager 32/32。最终包 D8ACF9C3…（9,422,620 B）。剩余一项：用最终包重装后跑 host-acceptance（第二次 UAC 在 122 s 后取消）。

stage: work | plan_id: PLAN-078 | plan_revision: r1 | round: 3 | from: work | to: work | status: archived | outcome: blocked | worktree: D:/autostack/.wt/os-078/auto-os | branch: plan-078-dev | commit: a43c94a | tasks: T-01–T-06 完成；T-07 除人工宿主走查外全部完成 | evidence: final/host-acceptance4.log | blocker: 打字验收需人工把输入法切到 AutoIME（Win+Space 不可靠） | next: user

第三轮：把宿主验收的自动化做成安全可重试——每次打字前强制并验证前台窗口，失败即中止（此前的按键外泄不可能再发生），并补上 078 的三个宿主用例（s→是、SQ→SQLite、AS→AutoScape）。实测得到关键证据：在一次成功取得前台的运行里，真实 TSF 窗口的 AutoIME 身份探针
os→ROS 2 通过，共享引擎为隔离档案
emote_ready；但 Win+Space 在多输入法机器上无法稳定激活 AutoIME，其余轮次在探针处安全中止。因此 V-07 剩余部分（切换后逐项打字、候选窗外观、双宿主学习）必须人工执行；本计划在拿到该结果或用户书面接受现有证据前不能进入 T-08 复审。

stage: work | plan_id: PLAN-078 | plan_revision: r1 | round: 5 | from: work | to: work | status: archived | outcome: waiting | worktree: D:/autostack/.wt/os-078/auto-os | branch: plan-078-dev | commit: 54e0f40 | tasks: T-01–T-06 完成；T-07 安装与包验收完成，仅剩人工打字走查 | evidence: final/host-acceptance7.log, t07/install6.log | blocker: 自动化注入键被其他窗口抢占前台，需用户手动跑 9 项检查 | next: user

第五轮：用户批准 UAC 后把最终包（A3A2FE89…）装好，已安装 DLL 与仓库构建逐字节一致；按用户反馈修正安装脚本，使 AutoIME 成为 zh-CN 默认输入法（新开记事本实测自动激活），并修掉 New-Item -Force 清空排序键的脚本缺陷；个人术语表的两处真实缺陷（空表无法编译、改动不重编译）已修复并有用例。自动打字验收仍因本机窗口抢占前台而无法可靠完成（脚本已做每键前台/焦点校验并 ABORT），已停止自动注入以免干扰用户当前工作；临时 AS→AutoScape 术语已加入真实档案供用户手测，验收后删除。

stage: work | plan_id: PLAN-078 | plan_revision: r1 | round: 6 | from: work | to: work | status: archived | outcome: blocked | worktree: D:/autostack/.wt/os-078/auto-os | branch: plan-078-dev | commit: 54e0f40 | tasks: T-01–T-06 完成；T-07 除人工打字走查外完成 | evidence: diag 显示自 18:53 起无 AutoIME 按键活动 | blocker: 用户未执行 9 项手测（当前在其它计划上工作） | next: user

第六轮：复查确认自最终包装机后没有任何 AutoIME 按键活动（utoime.log 最后一条为 18:53:46 的 activate，无 key op），说明人工走查尚未开始；临时 AS→AutoScape 术语保留在真实档案中等待手测，验收后删除。安装、默认输入法、个人术语修复与全部自动化套件均已就绪，计划只差 V-07 的人工打字/候选窗/双宿主记录。按目标策略标记阻塞，等待用户回复手测结果。

stage: work | plan_id: PLAN-078 | plan_revision: r1 | round: 7 | from: work | to: work | status: archived | outcome: waiting | worktree: D:/autostack/.wt/os-078/auto-os | branch: plan-078-dev | commits: 3911991,b41ccde | tasks: T-01–T-06 完成；T-07 反馈修复完成，待用户复验 | evidence: 已安装 04FF4C02… 与仓库一致；fixture 29/364、run-t04 32/32、语料 20/13/61 全过 | blocker: 等待用户复验 5 项反馈修复 | next: user

第七轮（用户验收反馈 5 项）：①边框改 2 px 四边填充；②无组合插入的符号提交把光标放到插入文本之后（原实现插入后未移动选区，光标停在符号左侧）；③数字按可见候选页标签直接选择（CandidateDigitIndex/TrySelectByDigit，并把「有可见候选」视为组合中），修复 SQ+1 变成 SQ1；④候选窗底色 #E8F0FA；⑤新增 ITfLangBarItemButton 语言栏项（橙色 A、点击切换中英文），因为 Windows 只在 IME 注册语言栏项时在任务栏显示其图标。新构建  4FF4C02… 已安装并与仓库一致；记事本实机复查显示新 DLL 正常激活、候选窗与按键处理无异常、无崩溃事件。等待用户复验后删除临时术语、冻结版本并进入 T-08。

stage: work | plan_id: PLAN-078 | plan_revision: r1 | round: 9 | from: work | to: work | status: archived | outcome: blocked | worktree: D:/autostack/.wt/os-078/auto-os | branch: plan-078-dev | commit: ded7c49 | tasks: T-01–T-06 完成；T-07 反馈修复已交付，等待用户复验 | evidence: 已安装 04FF4C02…；diag 自 19:23 起无按键活动 | blocker: 用户尚未复验 5 项反馈修复 | next: user

第九轮：反馈修复构建（ 4FF4C02…，包 BFDFB126…）已安装并与仓库一致，记事本实机激活正常、无崩溃；计划状态修正为 executing，新设计文档的相对链接全部修复（ded7c49）。诊断日志显示自 19:23 起没有新的按键活动，用户尚未复验 5 项修复；临时 AS→AutoScape 术语保留以支持复验。按目标策略标记阻塞，等待用户回复。

stage: work | plan_id: PLAN-078 | plan_revision: r1 | round: 11 | from: work | to: work | status: archived | outcome: waiting | worktree: D:/autostack/.wt/os-078/auto-os | branch: plan-078-dev | commit: fad028e | tasks: T-01–T-06 完成；T-07 反馈修复完成（用户确认 1/2/3/5 与圆角） | evidence: 已安装 3BE9B85F… 与仓库一致；包 ADA0E251…；fixture 29/364 | blocker: 任务栏橙色 A 在本机 Windows 11 构建上无可用 TSF 接口（AddItem 恒 E_FAIL），需用户决定接受语言名或改需求 | next: user

第十一轮（第二轮反馈）：用户确认 SQ+1、AutoScape、符号光标、2px 边框与圆角均正常；仅任务栏图标仍显示“简体”。已核实原因：方案图标已指向随包 utoime.ico（切换器里显示橙色 A），但语言栏项在调用 ITfLangBarItemMgr::AddItem 时 GetInfo 已被调用却对所有文档化样式（BTN_BUTTON/BTN_TOGGLE/BTN_MENU/SHOWNINTRAY 组合）返回 E_FAIL，启用桌面语言栏后同样失败——该 Windows 11 构建不承载 TSF TIP 的自定义语言栏项，输入指示器按系统行为显示语言名。实现改为每进程一次尽力注册并记录日志，不再重试；临时 AS→AutoScape 已从真实档案删除（文件仅保留随包占位行）。等待用户决定：接受“简体”行为，或提出替代显示方案。

stage: review | review_scope: final | reviewer: review-078 (independent agent context; no source ownership)
plan_id: PLAN-078 | plan_revision: 3 | outcome: needs_fix (first pass) -> repaired, re-review pending
worktree: D:/autostack/.wt/os-078/auto-os | branch: plan-078-dev | reviewed_commit: 756b58d | repair_commit: ef93b34+docs
findings: F-01..F-11
resolution:
- F-01 (blocking, AC-03): owner remembers the last confirmed (code,text) per session; empty-code Forget replays it (preedit spaces stripped); a miss is benign. Verified: shared probe ailed_host_edit_is_forgotten rank 2->1->2 through the production call; fixture remote case covers empty-code forget and no session teardown.
- F-02 (blocking, AC-06): Inno + install.cmd stop autoime_engine.exe before replacing files; run-t07 now verifies installed dll/engine/deploy hashes and that no owner runs an older image (27 checks, 0 failures; install13.log has no restartreplace).
- F-03 (major, AC-04): terms contract documented as last-writer-wins (phrase path keeps conflict detection); section restored after an accidental overwrite and re-verified.
- F-04 (major): package evidence rebound to the delivered artifact; run-t07 re-run on it.
- F-05 (major): cancel oracle tightened to learned_delta/10 (one entry would fail it).
- F-06 (major): plan file committed on main (blob recorded below).
- F-07/F-09/F-10/F-11: SD-03 literal newline fixed; protocol/design Hello text corrected; manager dead code removed; DLL version 0.3.2.0; plan color/marker/TODO fixed; evidence note for final/installed-terms.log (probe needs both AS and SQ seeded; run-t05c re-ran with the full seed 8/8).
- F-08 (minor): owner requires kHello before session ops (kPing open); deploy result fields read under the engine mutex.
verification: fixture 29/367/0; corpora 20/13/61; shared probe 689 checks 0 failures; run-t04 32/32; run-t05 8/8; manager 34/34; run-t07 27/27 on package F54DDC6B…; installed payload identical to the package.

stage: review | review_scope: final | reviewer: review-078 (independent) | plan_revision: 3 -> 3
round: r3 | outcome: needs_fix (second pass) -> repaired, targeted re-review pending
worktree: D:/autostack/.wt/os-078/auto-os | branch: plan-078-dev | commit: 6c1cc6a
findings_resolved:
- N-01 (blocking): the cancel oracle CHECKs are real again; fixture 29 cases / 369 checks.
- N-02 (major): shared probe uses a profile-instance pipe derived from its temp user dir and passes --instance to the spawned owner, so it cannot attach to (or write into) the installed owner; verified with the installed owner running.
- N-03: V-07 text now records the 20:30 walkthrough on the previous pair and the pending matched-pair re-confirmation.
- N-04: deploy reply fields copied under the engine mutex.
- N-05: code remembered on every context read (key-driven commits) and forget walks pages; spec states the no-code boundary.
- N-06: installed identity checks fail instead of silently skipping.
- N-07/N-08: docs record the installer taskkill stop and the restartreplace fallback.
- N-09: manager suite re-run after the r2 source edit (34/34).
verification: fixture 29/369/0; shared probe 689/0 (p95 7.1 ms, also green while the installed owner was running); run-t04 32/32; run-t05 8/8; manager 34/34; run-t07 27/27 on package 9CC83C36… after the matched install (installed DLL F89B06A3…, engine 8E5A5692…, deploy DA465AE2…, all equal to stage, no restart deferral); corpora 20/13/61.

### 待跟踪（非阻塞）：所有者完整性边界（AUTOIME-OWNER-INTEGRITY，open）

- **背景**：安装/升级期间由提升权限的宿主（如安装窗口的 TSF 激活）启动共享所有者时，所有者可能运行在 High IL；本次修复让管道显式授予当前用户 SID，使 Medium/Low IL 的同用户进程也能连接并驱动该 High IL 所有者（kDeploy 会令其解析调用方指定的 Rime YAML）。
- **风险**：新增 Medium→High 的同用户信任跨越；未证明可利用（需要 librime 解析缺陷），因此 AC-01–06 不因此阻塞，但不得静默落地。
- **归属/关闭条件**：owner = 用户后续计划；closure = 所有者以 Medium 令牌启动（或启动时主动降低 IL 并给管道 Medium 完整性标签），Medium→High 路径消失。在此之前保持 open，并在 KNOWN-DEBT-AND-RISKS.md 的 AUTOIME-OWNER-INTEGRITY 条目跟踪。

stage: review | review_scope: final | reviewer: review-078 (independent) | plan_revision: 3 | outcome: pass
worktree: D:/autostack/.wt/os-078/auto-os | branch: plan-078-dev | reviewed_tree_tip: 3ad7f6a | frozen_code_commit: 8360829
plan_commit: 421a8ce | package: 9CC83C36… (9,432,181 B) | installed dll F89B06A3… | engine 8E5A5692… | deploy DA465AE2…
closure: F-01..F-11, N-01..N-09, R1..R5 closed; AUTOIME-OWNER-INTEGRITY open (non-blocking hold, owner: user/follow-up)
acceptance: AC-01..AC-06 pass; AC-06 closed (identity + user-confirmed matched pair at 22:19)
evidence: fixture 29/369/0; shared probe 689/0 (p95 3.506 ms); run-t07 27/27 on 9CC83C36…; Medium→High pipe DACL verified; corpora 20/13/61; run-t04 32/32; run-t05 8/8; manager 34/34
next: merge (T-09) — archive, close the four AutoIME ledger items, keep AUTOIME-OWNER-INTEGRITY open, clean the worktree

stage: merge | plan_id: PLAN-078 | plan_revision: 3 | outcome: pass
delivery_commit: bd9bc00 | landed: ff-only on main (bd9bc00) | rebased_from: 3ad7f6a (range-diff equal)
package: 9CC83C36… (9,432,181 B) | installed dll F89B06A3… | engine 8E5A5692… | deploy DA465AE2…
canonical_specs: docs/specs/autoime/windows-daily-use.md, pinyin-quality.md, phrase-management.md
archive: docs/plans/archived/078-autoime-windows-daily-use-closeout.md
ledger: AUTOIME-EN-TRIGGER / AUTOIME-LEARNING-SHARING / AUTOIME-TERM-MANAGEMENT / AUTOIME-CANDIDATE-LAYOUT closed; AUTOIME-OWNER-INTEGRITY open (non-blocking hold)
worktree: D:/autostack/.wt/os-078/auto-os (cleaned after merge) | artifacts: D:/autostack/plan-artifacts/plan-078 deleted after folding

stage: merge | plan_id: PLAN-078 | plan_revision: 3 | outcome: pass
prepared: reviewed r3 (3ad7f6a, code 8360829); specs SD-01/02/03 in branch; delivery bd9bc00
landed: ff-only main = bd9bc00 (rebased 3ad7f6a -> bd9bc00, git range-diff equal)
canonical_verified: docs/specs/autoime/windows-daily-use.md (sha256 834CE84BF064…), pinyin-quality.md (2493F44B35D8…), phrase-management.md (FB7FB2A509B7…), docs/design/07-autoime-shared-engine.md (15429DC2F1E1…), docs/design/strategy/autoime-standalone-repo-assessment.md (73625966E0FF…)
archived: docs/plans/archived/078-autoime-windows-daily-use-closeout.md (commit 2ccb036; status archived; links resolve)
ledger: AUTOIME-EN-TRIGGER / AUTOIME-LEARNING-SHARING / AUTOIME-TERM-MANAGEMENT / AUTOIME-CANDIDATE-LAYOUT closed (KNOWN-DEBT-AND-RISKS.md); AUTOIME-OWNER-INTEGRITY open (named non-blocking hold, owner: user/follow-up)
cleaned: wt-guard clean; worktree D:/autostack/.wt/os-078/auto-os removed, branch plan-078-dev deleted, group dir removed; plan-artifacts/plan-078 deleted (834 files, 296,598,479 bytes); retention registry refreshed
package: 9CC83C36… (9,432,181 B) | installed dll F89B06A3… | engine 8E5A5692… | deploy DA465AE2…
acceptance: AC-01..AC-06 pass (V-07 user-confirmed matched pair 22:19); fixture 29/369; shared probe 689/0; run-t04 32/32; run-t05 8/8; manager 34/34; run-t07 27/27; corpora 20/13/61
## 10. 待澄清事项
T-02 负责实验后定下本地引擎所有者、TSF 确认边界以及旧版本交接。T-05 负责固定术语覆盖文件与事务字段。074、076 将来实施时由其 owner 核对 078 实际接口与合入版本。离线包验收通过后，再确定独立仓名字、地址、迁移时间及 AutoOS 的固定版本关系。

### 已解决：V-07 宿主走查与任务栏图标（2026-11-25 20:30）

用户手动完成 V-07 全部打字/候选窗/双宿主检查并逐项确认（`ros`/`autoos`/`tsf`、简拼、
`shide`/`sd`、`s`、`SQ`/`Sq`、`AS`、Shift 符号、横排候选窗与点击）。期间按反馈修复：
候选框 2 px 圆角蓝框与 `#E8F0FA` 淡蓝底、符号后光标、单候选数字选词、任务栏徽标。
任务栏图标的根因是**每用户语言配置缺失**：徽标读取 `HKCU\Software\Microsoft\CTF\TIP\<clsid>\LanguageProfile\0x0804\<profile>`，此前只有机器级 HKLM 一份；`enable-user-list.ps1` 现已补写（`Description/IconFile/IconIndex/Enable`，`IconFile` 指向随包 `autoime.ico`），`disable-user-list.ps1` 删除，用户确认橙色 A 恢复。临时 `AS→AutoScape` 已删除。计划进入 T-08 复审。

独立建仓的名字、远端地址、迁移时间与 AutoOS 固定版本关系仍是用户决策项（AC-08 记录）。
