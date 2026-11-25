# AutoInput 已知债务与风险

## AUTOIME-OWNER-INTEGRITY

共享所有者完整性边界。状态 open。

[PLAN-078](archived/078-autoime-windows-daily-use-closeout.md) 交付的管道 DACL 修复使普通权限（Medium/Low IL）的同用户进程可连接由提升权限宿主以 High IL 启动的共享所有者；kDeploy 会让该 High IL 进程解析调用方指定的 Rime YAML，形成 Medium→High 的同用户信任跨越（未证明可利用，需 librime 解析缺陷）。
**Owner**：用户/后续计划（AutoInput 引擎启动路径）。
**Closure**：所有者以 Medium 令牌启动，或启动时主动降低 IL 并给管道 Medium 完整性标签，使 Medium→High 路径消失；在此之前保持 open。
