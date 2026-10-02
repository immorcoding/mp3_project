# Code style

自维护 C 代码的命名、注释与 config 排版；正文在 [coding_standard.md](../coding_standard.md)，本文件只给入口规则。

Next id: STY-5

## Pillars

- 调用者只看公开 Interface 就能理解能力。
- 名字保留层级、Adapter 身份与并发语义，不为短而删。

## Rules

- **STY-1** · settled · 标识符按 `coding_standard.md` §3：如 `Service_<Capability>_<Verb>`、`Platform_<Capability>_<Verb>`、`<Module>_<Target>Adapter_<Verb>`，私有符号 `static snake_case`。_Check:_ 审阅按 `coding_standard.md` §3。
- **STY-2** · settled · 每个自维护函数的定义处写完整中文 Doxygen（`@brief`、带方向的 `@param`、`@return`/`@retval`），声明处不写。_Source:_ §4 _Check:_ 审阅按 `coding_standard.md` §4。
- **STY-3** · settled · 每个 Module 只有一份 `<module>_config.h`，按服务文件分组、行末 `/* */` 注释、三列对齐；其他 Module 不直接包含它。_Source:_ §2、§2.1 _Check:_ 审阅按 `coding_standard.md` §2。
