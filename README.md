# AutoInput

AutoInput 原名 AutoIME，是 AutoStack 的独立输入法项目。当前交付 Windows x64 离线拼音输入法，使用 TSF、librime 和原生候选窗，支持全拼、简拼与混输、跨应用个人词频、个人短语和技术术语。

- [Windows 构建与使用](windows/README.md)
- [离线打包](windows/installer/README.md)与[第三方声明](windows/installer/THIRD-PARTY-NOTICES.md)
- [Windows 设计](docs/design/05-auto-input-windows.md)与[共享引擎](docs/design/07-auto-input-shared-engine.md)
- [开发路线与后续计划](docs/ROADMAP.md)

在 Windows 开发机准备 MSVC 2022、CMake 和 Ninja。管理工具使用 Rust/MSVC 及 AutoLang ACE 源码，依次检查 `AUTO_LANG_ROOT`、兄弟目录和 `D:/autostack/auto-lang`。

```powershell
.\windows\ime\build.cmd -DCMAKE_BUILD_TYPE=Debug
.\windows\probe\build.cmd -DCMAKE_BUILD_TYPE=Debug
pwsh -NoProfile -File .\manager\scripts\prepare.ps1 -Configuration debug
```

运行 `windows/installer/build.cmd` 生成 `AutoInput-Setup-x64.exe`，发行构建使用 Inno Setup 6 和清单锁定的小狼毫材料。安装器 AppId、TSF 注册 GUID、安装目录 `%ProgramFiles%\AutoIME`、用户目录 `%LOCALAPPDATA%\AutoIME\rime`、Rime 方案键和 `autoime-config` 命令沿用原标识，支持用新版安装包继续读取原有词频、术语和短语。

源码基线为 [AutoOS](https://github.com/auto-stack/auto-os) `f75a3edc92e7329f734ae742044f186ab70a772b` 的 `experimental/autoime/`。历史验收见[路线](docs/ROADMAP.md)。
