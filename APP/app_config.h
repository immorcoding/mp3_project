/**
  ******************************************************************************
  * @file    app_config.h
  * @brief   应用层功能编译开关。
  *
  * @details
  *          本文件集中保存 Application 层需要了解的功能开关，避免各模块
  *          在 main.c 中散落条件编译宏。这里的宏只表达“应用是否使用该
  *          功能”，不负责配置 GPIO、总线、时钟或具体器件参数。
  *
  * @note    修改宏后需要重新执行 CMake 构建。当前只有 APP_LOG_ENABLE
  *          被日志模块实际读取，其余宏为后续 LCD、SD Card 功能预留。
  ******************************************************************************
  */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/** @brief 1U：启用系统日志；0U：LOG_Init()/LOG_Printf() 返回 LOG_ERROR。 */
#define APP_LOG_ENABLE          1U

/** @brief 1U：应用启动阶段初始化板级 PMIC。 */
#define APP_PMIC_ENABLE         1U
/** @brief LCD 功能预留开关；当前尚未接入应用流程。 */
#define APP_LCD_ENABLE          0U
/** @brief SD Card 功能预留开关；当前尚未接入应用流程。 */
#define APP_SD_CARD_ENABLE      0U

#endif /* APP_CONFIG_H */
