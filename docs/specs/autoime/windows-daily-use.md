# AutoInput Windows 日用基线（PLAN-078）

AutoInput Windows x64 在「日用」阶段的用户可见行为基线：候选窗形态、两个（或
更多）宿主并行时的共享引擎与学习、版本交接、离线交付与迁移前提。拼音召回与
质量口径见 [pinyin-quality.md](pinyin-quality.md)，个人短语/术语的数据与事务
合同见 [phrase-management.md](phrase-management.md)，架构决策见
[Design 07](../../design/07-auto-input-shared-engine.md)。图形管理、分段造词、
云同步、Linux 前端不属于本规范。

## 1. 候选窗

- 预编辑独占顶行；候选按引擎标签从左到右排列，标签与顺序完全来自引擎（页内
  序号一致），不做本地重排。
- 视觉：微蓝白灰底（`#E8F0FA`）+ 2 px 蓝色边框（`#2B6CF0`，随 DPI 缩放）+ 约 4 px 圆角；
  当前项实心蓝底白字，普通项深色文字。浅色与深色宿主背景下都必须可分辨。
- 几何单一来源：测量、绘制、高亮与点击命中使用同一批候选矩形；文字按项矩形
  裁剪，超长术语不得画出自己的点击区。
- 工作区过窄时同页换行；光标贴近屏幕边缘时窗口整体限制在工作区内；每显示器
  DPI 重建字体与度量。
- 输入指示器：切换器列表与任务栏徽标都显示 AutoInput 的橙色图标。要求**两级注册**：机器级 `HKLM\...\CTF\TIP\<clsid>\LanguageProfile\0x0804\<profile>`（激活/枚举/切换器，InstallServer 经 regsvr32 写入）与**每用户** `HKCU\Software\Microsoft\CTF\TIP\<clsid>\LanguageProfile\0x0804\<profile>`（`Description/IconFile/IconIndex/Enable`，任务栏徽标读这一份；缺了它徽标退回语言名“简体”，切换器仍正常）。`enable-user-list.ps1` 以安装用户身份写每用户项，`disable-user-list.ps1` 删除；`IconFile` 指向随包 `autoime.ico`（缺失时回退 DLL）。
- 交互：数字键与鼠标选中同一项；数字在**可见候选页**上总是按标签选中该项（即使引擎会把数字并入大写表码，如 `SQ`+`1` 不得变成 `SQ1`）；数字/鼠标选择按页内索引提交；页版本不符或
  索引越界的点击一律忽略（不提交、不学习）。弹出窗从不取得前台激活，点击
  预编辑行、留白与页外不改变焦点或正文。
- 自查：`autoime_fixture` 的 `candidate_layout_*`、`candidate_window_render_click`
  （真实窗口渲染像素 + 真实点击映射），以及 `run-t05` 之后的宿主走查。

## 2. 多宿主、共享引擎与学习

- 同一 Windows 用户只有一个可写词库所有者：`autoime_engine.exe`（每用户一个，
  互斥单实例）。TSF DLL 不加载 `librime`、不打开用户目录，只经本机命名管道
  转发会话操作与提交。
- 所有者按**用户数据根**区分：默认 `%LOCALAPPDATA%\AutoIME\rime` 使用无后缀
  实例；其它数据根（环境覆盖、`--user-data-dir`、隔离测试目录）使用
  `profile-<路径哈希>` 实例，拥有独立管道/互斥与独立词库。不同档案之间不共享
  学习；部署工具也按同一规则寻找对应所有者。
- 每个宿主/编辑上下文持有独立会话；组合、页码与页版本互不串扰。组合期间别的
  宿主输入不改变本窗口的预编辑与候选。
- 用户确认（确认候选并成功写入正文）写入 `%LOCALAPPDATA%\AutoIME\rime` 的同一
  用户词库；同一用户的其它已打开宿主在**下一次组合**读到更新。补充词
  （`autoime_words`）与普通词同一学习域。
- 取消（Esc/宿主终止）不写入；宿主正文写入失败时客户端调用 `Forget` 撤销刚学到的
  条目：编码为空时所有者复用该会话最近一次组合/确认的编码并翻页查找候选，会话
  从未上报编码时放弃并记录；失败不产生第二次学习。所有者重启后学习保留。
