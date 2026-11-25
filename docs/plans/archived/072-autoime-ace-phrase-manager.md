---
plan_id: PLAN-072
status: archived
feature_name: AutoIME 用户短语管理与 ACE AV 实装
author: [Codex]
created_at: 2026-11-20
updated_at: 2026-11-25
plan_revision: 2
depends_on: [PLAN-071]
current_step: 7
total_steps: 7
supersedes_spec_components: []
new_spec_components: [docs/specs/autoime/phrase-management.md]
touched_goals: []
---

# PLAN-072 · AutoIME 用户短语管理与 ACE AV 实装

## 0. 变更摘要

交付用户级命令行工具 `autoime-config`，支持查看、添加、删除、导入、导出显式短语，并通过现有部署工具使修改在真实 librime 会话生效。业务源码使用普通 Auto，开发与离线发布均使用新版 ACE AV。安装包携带 AV 启动器及实际运行所需材料。命令行是本计划选定的首个产品入口，业务模块为后续图形界面提供复用基础。

## 1. 目标

用户安装 AutoIME 后，可添加 `zlby` →「增量编译」这样的个人短码并在编辑器中选词上屏。工具直接读写现有 `custom_phrase.txt`，兼容 069–071 产生的个人数据。默认数据目录是当前用户的 `%LOCALAPPDATA%/AutoIME/rime`，常规短语管理以用户权限运行。

本计划的交付范围是短语管理及部署闭环。通用拼音词典与自然简拼排序优化、图形设置界面、TSF 全量 Auto 重写、独立引擎服务和 Linux 前端各由后续需求规划。个人显式短码的成功结果只证明个人短语功能，通用词库的 `zlby` 等现有排序边界继续单独记录。

## 2. 架构方案

普通 Auto CLI → Auto 短语解析/校验/变更模块 → 暂存文件 → 现有 `autoime_deploy.exe` 的事务应用入口 → 个人短语文件与 librime 部署 → 新会话候选验证。

Windows TSF 文本服务、候选窗与 librime 接入复用 071 的原生实现。必要的原生增量限于文件锁、事务提交/恢复、Rime 维护和 Windows 路径边界。短语语义、命令解释与导入合并由 Auto 源实现。PowerShell/CMD 仅承担准备、构建和测试调度。

AV 经 ACE 的共享源码前门及受检装配，使用已交 PackageAvTarget 等生产接口。若现有 CLI 尚未贯通应用工程，可在本仓增加只负责调用现有 ACE API 的薄启动/构建适配器，其输入是磁盘上的 Auto 工程，业务行为由 Auto 源决定。实现与验证均选择新版 ACE 路径。

新版 A2R 的应用编译与发布能力由后续计划承接，成熟后消费同一份业务源和用例。本计划的开发、测试、复审与安装包验收均以 AV 为目标，A2R 状态记为后续待验证。其整条应用链能力应通过未来实跑建立，不能从少量标准库测试推断。

## 3. 技术栈

Auto 业务源、新版 ACE AV、`std.file/path/env/process/bytes`、现有 C++ 部署工具、librime 1.13.1、Inno Setup 6。使用 071 的共享资源与候选用例作为回归基线。

主导仓为 auto-os。拟实施树 `D:/autostack/.wt/os-072/auto-os`，分支 `plan-072-dev`，执行与复审顺序复用同一树。源码根按 `AUTO_LANG_ROOT` → 同组兄弟 auto-lang → `D:/autostack/auto-lang` 解析。当前阶段无需等待 AutoUI、AGE、Linux 实测或 ACE 全语言齐平。

## 4. 需求分析与背景调查

2026-11-20 用户授权规划 071 的后继，并明确采用新版 ACE。随后指出 AV 有望实现，而新版 A2R 尚有较大差距，r2 据此调整为 AV 先交付。A2R 应用迁移在后续成熟后规划。本轮交付计划草案；实施角色和预算未另行指定。

已读取的依据与观察如下。auto-os 规划基线为 `efcfced51b58a1174f0b4fd3c0288c589495f70e`。语言仓 git 身份读取受当前权限限制，T-01 必须记录实际 SDK 提交与构建身份。

