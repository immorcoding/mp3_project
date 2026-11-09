/**
 * @file log_service_config.h
 * @brief Service_Log 静态消息块池深度。
 */

#ifndef LOG_SERVICE_CONFIG_H
#define LOG_SERVICE_CONFIG_H

/* log_service.h */
#define LOG_SERVICE_QUEUE_LENGTH  45U  /* free/ready 队列深度，亦为静态消息块个数。0.10.0 前台一次切（#218）从 30 加到 45：最坏同时到达 34 行（保守口径）仍留 11 块，Flash 压力钩子构建 37 行仍留 8 块，核算见 APP/tasks/monitor/README.md 与 Service/log/README.md。 */

#endif /* LOG_SERVICE_CONFIG_H */
