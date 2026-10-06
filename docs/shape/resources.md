# Resources

资源包兼容性、产品加载与设备安装边界。改启动/打包读[加载参考](resources.loading.md)，改解码器或协议读[Core 格式](resources.format.md)，启用新类型再读[扩展类型格式](resources.format-extensions.md)。

Next id: RES-4

## Pillars

- 内容身份与字节契约独立于介质和加载策略。
- 资源在消费者启动前完整可用，映射窗口不成为长期所有权。
- 设备安装与日常资源使用有不同生命周期。

## package

通用容器与产品策略的边界。

### Rules

- **RES-1** · provisional · RPKC1 的 Type 只描述内容，加载、必需性、目标内存与失败策略由使用 Service 决定；Component 仅解析调用者提供的只读内存，不依赖介质、RTOS 或 GUI。_Why:_ 相同格式可跨介质使用，不把当前产品部署写入协议。_Source:_ [Core 格式](resources.format.md)、[Component](../../Components/resource_pack/README.md)
- **RES-2** · provisional · 生效资源以机内 Pack 为来源；设备侧整包安装须先完整落入 FTL 暂存并校验，再写 Pack，不从 SD 流式编程，也不以 SD/FTL 普通文件作为长期资源源。当前安装尚未实现。_Why:_ 分离可拔介质、安装暂存和运行资源生命周期。_Source:_ [ADR-0015](../adr/0015-volume-roles-and-resource-install.md)

### References

- [加载、烧录与失败边界](resources.loading.md)：产品部署与现行四项资源。
- [Core / BINARY / IMAGE 字节契约](resources.format.md)：字段、CRC 与兼容性。
- [FONT / AUDIO / MODEL / FIRMWARE](resources.format-extensions.md)：预留类型，不表示已实现。

## loading

初始化资源向运行期交接。

### Rules

- **RES-3** · provisional · 启动加载把必需资源复制到归属明确的 SDRAM 区域，验证目标 CRC 与 Cache 交接后才启动消费者；任务期不持有 NOR 指针或 Entry View，未来按需读取经 Storage Task 串行化接口。_Why:_ FTL 间接操作可退出 QSPI 映射，长期窗口指针会失效。_Source:_ [加载顺序](resources.loading.md)、[Resource Service](../../Service/resource/README.md)
