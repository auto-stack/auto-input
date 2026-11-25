# AutoInput 路线

2026-11-25：AutoOS 完成 PLAN-078。Windows 日用版本作为 AutoInput 独立发展，AutoOS 后续可按需装配已验证的版本。

| 阶段 | 内容 | 状态 |
|---|---|---|
| 069–073 | Windows TSF 原型、离线包、ACE AV 短语管理、拼音质量 | AutoOS 已归档 |
| 078 | 中文首选、英文术语管理、横排候选、跨进程学习 | AutoOS 已归档 |
| 074 | 图形短语与术语管理 | [本仓草案](plans/074-auto-input-av-phrase-ui.md) |
| 076 | 分段选词与整词学习 | [本仓草案](plans/076-auto-input-segment-selection-learning.md) |
| 后续 | Linux 适配、AutoOS 发行装配、候选窗主题、可选语言模型 | 按需求另立计划 |

074 复用管理器业务逻辑与部署事务。076 复用共享引擎的确认与撤销学习能力。跨平台目标是统一会话、候选和词频行为。Linux 先验证系统输入框架与真实 Wayland 宿主，再评估 compositor 和 AutoUI 集成。参见[跨平台设计](design/cross-platform-input.md)。

历史计划和验收在 AutoOS 归档：

- [PLAN-069](https://github.com/auto-stack/auto-os/blob/main/docs/plans/archived/069-autoime-windows-x64-tsf-preview.md)
- [PLAN-070](https://github.com/auto-stack/auto-os/blob/main/docs/plans/archived/070-autoime-windows-daily-use.md)
- [PLAN-071](https://github.com/auto-stack/auto-os/blob/main/docs/plans/archived/071-autoime-windows-offline-installer.md)
- [PLAN-072](https://github.com/auto-stack/auto-os/blob/main/docs/plans/archived/072-autoime-ace-phrase-manager.md)
- [PLAN-073](https://github.com/auto-stack/auto-os/blob/main/docs/plans/archived/073-autoime-pinyin-quality.md)
- [PLAN-078](https://github.com/auto-stack/auto-os/blob/main/docs/plans/archived/078-autoime-windows-daily-use-closeout.md)
