# AutoInput 独立建仓评估（PLAN-078 T-07）

2026-11-25 · 评估记录 · r1 · 关联 [PLAN-078](../../plans/archived/078-autoime-windows-daily-use-closeout.md)、
[Design 07](../07-auto-input-shared-engine.md)、
[App 子模块工作流程](https://github.com/auto-stack/auto-os/blob/main/docs/design/strategy/app-submodule-workflow.md)

本文保存 PLAN-078 的拆分评估。2026-11-25 已确定仓名为 `auto-input`，产品名为 AutoInput。当前工作见[路线](../../ROADMAP.md)。

## 1. 现状与拆分信号

评估时，AutoInput 位于 `auto-os/experimental/autoime/`，规模与独立性已超出「实验目录」：

| 面 | 现状 | 独立仓信号 |
|---|---|---|
| 构建 | MSVC + CMake/Ninja 的 TSF DLL、`autoime_engine.exe`、`autoime_deploy.exe`、fixture、两套 probe | 与 AutoOS 其它模块无构建耦合 |
| 数据 | `shared/resources/`（方案、术语/别名/补充词/个人术语模板）+ 锁定的 Weasel 1.13.1 运行库 | 自带版本与哈希台账，可独立发布 |
| 管理工具 | `manager/src/*.at` 经 ACE AV 前门编译，`manager/native` 为薄启动器 | 依赖 auto-lang 的 ACE AV 能力面，不依赖 AutoOS 桌面 |
| 测试 | `ime/tests`（部署、宿主验收、个人术语整链）、`probe`（引擎、共享引擎双进程）、`manager/scripts/test.ps1`（CLI） | 自成体系，可独立 CI |
| 发行 | `windows/installer`（Inno Setup 6，离线包 + SHA-256） | 已有完整打包链 |
| 文档 | 本文、Design 05/07、`docs/specs/autoime/*.md`、PLAN-069–078 | 需要随仓迁移的规范正本 |

判据：输入法可脱离 AutoOS 独立安装使用（PLAN-070 起的设计前提），且它的构建、
数据、测试、发行都不消费 AutoOS 桌面/AutoUI/AGE/A2R。当前 078 之后日用基线成立，
074 和 076 的后续开发从独立仓继续。

## 2. 建议拆分的目录边界

**迁入新仓**：

```
<auto-input>/
├── windows/            # ime（DLL/fixture/tests/engine/deploy）、probe、installer、scripts
├── shared/             # resources/（方案与词表）、tests/（平台无关语料）
├── manager/            # Auto 业务源 + native 启动器 + tests
├── docs/specs/         # autoime 规范正本（见 §4）
└── docs/design/        # Design 05（Windows 版设计）、07（共享引擎决策）
```

**留在 auto-os**：

- `docs/plans/` 中的历史计划（PLAN-069–078 正文与归档）；新仓以指针行链接，不再
  重写历史。
- AutoOS 桌面接入位：`apps.manifest` 条目（若决定把已安装输入法纳入装配）、
  `docs/design/02-local-app-communication-f01.md` 等宿主侧协议。
- 本仓 `docs/specs/` 中与 AutoOS 桌面/输入协议相关的跨域规范（若有交叉，按
  「正本在主改面所在仓」规则处理）。

**不迁移**：`experimental/autoime/windows/installer/stage|dist`、`ime/build`、
`manager/bin|native/target` 等构建产物（已被 `.gitignore` 排除），新仓从零重建。

## 3. 历史、许可与来源

- 历史：新仓用 `git subtree split --prefix=experimental/autoime` 生成保留提交历史
  的初始分支（或在迁移决策里接受 squash + 指向 auto-os 历史）。两种做法都必须
  保留 069–078 的可追溯链接。
- 许可：AutoInput 自身代码与原创资源随本仓许可证；随包 `rime.dll` 与 Weasel 数据
  不从本仓再分发，由 `bundle-manifest.json` 在打包时从本机 Weasel 0.17.4 取值并
  校验（BSD-3-Clause / GPL-3.0 来源已在 `THIRD-PARTY-NOTICES.md` 与
  `shared/resources/README.md` 登记）。新仓必须原样迁移这两份台账与校验脚本。
- 身份：`manager/identity.txt`（rustc/ACE rev/源码哈希）与安装包的 SHA-256 记录
  一起构成发布身份；新仓 CI 需产出同类记录。

## 4. 独立构建与测试

| 步骤 | 命令（迁移后保持不变） | 前置 |
|---|---|---|
| 引擎/DLL/fixture | `windows/ime/build.cmd` | MSVC 2022 + CMake + Ninja |
| probe / 共享引擎 probe | `windows/probe/build.cmd` | 同上 |
| 管理器 | `manager/scripts/prepare.ps1` | auto-lang ACE 源码根（`$AUTO_LANG_ROOT`/兄弟/主检出） |
| 部署/回退/升级 | `windows/ime/tests/run-t04.ps1` | 已构建 ime |
| 个人术语整链 | `windows/ime/tests/run-t05.ps1` | manager 已 prepare |
| 包验收 | `windows/installer/run-t07.ps1` | 已打包 dist + stage |
| CLI 用例 | `manager/scripts/test.ps1` | manager 已 prepare |
| 宿主验收 | `windows/ime/tests/host-acceptance.ps1` | 已安装本构建、桌面可交互 |

边界：新仓不跑 auto-lang `crates/` 的测试；ACE AV 只作为管理器运行时的外部依赖，
按 `AUTO_LANG_ROOT` 解析序引用（禁止 junction/symlink）。跨仓验收脚本
（`run-t05.ps1`/`run-t07.ps1`）迁移后不再跨仓。

## 5. 与 AutoOS 的集成方式

三个选项，按侵入度排序：

1. **独立安装（默认）**：输入法由自身离线包装到 `%ProgramFiles%\AutoIME`，AutoOS
   不感知。当前 PLAN-071 起的发行形态，迁移成本最低。
2. **AutoOS 装配固定版本**：若桌面需要「开箱带输入法」，按现有独立 clone + 固定
   gitlink 约定，把新仓作为 submodule 收编到 `apps/<product>/` 或 `apps.manifest`
   的注册表条目；安装仍走自身安装包。需要决定 AutoOS 是否替用户安装/升级。
3. **深度集成**：AutoOS 桌面提供输入法设置页/候选窗主题（074 的图形管理延伸）。
   属于后续计划，不作为建仓前置。

迁移沿用选项 1，AutoOS 组合装配后续按需评估。

## 6. 074 / 076 的归属与接口

| 计划 | 内容 | 归属 | 与本包接口 |
|---|---|---|---|
| PLAN-074（`docs/plans/074-auto-input-av-phrase-ui.md`） | 图形短语/术语管理 | 输入法仓（UI 层）；若做 AutoOS 内嵌设置页则正文留在主改面仓并互链 | 复用 `manager/src/phrases.at` 与 `terms.at` 的解析/校验/写回；图形端不得自己重写文件格式或事务 |
| PLAN-076（`docs/plans/076-auto-input-segment-selection-learning.md`） | 分段选词与整词确认/撤销学习 | 输入法仓 | 复用共享引擎的 `Confirm`/`Forget` 与 `autoime_user` 学习域（Design 07 §5）；不得再开第二个可写词库所有者 |

两计划迁移到新仓后，`docs/plans/` 编号各自独立取号；auto-os 侧保留指针行。

## 7. 维护与发布

- 维护者：本仓当前唯一写入者（Codex）；建仓后沿 app 仓纪律「同一 app 一个写入者」。
- 发布：离线包 + `AutoInput-Setup-x64.exe.sha256` + 本评估记录与规范正本同一提交；
  升级保持 `%LOCALAPPDATA%\AutoIME\rime` 用户数据（短语、个人术语、学习库）。
- 待用户决定：仓名与远端、迁移时间窗、是否保留 `experimental/autoime` 历史、
  AutoOS 是否固定装配版本（选项 2）。
