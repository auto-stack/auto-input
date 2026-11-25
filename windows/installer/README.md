# AutoInput 离线安装包（PLAN-071 T-02；管理器 PLAN-072 T-04）

在干净 Windows 11 x64（无小狼毫、无开发工具、断网）上安装 AutoIME 的
单个离线安装包 `AutoInput-Setup-x64.exe`。

## 构建

```cmd
cd ..\ime && build.cmd        :: 显式准备：autoime_ime.dll + 工具
cd ..\..\manager && pwsh -NoProfile -File scripts\prepare.ps1   :: 显式准备：启动器 + identity
cd ..\installer
build.cmd                     :: 核对随包引擎 → 暂存 → ISCC → dist\AutoInput-Setup-x64.exe
```

- 前置：MSVC 2022 + CMake + Ninja（ime 构建）、Inno Setup 6（ISCC）。
- 随包引擎与共享数据逐项核对 `bundle-manifest.json` 的锁定 SHA-256 与
  字节数后进包；来源为已安装小狼毫 0.17.4（`AUTOIME_WEASEL_ROOT` 环境变量
  → 清单默认根 → 注册表 `WeaselRoot`），不一致即命名失败，不产包。
- 产物哈希写在同目录 `AutoInput-Setup-x64.exe.sha256`；交付记录用同一值。

## 包内容

| 部分 | 内容 | 来源 |
|---|---|---|
| `autoime_ime.dll` | TSF 文本服务（070 构建，版本资源 0.1.0.0） | `windows/ime/build` |
| `autoime-config.exe` | AV 短语管理器启动器（静态 CRT，无需 VC++ 运行库） | `manager/bin`（`prepare.ps1` 生成） |
| `manager\src\*.at` | 管理器 Auto 业务源（运行期从安装目录加载） | `manager/src` |
| `manager\identity.txt` | 启动器/ACE/源码哈希身份（交付记录引用） | `manager/bin/identity.txt` |
| `autoime_deploy.exe` | 同步部署 + 校验 + 延迟测量（命中在跑所有者时转发重部署） | 同上 |
| `autoime_engine.exe` | 每用户共享引擎所有者（librime + 可写用户词库；PLAN-078 T-03） | 同上 |
| `verify_registered.exe` | 注册校验（路径/文件/版本） | 同上 |
| `resources\` | 默认方案/词表/短语模板/个人英文术语模板 | `shared/resources`（T-01 迁移后唯一源） |
| `rime.dll` + `data\` | librime 1.13.1、朙月词库、stroke、代数/预设、OpenCC t2s、语言模型 essay.txt | 锁定清单（`bundle-manifest.json`，15 项） |
| `licenses\` | 第三方声明 | `THIRD-PARTY-NOTICES.md` |

`data/essay.txt` 自 0.2.0 起随包锁定（PLAN-072 T-04 更正 071 的「不随带」）：
用打包材料复跑冻结用例时，缺席会改变 luna 自然候选排序（`nihao`/`nh`/
`shuruf`/`regx` 首选不再命中）；随包后 13/13 恢复。许可证：librime
BSD-3-Clause；随包共享数据取自小狼毫（GPL-3.0）发行包原样再分发，声明见上。

## 安装形态

- **机器级**（需要管理员）：程序文件 → `%ProgramFiles%\AutoIME`，
  TSF 注册 `regsvr32`（HKLM）→ `verify_registered.exe` 校验注册。
- **实际使用者身份**：Rime 用户数据预部署（`{userappdata}\AutoIME\rime`，
  首次编译词表）与 Win+Space 列表登记都以 `runasoriginaluser` 执行——
  标准用户以另一管理员账户授权（UAC 肩越）后，安装仍准备该用户自己的
  数据，首次输入即用就绪的默认方案。
- **升级**：宿主仍持有旧 DLL 时同卷改名停放（`autoime_ime_old_*.dll`），
  新 DLL 原位落位；旧停放文件在下次安装/卸载清理。
- **卸载**：撤销 TSF 注册与程序文件，保留 `%LOCALAPPDATA%\AutoIME`
  个人数据（短语 + 学习库）与其他输入法。

发布模式的引擎解析顺序（`rime_engine.cpp`）：`AUTOIME_RIME_DLL`/
`AUTOIME_RIME_SHARED_DATA_DIR` 环境覆盖 → 安装目录（`rime.dll` 与 `data\`
与 DLL 同目录）→ 注册表 `WeaselRoot`（开发机兼容）→ 缺失时按键直通。

## 0.3.2 打包与验收（PLAN-078 T-07）

包版本 0.3.2：payload 增加 `autoime_engine.exe`（`restartreplace`，权限不足时
下次空闲替换）、个人英文术语模板与依赖 schema；`uninstall.cmd` 先停引擎再清理
引擎/管理器文件。`build.cmd` 产出 `dist\AutoInput-Setup-x64.exe`（9,418,814 字节）

`pwsh -NoProfile -File run-t07.ps1` 为无需管理员权限的包验收：包哈希、payload
清单、resources 与仓库逐文件一致、锁定清单哈希、模拟安装目录的全新部署与候选
召回、升级保留用户文件（23 检查，约 14 s）。真实冷装/双宿主走查仍需管理员权限，
见 PLAN-078 §10 的阻塞说明。

## 0.2.0 打包与真机验收（PLAN-072 T-04）

- 包体：**9,315,763 字节**，sha256
  `9CBFE087CAA5ACD1472B86F90278BD97FDC087ECE23B309B0AADC03962883CC1`
  （071 为 3.9 MB；增量 = 静态 CRT 启动器 + 管理器源码 + 语言模型）。
- 管理器：`autoime-config.exe`（静态 CRT，13,563,904 字节，32 MiB 栈，sha256
  `ACBC6774…95415`）+ `manager\src\{main,phrases}.at` +
  `manager\identity.txt`；打包前按 identity 复核启动器哈希。
- 真机闭环（本机，管理员静默安装）：升级 071 0.1.0 → 0.2.0、TSF 注册校验
  通过、普通用户用安装后的管理器 add/list/remove 成功、独立 probe 在安装
  材料上看到新词（rank=1）、真实窗口输入 `vhost`+`1` 上屏合成词、卸载保留
  个人数据、重装恢复交付状态。
- 基线（安装后的启动器，清理开发环境变量；实测与负载）：冷加词 4.7–5.0 s（负载下 7.3 s 记录）/ 峰值
  54.6 MB；热列表 1,487 ms / 56.0 MB；deploy 快路径 2,696 ms / 52.9 MB。
- 已知：活宿主映射的 `autoime_ime.dll`/`rime.dll` 在卸载/重装时会保持占用
  （071 记录）；`rime.dll` 用 `restartreplace`，锁定文件的替换排到重启。
- 环境待测仍为：干净快照断网首装、双账户 UAC 肩越、Edge 宿主与 071 的
  候选窗手感清单（本机为已有 Weasel + 开发工具的机器）。