- 所有者不可用（未启动、崩溃、重部署）时按键透明直通，不删除文档内容；重新
  可用后下一次组合重新接入并重建会话，绝不退化为第二个词库所有者。

## 3. 版本交接与部署

- 客户端与所有者用协议版本握手；不匹配的客户端拒绝接入，不做部分兼容。
- 资源升级由所有者执行部署（内容哈希 + 上一可用版本回退）；运行中的第二个
  工具不得替换所有者已映射的文件，`autoime_deploy` 命中同用户目录所有者时
  转发重部署并报告 `via=owner`，不同用户目录才走进程内路径。
- 重部署会终结现有会话（客户端恢复原文本并重建），新表在后续组合生效。
- 安装/升级在替换文件前停止运行中的所有者（`taskkill`，跳过其一次优雅 finalize，
  LevelDB 日志随后恢复），`restartreplace` 仅作回退；宿主在下一次组合重新拉起
  新版本所有者。所有者的命名管道在创建时显式授予当前用户 SID 完整访问，因此即使
  所有者由提升权限的宿主（如安装窗口的 TSF 激活）启动，普通权限的输入法也能连接；
  管道按“同一用户”授予访问，是明确的同用户信任边界（跨用户的完整性边界问题见
  `docs/plans/KNOWN-DEBT-AND-RISKS.md` 的 `AUTOIME-OWNER-INTEGRITY`）。被宿主映射的锁定数据
  （`rime.dll`、`data\`）在版本相同时用 `onlyifdoesntexist` 跳过，不在升级时触发
  「文件占用」中止。
- 所有者空闲 10 分钟后先 finalize 用户库再退出，不长期占用可写词库。

## 4. 离线交付与身份

- 交付物：`AutoInput-Setup-x64.exe`（Inno Setup 6，机器级）+ 同名 `.sha256`。
  包内含 TSF DLL、引擎所有者、部署工具、管理器启动器与 Auto 源、方案与词表
  （含个人英文术语模板）、锁定的 librime 1.13.1 与朙月/OpenCC/essay 数据。
- 锁定清单：`windows/installer/bundle-manifest.json` 逐文件记 SHA-256；打包脚本
  不一致即失败、不产包。仓库内 `shared/resources/README.md` 与清单两处一致。
- 离线：安装、首次部署与日常输入全程不需要网络；安装时预部署词表，首次按键
  不等待编译。
- 升级保留 `%LOCALAPPDATA%\AutoIME\rime` 的 `custom_phrase.txt`、个人英文术语
  文件与学习库；卸载默认保留用户数据，`--purge` 才删除。
- 身份记录：安装包 SHA-256、`manager/identity.txt`（rustc/ACE rev/源码哈希）、
  引擎与资源版本（所有者 `Hello` 返回 `resources_version`）三者共同构成发布身份。

## 5. 验收基线

| 面 | 证据 |
|---|---|
| 候选窗 | `autoime_fixture` 的 5 个候选窗/布局用例（真实窗口渲染像素 + 点击映射 + 页版本反例） |
| 共享学习 | `rime_shared_probe` 17 项（双进程双向学习、组合隔离、所有者被杀/重启、单所有者守卫、P95 < 10 ms、无 `LOCK`） |
| 补充词学习 | `autoime_fixture` 的 `supplement_learning_shared_db`（确认写入、取消不写） |
| 英文准入与术语 | `shared/tests/en-admission.json`、`user-terms.json` + `run-t05.ps1` 整链 |
| 部署/回退/升级 | `windows/ime/tests/run-t04.ps1` |
| 包 | `windows/installer/run-t07.ps1`（哈希、清单、模拟安装部署、升级保留） |
| 真实宿主 | 管理员安装后的记事本 + 浏览器双宿主走查（本计划 §10 记录执行条件） |

## 6. 迁移前提

`` 具备独立建仓条件（构建、数据、测试、发行均不依赖
AutoOS 桌面）；拆分边界、历史/许可、AutoOS 集成方式与 074/076 归属见
[独立建仓评估](../../design/strategy/auto-input-standalone-repo-assessment.md)。
建仓本身是用户决策，不在本规范内执行。
