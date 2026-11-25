---
plan_id: PLAN-073
status: archived
feature_name: AutoIME 拼音召回与候选质量改进
author: [Codex]
created_at: 2026-11-23
updated_at: 2026-11-25
plan_revision: 3
depends_on: [PLAN-071]
current_step: 7
total_steps: 7
supersedes_spec_components: []
new_spec_components: [docs/specs/autoime/pinyin-quality.md]
touched_goals: []
---

# PLAN-073 · AutoIME 拼音召回与候选质量改进

## 0. 变更摘要

修复用户实测的 Shift 标点错误和「是的」在简拼/混输下排名过低的问题，并让现有项目术语在全拼、首字母简拼和全简混输下更容易输入，补齐可复跑的召回、排序和学习验证。优先改善按键转换、已随包交付的词库、拼音方案及索引配置，用源码阅读和自动回放定位原因，最后集中做宿主体验验收。

## 1. 目标

在空用户目录、清除个人短码影响的条件下，`zlby` 能在前五候选找到「增量编译」，`lxpy` 找到「离线拼音」，`hxck` 找到「候选窗口」。保留 071 已冻结的 13 例原义务和正常中文歧义，例如 `nh` 的多个合理候选。改进来自通用拼音检索及词典配置，个人显式短语由 072 负责。

本包以现有 librime、朙月资源和原创术语为范围，整句模型与大规模第三方词库升级留给后续根据测量结果规划。实现可直接以已交付 071 为基线启动。

r2 增补：空用户目录中，`shide`、`shid`、`shde`、`shd`、`sd` 的前五候选均应包含「是的」。中文半角模式下 Shift+`;` 输出 `：`、Shift+`=` 输出 `+`；同源 Shift 标点和数字行符号需要一并核验。修复必须保留英文直输、普通数字选词、单击 Shift 切换和宿主快捷键行为，不通过显式短码或前端硬插候选实现。

r3 分工：用户新增的“一键分词选字 → 锁定前缀 → 自动预测后缀 → 最终整词学习”由独立 [076 分段选词与整词学习](../076-auto-input-segment-selection-learning.md) 实施。073 保留全部已有质量、普通组句/学习和升级验收，不依赖 076；先修复当前 Shift 与常用词排序问题。076 的键盘/鼠标入口、统一预编辑组合、学习确认及误学撤销有独立验收。

## 2. 架构方案

按键 → speller 音节/缩写路径 → 各 translator 的词典与 prism → 候选合并去重 → 原生候选窗。T-01 分别检查“未召回”和“召回后名次较低”，确定真实修改面；禁止靠前端硬插候选、按测试输入特判或隐式写入 custom_phrase 获得通过。

方案和词库继续作为 `shared/resources/` 的唯一作者源，复用现有 C++ probe 的真实 librime 调用与 `shared/tests/cases.json` 格式。所需新业务工具采用新版 ACE AV，但当前可直接扩展既有 probe，源码/资源调整无需引入额外运行时。A2R 及 AutoUI 不构成本包前置。

## 3. 技术栈

librime 1.13.1、Rime YAML、已有 Windows probe、C++ TSF 宿主、Inno Setup 6。主导仓 auto-os，拟实施树 `D:/autostack/.wt/os-073/auto-os`，分支 `plan-073-dev`。work/review 顺序使用同一实施树。

## 4. 需求分析与背景调查

2026-11-23 用户要求规划 072 后的下一阶段，r1 为规划授权。2026-11-25 用户进一步报告 Shift 标点与「是的」简拼体验问题，本轮按修复这两个问题处理；优先执行相关诊断与修复，原有较广质量任务仍分阶段实施。当前主检出为 `2654639ece64aa6ffe74920869b32ec2ac96d47d`，072 文档为 executing，已有独立工作树及已安装 manager；这不等于 072 已完成最终复审。本包不等待 072、新版 A2R 或 AutoUI，但安装文件若有重叠须核对 072 的交付身份。