| 来源 | 已观察事实与影响 |
|---|---|
| [071 归档](https://github.com/auto-stack/auto-os/blob/main/docs/plans/archived/071-autoime-windows-offline-installer.md) | 代码 `9b393ac5` 已落地，独立安装包和本机安装闭环已交付；13 个共享候选用例通过。干净系统、断网、双账户和部分宿主体验仍有待测项。 |
| [设计 05](../../design/05-auto-input-windows.md) | 已约定个人短语增删、重部署、用户数据保留与离线输入。当前 docs/specs 未找到 AutoIME 专属规范，本计划补具体短语管理合同。 |
| `experimental/autoime/shared/resources/custom_phrase.txt` | UTF-8 三列 text、code、weight，以 TAB 分隔，含注释；已有「高速缓存」模板。 |
| `experimental/autoime/shared/resources/autoime_pinyin.schema.yaml` | `custom_phrase` 使用 stabledb，精确匹配；数字键承担选词，短码采用字母。 |
| `windows/ime/deployment.cpp`、`ime/tools/deploy_main.cpp` | 已有 Prepare/Commit/回滚和显式部署命令。资源版本相同时可早退，个人短语修改需要专门验证刷新路径；不能仅据部署命令返回 0 判定生效。 |
| [ACE 系统库规范](https://github.com/auto-stack/auto-lang/blob/master/docs/specs/ace-migration/m2-stdlib-io.md) | 973+1064 已交文本读写、路径、argv、子进程与原子替换的 Windows AV/A2R 能力；read_text 失败返回空串，需防止把错误当作空文件。 |
| `auto-lang/experimental/ace/crates/av/tests/cases/sys_d01_d06_1064.rs` | 已有共享装配、PackageAvTarget、RustTargetProvider 的源码和两端 I/O 验证路径，可复用生产 API；各能力在本工具中的组合尚待 T-01 实跑。 |
| [1078 归档](https://github.com/auto-stack/auto-lang/blob/master/docs/plans/archive/1078-av-parity-baseline-and-autodown.md) | 已交普通源码 AV 前门；语言齐平后续计划仍在推进。本应用只核所需能力。 |

## 5. 详细设计

新增目录均位于 `experimental/autoime/`，下列路径为拟定。

- `manager/pac.at`、`manager/src/main.at`、`manager/src/phrases.at` 承担入口与可复用业务逻辑。
- `manager/scripts/prepare.ps1`、`run.ps1`、`test.ps1` 为显式准备和 AV 运行入口。具体 ACE CLI 参数在 T-01 根据现有生产 API 固定。
- `manager/tests/phrases.case.ad` 保存共享 AutoDown 测试作者源及独立预期。应用验证由本仓执行，新增 runner 优先消费已有测试库。
- 原生事务接入扩展 `windows/ime/tools/deploy_main.cpp`、`deployment.{h,cpp}` 及必要的 `rime_engine.{h,cpp}`。
- `windows/installer/autoime-setup.iss` 随包加入 AV 启动器、Auto 业务源或经验证可加载的工件，以及实际需要的运行材料；更新现有 README 与共享资源说明。

### AV 离线发布

T-01 选择已有支持的发布形态，优先复用 AV 的工程加载与执行入口。若采用随包源码，完整携带其加载、编译与执行依赖；若采用预编译工件，验证其真实加载接口、版本匹配及能力授权。选择与依赖清单在 T-01 固定，禁止把尚未交付的独立 ABC 加载器视为现成能力。

`autoime-config.exe` 可以是链接现有 ACE 库的薄原生启动器，启动时加载 Auto 管理模块并进入 AV。用户机运行只使用包内材料，按安装目录定位资源及当前用户目录定位数据。开发机仓库、Cargo/rustc、编译缓存和环境变量覆盖在发布验证时移除。记录包体增量、冷启动耗时和峰值内存，先建立实际基线。AV 仅随管理命令运行，其生命周期独立于 TSF 按键处理。

### 用户入口与数据规则

拟定命令如下；`--user-data-dir` 可指向隔离目录，默认使用当前用户目录。引号内中文、空格、emoji、路径必须在 AV 开发入口和安装后的管理入口保持原值。

```text
autoime-config phrases list
autoime-config phrases add --code zlby --text "增量编译" --weight 100
autoime-config phrases remove --code zlby --text "增量编译"
autoime-config phrases import --file "D:/词组/个人词组.txt"
autoime-config phrases export --file "D:/词组/备份.txt"
autoime-config deploy
```

`add/remove/import` 在候选数据校验通过后自动应用；`deploy` 用于应用用户手工编辑。导出使用现有 TSV 格式。相同 `(code,text)` 是同一条目，add/import 更新其权重，重试幂等；同码不同文字允许共存。remove 匹配完整键，重复删除返回成功且无变更。同码内部权重语义沿用 librime。

读取兼容 LF/CRLF、UTF-8 BOM、空行、注释以及合法现存条目。保留未修改行的内容与顺序；重复相同键在更新时归并为一条。新增短码为小写 ASCII 字母串，文字非空且不得含 TAB/换行/NUL，权重采用 librime 可接受的正整数字符串，数值边界由 T-01 对实际解析器确定并固定。非法编码、坏行、权限错误、重复键冲突处理均给出明确诊断；导入任一非法行时整批不应用。读取返回空串时须结合受检读取结果区分合法空文件和失败，保留原数据。资源预算超限要明确报错，禁止静默截断。

### 发布、并发与恢复

Auto 生成暂存的完整短语文件，并携带读取时的源版本身份。原生应用入口在同一用户目录的部署锁内复核源版本，再执行备份、发布、Rime 维护和新会话验证；若源已变化则返回冲突，由用户重读重试。锁覆盖整个应用过程，并与已有部署操作协调。原生 helper 处理 I/O 事务与引擎操作，最终候选业务内容由 Auto 生成。

调用通过结构化参数与标准库进程入口完成，文件路径不拼接为 shell 命令。工具用 UTF-8/UTF-16 明确转换处理中文目录；当前原生代码的 ANSI 文件 API 接面须沿此次路径修正。导出先写同目录暂存文件再替换；写入失败保留既有导出文件。

提交失败应恢复旧短语及其可用部署结果。中断恢复信息仅包含事务身份和阶段；必要的短语备份存放在用户数据目录并按事务生命周期管理。重启后的恢复入口验证新会话读到完整旧版或完整新版。部署失败、fallback、disabled、busy 和冲突均为失败或明确待重试状态，不能报作应用成功。

成功定义为新引擎实例加载正确方案并可见新增短语；删除后同一合成条目消失。已打开宿主可提示“重新打开应用后生效”，提示须反映实际刷新能力。验收包括新宿主输入，避免仅在 helper 内缓存验证成功。应用活跃时若无法安全维护，返回 busy 并保留当前可用状态。

列表/导出是用户显式读取产品数据；普通诊断只包含状态码、行号、阶段和数量，避免写入短语正文、短码或输入历史。真实个人词频库由 librime 管理；管理显式短语的路径不得重写学习库。

### 新版运行时验收

T-01 用最小 Auto 工程在新版 AV 真跑 argv→中文文件读取→条目变更→原子写入→调用现有部署工具→检查退出码及新会话候选。以一次变更 Auto 源的对照证明实际执行的是新源码，同时核对安装包形态的离线运行材料。AV 的完整业务能力仍由本探针与后续应用验收确认。

现有 ACE 应用工程入口、文本处理与系统库组合若有缺口，须留下最小 AutoDown 复现、实际阶段/错误和具体 owner。编译器或标准库缺陷交 auto-lang 的对应能力计划处理，再继续本计划的依赖任务；本计划不得以旧运行时回退或用手写 Rust/C++ 业务逻辑替代验收。T-01 可立即开始，后续按实际能力通过结果推进。

### 规范增量

| delta_id | add/modify/retire | docs/specs 目标 | before/after rule | rationale | acceptance IDs |
|---|---|---|---|---|---|
| SD-01 | add | docs/specs/autoime/phrase-management.md | 从可手改 TSV 到带验证、并发冲突和部署恢复的用户短语命令；记录新版 AV 执行与离线发布合同，业务源/用例供后续后端复用 | 多个入口共享用户数据，需有单一现行规则 | AC-01–AC-05 |

## 6. 测试设计

以下用例已于 2026-11-25 全部执行（V-01 T-01；V-02/V-03 T-02/V-03；V-04..V-06 T-03；V-07 T-04；V-08 T-05），逐项结果见 §8/§9 与 `plan-artifacts/plan-072/`。owner_stage 为 work，review 从同一树独立核对。在隔离用户目录跑新版 AV，验证外部文件、退出码与真实 Rime 候选；安装后入口复用同一份业务源与预期。保持 071 的 13 条原始用例与期望，新增个人词组用例单独执行。后续后端复用这些作者源和独立预期。

| Case ID | AC | 输入与入口 | 可观察预期 |
|---|---|---|---|
| V-01 | AC-01 | 新版 AV prepare/run，中文带空格目录、空参数、文本「甲😀乙」；再改变源中的输出值 | argv/文件字节/退出码符合独立预期，修改源码后结果相应改变；使用的 ACE/SDK 身份可追溯 |
| V-02 | AC-02/03 | 从 071 模板加 zlby→增量编译；再加唯一合成短语 qztest→星河测试词，修改权重、重复导入、删除 | 新会话显示目标；重复操作无重复条目，删除后唯一合成词消失；模板原词可用 |
| V-03 | AC-02 | BOM+CRLF、注释、同码两词、含空格/emoji；分别导入坏 UTF-8、非字母码、非法权重、TAB/NUL 与不可读文件 | 合法内容往返保真；非法整批拒绝且源文件字节及候选保持原状，指出行号和错误类型 |
| V-04 | AC-03 | 构造短语源变动但安装资源版本不变；调用公开 deploy，再启动独立 probe/新宿主 | 新源确实生效，不能被 Prepare unchanged 路径漏掉；fallback/disabled 返回失败 |
| V-05 | AC-03 | 两个管理进程同时编辑、外部编辑发生在读取和提交之间；已有部署持锁 | 成功提交互不覆盖；陈旧版本报冲突，busy 可重试；原文件无半写入 |
| V-06 | AC-03 | 发布前写失败、发布后引擎失败、提交中终止进程，随后重新启动管理工具 | 失败返回明确状态；恢复后读取及新会话得到完整可用版本；学习库保留 |
| V-07 | AC-04 | 从安装包运行 AV 管理入口，移除开发环境路径/覆盖、断网，从安装目录启动；新宿主输入合成短码；再升级/卸载 | 包内 AV 与业务材料足以运行，用户权限可改短语，管理程序无需外部开发工具；升级保留个人数据，卸载默认保留数据；记录包体与冷启动/内存基线 |
| V-08 | AC-05 | 071 冻结回放、现有 fixture、部署测试；扫描合成短语是否出现在默认诊断 | 原有输入结果与编辑行为回归通过，诊断无正文；合成短语只出现在用户请求的输出/数据中 |

显式准备与测试分开。拟新增脚本入口（由 T-01/T-02 建立，当前不可视为已可运行）如下。

```powershell
pwsh -File experimental/autoime/manager/scripts/prepare.ps1
pwsh -File experimental/autoime/manager/scripts/test.ps1 -Backend av
```

现有准备/验证入口为 `windows/ime/build.cmd`、`windows/probe/build.cmd`、`windows/ime/test.cmd`、`windows/ime/tests/run-t04.ps1`、probe 的 `--cases` 与显式路径参数。缺预构建产物时明确失败。仅本仓修改时使用已准备的 ACE 工具和应用级测试；auto-lang 的 cargo 全组测试归其实现 owner。

最终同一构建集中做记事本和 Edge 的添加、应用、上屏、删除一轮实测。干净系统断网首装、双账户、AutoEdit 的 071 待测项仍需实际环境取证；各项分别记录，不以本计划自动测试代替。Linux 本轮只交共享数据/业务源，实跑状态为待后续平台验证。

## 7. 验收标准

| ID | owner_stage | 关闭条件 |
|---|---|---|
| AC-01 | work | T-01 生产链探针和完整管理业务均在新版 AV 真执行，结果符合独立预期，运行身份可追溯。 |
| AC-02 | work | list/add/remove/import/export 支持合法历史 TSV 与 Unicode，坏输入整批拒绝，幂等行为符合 §5。 |
| AC-03 | work | 显式短语修改/删除在独立新 Rime 会话可见；锁、冲突、忙碌、失败和中断恢复有 V-04–06 的实际结果。 |
| AC-04 | work | AV 启动器、业务程序及所需运行材料纳入离线包，以 V-07 核对用户权限与开发环境独立性；记事本/Edge 合成词上屏通过，个人数据升级保留。干净快照等环境限制单列。 |
| AC-05 | work | 071 冻结用例与受影响原生测试通过；默认诊断隐私和学习库保护符合 §5，更新操作文档。 |
| AC-06 | review | 同一实施树的最终复审核对 AC-01–05 与 SD-01，已跑、待测和能力缺口分别记录。 |
| AC-07 | merge | 落地通过复审的代码与规范源，归档及交付回执注明实际包版本和剩余环境待测。 |

## 8. 执行步骤

实施前，如 harness 提供原生 TODO 能力，按以下 ID 登记。主检出先提交计划与编号；实施在 §3 的同一工作树进行。

- [x] T-01 验证新版 AV 能力链与发布形态

  owner_stage=work。依赖已交 071 与语言仓已交 973/1064，读上述源码和规范，固定 SDK 身份与编译参数。建立新增 manager 工程及最小探针，优先沿现有生产 CLI，必要时添加本仓薄适配器。验证 V-01 及最小真实部署调用，记录文字/数值支持范围、argv、退出码、进程权限和宿主预算，固定 AV 随包形态及依赖清单。AC-01；通过后解锁 T-02。

  证据（2026-11-25，提交 `d96a91d`，树 `D:/autostack/.wt/os-072/auto-os`；scratch `plan-artifacts/plan-072/t01/notes.md` sha256 `E3A9A3EC…46BA`）：现有生产 CLI 无 AV 工程入口（`auto` CLI 只有 AutoVM/Evaluator；auto-lang-ace-host 是库而非产品入口），按 §5 在 `experimental/autoime/manager/native/` 落薄启动器：发现 `src/*.at` → `assemble_stdlib_sys_inputs` → `compile_assembled_aggregates_inputs` → `PackageAvTarget::cold` → `run_platform_value_with`（`Win32PlatformIo`，19 模块），argv 即进程命令行、console 逐字节回放、`return n`/`exit(n)` 映射进程退出码，32 MiB 栈。实测（debug）：Unicode 中文/空格目录 + `甲😀乙` 读→原子写回字节一致（exit 0）；`exit3`→3；python 子进程 7 经 `spawn5` 可见；真实 `autoime_deploy.exe` 经 AV 子进程调用 exit 0（`ready:1`，独立直接运行 `median_us:326`）；改源后输出随源变化（证明执行磁盘新源）。预算：编译 ~5–7 s、运行 16–622 ms（debug 热缓存）；release/包体基线归 T-04。SDK 身份：rustc 1.98.0、auto-lang `857f4876`；随包形态固定为「预编译薄启动器 + `<install>\manager\src\*.at` 业务源 + 现有 deploy/rime.dll/data/resources」，运行期不需要 cargo/rustc。上游缺口（owner: auto-lang 972 前端）：`stdlib/ace/asm/list.at` 的 `xs[i]` 索引表达式在 packages 前端被拒（最小复现见 §10；仓库自带 `sys_d01_d06_1064::v02_d01_d02…` 同一失败）；manager 不导入 `std.list`，启动器只剔除该一模块，无旧运行时回退。

- [x] T-02 实现 Auto 短语命令和共享测试

  owner_stage=work。依赖 T-01。在新 `manager/src/` 实现 §5 业务和暂存输出，新增 AutoDown 作者源与 prepare/run/test 入口，在新版 AV 执行 V-02/V-03。错误读取不能转换为清空数据。AC-01/02。

  证据（2026-11-25，提交 `035aa0b`；scratch `plan-artifacts/plan-072/t02/notes.md` sha256 `76E6B67C…AB2E`，套件日志 sha256 `A9CBF641…CEAC`）：`src/phrases.at`（解析/校验/upsert/remove/序列化/导出，纯函数）+ `src/main.at`（argv、用户目录、严格读取、暂存+FNV-1a 复核+原子替换、诊断、deploy 直连）。release 启动器（rustc 1.98.0、auto-lang `1107ce03`，sha256 `7FBA2F01…035F`）跑 `scripts/test.ps1` 的 22 例全过（2.3–5.8 s/例；一次 18.7 s 属他仓构建争用，复跑 3.57/3.68 s）。覆盖：BOM+CRLF+注释读取与保序、add 幂等/就地改权重/同码异文并存/重复键归并、remove 幂等、import 整批校验（坏权重/缺失/非 UTF-8 均拒绝且目标字节不变，退出 3/4 带行号与类型）、坏现存行拒绝写、导出 TSV、空文件/纯注释、参数校验退出 2、真实 `autoime_deploy.exe` 子进程 exit 0（冷部署 29.2 s）。写路径恒为「同目录 `.autoime-staged` → 身份复核 → replace_atomic」，失败保留旧文件；`read_bytes` Option 区分缺失/空/不可读，错误不当作空数据。修复：默认 store 复用致 `PackageAvTarget::cold` 增长到 17 s，改为每次运行独立临时 store（运行后删除），单次 ~2.5 s（assemble<10 ms、compile ~1 s、link 0.8–1.5 s）。测试作者源用 `tests/phrases.cases.json`（替代拟定的 `.case.ad`：已注册 AutoDown 配方只覆盖 avm/a2r/probe 标量，无 argv+文件+退出码配方；沿用 071 `shared/tests/cases.json` 先例，作者源/独立预期纪律不变，未来配方可直接消费）。AC-03 的 Rime 会话可见性、锁/冲突/恢复未在本任务声明，归 T-03。

- [x] T-03 接通事务部署与恢复

  owner_stage=work。依赖 T-02。扩展已读部署工具、部署模块及必要引擎接口，使源版本冲突校验、锁、维护、回滚在同一路径闭合；Unicode 路径覆盖到实际 Win32 I/O。执行 V-04–06 和完整 V-02，新独立会话确认修改/删除。AC-03。

  证据（2026-11-25，提交 `85a775a`；scratch `plan-artifacts/plan-072/t03/notes.md` sha256 `CD866ABA…FA41`，CLI 日志 `891BDED1…B62D7`，事务日志 `5EB838EF…C21B6`）：Auto 侧改为「暂存完整文件 + 精确源快照 + 校验请求 + 内容哈希 token 文件名」，以 UTF-8 key/value 请求文件调用 `autoime_deploy.exe phrases apply --request`（短语正文不进命令行）；原生侧在部署锁内完成崩溃恢复 → 源字节复核（失配 exit 5）→ 备份+日志 → 原子发布 → 同一锁内 Rime 维护 → **新会话候选核验**（新增/导入 present、删除 absent）→ 提交或回滚（恢复旧文件、丢弃编译产物，exit 6）；忙时 exit 7。部署标记同时覆盖资源 digest 与短语文件身份，短语改动不会再被 unchanged 快路径漏掉（V-04）；路径 API 全部改 UTF-16（CreateFileW/MoveFileExW/LoadLibraryW/_wfopen/FindFirstFileW/CreateDirectoryW/宽字符环境变量）。两套套件全绿：CLI 22/22（mutating 全走真实事务+维护+核验，1.5–8 s/例）；事务 15/15（冲突/并发/忙/两处崩溃恢复/呈现与消失核验/校验失败回滚/Unicode 用户目录/学习库保留/坏资源失败/独立 probe 进程 rank=1/071 冻结 13 例回归；0.01–6.5 s/例）。V-04 另行实测：手工编辑 + 公开 deploy（资源版本不变，`copied=1`）后独立 probe 见 rank=1，再跑一次 `unchanged=1`/438 ms。修复：Trim 未去 \n 致快路径失效（4.5 s→438 ms）；资源失败落到 Luna 却未置 fallback（曾误报成功）。AC-04（打包/宿主）与 SD-01 仍归 T-04/T-05。

- [x] T-04 打包并验证真实宿主

  owner_stage=work。依赖 T-03。更新现有 Inno Setup 文件及准备脚本，加入 T-01 确认的 AV 启动器、业务程序与依赖，记录实际材料和版本；用户权限完成 V-07，记事本/Edge 集中验收。071 环境待测按实际可用环境补测。AC-04。

  证据（2026-11-25，提交 `67c67a6`；scratch `plan-artifacts/plan-072/t04/notes.md` sha256 `A58331C0…1F3C`）：安装包 0.2.0 = **9,313,651 字节**、sha256 `63F0F5E6…6DBC`；新增静态 CRT 启动器（13,563,904 字节，仅依赖 OS DLL）、`manager\src\{main,phrases}.at` 与 `manager\identity.txt`（启动器/ACE/源码哈希，打包前核对）；`stage-manager.ps1` 缺产物或哈希不符即命名失败。随包清单由 14 项增至 **15 项**（补 `data/essay.txt`，见下）。真机：静默管理员安装/升级 071→0.2.0 成功、TSF 注册校验通过；普通用户用安装后的 `autoime-config.exe` 对真实用户目录 add/list/remove 成功（add 4.9 s）；独立 probe 在安装材料上 rank=1；真实应用窗口（Win11 记事本为打包应用、窗口类不可达，改用 WinForms 文本框这一普通 TSF 客户端）输入 `vhost`+`1` 上屏「安装包验收词」；卸载保留个人数据，重装恢复交付。基线：冷加词 4,994 ms/峰值 54.6 MB，热列表 1,487 ms/56.0 MB，deploy 2,696 ms/52.9 MB。清理全部 `AUTOIME_*`/`AUTO_LANG_ROOT`、PATH 仅 system32、工作目录 %TEMP% 下仍可运行。发现并修复：①071 排除 `essay.txt` 会使打包材料的 luna 自然候选排序改变（`nihao`/`nh`/`shuruf`/`regx` 失败，`nihao` 上屏「㘈㘪」），纳入锁定后 13/13 恢复；②活宿主映射 `rime.dll` 时 Inno 重装 `DeleteFile code 5` 中止，改 `restartreplace` 后成功（锁定文件替换排到重启）。环境待测单列：干净快照断网首装、双账户 UAC 肩越、Edge 宿主与候选窗手感清单。

- [x] T-05 完成回归与现行说明

  owner_stage=work。依赖 T-04。执行 V-08 和 AV 应用测试，记录实际选中用例、耗时、通过/失败/待测，更新 Windows/installer/shared README。补 SD-01 草稿，手工简拼案例以个人显式短语身份说明。AC-05。

  证据（2026-11-25，提交 `6a97b32`；scratch `plan-artifacts/plan-072/t05/notes.md` sha256 `632B54C2…C681`）：V-08 四项全绿——共享冻结回放 13/13（11 首选/13 前五，与 071 一致）、原生夹具 17 用例/146 检查 + 隐私源码审计、071 部署/回退/升级/并发套件 32 检查（23.4 s），加上 AV CLI 22/22 与事务 15/15。运行期隐私扫描：add/deploy/参数错误的 stdout/stderr 与 `%TEMP%\\autoime.log` 中合成短语正文/短码出现 0 次，诊断仅阶段/状态码；学习库未改写。手工简拼：以个人显式短语登记 `zlby`→增量编译 后独立 probe rank=1（自然简拼仍不入前五，不改通用排序，已在 shared README 与 SD-01 说明）。SD-01 落地为 `docs/specs/autoime/phrase-management.md`（数据/命令/事务/刷新标记/AV 运行时/离线身份/隐私/验证边界与基线）；windows/shared/manager/installer README 同步。未测环境项：Edge 宿主、干净快照断网首装、双账户 UAC、候选窗手感。

- [x] T-06 最终复审

  owner_stage=review。依赖 T-05。同一树审核实现与测试，按高风险发布路径补针对性负例，完成 AC-06，明确产品缺陷与环境待测归属。复审通过后交 merge。

- [x] T-07 合入与归档

  owner_stage=merge。依赖 T-06。交付 SD-01 规范源和经复审代码，核对成品身份、归档计划、按仓库规约清理工作树。AC-07。

## 9. 复审记录

2026-11-20 起草。已静态核对 071 交付、当前部署实现、ACE 系统库规范及 1064 两端源码入口。未运行本计划探针或修改产品实现。Bash 取号脚本调用失败后，按本仓编号顺序在独占文件锁内核对活跃与两种归档目录，以 CreateNew 登记 072，下一编号推进至 073。

历史 r1 起草采用 AV 开发、A2R 发布及双端验收；本轮用户指出 A2R 成熟度差距后，由 r2 的 §5–8 替代该合同。

2026-11-20 r2 修订。当前交付与验证目标收敛为新版 AV；A2R 迁移留待后续成熟后规划。同步修改架构、发布材料、V-01/V-07、AC-01/AC-04、T-01/T-02/T-04/T-05 与 SD-01。新增 AV 随包发布形态及开发环境独立性验证，T-01 负责选择已支持的执行形态。全部任务仍未执行。

stage: new | plan_id: PLAN-072 | plan_revision: 2 | outcome: pass（可从 T-01 启动，AV 完整能力与随包形态待实证） | next: work | task_ids: T-01..T-07 | acceptance_ids: AC-01..AC-07

2026-11-25 T-01 完成（work 第 1 轮）。先证实「现有生产 CLI 未贯通 AV 应用工程」，再按 §5 落薄启动器与最小探针（提交 `d96a91d`，8 文件）。V-01 的五条腿与最小真实部署调用全部在新版 AV 真跑并通过；能力/所有权约束（owned Str 移动、List 视图、`remove` 取所有权、多模块 `use app.<mod>.<fn>`）记入 manager README 与证据。发现并命名上游解析缺口（`xs[i]`），以「剔除未被导入的 `std.list`」保持 AV 车道，未回退旧运行时。SDK 固定 rustc 1.98.0 / auto-lang `857f4876`；debug 预算与身份见 §8 T-01 证据。下一步 T-02（业务命令与共享测试）。

stage: work | plan_id: PLAN-072 | plan_revision: 2 | outcome: executing | worktree_path: D:/autostack/.wt/os-072/auto-os | branch: plan-072-dev | base_commit: 5eeb759 | code_commit: d96a91d | dependency_revisions: auto-lang 857f48768ceab10dd213144b2f5f9bc0ea64efcf (ACE crates path deps), rustc 1.98.0 | task_ids: T-01 done; T-02..T-05 pending | evidence: plan-artifacts/plan-072/t01/notes.md sha256 E3A9A3EC06D522D886C35E31EF1F37B76DBB6BEE02118F70A25FD4F8D54A46BA | blockers: none（named upstream gap: std.list 解析，owner auto-lang；已剔除不导入的模块） | next_responsible_role: work

stage: goal | plan_id: PLAN-072 | plan_revision: 2 | round: 1 | from: work | to: work | status: executing | outcome: pass | worktree: D:/autostack/.wt/os-072/auto-os | commit: d96a91d | next: work

2026-11-25 T-02 完成（work 第 2 轮）。`src/phrases.at` + `src/main.at` 落 §5 业务与暂存输出；`scripts/test.ps1` + `tests/phrases.cases.json` 在 release 启动器上 22/22 过（逐例 2.3–5.8 s）。发现并修复跨运行 store 复用导致的 17 s 冷启动增长（改每运行临时 store，~2.5 s/次）。测试作者源由拟定 `.case.ad` 调整为 JSON 用例表（AutoDown 无 argv+文件+退出码配方），理由与可迁移性见 §8 T-02 证据。Rime 会话可见性与事务部署归 T-03。

stage: work | plan_id: PLAN-072 | plan_revision: 2 | outcome: executing | worktree_path: D:/autostack/.wt/os-072/auto-os | branch: plan-072-dev | base_commit: 5eeb759 | code_commit: 035aa0b | dependency_revisions: auto-lang 1107ce03e7eaffd96893da9e17c01f2894e526cb (ACE crates path deps), rustc 1.98.0 | task_ids: T-01,T-02 done; T-03..T-05 pending | evidence: plan-artifacts/plan-072/t02/notes.md sha256 76E6B67CC4CDF0592972EB5A7952112F79F07235602F94644E9937D418B0AB2E; test-run-release.log sha256 A9CBF6413EA064F47898C3081476B221D1C9C8696CAE41D78C4C8A354CC7CEAC | blockers: none | next_responsible_role: work

stage: goal | plan_id: PLAN-072 | plan_revision: 2 | round: 2 | from: work | to: work | status: executing | outcome: pass | worktree: D:/autostack/.wt/os-072/auto-os | commit: 035aa0b | next: work

2026-11-25 T-03 完成（work 第 3 轮）。短语写入改为原生事务：部署锁内源字节复核、备份+日志、原子发布、维护、新会话候选核验、回滚与崩溃恢复；部署标记扩展为「资源 + 短语」双身份（V-04）；全部路径 API 改 UTF-16。CLI 22/22、事务 15/15 全绿（含独立 probe 进程与 071 冻结回归）。修两处实测缺陷（Trim 吞快路径、Luna 回退未置失败）。AC-03 达成；AC-04/AC-05 归 T-04/T-05。

stage: work | plan_id: PLAN-072 | plan_revision: 2 | outcome: executing | worktree_path: D:/autostack/.wt/os-072/auto-os | branch: plan-072-dev | base_commit: 5eeb759 | code_commit: 85a775a | dependency_revisions: launcher built against ACE auto-lang 1107ce03e7eaffd96893da9e17c01f2894e526cb (checkout HEAD moved to 4d41a9b56e7f9f6ebdceda15f6ba837c82f9b5d9 during the task); rustc 1.98.0 | task_ids: T-01..T-03 done; T-04,T-05 pending | evidence: plan-artifacts/plan-072/t03/notes.md sha256 CD866ABAB0E7D9E14FA9EA22512EA430F45705B396CDC09F9CF3BD693A51FA41; cli-suite.log sha256 891BDED134AA3ED1AED0880848C28C3BD51031AB51911AE08F285A06C93B62D7; txn-suite.log sha256 5EB838EF8318ACAB93B105DC3A09B38989095EA2FA5FC8524BE25B21F30C21B6 | blockers: none | next_responsible_role: work

stage: goal | plan_id: PLAN-072 | plan_revision: 2 | round: 3 | from: work | to: work | status: executing | outcome: pass | worktree: D:/autostack/.wt/os-072/auto-os | commit: 85a775a | next: work

2026-11-25 T-04 完成（work 第 4 轮）。离线包 0.2.0 纳入 AV 管理器（静态 CRT 启动器 + Auto 源 + 身份），15 项锁定随包材料；真机完成升级/注册校验/普通用户改短语/独立 probe/真实窗口上屏/卸载保留/重装；基线已记录。修复 071 的 essay.txt 缺口（候选排序）与 rime.dll 占用导致的重装中止。AC-04 的宿主腿用真实 WinForms TSF 客户端完成，Edge 与干净快照/双账户列为环境待测。

stage: work | plan_id: PLAN-072 | plan_revision: 2 | outcome: executing | worktree_path: D:/autostack/.wt/os-072/auto-os | branch: plan-072-dev | base_commit: 5eeb759 | code_commit: 67c67a6 | dependency_revisions: launcher built against ACE auto-lang 7d197e667ccbec640ed08af0a888434eb2617fe0; rustc 1.98.0 | task_ids: T-01..T-04 done; T-05 pending | evidence: plan-artifacts/plan-072/t04/notes.md sha256 A58331C058F7206A073612C7324FC644BAB9C4CDF439A562F4E62070E5971F3C; package sha256 63F0F5E63AE6A0434C1F5876F6A51C58D96D1DA22EA559E12D9D5B61D1BD6DBC; probe-packaged.log sha256 F00EEAC9EE8033755535308FC68FD4393E2C40C7588EB599D83352D145D304E3 | blockers: none | next_responsible_role: work

stage: goal | plan_id: PLAN-072 | plan_revision: 2 | round: 4 | from: work | to: work | status: executing | outcome: pass | worktree: D:/autostack/.wt/os-072/auto-os | commit: 67c67a6 | next: work

2026-11-25 T-05 完成（work 第 5 轮）。V-08 四项与两套应用套件全绿；运行期隐私扫描 0 泄漏；手工简拼以个人显式短语身份实测 rank=1；SD-01 落地为 docs/specs/autoime/phrase-management.md，四份 README 同步。work 阶段 T-01..T-05 全部完成，交 T-06 复审（同一树、独立上下文）。

stage: work | plan_id: PLAN-072 | plan_revision: 2 | outcome: pass | worktree_path: D:/autostack/.wt/os-072/auto-os | branch: plan-072-dev | base_commit: 5eeb759 | code_commit: 6a97b32 | dependency_revisions: launcher built against ACE auto-lang 7d197e667ccbec640ed08af0a888434eb2617fe0; rustc 1.98.0; deployed installer 0.2.0 sha256 63F0F5E63AE6A0434C1F5876F6A51C58D96D1DA22EA559E12D9D5B61D1BD6DBC | task_ids: T-01..T-05 done; T-06,T-07 review/merge-owned | evidence: plan-artifacts/plan-072/t05/notes.md sha256 632B54C2A211C683CEB208DAC528D80118C4842E18791349881D584AC0F0C681; V-08 replay D1A9CAD8…; fixture E23F7504…; deploy-suite 0F88B177…; cli 54119A90…; txn 682F866A… | blockers: none | next_responsible_role: review

stage: goal | plan_id: PLAN-072 | plan_revision: 2 | round: 5 | from: work | to: review | status: execution_done | outcome: pass | worktree: D:/autostack/.wt/os-072/auto-os | commit: 6a97b32 | next: review

2026-11-25 T-06 最终复审（独立子代理上下文 review-072，直接读取代码与证据、不采信执行摘要；未修改实现/计划/规范）。结论 **needs_fix**：AC-02 通过；AC-01/03/04/05 部分通过；AC-06 因下列发现暂不通过。复审逐项复核并独立重放了冲突、崩溃恢复、非法导入、同码多词、deploy 目录参数、BOM/CRLF 写入与安装后干净环境运行；已记录证据复用与自跑项（两条自跑命令因冷部署 >10 s，属复审探针非计划用例）。发现（稳定 ID，供 work 修复）：

- R-01（major，AC-03/SD-01）`deploy` 忽略 `--user-data-dir`：`AUTOIME_RIME_USER_DATA_DIR=<env> autoime-config deploy --user-data-dir <flag>` 退出 0 但只部署 env 目录（`main.at` 的 `run_deploy` 仍以空参 spawn5）。修复：把解析出的用户目录作为 `--user-data-dir` 传给原生工具，并补 1 条用例。
- R-02（major，AC-03/SD-01）新会话校验只看候选**当前页**（5 条）：同码第 6 个文字（如 5 个同码词 + 低权词）被判 `phrase not visible` 而回滚；`absent` 在文字位于第二页时也通过（把仍在文件的词报成已移除）。既有 `同码不同文字允许共存` 合同被窄化。修复：校验枚举该码的**全部候选**（展示快照仍可页内），补同码多词用例（`rime_engine.cpp` 的 Snapshot 增加全体候选，`deploy_main.cpp` 扫描全体）。
- R-03（major，AC-01/SD-01）随包启动器 PE `SizeOfStackReserve=0x100000`（1 MiB），与 SD-01/README/§8 的「32 MiB 栈」不符；根因假设为 `prepare.ps1` 的 RUSTFLAGS 只含 crt-static、产品 crate 无 `.cargo/config.toml`（事实已由 `dumpbin /headers` 确认）。修复：构建时加 `-C link-arg=/STACK:33554432`，重读 PE、重打包并更新身份/证据；若选择窄化合同须经用户批准并升 plan revision。
- R-04（major，AC-05）`windows/installer/README.md` 仍写「明确不随带 data/essay.txt」，与 0.2.0 包内容（15 项、含 essay.txt）矛盾。修复：删除/改写该段。
- R-05（minor，AC-02）`--user-data-dir` 位于末尾缺值时静默回退默认目录并 exit 0；期望 exit 2。修复：区分「无此参数」与「有参数无值」。
- R-06（minor，AC-03）同内容并发命令共享同一 token 文件名，README「两个管理器永不互相覆盖暂存文件」不成立（同内容重试可能 exit 4/5）。修复：加入每进程 nonce，或窄化该表述。
- R-07（minor，AC-03 证据准确性）`AUTOIME_TEST_CRASH_AFTER_JOURNAL` 在 `WriteJournal` 之前触发，故 T3.6 实际覆盖「备份后/写日志前」，未覆盖「已写日志/发布前」，且会留下孤儿 backup。修复：把探针移到 journal 之后，或改名并清孤儿备份。

修复归属：T-01（R-03 栈）、T-02（R-05 参数）、T-03（R-01/R-02/R-06/R-07）、T-04（重打包与身份刷新）、T-05（R-04 与 SD-01 三处一致化）。五项均已重新置为未完成，`current_step=0`；按 work 修复上限（≤3 轮 work↔review）推进。复审证据：`plan-artifacts/plan-072/review/notes.md`（复审自跑命令、原始输出、PE 头与复用证据的哈希绑定）。

stage: review | review_scope: final | reviewer_identity: independent subagent context (review-072; read code/evidence directly, no implementation writes) | plan_id: PLAN-072 | plan_revision: 2 | outcome: needs_fix | worktree_path: D:/autostack/.wt/os-072/auto-os | branch: plan-072-dev | reviewed_commit: 6a97b32b0e83bb9ff12007cfb0c117f98b9803ba | base_commit: 5eeb759 | dependency_revisions: ACE auto-lang 7d197e667ccbec640ed08af0a888434eb2617fe0; rustc 1.98.0; deploy 15C46C53046DF45C3C292F40AA2C30AC277A22EC3BEEDC13F28BEC0E6D4854C2; installer 0.2.0 sha256 63F0F5E63AE6A0434C1F5876F6A51C58D96D1DA22EA559E12D9D5B61D1BD6DBC | spec_inputs: docs/specs/autoime/phrase-management.md sha256 C37F44E5AC62F9859BD33B9E8EFDADFD17A7307EC03A15F6513E2B38E711E51B; plan sha256 68D00057DE26C21B661AD04B15CA9A7973EDD865C1C1013E2D55DBD8640D67D3 | acceptance_results: AC-01 partial, AC-02 pass, AC-03 partial, AC-04 partial(env-pending host legs), AC-05 partial, AC-06 needs_fix | pending_closeout_gates: T-07 merge; repackage/identity after R-03; Edge/Notepad-clean-snapshot-double-account window feel; affected-suite re-run | findings: R-01..R-07 | evidence: plan-artifacts/plan-072/review/notes.md sha256 96DD586F3FBAC0DD049A991F4D9336528B33103B4841FC2C2C1F55CF73E978DF | next: work

stage: goal | plan_id: PLAN-072 | plan_revision: 2 | round: 6 | from: review | to: work | status: executing | outcome: needs_fix | worktree: D:/autostack/.wt/os-072/auto-os | commit: 6a97b32 | next: work

2026-11-25 R-01..R-07 修复完成（work 第 7 轮，提交 `89630ba`；证据 `plan-artifacts/plan-072/repair/notes.md`）。逐项：R-01 `deploy` 转发 `--user-data-dir`（新 CLI 用例 PASS）；R-02 新会话校验逐页枚举候选（Page_Down 0xff56 直到最后一页）+ `custom_phrase.max_candidates: 50`，同码多词可翻页（T3.16/T3.17 PASS，冻结基线 13/13 不变）；R-03 构建加 `/STACK:33554432`，PE 实测 `0x2000000`，启动器 sha `9502F858…20F1`、安装包 `4EAE703E…28A0`/9,314,871 B；R-04 installer README 更正；R-05 末尾缺值参数 → exit 2（新用例 PASS）；R-06 暂存 token 加每运行时间 nonce 并收窄表述；R-07 崩溃探针移到写日志之后、清理孤儿备份（T3.6 断言 journal/backup，新 T3.18 PASS）。回归：CLI 24/24（最长 5,093 ms）、事务 18/18（最长 6,048 ms）、071 冻结回放 13/13、修复包 payload 干净环境模拟安装 add/list/deploy + 独立 probe rank=1。环境限制：修复包的真机重装撞到未答复的 UAC 提示（consent.exe 待处理，setup 未运行）；包/staged 身份与安装器哈希门已核，T-04 已用同一安装器完成真实装/升/卸，重装与宿主复验记为待交互授权，不计通过。

stage: work | plan_id: PLAN-072 | plan_revision: 2 | outcome: pass | worktree_path: D:/autostack/.wt/os-072/auto-os | branch: plan-072-dev | base_commit: 5eeb759 | code_commit: 89630ba | dependency_revisions: ACE auto-lang 4e2fd20e6e454152c77eab60ace31f92f721fd51 (launcher rebuild); rustc 1.98.0; deploy 7EA68D227BCDD0E23F69C8400A65C446CEA3F014D60D48E338D7F58B564B002A; installer 4EAE703E3372AEECBE3993CEDB2C0473A86CF7D9D4092E30511E99BFD32E28A0 | task_ids: T-01..T-05 repaired; T-06 re-review pending | findings_closed: R-01..R-07 | evidence: plan-artifacts/plan-072/repair/notes.md sha256 7A9584FA00C08249C725474FFFA51813052097229718F74A2C7DD95B1E0940FB; cli-suite.log sha256 73F673002BA6488F13215C1CF58EE5CF7CB1483D2A7B64B740C5D4319453549F; txn-suite.log sha256 1D939D87A76163180737C43099542E9AB7F6E59442954575D8CB242FD745EE41 | blockers: none | next_responsible_role: review

stage: goal | plan_id: PLAN-072 | plan_revision: 2 | round: 7 | from: work | to: review | status: execution_done | outcome: pass | worktree: D:/autostack/.wt/os-072/auto-os | commit: 89630ba | next: review

2026-11-25 T-06 第二次最终复审（独立上下文 rereview-072，直接读码/复跑；未改实现/计划/规范）。结论 **needs_fix**：R-01..R-07 全部关闭（R-01 deploy 转发、R-02 逐页候选、R-03 实测 32 MiB、R-04 README、R-05 末尾缺值、R-06 nonce、R-07 探针/孤儿备份），AC-01/02/03 pass、AC-04/05 partial；新增 N-01/N-02/N-03。复审自跑两套件（CLI 24/24、事务 18/18）、071 冻结 13/13、R-02 两向重放、冲突与两处恢复、55 词边界与「删掉 max_candidates」对照，并实测 staged/工作树两处 PE 栈。

**两次最终复审失败的诊断（work 修复前置检查点）**：剩余不变量与漏检原因——
- N-01（major，文档数字与冻结证据不符）：规范仍写「22/15 用例」「包体 9,313,651」，README 同值，而修复轮新增 2 CLI + 3 事务用例并重打包。漏检原因：修复轮只更新 §9，未把规范/README 的数字从产物重新推导；复审矩阵没有「文档数字 ↔ 产物」核对项。调整：本轮建立一次性核对（套件计数、包体字节/哈希、schema 字节/哈希、ACE 闭包清单），并把它列入下次复审范围。
- N-02（minor，R-05 残余）：空值 `--user-data-dir ""` 仍静默回退；末尾以选项名作值的文本被误判为缺值。漏检原因：R-05 用「最后 token 是选项名」启发式，只覆盖复审报告的单例；未建模选项文法（选项必消费下一个 token），也未覆盖空值分支。调整：改为顺序扫描（选项若无后继或后继为空 → 用法错误），并补「空值」「选项名作文本」两条用例。
- N-03（minor，机制与文档/死代码不一致）：`Snapshot::all_candidates` 填充后无人读取（页面枚举才是真实 oracle）；`custom_phrase.max_candidates: 50` 实测为空转（删掉该块第 6 词仍在 p1 可见）；规范写「全部候选」而实现是 20 页上限。漏检原因：先加字段后改用翻页、未删除死路径；把「改了配置」当作行为原因而未做对照；文档用「全部」替代边界。调整：删除死字段与空转配置（恢复 schema 原字节/哈希）、规范改「逐页枚举（最多 20 页，取到最后一页）」并在 ACE 闭包补 `std.time`；以后归因行为必须先做对照实验。

stage: review | review_scope: final | reviewer_identity: independent subagent context (rereview-072; code/evidence read directly, no writes) | plan_id: PLAN-072 | plan_revision: 2 | outcome: needs_fix | worktree_path: D:/autostack/.wt/os-072/auto-os | branch: plan-072-dev | reviewed_commit: 89630bae4c4de6f2c0adf9baf3ac469ca25bf2cc | base_commit: 5eeb759 | dependency_revisions: ACE auto-lang 4e2fd20e6e454152c77eab60ace31f92f721fd51; rustc 1.98.0; deploy 7EA68D227BCDD0E23F69C8400A65C446CEA3F014D60D48E338D7F58B564B002A; launcher 9502F85846860517047A5615D8D2D31BAD36C99748659129AF96FA650E1420F1; installer 4EAE703E3372AEECBE3993CEDB2C0473A86CF7D9D4092E30511E99BFD32E28A0 9,314,871 B | acceptance_results: AC-01 pass, AC-02 pass(N-02 residual), AC-03 pass, AC-04 partial(env-pending), AC-05 partial(N-01), AC-06 needs_fix | findings_closed: R-01..R-07; new: N-01 major, N-02 minor, N-03 minor | evidence: plan-artifacts/plan-072/rereview/notes.md sha256 A66555A67328688F055D1F5C42D0F64F8FC6C67CB66667ABEEF2CE7FB445E575 | next: work

stage: goal | plan_id: PLAN-072 | plan_revision: 2 | round: 8 | from: review | to: work | status: executing | outcome: needs_fix | worktree: D:/autostack/.wt/os-072/auto-os | commit: 89630ba | next: work

2026-11-25 N-01..N-03 修复完成（work 第 9 轮，提交 `b3fc48d`；证据 `plan-artifacts/plan-072/repair2/notes.md` sha256 `2A0C6F9E…3F94`）。N-01：规范改 26/18 用例、最终包体 9,316,442 B + sha256 `19C295E6…21F1`、ACE 闭包补 `std.time`、基线改区间，installer README 同步；已用 grep 复核无 9,313,651/63F0F5E6/22-15 残留。N-02：`scan_dangling` 按选项文法扫描（选项消费下一 token；缺失或空值 → exit 2；选项名可作正文），新增空值与「--file 作文本」两用例；测试 runner 每例设 `AUTOIME_RIME_USER_DATA_DIR`，任何回退都落在用例目录。N-03：删除死的 `Snapshot::all_candidates`，回退空转的 `max_candidates`（schema 恢复 3,000 B/`7012834E…`），规范改为「逐页枚举（PageDown 至最后一页，最多 20 页）」。回归：CLI 26/26（一次全量跑遇 2 例全局部署锁瞬时 busy，即时重跑两例通过后全量 26/26）、事务 18/18、071 冻结 13/13、最终包 payload 干净环境 add 4.8 s + 独立 probe rank=1。环境待测不变（UAC 重装/Edge/干净快照/双账户/手感）。

stage: work | plan_id: PLAN-072 | plan_revision: 2 | outcome: pass | worktree_path: D:/autostack/.wt/os-072/auto-os | branch: plan-072-dev | base_commit: 5eeb759 | code_commit: b3fc48d | dependency_revisions: ACE auto-lang 4e2fd20e6e454152c77eab60ace31f92f721fd51 (launcher build identity); rustc 1.98.0; deploy CCD0CB7E09AA3758911E11F1620C2E075A075EF762CF5707970EF58F3193A333; launcher 9502F85846860517047A5615D8D2D31BAD36C99748659129AF96FA650E1420F1; installer 19C295E6BC2897C09E7CFA19BA086DC16CDA6AA031B1ED6EA71256DC024F21F1 9,316,442 B | task_ids: T-02..T-05 repaired; T-06 third review pending | findings_closed: N-01..N-03 | evidence: plan-artifacts/plan-072/repair2/notes.md sha256 2A0C6F9E76D482CA4D18CAEE46D4CEC5E7D79A3AE8C1D8D03F30B3204FE93F94; cli-suite.log 19ACAC1F0957FEB390ED84158100CCD8430034099109DFEF91FE743105B9D6F4; txn-suite.log A84DE242F44E6297878380B6AE287A4E44E6968A2E03C3A2B680A8037D3AFCFF; baseline-071.log 16025259E2F9328E7005A0A044FCE7E966FF2B64B41BB3D3002A5F7A733AB439; payload-probe.log 4829A23CA1D68440F334E16E9205A52F1470CD5342097043D27FD8BB6EA4B191 | blockers: none | next_responsible_role: review

stage: goal | plan_id: PLAN-072 | plan_revision: 2 | round: 9 | from: work | to: review | status: execution_done | outcome: pass | worktree: D:/autostack/.wt/os-072/auto-os | commit: b3fc48d | next: review

2026-11-25 T-06 第三次最终复审（独立上下文 review3-072）。结论 **needs_fix**：N-02/N-03 关闭、R-01..R-07 维持关闭；N-01 因残留 **N-04** 未闭合；新增 N-05/N-06。AC-01/02/03 pass；AC-04 partial（N-06 + 环境待测）；AC-05 partial（N-04/N-05）；AC-06 needs_fix。复审自跑：CLI 26/26（绝对路径）、事务 18/18、071 冻结 13/13、原生夹具 17/146、N-02 四探针、R-02 四向重放、两处 PE 栈、最终包 sha/字节、stripped-env payload + probe rank=1；并证明「文档化相对路径」调用只有 16/26。

**第三次失败的诊断（work 修复前置检查点，第三个修复循环，上限内）**：
- N-06（major，交付身份失真）：`manager/identity.txt` 的 `source=main.at` 仍是 11:52 的 04739531，而同包 `manager/src/main.at` 是 12:22 的 705D6406（N-02 改了业务源但只重打包、未重跑 prepare；`stage-manager.ps1` 只核 launcher 哈希）。漏检原因：把「启动器字节未变」当作「身份无需重算」，没有把 identity 的 source 行与 src 逐文件比对，也没有在打包门里核源码哈希。调整：每次打包前强制 prepare，`stage-manager.ps1` 增加 `source=` 行逐项核验；本轮重算并重打包。
- N-04（minor，文档数字再漂移）：`windows/README.md` 仍写 136 checks/22 例/15 例，`installer/README.md` 仍写旧启动器哈希 3F717E6C；`shared/resources/README.md` 的 `autoime_terms.dict.yaml` 行（779 B/9B798B3D）与实际（775 B/BB08D34D，071 起既有）不符。漏检原因：上一轮只 grep 了少数精确串，未按「产物值 → 全仓规范文档」建清单，也漏掉「例/checks/哈希」变体。调整：本轮从产物重新推导并逐项核对所有运营文档（套件计数、夹具/部署检查数、启动器/包/架构哈希、资源字节与哈希、基线），并保留清单式核对记录。
- N-05（minor，文档命令不可复现）：`test.ps1` 不做路径绝对化，按 README 的相对路径调用会 Push-Location 到用例目录后 CreateProcess 失败（16/26）；`apply_content` 对 `AUTOIME_DEPLOY_EXE` 无存在性检查，错误退化为 status -1。漏检原因：我们一直用绝对路径跑套件，未执行文档中的原样命令。调整：runner 对输入路径 `Resolve-Path`，`apply_content` 增加存在性检查并给具名错误，然后按文档原样命令复跑 26/26。

stage: review | review_scope: final | reviewer_identity: independent subagent context (review3-072; code/evidence read directly, no writes) | plan_id: PLAN-072 | plan_revision: 2 | outcome: needs_fix | worktree_path: D:/autostack/.wt/os-072/auto-os | branch: plan-072-dev | reviewed_commit: b3fc48d112d3eb7b0a7fa178d541f165059e9d65 | base_commit: 5eeb759 | dependency_revisions: ACE auto-lang 4e2fd20e6e454152c77eab60ace31f92f721fd51; rustc 1.98.0; deploy CCD0CB7E09AA3758911E11F1620C2E075A075EF762CF5707970EF58F3193A333; launcher 9502F85846860517047A5615D8D2D31BAD36C99748659129AF96FA650E1420F1; installer 19C295E6BC2897C09E7CFA19BA086DC16CDA6AA031B1ED6EA71256DC024F21F1 9,316,442 B | acceptance_results: AC-01 pass, AC-02 pass, AC-03 pass, AC-04 partial(N-06), AC-05 partial(N-04/N-05), AC-06 needs_fix | findings_closed: N-02,N-03,R-01..R-07; open: N-01 residual N-04; new: N-04 minor, N-05 minor, N-06 major | evidence: plan-artifacts/plan-072/review3/ (cli-suite-abs.log, txn-suite.log, baseline-071.log, payload-probe.log, n02-probe.ps1, r02-replay.log) | next: work

stage: goal | plan_id: PLAN-072 | plan_revision: 2 | round: 10 | from: review | to: work | status: executing | outcome: needs_fix | worktree: D:/autostack/.wt/os-072/auto-os | commit: b3fc48d | next: work

2026-11-25 N-04..N-06 修复完成（work 第 11 轮，提交 `f7527d4`；证据 `plan-artifacts/plan-072/repair3/notes.md` sha256 `0F3892C3…76F9`）。N-06：`prepare.ps1` 强制重算身份并按「二进制哈希未变则沿用其构建 ACE rev、否则记当前 HEAD」记录，`stage-manager.ps1` 逐项核验 identity 的 `source=` 行与 `manager/src`（本轮 gate 输出 `source identity verified for 2 files`，staged main.at `17C4C601…` 与源码一致）；launcher 因强制重链变为 `ACBC6774…`（PE 栈仍 `0x2000000`，ace_rev `f4b97392…`），包体重打为 9,315,763 B / `9CBFE087…`。N-04：全部运营文档数字从产物重推（windows README 146 checks/26 例/18 例，installer README 启动器与包哈希，spec 包体与基线，shared README 的 terms 词表 775 B/`BB08D34D…` 更正 071 起旧行）。N-05：runner 对四个路径输入 `Resolve-Path` 并每例固定 `AUTOIME_RIME_USER_DATA_DIR`，`apply_content` 对不存在的部署工具给具名错误，脚本对可重试 exit 7 重试一次；按文档原样相对命令从仓根复跑 **26/26**。回归：事务 18/18、071 冻结 13/13、最终包 payload stripped-env add/list + 独立 probe rank=1。这是修复循环第 3 次（上限），交第 4 次（最后一次）最终复审。

stage: work | plan_id: PLAN-072 | plan_revision: 2 | outcome: pass | worktree_path: D:/autostack/.wt/os-072/auto-os | branch: plan-072-dev | base_commit: 5eeb759 | code_commit: f7527d4 | dependency_revisions: ACE auto-lang f4b9739206d65f8c71fa7224802bbbf81dfcb599 (relinked launcher); rustc 1.98.0; deploy CCD0CB7E09AA3758911E11F1620C2E075A075EF762CF5707970EF58F3193A333; launcher ACBC67745C0940D456CCE106614D1D836A9B06E8FCA9E986B313167AD6795415; installer 9CBFE087CAA5ACD1472B86F90278BD97FDC087ECE23B309B0AADC03962883CC1 9,315,763 B | task_ids: T-02..T-05 repaired; T-06 fourth review pending | findings_closed: N-04,N-05,N-06 (N-01 residual) | evidence: plan-artifacts/plan-072/repair3/notes.md sha256 0F3892C355E3DFFD691BD4E00D8DD50E06D5E3BA69CFA2C71430FB4736BE76F9; cli-suite-relative.log F38850AA584C0AF6CDF958D0939781524688772F7F46FC6C6335FA2112A89313; txn-suite.log 94475B0478F70F5B35B897BCBC3BA8F69B120A7D4DF90D24276D821A64B36B3F; baseline-071.log 89FC6E5C0D99E62FF4B719921251B3D12BD502D8AFC2AC40E6DB32BAC2246EAD; payload-probe.log 4829A23CA1D68440F334E16E9205A52F1470CD5342097043D27FD8BB6EA4B191 | blockers: none | next_responsible_role: review

stage: goal | plan_id: PLAN-072 | plan_revision: 2 | round: 11 | from: work | to: review | status: execution_done | outcome: pass | worktree: D:/autostack/.wt/os-072/auto-os | commit: f7527d4 | next: review

2026-11-25 T-06 第四次（最后一次）最终复审：**pass**（独立上下文 review4-072；直接读码/复跑，未改实现/计划/规范）。N-04/N-05/N-06 关闭，N-01..N-03 与 R-01..R-07 维持关闭；AC-01..AC-06 均通过（AC-04 的宿主/环境腿仍单列为待测）。复审自跑：文档化相对命令三次（26/26，最长 8,350 ms）、事务 18/18（最长 6,947 ms）、071 冻结 13/13、夹具 17/146、两处 launcher `dumpbin` 栈 `0x2000000`、identity 门三种负例（篡改/漏 prepare/缺行均具名拒绝）、部署工具缺失具名错误 exit 4、故意持锁验证 exit 7 语义、stripped-env payload + 独立 probe rank=1。唯一新观察 **N-07（minor，非阻塞）**：最终包内 `resources/README.md` 仍是旧 terms 行（775/`BB08D34D` 的更正比 ISCC 晚 12 s 落盘；规范文档与功能资源均正确，不覆盖任何 AC）；若日后重打包可顺带刷新。AC-07 归 merge。

stage: review | review_scope: final | reviewer_identity: independent subagent context (review4-072; code/artifacts read directly, no writes) | plan_id: PLAN-072 | plan_revision: 2 | outcome: pass | worktree_path: D:/autostack/.wt/os-072/auto-os | branch: plan-072-dev | reviewed_commit: f7527d48bed3b36032b8409168e218a48eb0fc5d | base_commit: 5eeb759 | dependency_revisions: ACE auto-lang f4b9739206d65f8c71fa7224802bbbf81dfcb599 (crates identical to 4e2fd20e); rustc 1.98.0; deploy CCD0CB7E09AA3758911E11F1620C2E075A075EF762CF5707970EF58F3193A333; launcher ACBC67745C0940D456CCE106614D1D836A9B06E8FCA9E986B313167AD6795415; installer 9CBFE087CAA5ACD1472B86F90278BD97FDC087ECE23B309B0AADC03962883CC1 9,315,763 B | spec_inputs: docs/specs/autoime/phrase-management.md sha256 292ED1FF937BAE4A1904551006242B3C34F2B43BB508DE45CC66545B36A977A0 | acceptance_results: AC-01..AC-06 pass; AC-07 merge-owned | pending_closeout_gates: T-07 merge/archive/cleanup/receipt; clean-snapshot offline install; dual-account UAC; Edge host; candidate-window feel; refreshed-package reinstall (UAC); optional N-07 repackage refresh | findings_closed: R-01..R-07, N-01..N-06 | findings_open: none blocking; N-07 minor non-blocking observation | evidence: plan-artifacts/plan-072/review4/notes.md sha256 6C3674674330C0F04362C50B93F64FB92C2CEFE7D58E6DBD69B1A216867570E3 | next: merge

stage: goal | plan_id: PLAN-072 | plan_revision: 2 | round: 12 | from: review | to: merge | status: reviewed | outcome: pass | worktree: D:/autostack/.wt/os-072/auto-os | commit: f7527d4 | next: merge

## 10. 待澄清事项

当前无需用户补充产品决策。T-01 负责确认普通工程的生产运行入口和所需能力组合；如发现真实语言/库缺口，以最小复现交对应 owner，并在本计划明确阻塞任务。CLI 为本轮规划选择，图形前端可在该模块可用后承接。071 的环境待测记录以其归档为准。

依赖缺口记录（非用户决策）：2026-11-25 T-01 发现 auto-lang HEAD `857f4876` 的 packages 前端拒绝索引表达式 `xs[i]`，致冻结 972 装配集 `stdlib/ace/asm/list.at` 不可解析；仓库自带用例 `ace-av --test conformance -- sys_d01_d06_1064::v02_d01_d02_file_text_range_and_dir_names_av --exact` 同样失败（`std::list` `@1526..1528`）。最小复现：模块源 `fn pick(xs List<Str>, i int) Str { let v Str = xs[i]\n return v }` → `source.unsupported@Text "`fn` is not part of this source profile"`；`xs.get(i)` 可编译。owner 为 auto-lang 972/前端；manager 不导入 `std.list`，启动器仅剔除该模块，修复落地后需复验并移除剔除。

实现环境待测清单（2026-11-25 T-04/T-05，按 AC-04 单列，不计入已通过）：Edge 宿主（TSF 同一路径，仅 WinForms 真实窗口实跑）；干净 Windows 快照断网首装；双账户 UAC 肩越；071 候选窗跟随/缩放/点击手感的人工确认。复验入口：安装 0.2.0 包后按 `manager/tests/host-textbox.ps1` 步骤在目标宿主执行，或按 installer README 的 V-07 清单补测。

## 交付与合入回执（T-07 merge，2026-11-25）

- prepared：r2 复审 `pass`（review4-072）绑定 `f7527d4`；SD-01 目标 `docs/specs/autoime/phrase-management.md`（sha256 `292ED1FF937BAE4A1904551006242B3C34F2B43BB508DE45CC66545B36A977A0`）。
- landed：`plan-072-dev` rebase 到 main 后 `git merge --ff-only`；`git range-diff 5eeb759..f7527d4 5848605..f37fe47` 八条全 `=`（补丁等价），main tip = **`f37fe474ddab7f4d35044cf9cfdbade9cc93c3c8`**，无合并提交。
- canonical_verified：落地树中 `experimental/autoime/**` 与 `docs/specs/autoime/phrase-management.md` 与复审内容逐字节一致（`git diff f37fe47` 为空），规范哈希同上。
- 交付身份：launcher `ACBC67745C0940D456CCE106614D1D836A9B06E8FCA9E986B313167AD6795415`（13,563,904 B，32 MiB 栈，静态 CRT）；deploy `CCD0CB7E09AA3758911E11F1620C2E075A075EF762CF5707970EF58F3193A333`；安装包 0.2.0 **9,315,763 B** sha256 `9CBFE087CAA5ACD1472B86F90278BD97FDC087ECE23B309B0AADC03962883CC1`；rustc 1.98.0，ACE `f4b9739206d65f8c71fa7224802bbbf81dfcb599`（ACE crates 与 `4e2fd20e` 相同）。安装包为构建产物（`dist/` 不入库），身份由 `manager/identity.txt` 与本文回执固定，重打包可复核。
- 测试范围（复审冻结基线，本次合入未改代码/规范，补丁等价故复用）：CLI 26/26（最长 8,350 ms，按文档原样相对命令）、事务 18/18（最长 6,947 ms）、071 冻结回放 13/13（11 首选/13 前五）、原生夹具 17 用例/146 检查/0 失败、隐私与学习库证据沿用；均为 debug/开发路径，无新增 >10 s 用例。合并阶段未重跑（复用等价基线证据）。
- 环境待测（不宣称通过）：干净快照断网首装、双账户 UAC 肩越、Edge 宿主、候选窗跟随/缩放/手感清单、修复后安装包的真机重装（UAC 交互未答复）。
- 观察项 N-07（minor，非阻塞）：最终包内 `resources/README.md` 快照早于 terms 行更正 12 s（规范与功能资源正确）；若重打包可顺带刷新。
- 部署观测（不属于本计划交付范围）：本机已安装产品仍是修复前的 0.2.0（launcher `9502F858…`），修复版需交互式 UAC 重装才生效；合入流程未触发任何发布构建或服务重启。
- cleaned：见下条清理回执。

stage: merge | plan_id: PLAN-072 | plan_revision: 2 | outcome: pass | delivery_commit: f37fe474ddab7f4d35044cf9cfdbade9cc93c3c8 | canonical: docs/specs/autoime/phrase-management.md sha256 292ED1FF937BAE4A1904551006242B3C34F2B43BB508DE45CC66545B36A977A0 | archive: docs/plans/archived/072-autoime-ace-phrase-manager.md | cleanup: cleaned | pending: 环境待测清单见上 | next: done
- cleaned：`wt-guard.ps1 D:/autostack/.wt/os-072/auto-os` 报告 clean（0 reparse point）；工作树于 `f37fe47` 干净且全部提交已落地后移除，`plan-072-dev` 删除（--ff 合并入 main），空组目录 `D:/autostack/.wt/os-072` 移除；`plan-artifacts/plan-072`（8,836 文件 / 1,304,517,809 字节）在证据折叠后整目录删除；`.retention/policy.json` 追加 closeout（delivered/archived，无具名 hold）并更新 `last_merge_closeout`。批量回归门（auto-lang `.last-batch-regression.json`）属 auto-lang 仓，本仓不适用。
- 归档校验（实际状态）：`status: archived`、`current_step: 7/7`、T-01..T-07 全勾、交付/清理回执与规范路径在案；`docs/specs/autoime/phrase-management.md` 在 main 上哈希 `292ED1FF…A977A0`；`git log` 线性（rebase 8 补丁 + ff-only，无合并提交）。
stage: goal | plan_id: PLAN-072 | plan_revision: 2 | round: 13 | from: merge | to: done | status: archived | outcome: pass | worktree: D:/autostack/.wt/os-072/auto-os (removed) | commit: ed9b62b (delivery f37fe47) | next: done
