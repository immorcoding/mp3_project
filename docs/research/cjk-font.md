# CJK 字体：存放位置、尺寸与加载方式的事实

- 票：[#101](https://github.com/immorcoding/mp3_project/issues/101)（wayfinder:research），父图 [#100](https://github.com/immorcoding/mp3_project/issues/100)
- 调研日期：2026-10-21
- 代码基线：`origin/main` 的 `c41b61f`。被测代码为 `575193a`，二者之间只差 `docs/shape/workflow.md` 两行，不影响结论。
- 工具与版本：LVGL v8.3.11（`Middlewares/Third_Party/LVGL/lvgl.h:16–18`）；arm-none-eabi-gcc 15.2.1、CMake、Ninja（构建输出）；lv_font_conv 1.5.3（npm，发布于 2024-06-10）；Node 24.21.0；Python 3.12.7；字体为 Noto Sans CJK SC Regular OTF（`name` 表：Adobe 2014–2021，版本 2.004，SIL Open Font License 1.1；SHA-256 `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`，取自 `notofonts/noto-cjk`）；W25Q256JV 手册为 Revision I（2018-10-23，SparkFun 托管的副本）。
- 适用：[ADR-0009](../adr/0009-w25q256-firmware-slots-and-diagnostic-reservation.md)、[存储介质事实](../shape/sources/storage-media.md)、[Resources](../shape/resources.md)。
- 范围：只陈述事实与估算，不选择字号、字集、存放方式，不修改 ADR、shape 规则、代码或 `lv_conf.h`。

证据标注：【源码】给出文件与行号；【实测】本次构建、编译或转换得到的数值；【手册】数据手册，附版次与页码；【推断】由上述事实推出，未直接测得；【未核实】查不到一手来源，或未运行。

## 1. 结论摘要

1. **片内 Flash**：Release 镜像占用 556,728 B（FLASH 340,320 B、FLASH2 216,408 B，合计占 1 MiB 的 53.1%）；Debug 占用 730,332 B（413,844 B、316,488 B，占 69.6%）。两 bank 合计剩余 491,848 B（Release）与 318,244 B（Debug）。可回滚镜像槽是 NOR 中固定的 2 MiB 分区，镜像头格式尚未设计。`objcopy` 生成的 BIN 因跨越两 bank 之间的地址空隙，文件大小为 1,264,984 B（Release）与 1,365,064 B（Debug），不是紧凑载荷。
2. **NOR 余量与映射读耗时**：资源包 v2 占用 `0x00401000–0x00477FFF`（487,424 B）。默认 24 MiB FTL 时，资源包之后到 FTL 基址 `0x007FF000` 之前剩余 3,698,688 B（3,612 KiB）。仓库内没有映射窗口随机读的实测数据。按手册时序与 120 MHz SCK 估算，单次突发读 119 B（全部汉字 16 px 4 bpp 的平均字形）约 2.2 µs；若每字节一次非缓存事务，下限约 24 µs。
3. **LVGL 外部字体方式**：C 数组在 8.3.11 中可用，不需要改 `lv_conf.h`，额外 RAM 为 0（只读常量）。`lv_font_load` 的源码存在，但当前配置没有任何文件系统驱动（`LV_USE_FS_*` 全为 0），且整段位图会一次读入 LVGL 堆（`LV_MEM_SIZE` = 256 KiB）。tiny_ttf 的源码存在，需 `LV_USE_TINY_TTF = 1`。FreeType 的适配源码存在，但仓库没有 FreeType 库本体，还需 `LV_USE_FREETYPE = 1`。
4. **字集尺寸**：lv_font_conv 1.5.3 实测了 27 组（3 个字集 × 3 个字号 × 3 个 bpp）的 bin 与 C 输出（§3.4）。例：全部汉字 16 px 4 bpp，bin 为 881,296 B，C 数组的 flash 占用为 881,385 B，`lv_font_load` 堆估算为 897,268 B。LVGL 堆为 256 KiB，全部汉字在 16 px 的任何 bpp、以及 12/14 px 的 2 bpp 及以上档位，堆估算都超出上限；GB2312 一级在 16 px 的 2 bpp 及以上档位也超出。
5. **模拟器**：C 数组可以用同一份 `.c` 编入模拟器，因为 `sim_lv_conf.h` 包装的是同一个产品 `lv_conf.h`；但需要在模拟器工程中新增源文件。bin 经 `lv_font_load` 不能直接在模拟器中加载：模拟器没有 `lv_fs` 驱动，也不读取 NOR 资源包，它只 `#include` 图像 C 数组。

## 2. 方法

- **构建**：用 `cmake --preset firmware-release` / `firmware-debug` 与 `cmake --build --preset ...` 生成 `build/firmware-{release,debug}/mp3_project.{elf,bin,map}`（`build/` 已被 git 忽略）。这两条命令与 `scripts/build-firmware.ps1` 的步骤等效。该脚本本身在本机的 Windows PowerShell 5.1 下以退出码 1 结束：CMake 的 `Build type:` 信息写到 stderr（`CMakeLists.txt:32`），而脚本设置了 `$ErrorActionPreference = 'Stop'`，据此推断 stderr 被当作错误（【推断】）。本次未修改脚本。
- **字形**：lv_font_conv 1.5.3，选项 `--font <Noto OTF> -r 0x20-0x7E --symbols <字表> --size {12,14,16} --bpp {1,2,4} --no-compress --format {bin,lvgl}`。字表由 CPython 标准库的 `gb2312` 编解码器生成（【实测】：一级 3755、全部汉字 6763、符号 682，与 GB2312 标准的计数一致）。
- **编译**：C 输出用 `build/firmware-release/compile_commands.json` 中一条 LVGL 源文件的编译选项（`-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -DLV_CONF_INCLUDE_SIMPLE -DSTM32H743xx`，`-fdata-sections -ffunction-sections`，实际生效的优化为 `-O3`，因为 `CMAKE_C_FLAGS_RELEASE` 的 `-O3` 出现在 `-Os` 之后），额外加 `-DLV_LVGL_H_INCLUDE_SIMPLE`（只影响头文件查找）。用 `arm-none-eabi-size` 量取 text、data、bss。编译在会话 scratchpad 中进行，仓库未写入。
- **bin 解析**：按 `lv_font_loader.c:86–585` 的读取顺序解析 27 个 bin（`head` 48 B、`cmap`、`loca`、`glyf`、`kern`，标签全部吻合）；统计字形条目数、位图字节数与加载器的堆分配（【推断】，见 §3.4）。

## 3. 逐条回答

### 3.1 片内 Flash 占用与镜像槽约束

| 项 | Release | Debug | 依据 |
| --- | ---: | ---: | --- |
| FLASH（bank 1，`0x08000000`，512 KiB） | 340,320 B（64.9%） | 413,844 B（78.9%） | 【实测】构建输出中的内存区占用报告 |
| FLASH2（bank 2，`0x08100000`，512 KiB） | 216,408 B（41.3%） | 316,488 B（60.4%） | 同上 |
| 两 bank 合计占用 / 剩余 | 556,728 B / 491,848 B | 730,332 B / 318,244 B | 【推断】= 1,048,576 − 占用 |
| 分 bank 剩余 | 183,968 B（bank 1）+ 307,880 B（bank 2） | 110,444 B（bank 1）+ 207,800 B（bank 2） | 【推断】 |
| `arm-none-eabi-size` text / data / bss | 556,016 / 700 / 7,611,744 | 729,532 / 788 / 7,611,680 | 【实测】 |
| BIN 文件大小 | 1,264,984 B | 1,365,064 B | 【实测】`build/firmware-*/mp3_project.bin` |

- 两 bank 由链接脚本定义：`FLASH` 512K 位于 `0x8000000`，`FLASH2` 512K 位于 `0x8100000`（`STM32H743xG_flash.ld:64–65`）。芯片型号为 STM32H743ZG（`Tools/external_loader/README.md:1`）。`.lvgl_flash2` 段放在 bank 2（`STM32H743xG_flash.ld:155–167`）。
- BIN 不是紧凑载荷：`1,264,984 = 0x100000 + 216,408`，即 `objcopy -O binary` 覆盖了 `0x08000000–0x080FFFFF` 整个窗口，其中包括 bank 1 尾部未用区和两 bank 之间 512 KiB 的空隙。空隙的地址范围依据 RM0433 的 bank 地址表，【未核实】：目前只找到论坛转引（见 §4）。
- 镜像槽：rollback 与 candidate 各为 2 MiB 的固定原始分区（ADR-0009；`docs/shape/sources/storage-media.md` 表）。镜像头、完整性与试运行状态的协议由后续 Bootloader 设计决定（ADR-0009 后果段）。GLOSSARY 将固件镜像槽定义为"容纳一份完整固件及镜像头的固定原始分区"。因此，镜像加头部的总长不能超过 2,097,152 B。当前两种 BIN（约 1.21 MiB、1.30 MiB）在数值上低于该上限，但含有空隙；紧凑载荷（556,728 B 与 730,332 B）的 Debug 值与 ADR-0009 的"约 700 KiB"相符。
- 若把镜像写入内部 App 区，可用空间为 1 MiB（两 bank 合计）。按 BIN 原样从 `0x08000000` 连续写入时，`0x08080000–0x080FFFFF` 的空隙会落在该区域之外（【推断】）。

### 3.2 NOR 余量与映射读耗时

地址表（默认 FTL 24 MiB；`Platform/flash/platform_flash_config.h:18–29`，ADR-0009，`docs/shape/sources/storage-media.md`）：

| 物理地址（含端点） | 用途 | 大小 |
| --- | --- | ---: |
| `0x00000000–0x00000FFF` | 首自检区 | 4 KiB |
| `0x00001000–0x00200FFF` | rollback 槽 | 2 MiB |
| `0x00201000–0x00400FFF` | candidate 槽 | 2 MiB |
| `0x00401000–0x00477FFF` | 资源包 v2（长度 `0x77000`） | 487,424 B |
| `0x00478000–0x007FEFFF` | 未分配（FTL 基址之前） | 3,698,688 B（【推断】） |
| `0x007FF000–0x01FFEFFF` | FTL 物理分区（`PLATFORM_FLASH_FTL_SIZE_BYTES` = 24 MiB） | 24 MiB |
| `0x01FFF000–0x01FFFFFF` | 尾自检区 | 4 KiB |

- **资源包**：`SERVICE_RESOURCE_FLASH_OFFSET = 0x00401000`（`Service/resource/resource_service_config.h:12`）。打包测试断言长度为 `0x77000`，映射地址为 `0x90401000`（`Tools/package_maker/tests/test_rpkc_pack.py:255、282`）。Issue #8 记录 Pack v2 于 2026-09-11 写入板上，之后未重写（【实测】，在板记录）。
- **资源区上限**：`SERVICE_RESOURCE_MAXIMUM_SIZE = 0x003FE000`，即 4,186,112 B（`resource_service_config.h:13`）。
- **资源包内容**：按 `resource_service_config.h:17–20` 的 ID 表，当前产品资源为 ID 1–4（两张 CP936 转码表、默认壁纸、唱盘底图），服务层没有字体的加载路径。载荷字节为 87,172 × 2 + 230,400 + 62,208 = 466,952 B；32 B 对齐后为 467,008 B，与 ELF 的 `.sdram_external_resources` 段大小一致（【实测】）。包内其余 20,416 B（487,424 − 467,008）为包头、索引与元数据等（【推断】，未逐字段核对）。
- **FONT 类型**：RPKC1 扩展定义了 FONT V1，载荷为可重定位序列化字体，不含 MCU 指针（`docs/shape/sources/rpkc1-extensions.md:9、20–28`）。资源包配置中 FONT 默认关闭，解码返回 `RESOURCE_PACK_UNSUPPORTED_TYPE`（`Components/resource_pack/README.md:19`）。`docs/shape/resources.md:16` 把 FONT 解码器何时启用列为待决问题。
- **编译期断言**：FTL 容量须为 4 KiB 整数倍、不小于 128 KiB，且不超过 `TAIL − FTL_MIN_ADDRESS`，即 `0x01FFF000 − 0x00401000 = 0x1BFE000`（`platform_flash_config.h:32–36`）。断言没有计入资源包长度，所以把 FTL 容量加大时，FTL 基址可以低至 `0x00401000`，与资源包区重叠，编译期不报错（【源码】推断）。
- **映射窗口**：`0x90000000–0x91FFFFFF`，共 32 MiB，只读（`platform_flash_config.h:24–25`；`Platform/flash/README.md:11、31`）。只在 WIP 为 0 且无在飞操作时打开；映射模式下不会等待 WIP。
- **读命令**：`0xEC`，32 位地址，1-4-4，模式字节 `0xFF`，4 个 dummy clock（`Components/w25qxx/w25qxx_config.h:40`；`Components/w25qxx/README.md:31`）。手册：EBh 明确要求 4 个 dummy clock，模式字节 M7–M0 应设为 `Fxh`（【手册】W25Q256JV Rev I，§8.2.20，第 47 页）。ECh 的时序图为模式字节之后两段 Dummy（四线下共 4 个时钟，按图推断），模式字节同样为 `Fxh`（§8.2.21，第 48 页，Figure 28）。
- **时钟**：HSE 25 MHz（`Core/Inc/stm32h7xx_hal_conf.h:109`）；PLL1 的 M=5、N=192、P=2 得 SYSCLK 480 MHz；AHB 分频 2 得 D1HCLK 240 MHz（`Core/Src/main.c:168–170、188`）。QSPI 内核时钟取 D1HCLK（`Core/Src/quadspi.c:74`），`ClockPrescaler = 1`（`quadspi.c:42`），由此 SCK = 120 MHz（【推断】：SCK = 内核时钟 / (预分频 + 1)）。
- **手册频率上限**：AC 特性表 §9.6（第 81 页）中，除 03h 与 DTR 外指令的 fC 为 133 MHz，对应 VCC 3.0–3.6 V 行（【手册】；§9.2 工作范围表也有 `FR = 133MHz` 的 3.0–3.6 V 条目）。pdftotext 的版式错乱，最终以原页为准。120 MHz 低于此上限。NOR 实际供电未在仓库中核实（【未核实】）。
- **随机读估算**（【推断】：按手册协议周期计算，不含控制器与 AHB 开销）：
  - 首字节前开销：命令 8 个时钟（单线）+ 地址 8 个时钟（四线）+ 模式 2 个时钟（四线）+ dummy 4 个时钟 = 22 个时钟，约 183 ns（@120 MHz）。
  - 每个数据字节 2 个时钟（四线），约 16.7 ns。
  - 突发读 32 B 约 0.70 µs；突发读 119 B（全部汉字 16 px 4 bpp 的平均字形，815,921 B / 6,859 条目 ≈ 119 B）约 2.15 µs。
  - 若每字节一次事务（非缓存访问），每字节约 24 个时钟，约 200 ns；119 B 的下限约 24 µs。
- **缓存属性**：`Core/Src/main.c:243–266` 的 `MPU_Config` 只配置了 `0xC0000000` 的 SDRAM 区（第 254 行）。映射窗口没有专门的 MPU 区域，其缓存属性由默认内存映射决定，【未核实】（ARM 文档页未取到表格）。因此上面的"非缓存"情形只是下限。
- **已有数据**：仓库的存储基准 `Bench memory-mapped read` 会顺序读 1 MiB 并输出吞吐（`APP/tasks/storage/benchmark/README.md:11`），但没有记录的数值；Issue #8 也没有。随机字形读取的耗时没有实测。

### 3.3 LVGL 8.3.11 外部字体方式、RAM 与 lv_conf.h

LVGL 版本为 8.3.11（`lvgl.h:16–18`）。字体相关源码位于 `Middlewares/Third_Party/LVGL/src/`。

| 方式 | 8.3.11 源码 | 数据位置 | 额外 RAM（LVGL 堆） | `lv_conf.h` 需要改动 |
| --- | --- | --- | --- | --- |
| C 数组（`lv_font_fmt_txt`，无压缩） | 默认支持 | 内部 Flash 的 `.rodata`，或映射区（需要链接段与烧录路径，仓库未提供） | 0（`const` 数据，只读指针） | 不需要。`LV_FONT_FMT_TXT_LARGE = 0`（`lv_conf.h:407`）时，单字体位图上限为 1 MiB（`lv_font_fmt_txt.h:31–32`） |
| `lv_font_load`（bin，文件系统） | `lv_font_loader.c:86–111` | 文件，经 `lv_fs` 驱动读取 | 整段位图一次读入堆（`lv_font_loader.c:417–445`），外加字形表 8 B/条目、cmap、kern，以及加载期临时数组 | 编译无需改动；但 `LV_USE_FS_*` 全为 0（`lv_conf.h:612–646`），`lv_fs_open` 返回 `LV_FS_RES_NOT_EX`（`lv_fs.c:57–69`）。注册自定义驱动可不改 lv_conf（`lv_fs_drv_register` 没有编译守卫，`lv_fs.c:398`）。堆不足需改 `LV_MEM_SIZE`（`lv_conf.h:52`）或改用 `LV_MEM_CUSTOM` |
| tiny_ttf（TTF 数据指针） | `src/extra/libs/tiny_ttf/lv_tiny_ttf.c` | TTF 本体位于可寻址内存（映射区或 SDRAM） | LRU 字形缓存，总量为 `cache_size`，`lv_tiny_ttf_create_data` 默认 4096 B（`lv_tiny_ttf.c:251–253`，`lv_lru.c:83–86`）；每个字形的 8 bpp 位图单独 `lv_mem_alloc`（`lv_tiny_ttf.c:125、158`） | 需要 `LV_USE_TINY_TTF = 1`（`lv_conf.h:683`，当前为 0）；数据源用指针时 `LV_TINY_TTF_FILE_SUPPORT = 0`（`lv_conf.h:686`） |
| FreeType | `src/extra/libs/freetype/lv_freetype.c`（包含 `ft2build.h`，第 12 行） | TTF 本体 | 缓存 `LV_FREETYPE_CACHE_SIZE` 默认 16 KiB（`lv_conf.h:669`）；库内部占用未测 | 需要 `LV_USE_FREETYPE = 1`（`lv_conf.h:666`）及 FreeType 库本体；仓库内检索不到 `ft2build.h`（【源码】检索结果） |

补充事实：
- `lv_font_loader.c` 中多数 `lv_mem_alloc` 的返回值没有判空，例如第 347、417、486、535 行。`lv_mem_alloc` 失败时只返回 NULL 并打一条 INFO 日志（`lv_mem.c:125–146`）。`lv_font_loader.c` 中没有 `LV_ASSERT` 调用。因此堆耗尽时，加载器不会让 `lv_font_load` 返回 NULL，而是在后续访问中解引用空指针；断言处理函数 `while(1)`（`lv_conf.h:275`）也不会被调用。
- 压缩字体：`LV_USE_FONT_COMPRESSED = 0`（`lv_conf.h:410`）。压缩字形在该配置下返回 NULL 并记录警告（`lv_font_fmt_txt.c:130–132`）。因此 lv_font_conv 的默认输出（RLE 压缩）在当前配置下不可用，本文全部数据使用 `--no-compress`。
- LVGL 自带字体 `lv_font_simsun_16_cjk.c`（16 px、4 bpp、1,414 条目）在 `lv_conf.h:390` 中关闭。在副本中把守卫改为 1 后编译（只为测量，仓库未改动）：字形位图 151,609 B，字形表 11,312 B，代码段合计 165,544 B（【实测】）。
- 现有字体对照：`lv_font_montserrat_10/12/14/16/48` 的目标文件代码段分别为 9,350 / 11,472 / 13,633 / 15,891 / 96,713 B，合计 147,059 B（【实测】，`build/firmware-release/` 中的 LVGL 目标文件）。`lv_font_load` 未出现在 release 映像的 map 中（0 次命中）。

### 3.4 字集大小（lv_font_conv 1.5.3 实测）

- **字集**：`gb2312_level1_3755`（一级 3755 字）、`gb2312_all_hanzi_6763`（全部 6763 字）、`gb2312_level1_first2500_proxy`（一级前 2500 字，代理）。每组额外含 ASCII `0x20–0x7E`（95 字）和 notdef；GB2312 的 682 个符号（第 1–9 区，全角标点等）未计入。
- **"常用 2500 字"**：仓库中没有该字表的定义。本文用一级前 2500 字（区位码顺序，即拼音顺序，不是频度表）作代理。字节数随字形条目数近似线性变化，因此换成真正的常用字表，数值量级相同，但具体字节数会不同（【推断】）。
- **字体**：Noto Sans CJK SC Regular，选项为 lv_font_conv 默认值，外加 `--no-compress`。所有组都带 kern 表，871 对，4,372 B（格式 0），来自字体的 ASCII 与标点字距。
- **字体敏感度**（【实测】）：同一字集用系统字体 SimHei 生成。16 px 4 bpp 全部汉字的 bin 为 880,160 B（相对 Noto 差 −0.13%）；12 px 1 bpp 为 163,788 B（差 −6.4%）。
- **堆估算**（【推断】）：`lv_font_load` 堆估算 = 字形表（8 B × 条目）+ 位图合计 + cmap 数组 + kern 数组。不含 `lv_font_t`、`lv_font_fmt_txt_dsc_t` 等结构体（约 100 B 量级）、cmap 结构数组、TLSF 块头，也不含加载期的临时数组（`glyph_offset` 为 4 × (条目 + 1) B，加载完成后释放）。峰值因此比表中的值高出约 27 KB（全部汉字，4 × 6,860 B）。
- **对 256 KiB 的关系**：`LV_MEM_SIZE` = 262,144 B（`lv_conf.h:52`）。表中"超出"表示堆估算超过该值，"内"表示未超过，但堆中还有其他分配，余量未测。

| 字集 | px | bpp | 条目 | bin（B） | C 数组 flash（B） | lv_font_load 堆估算（B） | 对 256 KiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 全部汉字 6763 | 12 | 1 | 6,859 | 174,916 | 180,597 | 190,889 | 内 |
| 全部汉字 6763 | 12 | 2 | 6,859 | 312,256 | 312,774 | 328,228 | 超出 |
| 全部汉字 6763 | 12 | 4 | 6,859 | 552,180 | 552,797 | 568,152 | 超出 |
| 全部汉字 6763 | 14 | 1 | 6,859 | 210,928 | 214,223 | 226,901 | 内 |
| 全部汉字 6763 | 14 | 2 | 6,859 | 391,520 | 392,047 | 407,492 | 超出 |
| 全部汉字 6763 | 14 | 4 | 6,859 | 708,108 | 708,197 | 724,080 | 超出 |
| 全部汉字 6763 | 16 | 1 | 6,859 | 247,172 | 250,915 | 263,145 | 超出（+1,001 B） |
| 全部汉字 6763 | 16 | 2 | 6,859 | 476,668 | 477,341 | 492,640 | 超出 |
| 全部汉字 6763 | 16 | 4 | 6,859 | 881,296 | 881,385 | 897,268 | 超出 |
| 一级 3755 | 12 | 1 | 3,851 | 99,972 | 103,035 | 111,365 | 内 |
| 一级 3755 | 12 | 2 | 3,851 | 176,336 | 176,577 | 187,728 | 内 |
| 一级 3755 | 12 | 4 | 3,851 | 309,824 | 310,117 | 321,216 | 超出 |
| 一级 3755 | 14 | 1 | 3,851 | 119,944 | 121,653 | 131,337 | 内 |
| 一级 3755 | 14 | 2 | 3,851 | 220,484 | 220,723 | 231,876 | 内 |
| 一级 3755 | 14 | 4 | 3,851 | 396,696 | 396,682 | 408,088 | 超出 |
| 一级 3755 | 16 | 1 | 3,851 | 140,188 | 142,139 | 151,581 | 内 |
| 一级 3755 | 16 | 2 | 3,851 | 268,156 | 268,460 | 279,548 | 超出 |
| 一级 3755 | 16 | 4 | 3,851 | 493,596 | 493,580 | 504,988 | 超出 |
| 常用 2500（代理） | 12 | 1 | 2,596 | 63,316 | 70,543 | 76,168 | 内 |
| 常用 2500（代理） | 12 | 2 | 2,596 | 119,792 | 119,942 | 127,451 | 内 |
| 常用 2500（代理） | 12 | 4 | 2,596 | 209,292 | 209,485 | 216,951 | 内 |
| 常用 2500（代理） | 14 | 1 | 2,596 | 76,752 | 83,103 | 89,604 | 内 |
| 常用 2500（代理） | 14 | 2 | 2,596 | 149,424 | 149,572 | 157,083 | 内 |
| 常用 2500（代理） | 14 | 4 | 2,596 | 267,604 | 267,583 | 275,263 | 超出 |
| 常用 2500（代理） | 16 | 1 | 2,596 | 95,428 | 96,722 | 103,088 | 内 |
| 常用 2500（代理） | 16 | 2 | 2,596 | 181,208 | 181,386 | 188,867 | 内 |
| 常用 2500（代理） | 16 | 4 | 2,596 | 332,204 | 332,183 | 339,863 | 超出 |

- 对照：LVGL 自带 SimSun 16 px 4 bpp（1,414 条目）的 C 数组代码段为 165,544 B。
- 内部 Flash 的粗略比较（按 bank 剩余空间，未计入现有段的对齐与布局，【推断】）：Release 的 bank 2 剩余 307,880 B、bank 1 剩余 183,968 B；Debug 的 bank 2 剩余 207,800 B、bank 1 剩余 110,444 B。全部汉字 16 px 4 bpp（881,385 B）超出两种构建的任何单个 bank 余量，也超出两 bank 的合计余量（Release 为 491,848 B）。
- 在 NOR 中：单个全部汉字 16 px 4 bpp 的 bin（881,296 B）可以放入 `0x00478000–0x007FEFFF`（3,698,688 B）。同一字集的 9 个档位全部放入则合计 3,955,044 B，超出该区域。

### 3.5 GUI 模拟器能否加载同一份字体数据

- `Tools/gui_simulator/config/sim_lv_conf.h:16` 包含产品 `lv_conf.h`，只覆盖 DMA2D 与性能/内存浮层。`LV_CONF_PATH=sim_lv_conf.h`（`Tools/gui_simulator/CMakeLists.txt:55`）。因此字体、堆与 FS 选项与板上一致，包括 `LV_MEM_SIZE` 与 `LV_USE_FS_*`。
- **C 数组**：`Tools/gui_simulator/fakes/resource_sim.c:23–24` 直接 `#include` `Resources/imgs/*.c` 数组，并在第 57–59 行拷入。字体 `.c` 可以用同样的方式编入模拟器，但需要在模拟器的源文件列表中新增该文件（`add_executable`，`CMakeLists.txt`；本文未改动）。
- **bin**：模拟器没有 `lv_fs` 驱动（`LV_USE_FS_*` 全为 0），`lv_font_load` 必然失败。模拟器也不读取 NOR 资源包。要加载同一份 bin，需要在模拟器中注册自定义 `lv_fs` 驱动，本文未实现。
- 结论：模拟器中"同一份字体数据"的来源只能是 C 数组（需新增源文件），或者新增自定义 FS 驱动读取的 bin。两者都尚未实现。

## 4. 未决与待上板事项

- **【hw:pending】** 映射窗口随机字形读的实测耗时。可以用现有 `Bench memory-mapped read` 作为顺序读的对照，但它不测随机读。
- **【hw:pending】** NOR 实际 VCC 与 133 MHz 上限的对应关系（【未核实】）。
- **【hw:pending】** LVGL 堆在运行期的实际占用（当前没有统计）。
- **【未核实】** RM0433 的 Flash bank 地址表原文。论坛转引称 1 MB 型号为两个 512 KB bank，中间有 512 KB 空隙（`https://community.st.com/t5/stm32-mcus-products/is-any-system-flash-writable/td-p/191370` 等，均为论坛帖，非原文）。
- **【未核实】** Cortex-M7 默认内存映射中 `0x80000000–0x9FFFFFFF` 的类型与 XN 属性（ARM 文档页未取到表格）。
- **【未测】** tiny_ttf 与 FreeType 的代码体积（需要改 `lv_conf.h` 后编译，或引入 FreeType 库）。
- **【未定】** 字集的定义（"常用 2500 字"的来源）、字号与 bpp、字体存放方式（C 数组、资源包 FONT 或映射区），以及镜像头格式与 BIN 空隙的处理。这些是决策，本文不替你选择。
- **手册原文**：本文引用的 W25Q256JV 段落来自 pdftotext 版式输出，表格有错位；fC 与时序以原页为准。

## 5. 复现要点

- 构建：`cmake --preset firmware-release` 后 `cmake --build --preset firmware-release`（Debug 同理）。产物在 `build/`（已被 git 忽略）。
- 字形：lv_font_conv 1.5.3，参数见 §2。字表由 CPython `gb2312` 解码 `0xA1–0xF7` 区生成：第 1–9 区为符号，`0xB0–0xD7` 为一级汉字，`0xD8–0xF7` 为二级汉字。
- 编译：LVGL 编译选项取自 `build/firmware-release/compile_commands.json` 中一条 LVGL 源文件的记录，并加 `-DLV_LVGL_H_INCLUDE_SIMPLE`。
- bin 解析：`lv_font_loader.c:86–585` 的读取顺序（`head` 48 B、`cmap`、`loca`、`glyf`、`kern`）。
- 探针与中间文件放在会话 scratchpad，未入库。
