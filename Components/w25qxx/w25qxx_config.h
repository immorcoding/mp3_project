/**
  ******************************************************************************
  * @file    w25qxx_config.h
  * @brief   W25Qxx Device 的私有协议与时序参数。
  ******************************************************************************
  */

#ifndef W25QXX_CONFIG_H
#define W25QXX_CONFIG_H

/* JEDEC ID 命令返回制造商、存储器类型和容量代码三个字节。 */
#define W25QXX_COMMAND_READ_JEDEC_ID          0x9Fu
#define W25QXX_JEDEC_ID_LENGTH                3u

/* SFDP 头从地址 0 以 24-bit 地址和 8 个 dummy cycle 读取。 */
#define W25QXX_COMMAND_READ_SFDP              0x5Au
#define W25QXX_SFDP_ADDRESS                   0u
#define W25QXX_SFDP_ADDRESS_LENGTH            3u
#define W25QXX_SFDP_DUMMY_CYCLES              8u
#define W25QXX_SFDP_SIGNATURE_LENGTH          4u
#define W25QXX_SFDP_SIGNATURE_BYTE_0          ((uint8_t)'S')
#define W25QXX_SFDP_SIGNATURE_BYTE_1          ((uint8_t)'F')
#define W25QXX_SFDP_SIGNATURE_BYTE_2          ((uint8_t)'D')
#define W25QXX_SFDP_SIGNATURE_BYTE_3          ((uint8_t)'P')

/* W25Q256JV 固定 4-byte 地址的 Quad I/O 读与 Quad 页编程命令。 */
#define W25QXX_COMMAND_FAST_READ_QUAD_IO_4BYTE  0xECu
#define W25QXX_COMMAND_QUAD_PAGE_PROGRAM_4BYTE  0x34u
#define W25QXX_ARRAY_ADDRESS_LENGTH              4u
#define W25QXX_FAST_READ_QUAD_IO_MODE_BYTE       0xFFu
#define W25QXX_FAST_READ_QUAD_IO_DUMMY_CYCLES    4u

/* W25Q256JV 各数据手册修订版的 tPP 上限可达 4 ms；保留 1 ms 调度测量裕量。 */
#define W25QXX_PAGE_PROGRAM_TIMEOUT_MS           5u

/* 状态寄存器读取命令及当前 W25Q 系列使用的位定义。 */
#define W25QXX_COMMAND_READ_STATUS_REGISTER_1 0x05u
#define W25QXX_COMMAND_READ_STATUS_REGISTER_2 0x35u
#define W25QXX_COMMAND_WRITE_ENABLE            0x06u
#define W25QXX_COMMAND_WRITE_STATUS_REGISTER_2 0x31u
#define W25QXX_STATUS_REGISTER_1_WIP_MASK     0x01u
#define W25QXX_STATUS_REGISTER_1_WEL_MASK     0x02u
#define W25QXX_STATUS_REGISTER_2_QE_MASK      0x02u

/* W25Q256JV tW 最大 15 ms；启动期 SR2 写入以 20 ms 为有界轮询上限。 */
#define W25QXX_STATUS_REGISTER_WRITE_TIMEOUT_MS 20u

#endif /* W25QXX_CONFIG_H */