| 依据 | 观察 |
|---|---|
| [071 归档](https://github.com/auto-stack/auto-os/blob/main/docs/plans/archived/071-autoime-windows-offline-installer.md) | 原 13 例首选 11/13、前五 13/13，明确记录 zlby/lxpy/hxck 不入前五。 |
| [设计 05](../../design/05-auto-input-windows.md) | 默认方案应支持全拼、简拼、混输和本地学习，术语参与自然竞争。 |
| `experimental/autoime/shared/resources/autoime_pinyin.schema.yaml` | 继承 luna，独立 `script_translator@autoime_terms`，术语 initial_quality=0.2，关闭该术语 translator 的用户词典与组句。 |
| `shared/resources/autoime_terms.dict.yaml` 与 `autoime_terms.schema.yaml` | 12 条原创术语带逐字拼音；依赖方案负责部署。主方案与依赖方案的拼音索引关系需要核对。 |
| `shared/tests/cases.json`、`windows/probe/rime_probe.cpp` | 已支持独立预期首选/前五/名次，现有冻结回放先于学习与提交。 |

当前 docs/specs 未找到 AutoIME 输入质量专属规范，实施时以 SD-01 补齐。r1 的静态读取未证明召回失败根因；r2 的实际排名证据见下文。

r2 实测纠正：已安装资源的空用户库回放表明，「是的」并非没有被简拼召回，而是排名很低，详见 §9。`data/luna_pinyin.dict.yaml` 未找到完整「是的」词条，但声明 `use_preset_vocabulary: true`，因此不能单凭源词表缺项就断定最终词典缺词或组句来源；须继续核对预设词频资源、编译词典及 translator 的候选来源。`data/pinyin.yaml` 已含首字母和 zh/ch/sh 缩写代数，不能把重新添加同一规则作为修复结论。

## 5. 详细设计

所有代码路径相对 `experimental/autoime/`。新增 `shared/tests/pinyin-quality.json`，由原 probe `--cases` 消费；保留原 13 例及其断言。新增按键序列验证优先扩展现有 probe 的内建测试，不另建引擎模拟器。报告包含资源/引擎哈希、方案、用户数据初态、逐例前五候选、目标名次与实际提交结果，正文仅取合成语料。

T-01 冻结现有 12 条术语每条全拼/首字母/一种混输，共 36 个观察输入。强制验收至少覆盖 §1 的三个已知失败简拼及其全拼、混输，共 9 条；其余形成质量观察集，按真实结果给出剩余缺口，不能以未通过观察项宣称完整覆盖。增加普通中文同码/歧义控制，具体合法候选先由词库读音与基线回放核对再冻结。调整配置后重跑同一批输入，逐例展示前后差异。

修复应优先复用 librime 的标准拼音代数与索引机制。如果问题是 translator 使用的 prism 与缩写路径不一致，修正相应方案/部署依赖；若是合并排序问题，调整词频和 translator 质量时同时检验普通词控制，避免术语普遍挤占常用词。根因与取舍由 T-01 根据锁定版本源码、实际部署产物和回放结果落入执行记录。

r2 常用词质量：在上述语料中增加「是的」五种输入，并保留 `shide` 的「使得」、`sd` 的「速度/时代/设定」等合法同码词观察。用通用词条、读音、词频及组句排序解决问题；如需补充词表，作为共享自然拼音词库数据记录来源、读音和权重，保留完整词汇索引与学习路径，禁止添加 `sd → 是的` 等测试专用短码。大规模换库不是本次修复的前置。

r2 Shift 转换：`windows/ime/textservice.cpp::MakeKeyEvent` 已读取 Shift，`windows/ime/key_router.cpp::KeysymForVk` 却只在字母分支使用它，OEM 标点和数字分支仍返回未移位字符。修复应让 keysym 表示实际移位后的符号，并保留 modifier；覆盖 OEM 键、数字行及空闲/组合中的路由。当前按键映射按现有美式键位语义处理，布局相关行为须明确边界；不能把简单标点表声称为任意键盘布局支持。空闲中文时 Shift+数字的符号也须按标点路径处理，未移位数字仍交给宿主。需同时检查 Rime 拒收时的宿主回退，不把 `process_key` 的布尔值当作已经上屏的证据。

补组句和部分提交验证，以冻结词库中确定可输入的短句为输入，检查完整输出顺序、残余预编辑及最终无重复提交。用户词典学习用独立目录验证一个低频合成词的重复选择、正常关闭和重开后的结果；固定基础质量测试使用空学习目录。学习用例需记录实际前后词频或排名变化，不能把显式短码置顶当学习效果。若术语启用学习，须说明新增用户词典身份和升级保留规则。

上述义务继续针对既有默认输入路径；076 的显式分段造词路径要求锁定前缀一直保留在同一 composition 中，并在成功完整提交后确认学习，由 076 新增其专属事件回放。两包共用 `key_router.*`、`rime_engine.*`、schema/probe/installer 时采用单写入者，绑定合入版本后复用未受影响的证据；不得把已有 V-03/04 删除或用未来 076 的计划文字代替结果。

源码词典与方案变动必须改变部署资源版本，升级后从新引擎实例回读方案及新索引。使用已准备的旧版本资源构造升级、失败恢复测试，确认个人短语和学习数据仍可读取，已打开的旧宿主按现有刷新规则处理。兼容改动限定为本包资源与验证所需原生接入，TSF 编辑错误只修本包复现的回归。

### 规范增量

| delta_id | add/modify/retire | docs/specs 目标 | before/after rule | rationale | acceptance IDs |
|---|---|---|---|---|---|
| SD-01 | add | docs/specs/autoime/pinyin-quality.md | 明确自然拼音/个人短码区分、简拼目标、固定初态与用户学习的独立证据 | 稳定跨平台共享数据与质量验收口径 | AC-01–04 |
| SD-03 | modify | docs/specs/autoime/phrase-management.md | 「某一类简拼不入前五」的旧边界改为：0.1.x 不入前五，0.2.0 起走自然召回；个人短语仍是任意短码入口 | 073 修复后旧规范结论失效，必须与现状一致 | AC-02 |
| SD-02 | add | docs/specs/autoime/pinyin-quality.md | 增加常用词五种拼法的前五要求、Shift 后实际字符及中英文按键所有权约束 | 固化用户实测回归，区分可召回与可用排名 | AC-02、AC-07 |

## 6. 测试设计

拟新增用例均未运行，owner_stage=work。沿原 build.cmd 准备 ime/probe；测试只消费预构建产物。准备锁定版本 librime 源码只需读取 translator/speller/词典部署相关模块，记录实际提交或发行版本。

| Case | AC | 入口与条件 | 独立判据 |
|---|---|---|---|
| V-01 | AC-01/02 | probe，空用户库，36 个观察输入与原 13 例；调整前后各跑一次 | 目标名次逐例可比；原断言通过；9 条强制用例目标进入前五；三条指定简拼不依赖用户短语 |
| V-02 | AC-02 | 固定普通词与多候选歧义控制、一个无词典命中的输入 | 不因术语修复丢失冻结的合法候选；无命中不伪造提交；按候选标签选择的文字与展示一致 |
| V-03 | AC-03 | probe 真实按键、部分选词后继续输入、完整提交及取消 | 提交文本与冻结短句一致，残余组合正确，确认仅一次，取消无多余正文 |
| V-04 | AC-03 | 单独学习目录，重复选择低频合成词，再关闭重开 | 记录学习写入及候选变化；新实例保留结果；空目录质量基线不受其污染 |
| V-05 | AC-04 | 旧包资源升级新方案；损坏新依赖索引后恢复 | 新实例实际使用新版本；失败可恢复可输入状态；个人数据保留 |
| V-06 | AC-04 | 同构建记事本/Edge，最终集中试打 | 三个目标简拼真实上屏，组句/部分提交与取消正确；记录候选等待手感 |
| V-07 | AC-07 | 扩展现有 `windows/ime/tests/key_router_test.cpp`，并让真实 librime 消费生产映射产生的 keysym/modifier | 中文 Shift+`;` 提交 `：`，Shift+`=` 提交 `+`；同源 `? !`、引号/括号及数字行映射正确；空闲/组合中、英文直输、数字选词、Shift 单击、Ctrl/Alt/Win/AltGr、重复 Test→Down/Down-only 路径保持其约定行为 |

r2 的 V-01 增加「是的」五例前五断言；基线诊断可扫描更多候选以区分未召回与低排名，修复验收仍按真实五候选首页。V-06 同时验收这些输入与两个 Shift 上屏反例。V-07 原生 fixture 必须独立断言 keysym，真实引擎测试必须断言最终文本，不能只在 fake target 上通过。预编译工具与部署准备独立记录；新增用例逐项计时且每例不超过 10 秒。新增永久回归当前均未实现或运行，§9 仅为诊断证据。

执行命令沿已有 probe 参数，新增语料时使用 `--cases shared/tests/pinyin-quality.json` 并提供显式引擎/共享资源/隔离用户目录。probe 的传入用户目录存在自动清理行为，执行者必须只使用专用测试目录。用例运行/部署时间分列，记录引擎就绪后候选中位数/P95和词库编译耗时，性能变化附同机基线，不把微秒级引擎延迟称为屏幕延迟。

## 7. 验收标准

| ID | owner_stage | 条件 |
|---|---|---|
| AC-01 | work | 冻结对照输入和版本，源码与实际产物共同解释已知简拼失败的根因。 |
| AC-02 | work | V-01/02 的强制用例通过，原 13 例保留；其余观察项逐项记录，收益来自自然拼音路径。 |
| AC-03 | work | V-03/04 在真实 librime 上证明部分提交与学习持久化，测试初态隔离。 |
| AC-04 | work | V-05/06 通过，候选/部署性能有可复核基线，安装包包含新资源及版本说明。 |
| AC-05 | review | 同一树复核上述条件、SD-01、实际失败/待测，不把未执行项计作通过。 |
| AC-06 | merge | 落地经复审代码和规范，归档计划，记录交付版本与剩余观察项。 |
| AC-07 | work | V-07 通过且 V-06 记录真实上屏；Shift 标点问题及同源路径得到修复，英文/快捷键/数字选词/Shift 切换不回归。 |

## 8. 执行步骤

如 harness 有 TODO 工具，先登记以下 ID。准备独立实施树后优先 T-07 与 T-01 中的用户反例，T-02 先处理常用词质量；其他任务沿原依赖顺序进行。同一文件的并行计划采用单写入者。T-01 的已有诊断为部分证据，不能勾选为整个质量基线完成。

- [x] T-07 修复 Shift 符号与按键路由

  owner_stage=work，无需等待 T-02 或 072。修改 `windows/ime/key_router.cpp` 及既有 fixture，真实引擎补充符号提交验证；覆盖 V-07，更新 SD-02。AC-07。只做这项定向修复时，不重跑整个引擎探针或安装验收套件。

  证据（2026-11-25，提交 `82a9677` + `f379f58`）：`KeysymForVk` 对数字行/OEM 标点返回移位字符，中文空闲 Shift+数字走标点路径、未移位数字仍归宿主；fixture 21 用例/220 检查全过（`shift_symbol_mapping`/`shift_symbol_routing` 断言 keysym 与路由；`shift_symbols_real_engine` 用生产映射驱动 librime 1.13.1，3.2–3.7s，断言 `： + ？ “ —— ！ （）` 上屏；`shift_test_only_no_toggle` 复现真实宿主反馈——Test 阶段被消费、无 KeyDown 时 Shift+符号曾被误判为单击切模式（红相 3 失败），修复方式为 `OnTestKeyDown` 在 `shift_down_` 时置 `shift_used_`，绿相通过，纯单击与 Down-only 路径不回归）。半角/全角取舍按用户实测裁定记入 SD-02：保持 Weasel `half_shape` 预设（`+` 半角、`` ` `` 首选 `~`、`|`、`「」`、`——`、`《》`、`……`），全角可达性不在本包。Design 05 输入表、windows README 与 SD-02 描述同步。测试范围：`ime\test.cmd`（debug，真实引擎用例 3.7s，其余 <1ms）。

- [x] T-01 冻结质量基线并定位简拼原因

  owner_stage=work。读上述方案/词典/probe，建立新增质量语料，记录实际版本与 V-01 修复前结果，并核对对应 librime 代码。「是的」已有排名诊断，但候选来源、普通词控制和术语基线仍待完成。AC-01。

  证据（2026-11-25，提交 `2c64571` 起）：probe 增加整表扫描（`scan`/`expect_rank_all`/`expect_none`），新增 `shared/tests/pinyin-quality.json`（61 例：14 强制 + 36 观察 + 11 控制）。修复前：50/61，11 条强制失败——6 条术语简拼/混输整表名次 0（未召回），5 条「是的」输入名次 7/31/40/157/199。根因（源码 1.13.1 `lever/deployment_tasks.cc` `WorkspaceUpdate`/`SchemaUpdate` + `dict/dict_compiler.cc` `BuildPrism`）：依赖方案编译自己的词典时用**该方案**的 `speller/algebra` 生成 prism，`autoime_terms.schema.yaml` 无代数，术语 prism 缺首字母拼写；主方案全拼按音节编号仍命中。产物对照：仅 `autoime_terms.prism.bin` 1340→3792 字节，`.table.bin`/`.reverse.bin` 字节不变。「是的」来源：`data/essay.txt` 权重 776（使得 28566、速度 35075、上的 83517），已召回但排名过低。资源/引擎哈希与逐例日志见 `plan-artifacts/plan-073/t01/`。

- [x] T-02 修复自然简拼与候选排序（复审 REV-073-05 已修：resources README 61/50/61）

  owner_stage=work，依赖 T-01 对相应反例的定位。修改 shared/resources 中经证实的方案/词典面，先修复「是的」五种输入的前五质量，再处理原项目术语目标；验证 V-01/02，更新版本与资源说明。AC-02。

  证据（2026-11-25，提交 `318c612`）：术语依赖方案补 `speller/algebra`（abbreviation/spelling_correction/key_correction），术语 `initial_quality` 0.2→1.0（0.2 会让 `gaosipoj` 的「高斯泼溅」掉到第 2）；新增 `autoime_words.dict.yaml`（是的 `shi de` 40000）与依赖方案，主方案加 `script_translator@autoime_words`。修复后质量语料 61/61（36 观察全部第 1 位，「使得」在 `shide` 第 2、速度在 `sd` 前五），071 冻结 13/13（首选 11→12）。资源 0.2.0，README 表/机制说明与 SD-01/02 草稿更新。V-02 控制：`nh`→你好第 2、`shuruf`→输入法第 1、无命中 `vvvvv` 无候选无提交。

- [x] T-03 验证组句与本地学习

  owner_stage=work，依赖 T-02。扩展现有 probe 自动序列，执行 V-03/04，必要修复限于对应引擎适配/配置。AC-03。

  证据（2026-11-25，提交 `f50ff5e`）：probe 内建扩到 t1–t12。`t11_partial_commit`（45 ms）：`nihaoshijie` 首选提交「你好世界」且只一次；提交「你好」后继续 `shijie` 得到新组合 `shi jie`，按展示候选提交「世界」无多余提交；同一路径 Esc 取消后预编辑清空、无提交。`t12_learning_persist`（3.2–5.5 s，含两次部署/重开）：独立空目录 `<user>-learn` 中选「李显」3 次，正常关闭重开后名次 2→1，`autoime_user.userdb` 在独立目录生成；固定质量目录未被学习污染。

- [x] T-04 完成升级与集中宿主验收（复审 REV-073-01..04 已修：SD-01 计数/资源/符号/宿主边界）

  owner_stage=work，依赖 T-03、T-07。复用 windows/ime/tests/run-t04.ps1、原 fixture、installer 构建入口，完成 V-05/06 与性能记录，更新 README 与 SD-01/02 草稿。AC-04/07。

  证据（2026-11-25，部分完成；V-06 环境阻塞）：`run-t04.ps1` 32 检查全过（28.1 s：A 冷启、B 幂等重部署、C 缺引擎直通、D 坏资源回退朙月、E 坏升级回滚上一可用版本、F 升级保留用户短语与学习库、G 双宿主并发串行、H 延迟门）。性能（`autoime_deploy --latency 20`）：首次部署 3995 ms，就绪后按键→候选中位数 313 µs、P95 327 µs，`resources_version=r5e9dee442fb52287`。安装包已用 Inno Setup 6.7.4 构建：`windows/installer/dist/AutoIME-Setup-x64.exe` 9,317,011 字节，SHA-256 `C084638E6D4550B07CDD25F96953D5300BA4B8577C73F231DE2D5DC7C57563AC`；staged `resources` 含 7 个随包文件（含新增 `autoime_words.*`）与 0.2.0 版本说明。并发基线备注：本轮另一会话 cargo/rustc 编译在进行，套件仍全过，延迟远低于门限；如需纯净基线应在空闲机复跑。V-06 宿主集中试打 **NOT_RUN**：当前会话非管理员，无法向 HKLM 注册/覆盖 `C:\Program Files\AutoIME`（已装构建 `5C96…F849` ≠ 本构建 `B0B1…ED68`），不以旧构建冒充。解锁：管理员会话安装 dist 包或 `scripts\install.cmd`，再跑 `manager\tests\host-textbox.ps1` 与记事本/VS Code 实打。

  V-06 验收补充（用户实测反馈，2026-11-25）：安装版 0.2.0 的真实 TSF 窗口基线为 `？` `：` `！` 正常、`——` `《` `》` `……` 正常、`+` 半角、`` ` `` 首选 `~`（`～` 次选）、`@`/`|`/`「」` 走 `symbols`；这些不是本构建证据。本包修复后须在**记事本与 VS Code**（不同宿主的 Test/Down 回调处理不同）复验：`zlby`/`lxpy`/`hxck`、`shide`/`sd`、Shift+`;`、Shift+`=`，并检查 `%TEMP%\autoime.log` 不再出现 Shift+符号触发的 `shift_click_toggle`。可复用用户脚本 `tmp\host-punct3.ps1`（已存档 `plan-artifacts/plan-073/t04/host-punct3.ps1`，sha256 `B6F07881…A07D`）做硬件级按键回放，再用 `manager\tests\host-textbox.ps1` 做 TSF 窗口上屏断言。

  V-06 通过（2026-11-25，安装构建 `736895A6…B78F` = 验证构建同哈希）：新增 `windows/ime/tests/host-acceptance.ps1`（同构建门槛 + 隔离空用户目录 + 硬件级按键），在 WinForms TSF 窗口与**记事本**各跑一遍：`zlby`→增量编译、`lxpy`→离线拼音、`hxck`→候选窗口、`shide`/`sd`→是的、Shift+`;`→`：`、Shift+`=`→`+`，`%TEMP%\autoime.log` 无新增 `shift_click_toggle`；19 检查/0 失败（`plan-artifacts/plan-073/t04/host-acceptance-run4.log`）。VS Code 在本机启动即退出，未能建立窗口，记 NOT_RUN 并保留脚本供后续复验。观察：记事本对连续两次独立标点提交的应用顺序不稳定（`Shift+;` 后紧接 `Shift+=` 出现 `+：`，2.5 s 间隔仍现；TSF 窗口为 `：+`），故验收按单符号断言，连续顺序不入本包规范。

- [x] T-05 最终复审

  owner_stage=review，依赖 T-04。同一实施树审核召回原因、测试隔离、数据保留和 SD-01，完成 AC-05。

- [x] T-06 合入与归档

  owner_stage=merge，依赖 T-05。交付规范与代码、更新交付记录，按仓库规约清理工作树。AC-06。

## 9. 复审记录

2026-11-23 静态规划，当前全部执行项未运行。已核 071 与输入方案/词典/probe，未确认 072 交付。Git Bash 取号命令无输出退出 1，独占分配锁下核对 073/074 无重复并登记，下一号推进至 075。

stage: new | plan_id: PLAN-073 | plan_revision: 1 | outcome: pass | next: work/T-01 | task_ids: T-01..T-06 | acceptance_ids: AC-01..AC-06

### 2026-11-25 r2：用户反例诊断与修订

源码证据：主检出上述 HEAD，`textservice.cpp:326` 读取 Shift，`key_router.cpp:93–94` 无条件返回 `;`、`=`；`KeysymForVk` 的数字分支也未处理 Shift。未修改产品源码，未安装新 DLL。

真实引擎诊断：Python 3.14.2 ctypes 按仓内 `windows/probe/rime_api.h` 的版本化 API 表调用已安装 `C:/Program Files/AutoIME/rime.dll`，以 `data` 为共享资源，复制安装目录 `resources` 中 YAML/TXT 到新建独立临时目录；固定 `autoime_pinyin`、简体、中文模式，部署后逐字符 `process_key`，用 `candidate_list_begin/next/end` 最多扫描 200 项。每例前 clear composition，不选词、不提交、不学习；关闭引擎后清理临时目录，未读取或改写日常用户词库。

| 输入 | 「是的」名次（从 1 起） | 引擎读取耗时 | 前五候选 |
|---|---:|---:|---|
| shide | 7 | 0.0028 s | 使得、时的、诗的、实德、似地 |
| shid | 31 | 0.0020 s | 时代、使得、时的、是对、适当 |
| shde | 40 | 0.0029 s | 上的、说的、使得、时的、啥的 |
| shd | 157 | 0.0023 s | 上的、时代、说的、设定、使得 |
| sd | 199 | 0.0037 s | 上的、时代、说的、速度、设定 |

控制：`nihao` 第 1「你好」，0.0015 s；`nh` 第 2「你好」，0.0027 s。所有「是的」输入均实际召回，根因调查应落在候选来源/排序，不能记录为简拼功能完全缺失。以上为空用户库基线，用户日常学习后的排名可能不同。

直接引擎符号诊断（mask=1，即 Shift）：`;` 提交 `；`，`:` 提交 `：`，`+` 提交 `+`，`?` 提交 `？`，`!` 提交 `！`；`=` 未产生 commit，尚不能用这条 API 观测断言宿主最终文本。这证明 Rime 接收正确符号后能输出目标标点，但不替代生产 KeyRouter 修复后的端到端断言。

实测范围：7 个候选诊断 + 6 个符号调用，进程 exit 0；资源准备/部署 2.551 s，总墙钟 2.890 s（含临时目录清理）。符号单例和清理阶段未单独计时。未编译产品、未运行原 fixture/完整 probe、未手动测宿主；永久新增回归及修复后通过证据均为 NOT_RUN。

绑定的安装文件 SHA-256：

- `rime.dll`: `2D8F1BC3737635A11D9FB1BFCA4DC9E70533633930A8A0142A81CA879C39C45B`
- `resources/autoime_pinyin.schema.yaml`: `7012834E307DE0634F39974C235B34DBD205EC219747AD42FF35CADAE2A5CB1A`
- `data/luna_pinyin.dict.yaml`（version 2024.02.10）: `DC15421F70E80B5D9DC0339DB79856B671A58F8E6779C1B5B7592B83A57B0817`
- `data/pinyin.yaml`: `F2D291166946A848FD88E541844567415F1ED3D9C5A7477757B88AEB3A69C0CA`

环境阻塞：本轮最初尝试为独立修复取新号，`bash scripts/new-plan.sh autoime-shift-and-common-phrases` 无输出 exit 1，`.next-id` 仍为 076，未创建新计划。随后 `git worktree add D:/autostack/.wt/os-076/auto-os -b plan-076-dev` 因无法创建 `.git/refs/heads/plan-076-dev.lock` 返回 Permission denied，未建立分支/工作树。经范围核对改为修订已有 073，不占用 076；同一仓 Git 写权限不足仍阻止创建 073 实施树。未绕过工作树规约在 main 或 072 树修改产品。

stage: new | plan_id: PLAN-073 | plan_revision: 2 | outcome: blocked | next: 恢复 Git 元数据及独立工作树目录写权限后 work/T-07、T-01 | changed: T-01/02/04/07, AC-02/07, V-01/06/07, SD-02 | execution: 尚未实施

文档检查：七个唯一 T-ID 与 total_steps=7 一致，r2/新增 AC/SD/阻塞字段存在；下一号仍为 076，无新增 076 文档，产品源码无本轮 diff。检查 exit 0，0.265 s。计划文件仍为主检出未跟踪文档，受 Git 写权限限制未提交。

### 2026-11-25 r3：分段造词独立计划

用户确认将上轮讨论落成计划。新增 PLAN-076 承接显式分段交互与整词学习事务，本包既有 T-01–07、AC-01–07 和 V-01–07 不删减，状态仍为 drafting。076 不成为本包依赖；建议先交付本包 Shift 修复，以便 076 的可选竖线快捷键集成。072 在本轮读取时已更新为 execution_done（5/7），仍不将其等同最终复审/归档。

stage: new | plan_id: PLAN-073 | plan_revision: 3 | outcome: pass | next: work（既有环境条件满足后） | change: 明确与 PLAN-076 的范围和共享文件交付边界 | implementation/tests: 本轮 NOT_RUN

### 2026-11-25 work：T-07 / T-01 / T-02 执行（round 1）

工作树 `D:/autostack/.wt/os-073/auto-os`（分支 `plan-073-dev`，基线 `2beec44`）已建立；r2 记录的 Git 写权限阻塞解除。实施树核验：`git rev-parse --show-toplevel` = 工作树，分支 `plan-073-dev`，主检出无实现 WIP（仅 docs 计划/研究文件）。

本阶段提交：`82a9677`（T-07 Shift 符号）、`318c612`（T-02 是的）、`2c64571`（T-01/T-02 术语简拼与质量语料）。

测试与证据（本会话实际执行，debug/dev 路径，预构建产物）：

| 范围 | 结果 | 准备 | 执行 | 清理 | 总墙钟 |
|---|---|---|---|---|---|
| `probe\build\rime_probe.exe --cases shared/tests/pinyin-quality.json`（修复后） | 61/61 通过；内建 t1–t10 全过 | 部署 3.9s（每次运行内） | 约 4.6s | 0（专用目录保留用于 hash 对照） | 8.5s |
| 同语料修复前（原 0.1.0 资源 + 同 probe） | 50/61，11 强制失败 | 部署 3.1s | 约 4.2s | 0 | 7.3s |
| `probe --cases shared/tests/cases.json`（071 冻结） | 13/13（首选 11→12） | 部署 3.0s | 1.8s | 0 | 4.8s |
| `ime\test.cmd`（= `autoime_fixture.exe`） | 20 用例/214 检查/0 失败；`shift_symbols_real_engine` 3.23s（引擎部署在用例内） | 编译 27 目标（预构建阶段，未计入测试） | 3.23s | 清理专用用户目录 | 3.3s |
| librime 源码核对 | `WorkspaceUpdate`/`SchemaUpdate`/`BuildPrism` 读毕（1.13.1） | 代理拉取 6 个源文件 | — | 0 | 约 30s |

新增用例逐例耗时（probe 单例，均 ≤ 305 ms）：`must-*`/`obs-*` 最长 304.5ms，`ctl-*` 最长 73.5ms；`shift_symbols_real_engine` 3.23s。均远低于 10s 上限。

未执行/待续：T-03（组句、部分提交与学习持久化）、T-04（升级/回退、集中宿主、性能与安装包）。

### 2026-11-25 work：T-03 完成、T-04 部分完成（V-06 环境阻塞）

T-03 提交 `f50ff5e`：probe 内建 t1–t12。`t11_partial_commit` 45 ms（完整短句提交一次、部分提交后继续输入、Esc 取消）；`t12_learning_persist` 3.2–5.5 s（独立学习目录、两次重开、名次 2→1、userdb 生成）。

T-04（V-05、安装包、性能）：

| 范围 | 结果 | 准备/执行/清理/总墙钟 |
|---|---|---|
| `ime\tests\run-t04.ps1` | 32/32 检查通过（A 冷启、B 幂等、C 缺引擎、D 朙月回退、E 坏升级回滚、F 短语/学习保留、G 并发串行、H 延迟） | 预构建已就绪；执行 28.1 s；清理在用例内；总 28.1 s |
| `autoime_deploy --latency 20` | deploy 3995 ms；中位数 313 µs、P95 327 µs、max 327 µs；version `r5e9dee442fb52287` | 部署在进程内；总 4.0 s |
| `installer\build.cmd`（Inno Setup 6.7.4 用户安装） | 成功；`dist\AutoIME-Setup-x64.exe` 9,317,931 字节 SHA-256 `AAEC0E0B91F343507BF509E238BDD60D1858335CA4EC115DA6448FCC8E7DD021`（`24a5974` closeout 重建）；stage\resources 含新增 `autoime_words.*` | stage+编译 6.3 s |
| V-06 记事本/Edge 实打 | **NOT_RUN（阻塞）** | — |

阻塞（V-06 / AC-04/AC-07）：会话为过滤令牌（Administrators 仅 deny-only），UAC=5 交互提示；无法写 `C:\Program Files\AutoIME`、无法写 HKLM TSF 注册。已装构建 `autoime_ime.dll` SHA-256 `5C96…F849` ≠ 本构建 `B0B1…ED68`，不以旧构建冒充同构建验收。`run-t04` 运行时另一会话 cargo/rustc 编译在进行（已记录），套件与延迟门仍全过；纯净性能基线可在空闲机复跑。

测试范围（本轮，debug/dev）：probe 全量（61 质量用例 + 12 内建）约 15 s；fixture 3.3 s；run-t04 28.1 s；installer 5.6 s；未跑 release/全量套件。

T-07 补丁（2026-11-25，用户实机反馈，提交 `f379f58`）：真实宿主中 Test 阶段被消费的 Shift+符号未标记 `shift_used_`，日志出现 `shift_click_toggle`；`OnTestKeyDown` 现在置位，fixture 新增 `shift_test_only_no_toggle`（红相 3 失败→绿相通过，21 用例/220 检查）。用户提供的安装版实测（`？：！`、`——《》……`、`+` 半角、`~`/`～`、`@|「」`）作为旧构建参考基线，已记入 SD-02 与 T-04 的 V-06 复验步骤；`tmp\host-punct3.ps1` 存档于 `plan-artifacts/plan-073/t04/`。

stage: work | plan_id: PLAN-073 | plan_revision: 3 | outcome: blocked | worktree_path: D:/autostack/.wt/os-073/auto-os | branch: plan-073-dev | base_commit: 2beec44 | code_commit: f379f58 | dependency_revisions: Weasel 0.17.4 / librime 1.13.1（2D8F1BC3…C45B） | task_ids: T-07/T-01/T-02/T-03 完成；T-04 部分（V-05/安装包/性能通过，V-06 NOT_RUN） | evidence: plan-artifacts/plan-073/{t01,t03,t04,t07} | blockers: V-06 需管理员安装本构建后做宿主集中试打 | next_responsible_role: user（管理员安装）→ work(T-04) → review

stage: goal | plan_id: PLAN-073 | plan_revision: 3 | round: 2 | from: work | to: work(T-04/V-06) | status: executing | outcome: blocked | worktree: D:/autostack/.wt/os-073/auto-os | commit: f379f58 | next: user

### 2026-11-25 work 完成：V-06 宿主集中试打通过

用户以管理员安装定稿构建：`C:\Program Files\AutoIME\autoime_ime.dll` SHA-256 `736895A6…B78F`（= 工作树构建），资源含 `autoime_words.*`、`autoime_pinyin` version 0.2.0。新增 `ime\tests\host-acceptance.ps1`，同构建门槛 + 隔离空用户目录（`AUTOIME_RIME_USER_DATA_DIR`）+ 硬件级按键，驱动两类真实宿主：

| 宿主 | 结果 |
|---|---|
| WinForms TSF 窗口 | `zlby`→增量编译、`lxpy`→离线拼音、`hxck`→候选窗口、`shide`/`sd`→是的；Shift+`;`→`：`、Shift+`=`→`+`、Shift+`/`→`？`、Shift+`1`→`！`；无新增 `shift_click_toggle` |
| 记事本（真实窗口） | 同一组简拼与「是的」全部上屏；Shift+`;`→`：`、Shift+`=`→`+` 单符号断言通过；`%TEMP%\autoime.log` 无新增 `shift_click_toggle` |

结果 19 检查 / 0 失败，32.9 s（含两次进程内引擎部署与就绪等待，重复运行一致）。VS Code 在本机启动即退出、无窗口，记 NOT_RUN（脚本保留供后续复验）。观察：记事本对连续两次独立标点提交的应用顺序不稳定（`Shift+;` 后紧接 `Shift+=` 得到 `+：`，2.5 s 间隔仍现；TSF 窗口为 `：+`），故验收按单符号断言，连续顺序作为观察项登记。

测试范围（本批）：`host-acceptance.ps1` 32.9 s / 19 检查；复用未受影响的证据：probe 61/61、fixture 21 用例/220 检查、`run-t04` 32/32、installer 6.3 s（`f379f58` 已含 shift_used_ 修复，部署路径未变）。

stage: work | plan_id: PLAN-073 | plan_revision: 3 | outcome: pass | worktree_path: D:/autostack/.wt/os-073/auto-os | branch: plan-073-dev | base_commit: 2beec44 | code_commit: 97444bd | dependency_revisions: Weasel 0.17.4 / librime 1.13.1（2D8F1BC3…C45B） | task_ids: T-01/T-02/T-03/T-04/T-07 全部完成 | acceptance: AC-01/02/03/04/07 有当前证据；AC-05/06 属 review/merge | evidence: plan-artifacts/plan-073/{t01,t03,t04,t07} | blockers: 无（V-06 已解除；观察项见下） | next_responsible_role: review

stage: goal | plan_id: PLAN-073 | plan_revision: 3 | round: 4 | from: work | to: review | status: execution_done | outcome: pass | worktree: D:/autostack/.wt/os-073/auto-os | commit: 97444bd | next: review

### 2026-11-25 review：T-05 最终复审（needs_fix）

复审基线：`plan_revision: 3`；工作树 `D:/autostack/.wt/os-073/auto-os`，分支 `plan-073-dev`，审查提交 `97444bd`（工作树干净），diff base `2beec44`；依赖 Weasel 0.17.4 / librime 1.13.1（`2D8F1BC3…C45B`）。Spec 输入：`docs/specs/autoime/pinyin-quality.md` sha256 `9E4B8C73…`、`docs/specs/autoime/phrase-management.md` sha256 `13F80171…`。审查者身份：实现会话内复审（工件重建 + 重跑受影响检查；非独立上下文，此限制记录在案）。

AC 证据映射：

| AC | 结果 | 方法 / 证据 |
|---|---|---|
| AC-01 | pass | 根因（librime 1.13.1 部署器源码 + prism/table 产物对照）与冻结输入/版本见 §9 与 `plan-artifacts/plan-073/t01/`；审查复核源码路径与产物哈希口径 |
| AC-02 | pass | 重跑质量语料 **61/61**（14.1 s，逐例 max 151 ms）；071 冻结 **13/13**（24.2 s，首选 12）；`must-*`/`ctl-*` 断言与观察集分离 |
| AC-03 | pass | 重跑内建 `t11_partial_commit` 147 ms、`t12_learning_persist` 5399.8 ms；提交一次/继续输入/取消与学习名次 2→1 均通过 |
| AC-04 | pass（复用） | `run-t04` 32/32、安装包 `9,317,011` 字节 `C084638E…63AC`、V-06 19/19——部署与宿主脚本在审查提交未变，安装构建 `736895A6…B78F` 与审查构建同哈希，故复用绑定证据 |
| AC-07 | pass | 重跑 fixture **21 用例/220 检查/0 失败**（5.6 s；real-engine 5.2 s）；Shift 符号、Test-only 误切换与路由断言通过 |
| AC-05 | needs_fix | 本记录：SD-01/SD-02 文本与当前行为/数字不一致（REV-073-01..05） |

发现（均为 in-contract 文档准确性，产品代码/测试未发现缺陷）：

- **REV-073-01**（SD-01，低）：`docs/specs/autoime/pinyin-quality.md:152` 写“20 用例/214 检查…约 3.2 s”；实际 fixture 为 **21 用例/220 检查**（审查重跑 5.6 s，real-engine 3.7–5.2 s 随负载）。修正为当前数字与区间。
- **REV-073-02**（SD-01，低）：同文件第 13 行把竞争资源写成 `autoime_terms`/`autoime_en`，漏掉新增 `autoime_words`；应补上（「是的」路径依赖它）。
- **REV-073-03**（SD-01，低）：同文件 72–73 行断言 Shift+`{ }` 出 `「」`、Shift+`<`/`>` 出 `《》`、Shift+`\` 出 `|`；审查 fixture 的 real-engine 记录显示这些 keysym 被消费但**不立即提交**（`ENGINE shift_bracket/… commit=` 为空，进入 symbols 组合）。应改为“进入符号组合、需再选”，或只保留已断言的 `; = / ' - 1 9 0` 即时上屏结论。
- **REV-073-04**（SD-01，低）：同文件 175–176 行仍写“未覆盖边界…宿主集中试打（记事本/Edge）…在 PLAN-073 执行记录中登记”，与上文 V-06 PASS 矛盾，且把记事本写成 Edge；应改为“记事本与 TSF 窗口已通过；Edge/VS Code NOT_RUN”。
- **REV-073-05**（SD-02/资源说明，低）：`experimental/autoime/shared/resources/README.md:36–41` 仍写“质量语料 52 例修复前 46 过”；最终为 **61 例 / 修复前 50 过 / 修复后 61 过**（36 个术语观察仍全部第 1 位）。修正数字。
  最小复验：`Select-String` 上述文本 + 对照 `plan-artifacts/plan-073/{review,t01}/` 日志与 `quality-yes.log`；无需产品测试。

测试耗时（本轮实际执行，debug/dev 预构建产物）：`cases.json` 24.2 s（13 例+部署，逐例 max 119 ms）；`pinyin-quality.json` 14.1 s（61 例+部署，逐例 max 151 ms）；fixture 5.6 s（21 用例/220 检查，real-engine 5.2 s）；`t12` 5.40 s ≤10 s。占用检查：运行时有另一会话 cargo/rustc 测试/编译（lang-1136），本轮只运行既有产物短检查，未启动准备构建或统一回归；`run-t04`/`host-acceptance` 复用绑定证据。主检出存在他人未提交 docs 修改（非 073 代码路径），不影响本审查。

stage: review | review_scope: final | reviewer_identity: 实现会话内复审（工件重建；非独立上下文） | plan_id: PLAN-073 | plan_revision: 3 | outcome: needs_fix | worktree_path: D:/autostack/.wt/os-073/auto-os | branch: plan-073-dev | reviewed_commit: 97444bd | base_commit: 2beec44 | dependency_revisions: Weasel 0.17.4 / librime 1.13.1（2D8F1BC3…C45B） | spec_inputs: pinyin-quality 9E4B8C73… / phrase-management 13F80171… | acceptance_results: AC-01/02/03/04/07 pass; AC-05 needs_fix | pending_closeout_gates: 修复后复审；统一回归按仓库流程在冻结基线执行 | findings: REV-073-01..05（文档准确性） | evidence: plan-artifacts/plan-073/review/{cases,quality,fixture}.log | next: work（T-02/T-04 文档修正）


stage: goal | plan_id: PLAN-073 | plan_revision: 3 | round: 5 | from: review | to: work | status: executing | outcome: needs_fix | worktree: D:/autostack/.wt/os-073/auto-os | commit: 97444bd | next: work

### 2026-11-25 work 修复复审发现（REV-073-01..05）

提交 `ca3acbc`（仅文档，`git diff --stat` 2 文件 20+/17-；产品代码与测试未变，故不重跑产品检查）：

- REV-073-01 → SD-01 改为 `21 用例/220 检查`、real-engine `3.7–5.2 s`（随负载）。
- REV-073-02 → 自然召回资源列表补 `autoime_words`（「是的」路径）。
- REV-073-03 → Shift 段区分「本构建即时上屏」（`;`→`；`、`Shift+;`→`：`、`Shift+=`→`+`、`Shift+/`→`？`、`Shift+'`→`“`、`Shift+-`→`——`、`Shift+1`→`！`、`Shift+9`/`0`→`（）`）与「上游 `symbols` 组合、需再选」（`` ` ``、`\`、`{`、`}`、`<`、`>`、`@`、`=`），旧构建观察降为参考基线。
- REV-073-04 → 宿主边界改为「记事本 + WinForms TSF 已通过，Edge/VS Code NOT_RUN」，Shift 释放保护段同步。
- REV-073-05 → resources README 改为 `61 例 / 修复前 50 过 / 修复后 61 过`。

校核（文本级）：`Select-String` 无残留 `20 用例/214 检查`、`52 例/46 过`、`（记事本/Edge）…待登记`；SD-01 新 sha256 `71983ED2…`、resources README 新 sha256 `72361E40…`；最小复验命令与命中行号记录于复审 finding。工作树干净。

stage: work | plan_id: PLAN-073 | plan_revision: 3 | outcome: pass | worktree_path: D:/autostack/.wt/os-073/auto-os | branch: plan-073-dev | base_commit: 2beec44 | code_commit: ca3acbc | task_ids: T-02/T-04 复审修复完成；T-01/T-03/T-07 保持完成 | acceptance: AC-01..04/07 证据沿用（产品未变）；AC-05 待复审复核 | evidence: plan-artifacts/plan-073/review/ + 上述文本校核 | blockers: 无 | next_responsible_role: review

stage: goal | plan_id: PLAN-073 | plan_revision: 3 | round: 6 | from: work | to: review | status: execution_done | outcome: pass | worktree: D:/autostack/.wt/os-073/auto-os | commit: ca3acbc | next: review

### 2026-11-25 review 复核（T-05，pass）

基线：`reviewed_commit: ca3acbc`（相对上一审查 `97444bd` 仅 2 个文档，20+/17-）；工作树干净；`plan_revision: 3`；Spec 输入更新为 `pinyin-quality.md` sha256 `71983ED2…`、`resources/README.md` sha256 `72361E40…`。reviewer_identity：实现会话内复审（修复复核；非独立上下文，记录限制）。

Finding 逐项复现（文本级，最小复验）：

| Finding | 状态 | 复核证据 |
|---|---|---|
| REV-073-01 | closed | 第 154/156 行：`21 用例/220 检查`、`3.7–5.2 s`；全仓无 `20 用例/214 检查` 残留 |
| REV-073-02 | closed | 第 13 行资源列表含 `autoime_words` |
| REV-073-03 | closed | 第 71–78 行区分“本构建即时上屏”（`;`→`；`、`Shift+;`→`：`、`Shift+=`→`+`、`Shift+/`→`？`、`Shift+'`→`“`、`Shift+-`→`——`、`Shift+1`→`！`、`Shift+9`/`0`→`（）`）与“上游 symbols 组合、需再选”（`` ` ``、`\`、`{`、`}`、`<`、`>`、`@`、`=`）；与 `plan-artifacts/plan-073/review/fixture.log` 的 `ENGINE … commit=` 记录一致；旧构建观察仅作基线 |
| REV-073-04 | closed | 第 177–178 行边界为“记事本 + WinForms TSF 已通过，Edge/VS Code NOT_RUN”；无旧“待登记”句 |
| REV-073-05 | closed | `resources/README.md` 第 38–40 行：61 例 / 修复前 50 过 / 修复后 61 过，36 观察第 1 位 |

无产品代码/测试变化（diff 仅文档），受影响 AC 沿用已审查证据；未发现新缺陷。非阻断可读性观察：第 68 行符号列表未标注“未移位形态”，不影响规则语义，可在后续编辑顺手澄清。

AC-05 → pass；AC-01/02/03/04/07 维持 pass。归 merge（T-06）执行：① 因 `shared/resources/README.md` 会被 installer staging 打包，需在 `ca3acbc` **重建安装包**并更新包哈希记录（现有 `9,317,011 B / C084638E…` 系 `f379f58` 构建）；② 计划文档提交、归档、工作树清理与 landing checks。

stage: review | review_scope: final | reviewer_identity: 实现会话内复审（修复复核；非独立上下文） | plan_id: PLAN-073 | plan_revision: 3 | outcome: pass | worktree_path: D:/autostack/.wt/os-073/auto-os | branch: plan-073-dev | reviewed_commit: ca3acbc | base_commit: 2beec44 | dependency_revisions: Weasel 0.17.4 / librime 1.13.1（2D8F1BC3…C45B） | spec_inputs: pinyin-quality 71983ED2… / phrase-management 13F80171… / resources README 72361E40… | acceptance_results: AC-01/02/03/04/05/07 pass | pending_closeout_gates: 安装包在 ca3acbc 重建并更新哈希；归档/清理/落地检查 | findings: REV-073-01..05 closed | evidence: 上述 grep 行号 + plan-artifacts/plan-073/review/ | next: merge

stage: goal | plan_id: PLAN-073 | plan_revision: 3 | round: 7 | from: review | to: merge | status: reviewed | outcome: pass | worktree: D:/autostack/.wt/os-073/auto-os | commit: ca3acbc | next: merge

### 2026-11-25 merge：T-06 合入与归档（consolidation receipt）

| checkpoint | evidence |
|---|---|
| prepared | reviewed r3 / final pass @ `ca3acbc`；delta SD-01 add pinyin-quality、SD-02 add（r3 常用词/Shift 规则）、SD-03 modify phrase-management；delivery commit `24a5974`（reviewed 后的仅文档后裔：只改安装包身份段落，实现/依赖未变）；reviewed spec sha `71983ED2…` → delivery spec sha `C5A0AFBE…` |
| landed | `git merge --ff-only plan-073-dev`：`2beec44 → 24a5974`，main tip == delivery commit，无 merge commit，无 rebase/哈希改写（分支本就基于 main tip） |
| canonical_verified | main 上 `docs/specs/autoime/pinyin-quality.md`（`C5A0AFBE…`）与 `docs/specs/autoime/phrase-management.md`（`13F80171…`）与交付树逐字节一致；`shared/resources/README.md` `72361E40…`；closeout 安装包 `9,317,931 B / AAEC0E0B…`（ca3acbc 资源 + 修正 README，Inno 6.4 s 重建） |
| archived | 本文件移至 `docs/plans/archived/073-autoime-pinyin-quality.md`，`status: archived`，T-01..T-07 全勾选（7/7）；相对链接已按新父目录重写（`../076-…`、`071-…`、`../../design/05-…`）；随 `f965f82` 落地 |
| cleaned | 待清理回执（worktree/branch/plan-artifacts/registry） |

merge 阶段测试：仅安装包重建 6.4 s（packaging；为避免与他人 cargo 测试/编译竞争，先等待约 3 分钟后执行，未启动编译器或测试套件）；复用复审/交付证据：probe 61/61、071 13/13、fixture 21/220、host 19/19、run-t04 32/32；无新增用例。

### 2026-11-25 merge 收尾：cleaned

| checkpoint | evidence |
|---|---|
| cleaned | ① 移除前 fresh `wt-guard.ps1`：clean（0 reparse point）；② worktree `D:/autostack/.wt/os-073/auto-os`（HEAD `24a5974`，已落地 main）移除成功；③ 分支 `plan-073-dev` 删除（was `24a5974`）；④ 组目录 `D:/autostack/.wt/os-073` 空目录删除；⑤ `plan-artifacts/plan-073` 整目录删除：**2,353 files / 971,402,334 bytes**（probe 用户目录、librime 源码副本、安装包 stage/dist、日志；证据已折叠入本计划）；⑥ registry `.retention/policy.json` 刷新为 deleted-at-merge、无 holds |

landing checks：main tip `24a5974`（其后仅本计划 bookkeeping commits）；`docs/specs/autoime/pinyin-quality.md` 与 `phrase-management.md` 在 main 且与交付树一致；主检出其余未提交 docs 属其他会话 WIP，未触碰。build intermediates 随工作树删除，不回收。

deployment observation（landing ≠ deployment）：本机已安装 AutoIME `autoime_ime.dll` `736895A6…` 与本交付代码一致（`24a5974` 为 doc-only 后裔，未改 DLL）；已装 `%ProgramFiles%\AutoIME\resources\README.md` 仍为修正前文本，closeout 包 `9,317,931 B / AAEC0E0B…` 已修正但未重装——只影响随包说明文本，不影响输入行为；如需刷新宿主资源，管理员重装 closeout 包即可，不属本次交付门。

无 artifact holds。保留观察项：记事本连续两次独立标点提交顺序不稳定；Edge/VS Code 宿主未复验（见 §9/§10）。

### goal 完成回执（2026-11-25）

- Plan：PLAN-073，plan_revision 3，status `archived`。
- 交付提交：`24a5974`（ff-only 于 `2beec44`；main tip 后仅本计划 bookkeeping）。
- 规范：`docs/specs/autoime/pinyin-quality.md`（新增，sha16 `C5A0AFBE`）、`docs/specs/autoime/phrase-management.md`（修改）。
- 归档：`docs/plans/archived/073-autoime-pinyin-quality.md`；bookkeeping commits `f965f82`（archive）、`a28d613`（merge receipt）、`d54d587`（cleanup receipt）。
- 清理：wt-guard clean 后 worktree/branch/组目录移除；`plan-artifacts/plan-073` 整目录删除（2,353 files / 971,402,334 bytes）；registry 刷新，无 holds。
- 交付物身份：安装包 `AutoIME-Setup-x64.exe` 9,317,931 B / sha256 `AAEC0E0B…D021`；本机已装构建 `736895A6…` 与交付代码一致。
- 剩余 merge-owned gate：无；观察项（记事本连续标点顺序、Edge/VS Code 未复验）按非阻断记录。

## 10. 待澄清事项

T-01 负责关闭根因不确定性，无需等待 072。若 072 同期改部署工具或安装脚本，执行者先核其交付身份，并串行合并共享文件。更换通用词库或加入语言模型属于后续产品决策。

当前无需用户补充输入用例或重新授权修复；实施阻塞来自会话对 Git 元数据及工作树目录的写权限。下一执行环境须可创建/写入本仓 `.git` 和 `D:/autostack/.wt/os-073/auto-os`，然后按原规约提交计划登记并创建该实施树。不得把本次诊断或文档修订当作修复交付。

**2026-11-25 更新（V-06 已解除）**：用户以管理员安装定稿构建（`736895A6…B78F`），`ime\tests\host-acceptance.ps1` 在 WinForms TSF 窗口与记事本上 19/19 通过；原管理员阻塞关闭。保留观察项：① 记事本连续两次独立标点提交顺序不稳定（单符号验收已通过）；② VS Code 在本机无窗口，复验 NOT_RUN，可在其可用时复用同一脚本。
