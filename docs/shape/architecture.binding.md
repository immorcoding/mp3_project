# Architecture · binding

实例装配与调用期间的对象寿命。

[返回 Architecture](architecture.md)

### Rules

- **ARC-8** · settled · Ops 与 Context 成对绑定且生命周期覆盖调用；Platform 用 static 长期持有 Handle/Context；Bind 只装配且检查 Handle/Ops/Context/资源，Init 再检查必需回调后访问硬件，APP 不调用 Bind。 _Why:_ 调用期间悬空 Context 或装配时访问硬件都会破坏生命周期。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §4 _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。
