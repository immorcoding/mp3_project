/*
******************************************************************************
**

**  File        : LinkerScript.ld
**
**  Author		: STM32CubeMX
**
**  Abstract    : Linker script for STM32H743ZGTx series
**                1024Kbytes FLASH and 1056Kbytes RAM
**
**                Set heap size, stack size and stack location according
**                to application requirements.
**
**                Set memory bank area and size if external memory is used.
**
**  Target      : STMicroelectronics STM32
**
**  Distribution: The file is distributed “as is,” without any warranty
**                of any kind.
**
*****************************************************************************
** @attention
**
** <h2><center>&copy; COPYRIGHT(c) 2025 STMicroelectronics</center></h2>
**
** Redistribution and use in source and binary forms, with or without modification,
** are permitted provided that the following conditions are met:
**   1. Redistributions of source code must retain the above copyright notice,
**      this list of conditions and the following disclaimer.
**   2. Redistributions in binary form must reproduce the above copyright notice,
**      this list of conditions and the following disclaimer in the documentation
**      and/or other materials provided with the distribution.
**   3. Neither the name of STMicroelectronics nor the names of its contributors
**      may be used to endorse or promote products derived from this software
**      without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
*****************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

/* Specify the memory areas */
MEMORY
{
DTCMRAM (xrw)      : ORIGIN = 0x20000000, LENGTH = 128K
RAM (xrw)      : ORIGIN = 0x24000000, LENGTH = 512K
RAM_D2 (xrw)      : ORIGIN = 0x30000000, LENGTH = 288K
RAM_D3 (xrw)      : ORIGIN = 0x38000000, LENGTH = 64K
ITCMRAM (xrw)      : ORIGIN = 0x00000000, LENGTH = 64K
SDRAM (xrw)      :  ORIGIN = 0xC0000000, LENGTH = 32M
FLASH (rx)      : ORIGIN = 0x8000000, LENGTH = 512K
FLASH2 (rx)      : ORIGIN = 0x8100000, LENGTH = 512K
}

/* Highest address of the user mode stack */
_estack = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM);    /* end of RAM */
_sstack = _estack - _Min_Stack_Size;
/* Generate a link error if heap and stack don't fit into RAM */
_Min_Heap_Size = 0x200;      /* required amount of heap  */
_Min_Stack_Size = 0x400; /* required amount of stack */

