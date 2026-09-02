/**
  ******************************************************************************
  * @file    service.h
  * @brief   Service 层公开操作结果 Interface。
  ******************************************************************************
  */

#ifndef SERVICE_H
#define SERVICE_H

/**
  * @brief Service 对上层公开的通用操作结果。
  * @note  此枚举只表达调用者需要据此作出流程决策的语义；Platform、Component
  *        与 Middlewares 的原始错误码仅限各 Service 的 Implementation 使用。
  */
typedef enum
{
    SERVICE_OK = 0U,         /**< 本次调用成功完成。 */
    SERVICE_ERROR,           /**< 失败且没有更具体的公开语义，例如底层 I/O 或数据损坏。 */
    SERVICE_INVALID_PARAM,   /**< 参数非法，例如空指针、非法卷、非法路径或非法打开方式。 */
    SERVICE_NOT_READY,       /**< 能力尚未就绪，例如未初始化、未挂载或当前任务不是执行器所有者。 */
    SERVICE_BUSY,            /**< 资源正被占用，例如槽用尽、目标已存在、卷上仍有打开对象或目录非空。 */
    SERVICE_TIMEOUT,         /**< 等待底层完成超时。 */
    SERVICE_NO_FILESYSTEM,   /**< 介质存在，但没有可识别的文件系统或 FTL 卷。 */
    SERVICE_INVALID_HANDLE,  /**< 句柄无效或已失效，例如关闭、卸载、格式化或恢复之后的旧 Token。 */
    SERVICE_NO_MEDIA,        /**< 物理介质当时不可用，例如 SD 已拔出或磁盘驱动报告未就绪。 */
    SERVICE_NO_SPACE         /**< 没有足够空间完成写入或创建。 */
} Service_StatusTypeDef;

#endif /* SERVICE_H */
