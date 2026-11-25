# AutoInput 拼音召回与候选质量（PLAN-073）

AutoInput 默认方案 `autoime_pinyin` 的自然拼音召回口径：项目术语在全拼、首字母
简拼与全简混输下进入候选的判据，固定初态与用户学习的独立证据，以及随包词库
/方案改动的部署约束。个人显式短语的数据与事务合同见
[phrase-management.md](phrase-management.md)；TSF 宿主行为见
[Design 05](../../design/05-auto-input-windows.md)。整句语言模型、第三方大词库
替换与 Linux 前端不属于本规范。

## 自然召回与个人短码的区分

- **自然拼音召回**：用户只按普通拼音输入，候选由共享词库（朙月）与随包资源
  （`autoime_terms`/`autoime_words`/`autoime_en`）在 librime 内竞争产生；不依赖
  用户个人数据，空用户目录即可复现。
- **个人显式短语**：用户通过 `custom_phrase.txt`（`autoime-config`）登记的
  自定义 `code` → `text`，属于个人数据。个人短语可以覆盖任意短码，但不能用来
  证明自然召回，也不作为本规范的质量证据。
- 判定质量时必须使用**全新空用户目录**（无 `custom_phrase.txt`、无学习库、
  无历史候选）。个人短语或学习数据参与时只属于学习/个人路径的证据。

## 术语与目标

随包原创术语 12 条（`shared/resources/autoime_terms.dict.yaml`）。对本规范，
每条术语的三种输入形态都必须可达：

| 形态 | 定义 | 例 |
|---|---|---|
| 全拼 | 逐字完整拼音，无分隔 | `zengliangbianyi` |
| 首字母简拼 | 逐字声母/首字母 | `zlby` |
| 全简混输 | 首字全拼 + 其余首字母（或等价自然混输） | `zengliangby` |

强制目标（§1）：`zlby`→增量编译、`lxpy`→离线拼音、`hxck`→候选窗口，及其
全拼、混输形态，共 9 条必须在修复后进入前五。其余术语的三种形态构成观察集，
逐例记录名次；未通过项按实际结果登记缺口，不以观察集宣称完整覆盖。

## 常用词质量（是的）

「是的」属于普通自然词，不是项目术语：它必须与朙月/预设词频的候选在同一
自然拼音路径上竞争。空用户目录下的强制目标是 `shide`、`shid`、`shde`、
`shd`、`sd` 五种输入的前五均包含「是的」。

- 现状来源：小狼毫 `data/essay.txt` 给「是的」的权重是 776，明显低于
  「使得」28566、「速度」35075、「上的」83517；词被召回但 `shide` 第 7、
  `sd` 第 199，前五不可用。**不能**用显式短码（`sd → 是的`）或前端硬插候选。
- 修复口径：`shared/resources/autoime_words.dict.yaml` 作为共享自然拼音词库
  补录，标准读音 `shi de`、权重 40000（与高频双音节词同带），由
  `script_translator@autoime_words` 参与正常竞争；依赖方案
  `autoime_words.schema.yaml` 带与主方案一致的拼音代数。
- 保留同码合法词：`shide` 的「使得」、`sd` 的「速度」「时代」「设定」等仍在
  同一候选表；规范只锁「是的」前五与「使得」在 `shide` 前五，其余按
  `ctl-*` 观察名次，不锁死相对次序。
- 统计口径：`shide`/`sd` 等的名次可随预设词频与学习变化；质量断言只使用空
  用户目录 + 固定资源版本。

## Shift 符号与按键所有权

- keysym 必须是用户实际看到的字符：`KeysymForVk(vk, shift)` 对字母保留大小写，
  对数字行与 OEM 标点返回移位后的符号（`;`→`:`, `=`→`+`, `/`→`?`,
  `` ` ``→`~`, `[`→`{`, `\`→`|`, `]`→`}`, `'`→`"`, `,`→`<`, `.`→`>`,
  `-`→`_`, `1`→`!`, `9`→`(`, `0`→`)`）；librime 的标点映射按字符而非
  Shift 位，传未移位符号会得到另一个标点（`;` → `；`）。