/* Define output sections */
SECTIONS
{
  /* The startup code goes first into FLASH */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >FLASH

  /* 提前取走cp936的两张映射表和默认壁纸 */
  .sdram_external_resources (NOLOAD) :
  {
      . = ALIGN(32);

      __external_resource_uni2oem_start__ = .;
      *(.rodata.uni2oem)
      __external_resource_uni2oem_end__ = .;

      . = ALIGN(32);
      __external_resource_oem2uni_start__ = .;
      *(.rodata.oem2uni)
      __external_resource_oem2uni_end__ = .;

      . = ALIGN(32);
      __external_resource_wallpaper_start__ = .;
      *(.rodata.ui_img_wallpaper_indigo_mist_soft_dark_png_data)
      __external_resource_wallpaper_end__ = .;

      . = ALIGN(32);
      __external_resource_vinyl_start__ = .;
      . += 0xF300;
      __external_resource_vinyl_end__ = .;
      . = ALIGN(32);
  } >SDRAM

  ASSERT((__external_resource_uni2oem_end__ - __external_resource_uni2oem_start__) == 0x15484,
        "External resource uni2oem size mismatch")
  ASSERT((__external_resource_oem2uni_end__ - __external_resource_oem2uni_start__) == 0x15484,
        "External resource oem2uni size mismatch")
  ASSERT((__external_resource_wallpaper_end__ - __external_resource_wallpaper_start__) == 0x38400,
        "External resource wallpaper size mismatch")
  ASSERT((__external_resource_vinyl_end__ - __external_resource_vinyl_start__) == 0xF300,
        "External resource vinyl size mismatch")

  /*
  * 提前取走存储测试代码放到FLASH2
  */
  .storage_benchmark :
  {
      . = ALIGN(4);

      *APP/tasks/storage/benchmark/*.obj(
          .text
          .text.*
          .rodata
          .rodata.*
      )

      . = ALIGN(4);
  } >FLASH2

  /* 图片与字体的只读资源。在匹配到下面的.rodata之前先取走。 */
  .gui_assets :
  {
    . = ALIGN(4);

    *(.rodata.ui_img_*)
    *(.rodata.glyph_*)
    *(.rodata.cmaps)
    *(.rodata.kern_*)
    *(.rodata.ui_font_Number)

    . = ALIGN(4);
  } >FLASH

  /*
  * 提前取走 LVGL 静态库中的其他代码和只读数据，放到 FLASH2。
  */
  .lvgl_flash2 :
  {
      . = ALIGN(4);

      *liblvgl.a:*(
          .text
          .text.*
          .rodata
          .rodata.*
      )

      . = ALIGN(4);
  } >FLASH2

  /* The program code and other data goes into FLASH */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >FLASH

  /* Constant data goes into FLASH */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >FLASH

  .ARM.extab (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >FLASH

  .ARM (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >FLASH

  .preinit_array (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .init_array (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .fini_array (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >FLASH

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections goes into RAM, load LMA copy after code */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    . = ALIGN(4);
  } >DTCMRAM AT> FLASH

 /* Initialized TLS data section */
  .tdata : ALIGN(4)
  {
    *(.tdata .tdata.* .gnu.linkonce.td.*)
    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
    PROVIDE(__data_end = .);
    PROVIDE(__tdata_end = .);
  } >DTCMRAM AT> FLASH

  PROVIDE( __tdata_start = ADDR(.tdata) );
  PROVIDE( __tdata_size = __tdata_end - __tdata_start );

  PROVIDE( __data_start = ADDR(.data) );
  PROVIDE( __data_size = __data_end - __data_start );

  PROVIDE( __tdata_source = LOADADDR(.tdata) );
  PROVIDE( __tdata_source_end = LOADADDR(.tdata) + SIZEOF(.tdata) );
  PROVIDE( __tdata_source_size = __tdata_source_end - __tdata_source );

  PROVIDE( __data_source = LOADADDR(.data) );
  PROVIDE( __data_source_end = __tdata_source_end );
  PROVIDE( __data_source_size = __data_source_end - __data_source );
  /* Uninitialized data section */
  .tbss (NOLOAD) : ALIGN(4)
  {
     /* This is used by the startup in order to initialize the .bss secion */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.tbss .tbss.*)
    . = ALIGN(4);
    PROVIDE( __tbss_end = . );
  } >DTCMRAM

  PROVIDE( __tbss_start = ADDR(.tbss) );
  PROVIDE( __tbss_size = __tbss_end - __tbss_start );
  PROVIDE( __tbss_offset = ADDR(.tbss) - ADDR(.tdata) );

  PROVIDE( __tls_base = __tdata_start );
  PROVIDE( __tls_end = __tbss_end );
  PROVIDE( __tls_size = __tls_end - __tls_base );
  PROVIDE( __tls_align = MAX(ALIGNOF(.tdata), ALIGNOF(.tbss)) );
  PROVIDE( __tls_size_align = (__tls_size + __tls_align - 1) & ~(__tls_align - 1) );
  PROVIDE( __arm32_tls_tcb_offset = MAX(8, __tls_align) );
  PROVIDE( __arm64_tls_tcb_offset = MAX(16, __tls_align) );

  .bss (NOLOAD) : ALIGN(4)
  {
    *(.bss)
    *(.bss*)
    *(COMMON)

      . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
      PROVIDE( __bss_end = .);
  } >DTCMRAM
  PROVIDE( __non_tls_bss_start = ADDR(.bss) );

  PROVIDE( __bss_start = __tbss_start );
  PROVIDE( __bss_size = __bss_end - __bss_start );

  /*
   * SDMMC 的内部 DMA 必须访问 AXI SRAM，不能使用默认位于 DTCMRAM 的 .bss。
   * 此段由每个用户在第一次 DMA 前完整写入，故设为 NOLOAD；32 字节对齐避免
   * Cortex-M7 D-Cache Clean/Invalidate 操作覆盖同一 cache line 的相邻变量。
   */
  .sd_dma_buffer (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sd_dma_buffer)
    . = ALIGN(32);
  } >RAM

  /*
   * W25Q256 显式自检及未来 QSPI DMA 共用的 4 KiB 临时缓冲区。
   * 自检代码会在每次使用前填满或接收完整数据，故设为 NOLOAD；32 字节对齐
   * 让未来 DMA 的 Cache 维护不会触及相邻普通变量。
   */
  .flash_write_buffer (NOLOAD) :
  {
    . = ALIGN(32);
    *(.flash_write_buffer)
    . = ALIGN(32);
  } >RAM

  /* LVGL 大数组内存池可被 DMA2D 用作文字遮罩等源数据，必须位于 AXI RAM。 */
  .lvgl_large_ram_array (NOLOAD) :
  {
    . = ALIGN(32);
    *(.lvgl_large_ram_array)
    . = ALIGN(32);
  } >RAM

  /*
   * 播放模块 PCM 半区（I2S 循环 DMA 源），位于 AXI SRAM。
   * Init 写 0，首次 Start 前由预填完整覆盖，故设为 NOLOAD；32 字节对齐避免 Cache 维护
   * 触及相邻变量。
   */
  .playback_pcm (NOLOAD) :
  {
    . = ALIGN(32);
    *(.playback_pcm)
    . = ALIGN(32);
  } >RAM

  /* FTL 扫描在使用前完整重建；禁止与破坏性 SDRAM 诊断同时使用。 */
  .flash_ftl_tables (NOLOAD) :
  {
    . = ALIGN(32);
    *(.flash_ftl_tables)
    . = ALIGN(32);
  } >SDRAM

  /* Storage Catalog 曲目表；NOLOAD，由 Storage Task 在挂载扫描时完整写入。 */
  .storage_catalog (NOLOAD) :
  {
    . = ALIGN(32);
    *(.storage_catalog)
    . = ALIGN(32);
  } >SDRAM

  /* 歌曲清单；NOLOAD，由 Storage Task 在挂载扫描时完整写入，在catalog初始化之后。 */
  .music_sheet (NOLOAD) :
  {
    . = ALIGN(32);
    *(.music_sheet)
    . = ALIGN(32);
  } >SDRAM

  /* SDRAM 帧缓冲区域：不加载、不自动清零，由拥有者在首次使用前完整写入。 */
  .sdram_framebuffer (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sdram_framebuffer)
    . = ALIGN(32);
  } >SDRAM

  /*
   * Storage 音频块池（32 × 32 KiB）；NOLOAD，块内容入队才有效，不依赖清零。
   * 放在帧缓冲之后，不移动既有 SDRAM 段的地址；块不作 DMA 目标。
   */
  .storage_audio_pool (NOLOAD) :
  {
    . = ALIGN(32);
    *(.storage_audio_pool)
    . = ALIGN(32);
  } >SDRAM

  /*
   * 可选 FONT 载荷（lv_font_conv bin 原样字节）；唯一所有者 Resource Service。
   * NOLOAD，由 Service_Resource_Init() 在正式 SDRAM 初始化后、任务启动前复制并校验目标 CRC，
   * 失败时不发布给消费者。容量 = 实测 623,520 B 加约 49.1 KiB 余量（Pack v4 起比 v3 扩 64 KiB，为今后小幅扩充字集留余量），32 B 对齐，Clean 取整不出本区。
   * 放在既有 SDRAM 段之后，不移动它们的地址。
   */
  .sdram_font_resource (NOLOAD) :
  {
    . = ALIGN(32);
    __external_resource_font_start__ = .;
    . += 0xA4800;
    __external_resource_font_end__ = .;
    . = ALIGN(32);
  } >SDRAM

  ASSERT((__external_resource_font_end__ - __external_resource_font_start__) == 0xA4800,
        "External resource font size mismatch")
  ASSERT((__external_resource_font_end__ - __external_resource_font_start__) >= 623520,
        "External resource font region smaller than the committed font bin")

  /*
   * GUI 字体解码区（字形描述、cmap、字距与重排位图）；唯一所有者 Service/gui 字体子模块。
   * NOLOAD，由 GUI Task 在 Service_GUI_Init() 中、曲名 Label 创建前写入；只经 CPU 读写，
   * 不作 DMA 源或目标。容量 = 当前字体所需 636,240 B（Cortex-M7 32 位指针口径，估算）加约 50.5 KiB 余量（比 Pack v3 扩 64 KiB）；
   * 主机 64 位口径为 636,944 B，模拟器用同样大小也放得下。
   */
  .sdram_gui_font (NOLOAD) :
  {
    . = ALIGN(32);
    __gui_font_decode_start__ = .;
    . += 0xA8000;
    __gui_font_decode_end__ = .;
    . = ALIGN(32);
  } >SDRAM

  ASSERT((__gui_font_decode_end__ - __gui_font_decode_start__) == 0xA8000,
        "GUI font decode region size mismatch")
  ASSERT((__gui_font_decode_end__ - __gui_font_decode_start__) >= 636240,
        "GUI font decode region smaller than the committed font needs")

  /* User_heap_stack section, used to check that there is enough RAM left */
  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >DTCMRAM



  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a:* ( * )
    libm.a:* ( * )
    libgcc.a:* ( * )
  }

}
