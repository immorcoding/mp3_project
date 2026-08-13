# Middlewares

本目录包含第三方中间件及项目级配置接缝。FreeRTOS Kernel 与 FatFs 源码视为外部代码；项目配置和 Hook 位于允许维护的位置。

Middleware 是旁路外部依赖，不构成 `APP`、`Service`、`Platform`、`Components`、`Adapters` 的额外产品层。项目 Module 可以使用其公开头；中间件也可以经官方回调、Hook 或 Override Seam 在运行时进入项目，但这不表示反向 `#include` 依赖。

## 约束

- `Third_Party/FreeRTOS/Source` 保持未修改；
- `Third_Party/FreeRTOS/Config` 可维护 `FreeRTOSConfig.h`、Hook 和断言处理；
- FatFs 运行时策略属于 `Service/filesystem`，不是第三方源码；
- 不在第三方目录加入 APP、Platform 或硬件业务代码。
