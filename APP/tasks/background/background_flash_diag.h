/**
 ******************************************************************************
 * @file    background_flash_diag.h
 * @brief   Flash 失败诊断的日志片段：把 Platform 记下的最近一次 FTL 失败格式化成一小段文字（#220，第 6 次上板追加）。
 *
 * @details
 *          回收作业失败行和 Flash 压力钩子的停下行都带这一段，用来分清 FTL 的失败到底是 `NO_SPACE`、`CORRUPT` 还是 `IO_ERROR`：
 *          文件接口把 FTL 的任何非 OK 结果都压成 `SERVICE_ERROR`（状态数值 1），只看状态分不出来。数据来自
 *          `Platform_Flash_GetVolumeDiagnostics()`，只读，不拿任何锁，不改变产品行为；只在失败那一次读。
 *          格式（以空格开头，可以直接接在别的文字后面，一律不换行）：
 *          ` ftl=<结果> op=<请求> raw=<ok|busy|error|timeout|?>[ n=<失败次数>] free=<空闲块>[ valid=<有效块> stale=<失效块>][ <额外文字>][ qspi=<ErrorCode>/<State>/<SR>][ rej=<受理拒绝>][ hw=<芯片错误>/<端口状态>]`。
 *          - `ftl` 是 FTL 最近一次以非 OK 收尾的请求的结果（`NO_SPACE`、`CORRUPT`、`IO_ERROR`……），`none` 表示从没失败过，
 *            也就是文件接口的错误不是 FTL 来的；`op` 是那次请求的类别，`raw` 是失败时 FTL 最近一次原始访问的结果；
 *            `n` 是开机以来以非 OK 收尾的请求数（最多写到 999），**等于 1 时省略**（第一次失败，没有信息量，省出的字符给 `qspi`）。
 *          - FTL 失败后不就绪，`valid`、`stale` 读作 0，所以两者都为 0 时整段省略；`free` 始终有。
 *          - 额外文字由调用者给（压力钩子放写之前的快照与写请求字节数），紧跟在必有部分后面。
 *          - `qspi` 是 Platform 记下的失败时 QSPI 的三个原始值 `<ErrorCode>/<State>/<SR>`，十六进制，原样打印，APP 不解读；三个值都为 0（没有记录）时省略。
 *            位含义和判读见 `Platform/flash/README.md`“QSPI 失败快照”。
 *          - **一行放不下时，每一段整段取舍，不留半截字段**：从额外文字之后起，放不下的那一段和它后面的各段一律丢掉（顺序即优先级：
 *            `qspi` 比 `rej` 比 `hw` 重要），所以拿到的永远是完整内容的前缀。
 *          - `rej` 是最近一次被 FTL 在受理时就拒绝的请求的结果（例如卷故障后的 `NOT_READY`），没有则省略；
 *            `hw` 是失败时芯片后端的错误码和端口状态（十六进制），都为 0 则省略。
 ******************************************************************************
 */

#ifndef BACKGROUND_FLASH_DIAG_H
#define BACKGROUND_FLASH_DIAG_H

#include <stddef.h>

#include "Platform/flash/platform_flash.h"

/**
 * @brief  FTL 失败分类的短名，写进日志。
 * @param[in] failure 分类。
 * @return 静态字符串：`none`、`INVALID`、`NOT_READY`、`NO_SPACE`、`IO_ERROR`、`CORRUPT`、`INCOMPAT`、`INCOMPL`、`UNFORMAT`、`OTHER`。
 */
const char *background_flash_diag_failure_name(Platform_Flash_FailureTypeDef failure);

/**
 * @brief  失败时 FTL 请求类别的短名，写进日志。
 * @param[in] operation 请求类别。
 * @return 静态字符串：`-`、`open`、`format`、`read`、`write`、`sync`、`reclaim`。
 */
const char *background_flash_diag_operation_name(Platform_Flash_FailedOperationTypeDef operation);

/**
 * @brief  读 Platform 的失败诊断，格式化成日志片段并追加到缓冲末尾（格式见文件头）。
 * @param[in,out] buffer 缓冲，已含前面的文字（以 '\0' 结尾）。
 * @param[in] size 缓冲容量，含结尾 NUL；放不下的段整段丢掉（不留半截字段），结果始终以 '\0' 结尾。
 * @param[in] extra 调用者的额外文字，插在必有部分与 `qspi`、`rej`、`hw` 之间，要自带开头的空格，整段追加；可为 NULL。
 * @note   Platform 读不到（FTL 还没绑定）时追加 ` diag=n/a`，同样带上额外文字。
 */
void background_flash_diag_append(char *buffer, size_t size, const char *extra);

#endif /* BACKGROUND_FLASH_DIAG_H */
