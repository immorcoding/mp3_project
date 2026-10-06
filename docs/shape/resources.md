# Resources

资源容器兼容性、产品加载与安装边界。

Next id: RES-9

## Pillars

- 内容身份与字节契约独立于介质和加载策略。
- 消费者只接收完整可用、生命周期明确的资源。
- 安装暂存不成为运行期资源来源。

## Open questions

- 设备侧整包安装、A/B 或掉电提交何时进入 spec？当前启动加载不能视为安装能力。
- FONT/AUDIO/MODEL/FIRMWARE 解码器、运行期流式读取何时启用？预留格式不表示已有解码、验签或执行能力。

## container

通用内容模型与二进制兼容性。

### Rules

- **RES-1** · provisional · RPKC1 Type 只描述内容，加载、必需性、目标内存与失败策略由 Service 决定；Component 只解析调用者只读窗口，不依赖介质、RTOS 或 GUI。_Why:_ 格式可跨介质复用，不把产品部署写入协议。_Source:_ [Component](../../Components/resource_pack/README.md)
- **RES-4** · provisional · 打包器与解析器共同遵循 RPKC1 字节契约；显式小端解码、64位范围运算，验证 CRC/对齐/任意重叠/严格升序非零 ID；未知类型公共合法则允许 Open，未启用类型解码返回不支持。_Why:_ 字节兼容不能依赖 C 布局或当前启用类型集合。_Source:_ [Core 契约](sources/rpkc1-layout.md)、[扩展类型契约](sources/rpkc1-extensions.md)

## installation

可拔介质、机内暂存与生效资源的关系。

### Rules

- **RES-2** · provisional · 生效资源以机内 Pack 为来源；设备整包安装先完整落入 FTL 暂存并校验，再写 Pack，不从 SD 流式编程，也不以 SD/FTL 普通文件作长期源；当前安装尚未实现。_Why:_ 隔离可拔介质与运行资源生命周期。_Source:_ [ADR-0015](../adr/0015-volume-roles-and-resource-install.md)

## loading

启动资源向消费者交接与故障封闭。

### Rules

- **RES-3** · provisional · 必需资源复制到归属明确的 SDRAM 后验证目标 CRC 和 Cache 交接，再启动消费者；任务期不持有 NOR 指针或 Entry View，未来按需读取经 Storage Task 串行接口。_Why:_ FTL 间接操作会退出 QSPI 映射，窗口不具备长期有效性。_Source:_ [Resource Service](../../Service/resource/README.md)
- **RES-5** · provisional · Platform SDRAM/Flash 成功后才加载；先验证来源窗口、PackSize、ID/类型与业务 Metadata、目标容量，再复制和目标 CRC；必需项任何失败阻止任务启动并保留诊断，只有可选项可警告继续。_Why:_ 避免未初始化或不兼容资源被消费者使用，目标 CRC 同时验证复制结果。_Source:_ [Resource Service](../../Service/resource/README.md)
- **RES-6** · provisional · 资源目标采用链接器预留 SDRAM NOLOAD 区域，32 B Cache 向外取整仍须在自身归属内；启动 CPU 复制链仅 Clean，不重复扫 NOR 或无因 Invalidate。_Why:_ 尾部 Cache Line 不能覆盖别的所有者，校验与可见性须对应真实数据方向。_Source:_ [Resource Service](../../Service/resource/README.md)

## packaging

主机生成、资源身份与烧录验证。

### Rules

- **RES-7** · provisional · 相同输入生成同包，ResourceID 保持语义身份而非文件排序位置，同一 ID 内容变化递增 ResourceVersion；名称/来源留外部 Manifest；用已验证 External Loader 写后校验，并按实际 PackSize 核对资源区边界，旧 RPK1 长度不沿用。_Why:_ 确定性与稳定身份防止资源误绑，独立回读与边界核对防止覆盖 FTL。_Source:_ [打包入口](../../Tools/package_maker/main.py)、[烧录工具](../../Tools/external_loader/README.md)

## firmware-trust

未来启用固件载荷时的执行权限与发布认证。

### Rules

- **RES-8** · provisional · 未来启用 FIRMWARE 时，由独立升级流程决定写入和执行，Resource Service 不因解析载荷而自动写内部 Flash、外设或跳转执行；Debug 可允许未签名载荷，正式发布必须拒绝未签名或签名错误的载荷。当前尚未实现解码、验签或执行能力。_Why:_ 资源声明与普通 CRC 不授予执行权限，发布载荷须绑定可信身份和使用元数据。_Source:_ [FIRMWARE 字节与签名契约](sources/rpkc1-extensions.md)
