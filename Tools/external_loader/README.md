# W25Q256JV STM32H743ZG 外部烧录算法

此目录提供可由 STM32CubeProgrammer 加载的 W25Q256JV 外部 QSPI NOR 烧录算法。它以官方 `STM32H743I-EVAL` 外部 loader 的 ABI、RAM 链接布局和 HAL 使用方式为参考，但按本工程实际硬件重写：STM32H743ZG、25 MHz HSE、PE2/PF6/PF8/PF9/PF10/PG6，以及单颗 32 MiB W25Q256。

## 能力与边界

- `Init()` 将核心配置为 480 MHz、D1HCLK 为 240 MHz；QSPI 使用分频 1，即 120 MHz。
- 启动时复位 NOR、检查 JEDEC `EF xx 19`，并在必要时设置 SR2.QE。
- `Write()` 用 `0x34` 以四线数据编程，自动按 256 B 页拆分并等待 WIP 清零。
- `SectorErase()` 用 `0x21` 擦除覆盖请求闭区间的全部 4 KiB 扇区；`MassErase()` 用 `0xC7` 擦除全片。
- `StorageInfo` 声明地址窗口 `0x90000000–0x91FFFFFF`、容量 32 MiB、页 256 B、扇区 4 KiB。
- 擦写时临时退出内存映射，完成后恢复 `0xEC` 四线读映射，供 CubeProgrammer 的 `CheckSum()` 与 `Verify()` 读取。
- `SystemInit()` 后先按 64 MHz HSI 建立 DWT 毫秒时基，PLL 切换完成后再按 480 MHz 重新建立换算基准，避免 HAL 在等待 HSE/PLL 时因虚假 tick 提前超时。

它是独立的烧录工具，不使用主固件的 FreeRTOS、FTL、Service 或 MDMA 异步路线。它会访问整片物理 Flash；加载到 CubeProgrammer 后应谨慎选择擦写范围，避免覆盖既有分区。

`.stldr` 不是需要永久烧进 STM32 内部 Flash 的固件。CubeProgrammer 在每次外部存储操作前把它临时下载到 MCU 的 AXI SRAM，调用 `Init()` 建立 QSPI 映射，再调用擦除、写入和校验入口。主固件仍单独烧录到 `0x08000000`。

## 构建

在本目录执行：

```powershell
cmake -S . -B build/arm-gcc -G Ninja "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=$PWD/cmake/arm-none-eabi-gcc.cmake"
cmake --build build/arm-gcc
```

地址范围的主机单元测试不参与交叉编译，可单独执行：

```powershell
gcc -std=c11 -Wall -Wextra -Werror -I include tests/loader_geometry_test.c src/loader_geometry.c -o build/loader_geometry_test
.\build\loader_geometry_test.exe
```

输出文件为 `build/arm-gcc/W25Q256_STM32H743ZG.stldr`。这是带 `.info` 段的 ELF loader；代码和可写数据链接至 AXI SRAM `0x24000004`，存储描述表位于 `0x00000000`，符合 STM32CubeProgrammer 外部 loader 约定。

## CubeProgrammer 使用

1. 完全退出 STM32CubeProgrammer。
2. 把 `build/arm-gcc/W25Q256_STM32H743ZG.stldr` 复制到 CubeProgrammer 安装目录的 `bin/ExternalLoader/`。
3. 重新打开 CubeProgrammer，用 ST-LINK/SWD 连接目标板。
4. 打开 **External loaders**，勾选 `W25Q256_STM32H743ZG`；右侧连接信息应显示该 External loader。
5. 在 **Memory & File editing** 中读取 `0x90000000`、长度 `0x10`。能读出数据或全 `0xFF`，表示 Loader 初始化和内存映射成功。
6. 烧录文件时填写外部映射地址，勾选写后校验，不使用 **Full chip erase**，也不使用 **Run after programming**。

CubeProgrammer 会缓存已经装载的算法。替换 `.stldr` 后必须重启 CubeProgrammer，仅覆盖磁盘文件不足以让当前会话使用新版本。

### 当前资源包的已验证烧录参数

| 项目 | 值 |
| --- | --- |
| 文件 | `../../build/external-resources/resource_pack.bin` |
| 映射起始地址 | `0x90401000` |
| 芯片物理偏移 | `0x00401000` |
| 文件长度 | `0x65000`，即 413696 B |
| 映射结束地址（含） | `0x90465FFF` |
| 映射结束地址（不含） | `0x90466000` |
| 文件头魔数 | `52 50 4B 31`，即 `RPK1` |

烧录完成后，从 `0x90401000` 回读应得到以下资源头摘要：

| 资源 | 长度 | CRC32 |
| --- | ---: | --- |
| `uni2oem` | 87172 B | `0xFBAAB4D2` |
| `oem2uni` | 87172 B | `0x60F7F8F0` |
| 默认壁纸 | 230400 B | `0xBBB21D5D` |

CubeProgrammer 的 32 位数据显示为小端整数，因此文件开头可能显示成 `314B5052`；内存中的实际字节仍为 `52 50 4B 31`。本资源包已经完成实际硬件烧录和回读验证。

### 读取失败排查

若内部 Flash `0x08000000` 可以读取，而外部窗口 `0x90000000` 报 `Data read failed`，按以下顺序检查：

1. 确认 External loader 已勾选，右侧连接信息显示 `W25Q256_STM32H743ZG`。
2. 若刚替换 `.stldr`，完全退出并重启 CubeProgrammer。
3. 确认使用 ST-LINK/SWD；普通 ROM DFU 不会执行这个 SWD External Loader。
4. 把日志详细度调到 3，区分连接失败与 Loader `Init()` 返回失败。
5. 检查启动时基是否在 `HAL_RCC_OscConfig()` 前已经建立。错误地让 `HAL_GetTick()` 每调用一次就增加 1，会把 HSE 的毫秒超时压缩成极短的循环次数，导致 Loader 无法进入 QSPI 初始化。

## 第三方来源

`third_party/` 中的 CMSIS 和 STM32H7 HAL 文件复制自 STMicroelectronics `stm32-memory-loaders` 的 `MT25TL01G_STM32H743I-EVAL` 官方例程，保留原始文件头中的 BSD-3-Clause 许可说明。`src/`、`include/`、构建脚本和本文档是本工程的自维护代码，注释遵循母工程中文规范。
