# AutoInput Windows x64（PLAN-069 / PLAN-070 / PLAN-071 / PLAN-072）

AutoIME 的 Windows x64 离线拼音输入法：TSF 文本服务 + librime 引擎 + 原生
Win32 候选窗。PLAN-069 交付了记事本上屏原型（代码 `94f1101`）；PLAN-070
在其上交付日常试用版（按键/编辑/方案/部署），行为合同见
[Design 05](../docs/design/05-auto-input-windows.md)。PLAN-071 交付
Windows x64 离线安装包，并把方案资源与回放用例升为跨平台共享
（`../shared/`，Linux 由后续计划消费）。PLAN-072 交付 AV 实装的用户短语
管理器（`autoime-config`）、短语部署事务与规范
[docs/specs/autoime/phrase-management.md](../docs/specs/autoime/phrase-management.md)。

## 目录

```
probe/         引擎探针：直接加载 rime.dll 回放词库、方案与候选行为
ime/           TSF 文本服务、按键路由、编辑会话、候选窗、部署与诊断
  tests/       自动化夹具（TSF 回调序列、编辑计划、emoji）与 T-04 部署套件
  tools/       autoime_deploy.exe（同步部署 + 校验 + 延迟测量）
installer/     Inno Setup 离线安装包定义与构建入口（PLAN-071/072）
../manager/    AV 短语管理器（CLI + Auto 业务源 + 薄启动器；PLAN-072）
scripts/       安装/卸载脚本（UAC；安装时预部署资源）
../shared/resources/  默认方案 autoime_pinyin、术语/别名词表、用户短语模板、
                      依赖台账（Windows/Linux 共用源数据）
../shared/tests/      平台无关按键/候选期望用例（Windows/Linux 共用）
```

## 前置

- Windows 11 x64；小狼毫（Weasel）0.17.4 已安装（提供 `rime.dll`
  = librime 1.13.1 与共享数据；路径来自注册表 `WeaselRoot`，
  可用 `AUTOIME_RIME_DLL` / `AUTOIME_RIME_SHARED_DATA_DIR` 覆盖）。
- MSVC 2022 + CMake + Ninja。

## 构建

```cmd
cd ime
build.cmd          :: build\autoime_ime.dll + verify_registered.exe
                   :: + autoime_fixture.exe + autoime_deploy.exe
                   :: + autoime_engine.exe（PLAN-078 T-02 每用户引擎所有者）
cd ..\probe
build.cmd          :: build\rime_probe.exe + build\rime_shared_probe.exe
cd ..\manager
pwsh -NoProfile -File scripts\prepare.ps1   :: bin\autoime-config.exe + identity（AV 启动器）
```

## 自动化验证

```cmd
ime\test.cmd                      :: 先跑 ime\build.cmd；此后仅运行（21 用例/
                                  :: 220 检查，约 3.7s，含真实引擎符号上屏）
probe\build\rime_probe.exe --schema autoime_pinyin ^
    --resources-dir ..\..\shared\resources
                                  :: 方案/候选套件（12 用例，含 t11 部分提交/
                                  :: t12 学习持久化，~6s）
probe\build\rime_probe.exe --schema autoime_pinyin ^
    --resources-dir ..\..\shared\resources ^
    --cases ..\..\shared\tests\cases.json
                                  :: 共享用例回放（PLAN-071 T-01：空白用户数据，
                                  :: 逐例排名与首选/前五命中率，只读不选词）
probe\build\rime_probe.exe --schema autoime_pinyin ^
    --resources-dir ..\..\shared\resources ^
    --cases ..\..\shared\tests\pinyin-quality.json
                                  :: 拼音召回质量语料（PLAN-073 T-01：9 强制 +
                                  :: 36 观察 + 6 控制；scan=true 记录整表名次，
                                  :: whole-list=0 表示未召回）
probe\build\rime_probe.exe --schema autoime_pinyin ^
    --resources-dir ..\..\shared\resources ^
    --cases ..\..\shared\tests\en-admission.json
                                  :: 英文别名准入语料（PLAN-078 T-01/T-05：20 例；
                                  :: expect_absent 断言整表缺席，大写键按生产
                                  :: KeyRouter 的大写 keysym + Shift mask 回放）
pwsh -NoProfile -File ime\tests\run-t04.ps1
                                  :: 部署/回退/升级/并发/延迟（T-04，~30–40s）
pwsh -NoProfile -File ime\tests\host-acceptance.ps1
                                  :: 宿主验收（V-06，PLAN-073）：同构建门槛、
                                  :: WinForms TSF 窗口 + 记事本实打（三个简拼、
                                  :: 是的、Shift 符号、无 shift_click_toggle）；
                                  :: 用隔离空用户目录，需已安装本构建且桌面可交互
pwsh -NoProfile -File ..\manager\scripts\test.ps1
                                  :: 短语/CLI 用例（PLAN-072：26 例，含真实部署事务）
pwsh -NoProfile -File ..\manager\tests\txn.ps1 -Exe ime\build\autoime_deploy.exe -Resources shared\resources -ManagerExe manager\bin\autoime-config.exe -Probe probe\build\rime_probe.exe -BaselineCases shared\tests\cases.json
                                  :: 短语事务集成套件（18 例：冲突/忙/崩溃恢复/校验/probe）
probe\build\rime_shared_probe.exe --engine ime\build\autoime_engine.exe ^
    --self probe\build\rime_shared_probe.exe --resources-dir ..\..\shared\resources
                                  :: 共享引擎双进程用例（PLAN-078 T-02：17 项，
                                  :: 跨进程学习/隔离/恢复/单所有者/延迟；spawn 真实
                                  :: 引擎与子客户端，约 5–7 s，含首次部署）
pwsh -NoProfile -File ime\tests\run-t05.ps1
                                  :: 个人英文术语整链（PLAN-078 T-05：CLI 写入 →
                                  :: 部署编译 → 真实引擎召回；需先 prepare manager，
                                  :: 约 13 s）
pwsh -NoProfile -File ..\manager\tests\host-textbox.ps1 -Code vhost -Text 安装包验收词
                                  :: 宿主上屏验收（真实 WinForms TSF 窗口；需先 add 该短语）
```

