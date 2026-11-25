# AutoInput 资源与依赖登记（PLAN-070 T-03；PLAN-071 T-01 迁至 shared/）

本目录是 AutoIME 随版本发布的 Rime 资源（PLAN-071 起 Windows/Linux 共用源
数据，路径 `experimental/autoime/shared/resources/`）。部署时由安装/首启流程
复制到 AutoIME 私有用户目录（Windows：`%LOCALAPPDATA%\AutoIME\rime`），不修改
小狼毫的共享目录，也不覆盖用户已编辑的 `custom_phrase.txt`。

## 本仓原创资源

| 文件 | 作用 | 字节 | SHA-256 |
|---|---|---|---|
| `autoime_pinyin.schema.yaml` | 默认方案 `autoime_pinyin`（全拼/简拼/全简混输、术语、常用词修正、中英混输、简体输出） | 5841 | `4A62060376F6274C913E156EDE01E74F931129158B16ECB19ACC91A3C90D1037` |
| `autoime_terms.dict.yaml` | 项目术语词表（原创条目，逐字拼音码） | 775 | `BB08D34D11E31F87ECBB7D2E5FE235D576C72D01F1149289840A08AC506154DC` |
| `autoime_terms.schema.yaml` | 术语表部署入口（依赖方案，不在切换器展示；含拼音代数） | 1412 | `38CFA81B07D9D166D08C78E3357C38555037736654BBECF24B52337EA303D62E` |
| `autoime_words.dict.yaml` | 共享自然拼音常用词修正（如「是的」；标准读音+权重，非短码） | 1091 | `9CBC93749591567DF778B3DC9C2FFDB5179CD8AA9D51CD96662BED397C831C86` |
| `autoime_words.schema.yaml` | 常用词修正词表部署入口（依赖方案，含拼音代数） | 967 | `D39D0CD191664EF69E833ED6A1273761277BE85E12C47ACE89C6C1F8F983AE52` |
| `autoime_en.dict.yaml` | 中英混输别名表（字母码，提交保留大小写/空格；含显式大小写触发码） | 732 | `D63B5FF7690E28D86E7D010DFF1D4B39F69A7A3BA72AFE6D875BC177177C0B8E` |
| `autoime_en.schema.yaml` | 别名表部署入口（依赖方案，不在切换器展示） | 806 | `E0DC97CDDA323C239E31B8CABFCDC1DF0C37034F775111EE938F6CB5D9EBE112` |
| `custom_phrase.txt` | 用户可编辑的显式短语模板（已存在时不覆盖） | 370 | `FB897BF717E34B5B4751D73E4878A87AF133F11133ED0D349AF77A2F0D3C0EB4` |
| `autoime_user_terms.dict.yaml` | 个人英文术语覆盖层模板（Rime 表词库；已存在时不覆盖，管理者独占维护） | 905 | `4B095B6A12477D19F6748C06F1DE6B53A161C8142EF43F5FCDDAAB22ECF2F051` |
| `autoime_user_terms.schema.yaml` | 个人术语表的部署入口（依赖方案，不在切换器展示） | 790 | `A32725DE01716107A3F02FCDA7B7C8AF2124F48683BFE48E95D01232E5DB92A1` |

依赖方案（`autoime_terms`/`autoime_en`/`autoime_words`/`autoime_user_terms`）的作用
是让 librime 的部署器编译对应词表：部署器只为 `schema/dependencies` 中每个方案的
`translator/dictionary` 准备词典。它们不出现在方案选单中。`autoime_user_terms`
的词典文件由管理者写入用户目录（模板只在缺失时复制），部署器编译用户目录里的
那一份。

## 版本 0.2.0（PLAN-073）：术语简拼召回

