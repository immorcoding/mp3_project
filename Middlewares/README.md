# Middlewares

本目录包含第三方中间件及项目级配置接缝。FreeRTOS Kernel 与 FatFs 源码视为外部代码；项目配置和 Hook 位于允许维护的位置。

## 约束

- `Third_Party/FreeRTOS/Source` 保持未修改；
- `Third_Party/FreeRTOS/Config` 可维护 `FreeRTOSConfig.h`、Hook 和断言处理；
- FatFs 运行时策略属于 `Service/filesystem`，不是第三方源码；
- 不在第三方目录加入 APP、Platform 或硬件业务代码。