`run-t04.ps1` 覆盖：全新用户目录冷启、未变更重部署、引擎缺失直通、主方案
损坏回退朙月、坏资源升级回滚到上一可用版本、升级保留用户短语与学习库、
双宿主并发冷启、就绪后按键到候选延迟（中位数/P95）。

## 安装、部署与恢复

安装需要管理员权限（HKLM 注册）：

```cmd
scripts\install.cmd [optional\path\to\autoime_ime.dll]
```

安装流程：复制 DLL/`autoime_deploy.exe`/`resources` → **预部署** Rime 资源
（首次部署在安装时编译词表，首个按键不等待）→ `regsvr32` 注册 → 加入
当前用户 Win+Space 列表 → 清理原型遗留的 `%TEMP%\autoime-ime.log`（明文
键轨迹）。已加载的旧 DLL 无法覆盖时会被改名停放，下次安装清理。

运行期：文本服务在 `Activate` 时把资源解析与部署放到工作线程；就绪前的
按键全部直通。部署失败时依次回退：上一可用资源版本 → 朙月简化字
（`luna_pinyin_simp`）→ 引擎禁用（键盘直通）；回退与禁用一律是失败状态，
不报成功。资源与用户数据都在
`%LOCALAPPDATA%\AutoIME\rime`，版本标记 `.autoime-resources`（内容哈希）与
`.autoime-phrase-version`（短语文件身份）成对校验，`custom_phrase.txt` 与
学习库 `autoime_user.userdb` 在升级时保留。

短语修改走事务入口（管理器自动调用；无需管理员）：

```cmd
"%ProgramFiles%\AutoIME\autoime_deploy.exe" phrases apply --request "%LOCALAPPDATA%\AutoIME\rime\.autoime-apply-<token>.req"
```

请求文件是 UTF-8 `key=value`（`user`/`source`/`source_missing`/`staged`/
`verify`/`timeout_ms`）。原生工具在部署锁内复核源字节、备份并记日志、原子
发布、执行 Rime 维护与**新会话候选校验**，失败恢复旧文件；退出码：0 成功，
4 前置/I/O，5 源已变化冲突（重读重试），6 部署或校验失败（已回滚），
7 部署忙（重试）。中断事务由下一次事务或 `deploy` 恢复。

卸载默认保留用户数据：

```cmd
scripts\uninstall.cmd          :: 移除注册与程序文件，保留短语/学习库
scripts\uninstall.cmd --purge  :: 同时删除 %LOCALAPPDATA%\AutoIME
```

单独重部署（无需管理员）：

```cmd
"%ProgramFiles%\AutoIME\autoime_deploy.exe" ^
  --resources-dir "%ProgramFiles%\AutoIME\resources" ^
  --user-data-dir "%LOCALAPPDATA%\AutoIME\rime" --latency 20
```

退出码：0 主方案就绪；5 回退朙月；3 引擎禁用；2 缺少 rime.dll。

## 方案与资源

