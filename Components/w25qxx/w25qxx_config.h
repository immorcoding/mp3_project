/**
  ******************************************************************************
  * @file    w25qxx_config.h
  * @brief   W25Qxx Device 的私有协议与时序参数。
  ******************************************************************************
  */

#ifndef W25QXX_CONFIG_H
#define W25QXX_CONFIG_H

/* w25qxx.h */
#define W25QXX_MANUFACTURER_ID_WINBOND            0xEFu           /* JEDEC 制造商：Winbond。 */
#define W25QXX_CAPACITY_ID_1MBIT                  0x11u           /* JEDEC 容量码：1 Mbit。 */
#define W25QXX_CAPACITY_ID_2MBIT                  0x12u           /* JEDEC 容量码：2 Mbit。 */
#define W25QXX_CAPACITY_ID_4MBIT                  0x13u           /* JEDEC 容量码：4 Mbit。 */
#define W25QXX_CAPACITY_ID_8MBIT                  0x14u           /* JEDEC 容量码：8 Mbit。 */
#define W25QXX_CAPACITY_ID_16MBIT                 0x15u           /* JEDEC 容量码：16 Mbit。 */
#define W25QXX_CAPACITY_ID_32MBIT                 0x16u           /* JEDEC 容量码：32 Mbit。 */
#define W25QXX_CAPACITY_ID_64MBIT                 0x17u           /* JEDEC 容量码：64 Mbit。 */
#define W25QXX_CAPACITY_ID_128MBIT                0x18u           /* JEDEC 容量码：128 Mbit。 */
#define W25QXX_CAPACITY_ID_256MBIT                0x19u           /* JEDEC 容量码：256 Mbit。 */
#define W25QXX_PAGE_PROGRAM_MAX_SIZE_BYTES        256u            /* Quad Input Page Program 单页上限。 */
#define W25QXX_SECTOR_ERASE_SIZE_BYTES            (4u * 1024u)    /* 4-byte Sector Erase 粒度。 */
#define W25QXX_QUAD_READ_ADDRESS_ALIGNMENT_BYTES  4u              /* 0xEC Quad I/O Read 起始地址对齐。 */

/* w25qxx.c */
#define W25QXX_COMMAND_READ_JEDEC_ID              0x9Fu           /* 读取 3 字节 JEDEC ID。 */
#define W25QXX_JEDEC_ID_LENGTH                    3u              /* JEDEC ID 返回字节数。 */

#define W25QXX_COMMAND_READ_SFDP                  0x5Au           /* 以 24-bit 地址和 8 个 dummy cycle 读取 SFDP。 */
#define W25QXX_SFDP_ADDRESS                       0u              /* SFDP 头起始地址。 */
#define W25QXX_SFDP_ADDRESS_LENGTH                3u              /* SFDP 地址字节数。 */
#define W25QXX_SFDP_DUMMY_CYCLES                  8u              /* SFDP 读取的 dummy cycle 数。 */
#define W25QXX_SFDP_SIGNATURE_LENGTH              4u              /* SFDP 签名字节数。 */
#define W25QXX_SFDP_SIGNATURE_BYTE_0              ((uint8_t)'S')  /* SFDP 签名第 1 字节。 */
#define W25QXX_SFDP_SIGNATURE_BYTE_1              ((uint8_t)'F')  /* SFDP 签名第 2 字节。 */
#define W25QXX_SFDP_SIGNATURE_BYTE_2              ((uint8_t)'D')  /* SFDP 签名第 3 字节。 */
#define W25QXX_SFDP_SIGNATURE_BYTE_3              ((uint8_t)'P')  /* SFDP 签名第 4 字节。 */

#define W25QXX_COMMAND_FAST_READ_QUAD_IO_4BYTE    0xECu           /* W25Q256JV 固定 4-byte 地址的 Quad I/O 快读。 */
#define W25QXX_COMMAND_QUAD_PAGE_PROGRAM_4BYTE    0x34u           /* W25Q256JV 固定 4-byte 地址的 Quad 页编程。 */
#define W25QXX_COMMAND_SECTOR_ERASE_4BYTE         0x21u           /* W25Q256JV 固定 4-byte 地址的 4 KiB 擦除。 */
#define W25QXX_ARRAY_ADDRESS_LENGTH               4u              /* 数组访问使用的地址字节数。 */
#define W25QXX_FAST_READ_QUAD_IO_MODE_BYTE        0xFFu           /* 0xEC 传输的 mode byte。 */
#define W25QXX_FAST_READ_QUAD_IO_DUMMY_CYCLES     4u              /* 0xEC 读取的 dummy cycle 数。 */

#define W25QXX_ARRAY_READ_TIMEOUT_MS              100u            /* 4 KiB 0xEC 间接读取在 MDMA 路径中等待完成事件的有界上限。 */
#define W25QXX_PAGE_PROGRAM_TIMEOUT_MS            5u              /* tPP 上限可达 4 ms；另留 1 ms 调度测量裕量。 */
#define W25QXX_SECTOR_ERASE_TIMEOUT_MS            500u            /* 4 KiB tSE 最大 400 ms；另留 100 ms 裕量。 */

#define W25QXX_COMMAND_READ_STATUS_REGISTER_1     0x05u           /* 读取状态寄存器 1。 */
#define W25QXX_COMMAND_READ_STATUS_REGISTER_2     0x35u           /* 读取状态寄存器 2。 */
#define W25QXX_COMMAND_WRITE_ENABLE               0x06u           /* 写使能。 */
#define W25QXX_COMMAND_WRITE_STATUS_REGISTER_2    0x31u           /* 写入状态寄存器 2。 */
#define W25QXX_STATUS_REGISTER_1_WIP_MASK         0x01u           /* SR1 WIP 位掩码。 */
#define W25QXX_STATUS_REGISTER_1_WEL_MASK         0x02u           /* SR1 WEL 位掩码。 */
#define W25QXX_STATUS_REGISTER_2_QE_MASK          0x02u           /* SR2 QE 位掩码。 */
#define W25QXX_STATUS_REGISTER_WRITE_TIMEOUT_MS   20u             /* tW 最大 15 ms；启动期 SR2 写入以 20 ms 为有界轮询上限。 */

#endif /* W25QXX_CONFIG_H */