- 中文空闲：Shift+标点与 Shift+数字走标点路径；**未移位数字仍归宿主**（不
  拦截数字选词入口）。中文组合中：标点/数字仍由组合消费，Shift 后 keysym 保
  持移位字符。英文模式与 Shift 单击切换、Ctrl/Alt/Win/AltGr 快捷键不受影响。
- 真实引擎断言（空用户目录、librime 1.13.1）：Shift+`;` 提交 `：`、
  Shift+`=` 提交 `+`、Shift+`/` 提交 `？`、Shift+`'` 提交 `“`、
  Shift+`-` 提交 `——`、Shift+`1` 提交 `！`、Shift+`9`/`0` 提交 `（`/`）`。
  上游对部分符号（`/`, `~`, `{`, `|`, `}`, `<`, `>`, `@`, `=`）先开符号组合、
  不立即提交，此时只断言路由消费，不把上游行为计作本规范缺陷。
- 半角/全角取舍（用户实测裁定）：半角中文沿用小狼毫 `half_shape` 预设，不做
  自定义 `punctuator` 覆盖。本构建已断言的即时上屏：`;`→`；`、`Shift+;`→`：`、
  `Shift+=`→`+`、`Shift+/`→`？`、`Shift+'`→`“`、`Shift+-`→`——`、`Shift+1`→`！`、
  `Shift+9`/`0`→`（`/`）`；`+` 保持半角（全角 `＋` 只在全角模式）。Shift+`` ` ``、
  `\`、`{`、`}`、`<`、`>`、`@`、`=` 由上游 `symbols` 预设先开符号组合、需再选，
  本构建只断言其被路由消费。用户对安装版 0.2.0 的旧构建观察（`~` 首选、`～` 次选、
  `|`、`「」`、`《》`、`……`）仅作参考基线，不作为本构建断言。
  让 `Shift+Space`/`Ctrl+Shift+3` 真正可达全角属于独立输入法功能，不在本规范；
  需要中文习惯全角形态时应另立计划改标点映射。
- Shift 释放保护（用户实测发现）：部分宿主在 Test 阶段已消费按键、不再发送
  KeyDown；此时必须把 Shift 标记为已用于组合，否则 Shift+符号被误判为“单击
  Shift 切换中英”。`KeyRouter::OnTestKeyDown` 在 `shift_down_` 时置
  `shift_used_`，fixture `shift_test_only_no_toggle` 覆盖（Test 消费后
  Shift 抬起不切换；纯单击仍切换）；已在记事本与 WinForms TSF 窗口验证无
  `shift_click_toggle`，VS Code 未复验。

## 英文别名准入（PLAN-078 T-01/T-05）

英文/技术别名表 `autoime_en` 只接受**完整码**（`enable_completion: false`）：

- 单字母与短前缀不召回别名。空用户词库下 `s` 的首选是「是」，SQLite 在整表
  缺席；`sq`、`sql`、`Sql` 同样不召回。完整小写码继续有效：`sqlite`、`ros`、
  `autoos`、`tsf`。
- 显式大小写触发码逐字符匹配：`SQ` 与 `SQLite` 命中 `SQLite`；`S`、`Sq`、
  `Sqlite`、`sqlITE` 不命中。触发码由词表作者（或 T-05 的个人术语覆盖层）
  显式声明，至少两个 ASCII 字母。
- speller 字母表包含大小写 A–Z，因此大写按键进入组合；普通中文拼音、Shift
  符号与英文模式按键不受影响（大写字母不参与拼音音节匹配）。
- 个人术语覆盖层（`autoime_user_terms`）先于内置别名被查询：同一触发码上
  个人条目排在内置条目前，可覆盖内置触发。

以上行为由 `shared/tests/en-admission.json`（20 例）与
`shared/tests/user-terms.json`（8 例，配合 `run-t05.ps1`）固定。

## 补充词的个人学习（PLAN-078 T-04）

`autoime_words`（如「是的」）与主拼音共用同一用户词库 `autoime_user`：

- 确认补充词写入与普通词相同的文字/读音条目域；全拼、简拼与混输读取同一份
  权重；重启后仍有效。
- 取消组合不写入；宿主正文写入失败时由共享引擎按同一编码删除刚学到的条目
  （编码为空时复用该会话最近一次组合/确认编码并翻页查找）
  （见 [共享引擎决策记录](../../design/07-auto-input-shared-engine.md)）。
- 空用户目录下补充词按随包权重参与竞争，排名与 PLAN-073 冻结结果一致；个人
  学习是独立证据，不用于固定初态的质量判定。

## 质量语料合同

- 位置：`shared/tests/pinyin-quality.json`，平台无关，
  Windows probe 与 Linux 回放消费同一份文件与同一份 `shared/resources`。
- 用例字段：`id`、`keys`（逐键回放）、`target`（被观察文本）、`scan`（扫描
  整张候选表）、`expect_top5`/`expect_first`（硬断言）、`expect_rank_all`
  （整表名次断言）。`scan: true` 时输出 `whole-list=N/total`，`N=0` 表示未召回；
  page-0 名次与 PLAN-071 语料语义一致。
- 固定前置：librime 1.13.1（`rime.dll` 1.13.1.0，SHA-256 `2D8F1BC3…C45B`）、
  朙月共享数据（`luna_pinyin.dict.yaml` SHA-256 `DC15421F…0817`）、
  `shared/resources` 版本 0.2.0、全新空用户目录、只读回放（不选词、不提交）。
- 观察集与强制集分开：`must-*` 是断言用例，`obs-*` 只记录名次，`ctl-*` 是
  普通中文同码/歧义控制（`nh`→你好/内核、`shuruf`→输入法、`ros`→ROS 2）。

## 部署约束（prism 代数）

librime 1.13.1 的部署器（源码核对：`lever/deployment_tasks.cc` 的
`WorkspaceUpdate`/`SchemaUpdate`、`dict/dict_compiler.cc` 的
`DictCompiler::BuildPrism`）：

- 主方案的 `SchemaUpdate` 只编译该方案自己的 `translator/dictionary`；依赖方案
  按 `schema/dependencies` 依次部署，用于编译各自词表。
- 词典的 `.prism.bin` 由**被部署方案**的 `speller/algebra` 派生生成，并记录
  `schema_file_checksum`；另一个方案再编译同一词表会因校验和不同重建 prism。
- 因此，任何“由依赖方案编译”的词表，其依赖方案必须携带与主方案一致的
  `speller/algebra`；否则该词表只有完整音节拼写，首字母缩写路径查不到条目
  （全拼按音节编号查询仍可用）。

`autoime_terms.schema.yaml` 0.2.0 起带 `abbreviation`/`spelling_correction`/
`key_correction`，与主方案一致。改动只影响 `.prism.bin`：同一词库下
`autoime_terms.table.bin`、`autoime_terms.reverse.bin` 字节不变。

## 候选排序

术语 translator 的 `initial_quality` 固定为 1.0（资源 0.2.0 起）。理由：术语
进入自然简拼竞争后，与朙月候选按同一路径竞争；0.2 会让 `gaosipoj` 的首选从
「高斯泼溅」掉到第 2（PLAN-071 冻结用例要求首选），而 1.0 让术语在自身精确
编码上领先，同时普通短码控制不变（`nh`→你好第 2、`shuruf`→输入法第 1）。
术语只有 12 条长编码，不改变无关键词的常规排序。

## 固定初态与用户学习的独立证据

- **固定初态**（本规范的质量基线）：空用户目录，`enable_user_dict` 的学习库
  不存在；候选名次可复现。每次质量回放使用专用隔离目录，probe 传入的目录按
  probe 规则清理，绝不指向真实用户目录。
- **用户学习**（独立证据，解释性而非质量基线）：使用单独的学习目录，重复选择
  一个低频合成词，记录学习库写入前后候选名次/词频变化；关闭并重开引擎（新
  实例）后结果保留。学习效果不得计入自然召回指标，也不得用“显式短码置顶”
  冒充学习。术语词表 `enable_user_dict: false`，学习只发生在 `autoime_user`
  主转换器上；若未来对术语启用学习，必须登记新的用户词典身份与升级保留规则。
- **组句与部分提交**：用冻结词库中可确定的短句，验证部分选词后继续输入、
  完整提交、取消四条路径的提交文本、残余预编辑与提交次数（见
  Design 05 与 probe 内建序列）。
- **实测（PLAN-073 T-03）**：probe 内建 `t11_partial_commit`——`nihaoshijie`
  首选提交「你好世界」且只提交一次；提交「你好」后继续输入 `shijie`，新组合
  的预编辑为 `shi jie`，按展示候选提交「世界」且无多余提交；同样路径下 Esc
  取消后预编辑清空、无提交。
- **学习实测（PLAN-073 T-03）**：`t12_learning_persist` 在独立目录
  `<user>-learn`（空学习库）选择「李显」3 次，正常关闭并重开引擎后同一输入
  的名次由 2 提升到 1，`autoime_user.userdb` 在独立目录生成；主质量目录全程
  未被学习数据污染。该测试含两次部署/重开，约 3.2–5.5 s。

## 验证与基线（PLAN-073）

- 质量语料 61 例（14 强制 + 36 观察 + 11 控制，含 1 条无命中负例）：修复前
  50 过，11 条强制用例失败（6 条术语简拼/混输未召回，5 条「是的」输入不在
  前五）；修复后 61 过，14 条强制全部前五，36 个观察输入全部第 1 位。
- PLAN-071 冻结语料 13 例保持全过；首选命中率 11→12（术语简拼改善，断言不变）。
- 原生夹具（`ime/build/autoime_fixture.exe`）21 用例/220 检查全过，其中
  `shift_symbols_real_engine` 用生产 `KeysymForVk` 驱动真实 librime 断言
  上屏字符（3.7–5.2 s，含部署，随机器负载）；其余 keysym/路由检查 <1 ms。
- 升级/回退与性能（`ime/tests/run-t04.ps1`，32 检查全过）：首次部署
  3.99 s（`autoime_deploy --latency 20`），就绪后按键到候选中位数 313 µs、
  P95 327 µs；损坏升级回滚到上一可用版本、用户短语与学习库在升级中保留、
  双宿主并发冷启均通过。当时的安装包 `AutoIME-Setup-x64.exe` 9,317,931 字节，
  SHA-256 `AAEC0E0B…D021`（交付提交 `ca3acbc` closeout 重建；功能资源与
  `f379f58` 构建相同，差异仅为随包 `resources/README.md` 文本），staged
  `resources` 含新增 `autoime_words.*`。
- 宿主集中试打（记事本/VS Code，V-06）：**PASS（2026-11-25，安装构建
  `736895A6…B78F`，与验证构建同哈希）**。`windows/ime/tests/host-acceptance.ps1`
  在隔离空用户目录（`AUTOIME_RIME_USER_DATA_DIR`）下驱动 WinForms TSF 窗口与
  记事本：`zlby`→增量编译、`lxpy`→离线拼音、`hxck`→候选窗口、`shide`/`sd`→
  是的；Shift+`;`→`：`、Shift+`=`→`+`；`%TEMP%\autoime.log` 无新增
  `shift_click_toggle`。19 检查/0 失败。VS Code 在本机启动即退出（本会话未能
  建立窗口），记事本与 TSF 窗口两类宿主已覆盖；VS Code 复验在当前环境记
  NOT_RUN。
- 观察（非本包缺陷归属未定）：记事本（WinUI）连续两次独立标点提交的应用顺序
  不稳定——`Shift+;` 后紧接 `Shift+=` 时文档出现 `+：`（逆序，2.5 s 间隔仍现），
  同序列在 WinForms TSF 窗口为 `：+`。因此宿主验收按单符号断言（每个符号单独
  读取并回正选区），连续符号顺序不纳入本规范；如需修复应另立 TSF 编辑顺序调查。
- 性能口径：prism 体积 1340→3792 字节，部署耗时同机可比；候选就绪后的响应
  以 probe 记录的中位数/P95 为准，不把引擎微秒级查询称为屏幕延迟。
- 未覆盖边界：整句语言模型、第三方大词库替换、Linux 前端；宿主方面记事本与
  WinForms TSF 窗口已通过（见上），Edge 与 VS Code 未复验（VS Code 在本机无窗口，
  当前环境记 NOT_RUN）。
