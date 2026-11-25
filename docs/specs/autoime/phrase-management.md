# AutoInput 用户短语管理（PLAN-072）

AutoInput 显式短语（personal explicit phrases）的数据合同、命令行入口、部署事务、
离线发布与验证边界。自然拼音候选排序、图形设置界面、TSF 全量重写与 Linux
前端不属于本规范；它们各自规划。

## 范围与身份

- 产品入口：`autoime-config`（AutoOS 仓 `manager/`），
  Windows x64，命令行为首批入口；短语语义与校验由 Auto 业务源实现，
  图形前端后续复用同一模块。
- 运行：新版 ACE AV（普通 Auto 源经共享前门编译执行），无旧 AutoVM 回退。
- 数据：用户自定义的 `custom_phrase.txt`，与 PLAN-069–071 产生的个人数据
  兼容；`autoime_deploy.exe` 仍是资源应用与 Rime 维护的唯一原生入口。

## 数据文件合同

路径：`<user-data-dir>\custom_phrase.txt`；默认 `<LOCALAPPDATA>\AutoIME\rime`
（`AUTOIME_RIME_USER_DATA_DIR` 覆盖），`--user-data-dir` 可指向隔离目录。

- 编码 UTF-8；允许开头 BOM（读取后丢弃，写回不写 BOM）；LF 与 CRLF 均可读，
  写回统一为 LF，且非空文件以单个 LF 结尾。
- 行格式：`<text><TAB><code><TAB><weight>`。`#` 开头（可带前导空白）为注释，
  空行为空行；注释与空行原样保留、保序。
- 校验：`code` 为 1..=64 个小写 ASCII 字母（数字留给选词键）；`text` 为
  1..=1024 个标量，不含 TAB/CR/LF/NUL；`weight` 为 1..=1000000 的十进制
  正整数（本规范固定的 librime 可接受区间）。
- 相同 `(code,text)` 是同一条目：新增/导入就地更新权重（并发安全条件下），
  未变化时重试逐字节幂等；同码不同文字允许共存；更新某键时该键的重复行
  归并为第一条并保留其原位置。未修改行的内容与顺序不变。
- 缺失文件按空数据处理；不可读、非 UTF-8 或坏行是明确失败，绝不当作空数据
  覆盖原文件。
- 学习库（`autoime_user.userdb` 等 librime 数据）由引擎管理，短语路径不得
  重写它。

## 命令合同

```text
autoime-config phrases list   [--user-data-dir D]
autoime-config phrases add    --code C --text T [--weight W] [--user-data-dir D]
autoime-config phrases remove --code C --text T [--user-data-dir D]
autoime-config phrases import --file F [--user-data-dir D]
autoime-config phrases export --file F [--user-data-dir D]
autoime-config deploy         [--user-data-dir D]
```

- `add` 缺省权重 100；`remove` 匹配完整键，键不存在为成功无变更。
- `import` 先整批校验：任一行非法即整批拒绝（指出行号与类型），数据文件
  字节不变；合法时逐条 upsert，注释行不进入数据。
- `export` 写 `text<TAB>code<TAB>weight` 数据行（源顺序、每行一个 LF），
  经同目录暂存 + 原子替换；写失败保留既有导出文件。
- `list`/`export` 是用户显式读取产品数据；其余诊断只含状态码、行号、阶段与
  数量，不含短语正文、短码或输入历史。
- 退出码：`0` 成功（含幂等无变更）；`2` 参数/用法；`3` 数据非法（行诊断）；
  `4` I/O（缺失、不可读、非 UTF-8、写入失败）；`5` 源已变化冲突（重读重试）；
  `6` 部署或校验失败（已恢复旧文件）；`7` 部署忙（可重试）。

## 个人英文术语（PLAN-078 T-05）

与短语并存、互不替代的另一层个人数据：大小写敏感的英文术语触发码。

- 数据文件：`<user-data-dir>\autoime_user_terms.dict.yaml`，一个 Rime 表词库，
  由管理者（`autoime-config terms`）独占维护并规范化写回（固定头 + 每行一条）。
  安装包只提供模板；部署只在该文件缺失时复制模板，绝不用模板覆盖用户内容，
  也不把它计入资源版本哈希。
