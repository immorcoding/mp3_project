# Architecture · module-growth

新增模块与目录分类的准入条件。

[返回 Architecture](architecture.md)

### Rules

- **ARC-15** · settled · 未实现的 Module 不建立空目录或占位 Interface，新增模块先有真实职责与实现。 _Why:_ 占位抽象制造不存在的依赖边界。 _Source:_ [模块所有权决定](../adr/0002-layering-and-interface-ownership.md) _Check:_ [REVIEW](REVIEW.md) 对应领域审阅。
- **ARC-16** · settled · Components 保持扁平，只有稳定分类需求才增加分组。 _Why:_ 目录层级需要服务实际查找和所有权，而非预想规模。 _Source:_ [模块所有权决定](../adr/0002-layering-and-interface-ownership.md) _Check:_ [REVIEW](REVIEW.md) 对应领域审阅。