默认方案 `autoime_pinyin`：通用字词复用已安装小狼毫共享数据，术语、
中英别名与用户短语由 `resources/` 提供（版本、字节、SHA-256、许可证
来源见 [resources/README.md](../shared/resources/README.md)）。数字保持选词键，
中英别名使用字母码（如 `ros` → `ROS 2`）。用户短语在同目录
`custom_phrase.txt`，增删后重新部署即生效。

## 按键转换（PLAN-073 T-07）

`KeysymForVk` 返回用户实际看到的字符：字母保留大小写，数字行与 OEM 标点返回
移位符号（`;`→`:`、`=`→`+`、`/`→`?`、`'`→`"`、`-`→`_`、`1`→`!`、
`9`→`(`、`0`→`)` 等）。中文空闲时 Shift+标点/Shift+数字走标点路径（`：`、`+`、
`？`、`！`、`（）` 等），未移位数字仍交给宿主；组合中按键由组合消费；英文
模式、Shift 单击切换与 Ctrl/Alt/Win/AltGr 快捷键不变。半角标点沿用 Weasel
`half_shape` 预设：`+` 保持半角、Shift+`` ` `` 首选 `~`、`\` 出 `|`、`{ }` 出
`「」`；需要全角形态属独立功能。Test 阶段已消费的按键也会标记 Shift 已用于
组合，避免个别宿主把 Shift+符号的抬起误判为单击切换；`ime\test.cmd` 的
`shift_test_only_no_toggle` 与 `shift_symbols_real_engine` 分别覆盖该路径和
真实 librime 上屏。

## 隐私

默认诊断只记录身份、阶段码、计数与耗时；按键路径不记录键码、预编辑、
选区或提交正文。敏感输入范围（密码框等）与宿主禁用 IME 时整键直通，
不建立学习会话，不显示候选。原型的明文日志位置与清理入口见上。

## 集成验收记录（2026-11-17，交付构建 = 安装目录 `autoime_ime.dll`）

每个场景开始前先做 TIP 判定：在目标窗口起一段组合，枚举宿主进程中是否
出现 AutoIME 自带候选窗类 `AutoIMECandidateWindow`；小狼毫不产生该窗口，
因此可区分两者。判定失败时脚本中止，不向其他窗口发送按键。

| 宿主 | 结果 | 证据 |
|---|---|---|
| 记事本（WinUI） | 选区替换「甲旧词乙」→「甲你好乙」；Esc 取消恢复「甲旧词乙」；emoji 前缀/后缀「甲😀你好乙」；`autoos`→`AutoOS`、`ros`→`ROS 2`、`tsf`→`TSF`；半截拼音 `ni` 切到另一窗口提交 `好`，旧窗无串写且预编辑被清空；单击 Shift 切英文后 `abc` 直通，Ctrl+S 打开宿主“另存为”对话框、Esc 关闭后正文不变 | `plan-artifacts/plan-070/t05/notepad-verified-2.log` |
| 浏览器文本框（Edge/Chromium） | 选区替换「甲旧词乙」→「甲你好乙」；Esc 取消恢复「甲旧词乙」（宿主终止组合时按保存的原选区回写）；`tsf`→`TSF` | `plan-artifacts/plan-070/t05/edge-verified.log` |
| 041 AutoEdit | **待测**：验收时 AutoOS 桌面/AutoEdit 未运行（仅有 `auto-ui-tool` 进程，无 AutoEdit 窗口），不能计作通过。复验入口：启动 AutoOS 桌面并打开 041 AutoEdit，重复上面的选区替换/取消、中英混输与半截拼音切焦 | — |

候选窗：纵向原生 Win32 窗口（非小狼毫横向候选条），按引擎标签显示
1–5，`GetTextExt` 成功且随光标移动；日志记录每显示器 DPI（本次 192）与
窗口矩形。候选窗可见时每 250ms 重新查询光标并重定位，DPI 变化时重建字体；
预编辑行显示在候选之上，光标按引擎 `cursor_pos` 落在组合内部。手感与视觉
最终由用户确认。

## 探针（词库层）

```cmd
cd probe
build\rime_probe.exe                       :: 默认注册表定位，临时空用户目录
build\rime_probe.exe --keep                :: 保留用户目录
build\rime_probe.exe --rime-dll PATH --shared-data-dir DIR ^
    --user-data-dir DIR --schema ID [--resources-dir DIR]
```

退出码：0 全过；2 缺 rime.dll；3 缺方案文件；4 部署/当前方案不符；
5 有用例失败。`SUMMARY` 行输出 DLL 版本、位数、方案、部署耗时与用例表。