- **空表占位行**：librime 无法编译零条目的表，模板与「删除最后一条个人术语后」
  的文件都保留一行占位（`AutoInput<TAB>zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz<TAB>1`）。
  该保留码不可 add/remove、不出现在 `list`/`export`，用户数据为空时自动重新写入。
- **重编译触发**：部署身份（`.autoime-phrase-version` 与 pending 标记）同时覆盖
  `custom_phrase.txt` 与个人术语文件的字节；任一变化都会使「资源未变」快路径
  失效并重新编译个人术语表。只写文件不重编译的旧行为会让新增术语在下次会话不
  生效（2026-11-25 实测修复）。
- 行格式：`<text><TAB><code><TAB><weight>`。`code` 为 2..=32 个 ASCII 字母，
  **逐字符区分大小写**（`AS` 与 `as` 是不同触发码）；`text` 1..=1024 标量，
  不含 TAB/CR/LF/NUL，保留大小写与空格；`weight` 1..=1000000，`add` 缺省 1000。
  同码不同文字允许共存；`(code,text)` 为同一条目。
- 生效：写入成功后调用部署入口重新编译该词库（运行中的引擎所有者负责重载，
  无所有者时由部署工具直接编译）。新条目在**下一次组合**读取；重部署会使现有
  组合按共享引擎合同失效并恢复原文本，不静默改写正文。
- 冲突优先级：同一触发码上个人条目先于内置别名（`autoime_en`）被查询，可覆盖
  内置触发；`terms list` 同时给出内置与个人来源，并隐藏被个人覆盖的内置同码行。

```text
autoime-config terms list   [--user-data-dir D]
autoime-config terms add    --code C --text T [--weight W] [--user-data-dir D]
autoime-config terms remove --code C --text T [--user-data-dir D]
autoime-config terms import --file F [--user-data-dir D]
autoime-config terms export --file F [--user-data-dir D]
```

- `list` 每行 `text<TAB>code<TAB>weight<TAB>personal|builtin`：个人条目在前，
  其后是未被个人同码覆盖的内置条目。
- `add`/`remove` 幂等（无变化时不重写、不部署）；`import` 先整批校验，任一行
  非法即整批拒绝且文件不变；`export` 只写数据行（源顺序、每行一个 LF）。
- 退出码：0 成功（含幂等无变化），2 用法，3 数据非法（行号 + 类型，不回显
  正文），4 I/O，6 部署失败（已恢复上一份文件）。个人术语的 `add`/`remove`/`import`
  不做并发协调，同一文件的并发写为**最后写入者胜**；`phrases`（072 事务路径）保留
  并发冲突检测。
- 诊断与 `list` 只输出条目数据，不记录按键轨迹；密码框/隐私范围与短语相同。

验证：`manager/tests/phrases.cases.json` 的 `T-05-*` 用例覆盖命令合同；
`windows/ime/tests/run-t05.ps1` 覆盖「CLI 写入 → 部署编译 → 真实引擎召回」
整链（`AS`→`AutoScape`、个人 `SQ` 覆盖内置、`as/As/A` 不命中）。

## 部署事务

`add`/`remove`/`import` 不直接改写数据文件，而是生成：

1. 完整的新短语文件（暂存）；
2. 读取时的源字节快照（或「读取时不存在」标记）；
3. 期望候选校验表（新增/导入 `present`，删除 `absent`）；
4. 内容哈希 + 每次运行时间 nonce 命名的请求文件（UTF-8 `key=value`，短语正文不进命令行；并发管理器各有独立暂存文件，同内容重试仍按源校验返回冲突）。

原生入口 `autoime_deploy.exe phrases apply --request <file>` 在部署锁内执行：

1. 恢复上次被中断的事务（还原本地备份、丢弃编译产物、清日志），并清理
   无日志的孤儿短语备份；
2. 逐字节复核源（与资源版本无关）：不一致即冲突，不写任何文件；
3. 备份当前短语文件并写事务日志（`published`）；
4. 原子发布暂存文件；
5. 在同一锁内执行 Rime 维护（资源部署、词典编译）；
6. **新会话候选校验**：新引擎实例加载当前方案后，按短语码组合并检查期望
   文字出现（新增/导入）或消失（删除）；校验逐页枚举该码的候选（发送
   PageDown 直到最后一页，最多 20 页），因此同码多词与「删除后消失」都以
   全部分页为准；
