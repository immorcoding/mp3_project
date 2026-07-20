/**
  ******************************************************************************
  * @file    app.h
  * @brief   应用层生命周期公共接口。
  *
  * @details
  *          Core/Src/main.c 只需要调用本文件提供的三个入口，不必了解
  *          PMIC、日志、USB 等子模块的装配细节。这样 CubeMX 重新生成
  *          Core 目录时，应用功能仍集中保存在 APP 目录中。
  ******************************************************************************
  */
#ifndef APP_H
#define APP_H

/**
  * @brief  初始化应用使用的各项服务和板级设备。
  * @note   必须在 HAL、系统时钟、GPIO 和 USB Device 初始化完成后调用。
  * @retval None
  */
void app_init(void);

/**
  * @brief  执行一次应用主循环任务。
  * @note   main() 应在 while(1) 中高频调用；本函数不得包含长时间阻塞。
  * @retval None
  */
void app_run(void);

/**
  * @brief  应用层错误处理扩展入口。
  * @note   当前为空实现，预留给以后记录故障或执行安全关断。
  * @retval None
  */
void app_error(void);

#endif /* APP_H */