`autoime_terms.schema.yaml` 的 `speller/algebra` 与主方案保持一致
（abbreviation/spelling_correction/key_correction）。原因是 librime 1.13.1 的
部署流程：`WorkspaceUpdate` 先构建主方案、再构建其 `schema/dependencies`；
`SchemaUpdate` 只编译该方案自己的 `translator/dictionary`；`DictCompiler::
BuildPrism` 用**被部署方案**的 `speller/algebra` 生成 `.prism.bin`，且 prism
记录 `schema_file_checksum`（另一方案再编译同一词表时会重建）。依赖方案原
没有代数，术语 prism 只有全拼音节拼写，`zlby`/`lxpy`/`hxck` 这类首字母路径
查不到词条；全拼路径按音节编号查询，因此一直可用。

实际产物对照（空用户目录、同一 rime.dll）：修复后 `autoime_terms.table.bin`
（5848 字节）与 `autoime_terms.reverse.bin`（6928 字节）字节不变，只有
`autoime_terms.prism.bin` 由 1340 变为 3792 字节。质量语料 61 例修复前 50 过
（11 条强制用例失败：6 条术语简拼/混输未召回、5 条「是的」输入不在前五），
修复后 61 过；36 个术语观察输入全部第 1 位
（`../tests/pinyin-quality.json`）。

排序侧同时把术语 translator 的 `initial_quality` 从 0.2 提到 1.0：术语进入
自然简拼竞争后，0.2 会让 `gaosipoj` 的首选从「高斯泼溅」掉到第 2（PLAN-071
冻结用例）；1.0 保持术语在自身编码上优先，同时不改变普通短码控制
（`nh`→你好第 2、`shuruf`→输入法第 1、`ros`→ROS 2 第 1）。

## 版本 0.2.0（PLAN-073）：常用词「是的」排序

`data/essay.txt`（小狼毫预设词频）给「是的」的权重是 776，而「使得」28566、
「速度」35075、「上的」83517；词没有被漏收，但 `shide` 只到第 7、`sd` 到第
199，前五不可用。本仓不修改小狼毫共享数据，改为 `autoime_words.dict.yaml`
补一条普通自然拼音词条：标准读音 `shi de`、权重 40000（与高频双音节词同带），
经 `script_translator@autoime_words` 参与正常音节/缩写竞争，不使用显式短码。

实测（空用户目录、0.2.0 资源）：`shide`/`shid`/`shde`/`shd`/`sd` 五种输入的
前五均含「是的」且均列第 1；「使得」在 `shide` 列第 2，「速度」在 `sd` 仍在
前五，「时代」「设定」等合法同码词仍被召回（名次见质量语料 `ctl-*`，不锁死
前五）。词典补录来源为小狼毫预设词频表的权重对比，属共享自然拼音词库数据；
个人显式短语仍由 `custom_phrase.txt` 承担。

## 运行时依赖（本机已安装的小狼毫）

AutoIME 复用已安装的小狼毫 0.17.4 的 `rime.dll` 与共享数据；本仓不复制、
不再分发这些文件。版本与校验值来自本机 2026-11-17 实测：

| 组件 | 路径（本机） | 版本/字节 | SHA-256 |
|---|---|---|---|
| librime | `C:\Program Files\Rime\weasel-0.17.4\rime.dll` | 1.13.1（文件版本 1.13.1.0）/ 3524096 | `2D8F1BC3737635A11D9FB1BFCA4DC9E70533633930A8A0142A81CA879C39C45B` |
| 通用字词 | `...\data\luna_pinyin.dict.yaml` | 962079 | `DC15421F70E80B5D9DC0339DB79856B671A58F8E6779C1B5B7592B83A57B0817` |
| 基础方案 | `...\data\luna_pinyin.schema.yaml` | 2184 | `A80C89BD16D31842BACBEE6B17B9BA1A4F5FD862B7DB31049A229AED8628299E` |
| 拼音代数 | `...\data\pinyin.yaml` | 4965 | `F2D291166946A848FD88E541844567415F1ED3D9C5A7477757B88AEB3A69C0CA` |
| 全局默认 | `...\data\default.yaml` | 1431 | `5A438C92AA7DDD749644C7C799967074634D41ACE924859BE2A371F29565C892` |
| 按键绑定 | `...\data\key_bindings.yaml` | 3021 | `19C52D80A4BF043C16C49FDFEB58CFA4D7A9F2B94BD18C79FF2485BD7627DB5F` |
| 标点 | `...\data\punctuation.yaml` | 2212 | `C9E5147FDA24D1A13085075DF5A2E73DEECB643069BE3751C6D4D793CEB2DE5B` |
| 符号 | `...\data\symbols.yaml` | 28729 | `D3C1823F0E3BAE5FB2DAC338F4863A0D7164436D1EE132727FF5C4687F53601A` |
| 简繁转换 | `...\data\opencc\t2s.json` | 428 | `EBE32A6766BFBAEA1869F82368F54A8FC6C977A235E8A896B618714872B5DF09` |

