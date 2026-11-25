# AutoInput 开发规约

本仓维护输入法、词典、管理工具与发布。计划和架构入口见 [docs/ROADMAP.md](docs/ROADMAP.md)。

- 独立 Git clone 在 main 开发，沿 074、076 的计划流程登记、执行、复审和归档。
- AutoLang 依赖按 `AUTO_LANG_ROOT`、兄弟 checkout、`D:/autostack/auto-lang` 解析。
- 保留 TSF 注册 GUID、安装器 AppId、Rime 方案、用户数据根和持久化格式的升级兼容。
- 测试使用隔离用户目录。构建产物、部署词典、日志和用户数据属于本地数据。
- 输入法验证使用本仓 fixture 和探针。单独修改输入法时无需运行 AutoLang 全量 Cargo 测试。