7. 提交（清日志/备份）或失败回滚（恢复旧文件、丢弃编译产物、置失败状态）。

回退（Luna）、引擎禁用、校验失败、部署失败、忙与冲突都是失败或明确待重试
状态，不报应用成功。锁超时返回忙；锁覆盖发布与维护全过程，与已有部署操作
互斥。中断恢复信息只含事务身份与阶段，短语备份按事务生命周期保存在用户
数据目录。

## 部署刷新与 unchanged 快路径

部署标记同时覆盖「受管资源内容哈希」与「短语文件身份」；`Commit` 只在引擎
验证成功后同时写入两者，`Rollback` 移除短语标记。因此：

- 资源不变而短语被手工编辑时，`deploy` 必须重新编译短语表（不得走
  unchanged 快路径）；
- 两者都未变时，`deploy` 走快路径（不重拷资源、不重编译）。

## 运行时与离线发布

- 薄启动器 `autoime-config.exe`：发现 `<exe_dir>\manager\src\*.at`（或
  `AUTOIME_MANAGER_DIR`），组装 ACE 系统库闭包（`std.file/path/env/process/
  text/bytes/conv/time` 及其依赖），用 `PackageAvTarget` 执行；argv 为真实进程
  命令行，console 流逐字节回放，程序 `int` 结果即进程退出码；180 s 墙钟预算；
  32 MiB 栈。
- 启动器静态链接 MSVC CRT，仅依赖操作系统 DLL；运行期不需要 rustc/cargo/
  Auto 开发环境。
- 离线包携带：静态 CRT 启动器、`manager\src\*.at`、`manager\identity.txt`
  （启动器/ACE/源码哈希）、`autoime_deploy.exe`、`rime.dll`、`data\`
  （含语言模型 `essay.txt`，缺席会改变 luna 自然候选排序）、`resources\`、
  许可证声明。安装器在打包前核对启动器哈希与随包引擎逐项 SHA-256。
- 升级保留 `%LOCALAPPDATA%\AutoIME` 个人数据；卸载默认保留。活宿主映射的
  DLL 允许改名停放/延迟替换，不阻塞安装。

## 隐私与数据保护

- 默认诊断（日志、stderr、JSON 输出）不含短语正文、短语码、预编辑、选区或
  输入历史；只含阶段、状态码、行号、数量、版本身份与耗时。
- 短语正文只出现在用户显式请求的输出与数据文件中（`list`、`export`、
  `custom_phrase.txt`）；校验请求文件在事务结束后删除。
- 敏感输入与宿主禁用 IME 的行为沿用 Design 05（整键直通、不建学习会话）。

## 验证边界

- 自动化：短语/CLI 用例 26 条；事务用例 18 条（冲突、并发、忙、两处崩溃恢复、
  新增/删除候选校验、回滚、Unicode 用户目录、学习库保留、坏资源失败、
  独立 probe 进程、071 冻结回放）；071 冻结回放 13 例与 probe 内建套件；
  原生夹具 17 用例/146 检查 + 诊断隐私源码审计；071 部署/回退/升级/并发套件
  32 检查。
- 基线（0.2.0 安装包、清理开发环境；实测与负载）：冷 `add` 4.7–5.0 s（负载下 7.3 s 记录）、峰值约 55 MB（采样峰值 69 MB）；
  热 `list` 约 1.5 s；`deploy` 快路径约 2.7 s；包体 9,315,763 字节（sha256
  `9CBFE087CAA5ACD1472B86F90278BD97FDC087ECE23B309B0AADC03962883CC1`）。
- 已知边界：四字术语首字母简拼（如 `zlby`）在资源 0.1.x 不属于自然候选前五；
  PLAN-073 资源 0.2.0 补齐术语依赖方案的拼音代数后，`zlby`/`lxpy`/`hxck` 与
  全拼、全简混输一样走自然候选前五（质量口径见
  [pinyin-quality.md](pinyin-quality.md)）。个人显式短语（本规范）仍是任意
  自定义短码的入口，与自然召回互不替代。Edge 宿主、干净快照断网首装、
  双账户 UAC 肩越与候选窗手感仍待相应环境实测。