解析顺序：`AUTOIME_RIME_DLL`/`AUTOIME_RIME_SHARED_DATA_DIR` 环境变量覆盖 →
注册表 `WeaselRoot`；缺失时按键直通（不阻塞宿主）。`rime.dll` 缺失或方案
损坏时的回退见 T-04 部署章节。

## 许可证与来源

- 本项目资源（上表前半）为本仓原创，随本仓许可证分发。
- librime 为 BSD-3-Clause；小狼毫（Weasel）为 GPL-3.0；其共享数据词表源自
  CC-CEDICT、开放词典等上游资源，逐文件许可与署名以 Weasel 0.17.4 发行包
  为准。AutoIME 仅在本机调用已安装的小狼毫，不在本仓再分发这些文件。
- 若后续改为自带共享数据，需按上述来源逐项补充许可与版本登记。

## 版本 0.3.0（PLAN-078 T-01）：英文别名完整码准入

`table_translator@autoime_en` 的 `enable_completion` 由 true 改为 false，依赖方案
`autoime_en.schema.yaml` 同步关闭（部署器按该方案的 `translator` 编译 `.prism.bin`）。
原因是补全模式会按码前缀展开别名：输入单字母 `s` 即可召回 `SQLite`，`sq`、`sql`
同样命中，且合并排序后压过普通中文首选。关闭后别名只按完整码召回：
`sqlite`、`ros`、`autoos`、`tsf` 不变，`s` 的首选回到「是」，`sq`/`sql`/`S`/`Sq`/`SQ`/
`Sql` 都不再召回 `SQLite`。空库整表观测：`s` 有 2752 条候选、「是」第 1，SQLite 全表
缺席（`shared/tests/en-admission.json`，17 例全过；PLAN-071 的 13 例与 PLAN-073 的
61 例质量语料同时保持全过）。显式大小写触发码（`SQ`、`SQLite` 命中 `SQLite`）属于
[PLAN-078](https://github.com/auto-stack/auto-os/blob/main/docs/plans/archived/078-autoime-windows-daily-use-closeout.md) T-05，不在本条
开关内；本阶段大写输入仍不产生候选，只是不再借小写码泄漏到别名表。

## 版本 0.3.1（PLAN-078 T-04）：补充词与主词库同一学习域

`autoime_words` 的 translator 由 `enable_user_dict: false` 改为 true，并显式声明
`user_dict: autoime_user`（与主拼音 translator 同一个用户词库）。原因：补充词
「是的」的权重 40000 使它在自身编码上胜出，选中后由该 translator 处理；学习关
闭时这次确认被丢弃，用户反复选它也不会进入个人词频。改为共用学习域后，补充词
按同一文字与读音写入 `autoime_user`，主拼音、简拼与混输路径读取同一份权重，
和 PLAN-073 已冻结的「是的」排序不冲突（空库无学习条目时排名不变）。

验证（`ime/build/autoime_fixture.exe` 的 `supplement_learning_shared_db`）：
隔离用户目录、真实共享引擎所有者；确认「是的」10 次并 finalize 后用户词库增长
1174–1179 字节（关闭学习时仅 135 字节的日志波动），随后一次取消组合不增长
（-386 字节，属日志轮转）。配置改动前的同一用例以「confirmation did not write
the shared user dictionary」失败，改动后通过。

## 版本 0.3.2（PLAN-078 T-05）：显式大小写触发码与个人术语层

内置别名表新增两条显式触发码（`SQLite` → `SQ`、`SQLite` → `SQLite`，权重 1200），
并把 speller 字母表扩到大小写 A–Z，使大写按键能进入组合。效果：`SQ`、`SQLite`
逐字符命中 SQLite；`S`、`Sq`、`Sqlite`、`sqlITE`、`sq`、`sql` 都不命中；单字母
`s` 仍首选「是」。个人术语层 `autoime_user_terms`（新的依赖词库 + 部署入口）
由 `autoime-config terms` 独占维护，个人同码条目排在内置之前，可覆盖内置触发；
模板只在该文件缺失时复制，不覆盖用户内容，也不计入资源版本哈希。

模板末行是**占位行**（`AutoIME<TAB>32 个 z<TAB>1`）：librime 编译不了零条目的
表，空模板会报 `dictionary 'autoime_user_terms' failed to compile`。管理者
隐藏并保护该行，删除最后一条个人术语时自动重写；部署身份（短语版本标记）同时
覆盖个人术语文件，任一变化都会触发重新编译（否则新增术语不生效）。

验证：`shared/tests/en-admission.json` 20 例（新增 `SQ`/`SQLite` 命中与
`Sqlite`/`sqlITE` 反例）；`shared/tests/user-terms.json` 8 例经
`windows/ime/tests/run-t05.ps1`（CLI 写入 → 部署编译 → 真实引擎）全过。

## 0.3.2 候选窗与数字选词（PLAN-078 反馈修复）

- 候选窗边框 2 px 蓝色（`#2B6CF0`）、底色 `#E8F0FA`（微蓝白灰），选中项实心蓝底白字。
- 数字在可见候选页上按标签选中：TSF 层直接按页内索引选择，避免 librime 把数字并入大写表码（`SQ`+`1` 曾变成 `SQ1`）。
- 无组合直接插入的提交（如符号）把光标放到插入文本之后，不再停在符号左侧。

## 中英混输触发约定

数字键在组合中用于选词（`menu/alternative_select_keys: "12345"`），因此别名
码保持纯字母。默认别名例如 `ros` → `ROS 2`、`autoos` → `AutoOS`、
`tsf` → `TSF`（见 `autoime_en.dict.yaml`）。别名按**完整小写码**召回（PLAN-078
T-01：`sqlite` 有效，`s`/`sq`/`sql` 留给拼音）；显式大小写触发码由 PLAN-078
T-05 交付。设计要求示例中的 `ros2` 形态
需要把数字纳入编码，会牺牲数字选词；本方案选择数字选词，触发条件由方案
定义（Design 05 §5 允许）。

## 用户短语与简拼（PLAN-072 / PLAN-073）

`custom_phrase.txt` 由 `autoime-config` 管理（`list`/`add`/`remove`/`import`/
`export`），数据规则、退出码与部署事务见
[docs/specs/autoime/phrase-management.md](../../docs/specs/autoime/phrase-management.md)。
权重区间 1..=1000000，写入统一 LF 并保留注释/空行；`(code,text)` 同键幂等，
同码异文共存。模板文件仍是初始数据：部署只在文件缺失时复制，不覆盖用户编辑。

自然简拼（PLAN-073）：术语依赖方案 `autoime_terms` 现在带与主方案相同的
`speller/algebra`，因此术语词表的 `.prism.bin` 含首字母缩写拼写。12 条术语的
全拼、首字母简拼与全简混输都走自然拼音路径；`zlby`、`lxpy`、`hxck` 在空用户
目录下进入前五（质量语料与逐例名次见 `shared/tests/pinyin-quality.json`）。
个人显式短语（PLAN-072）仍是任意自定义短码的入口，不参与本条自然召回。