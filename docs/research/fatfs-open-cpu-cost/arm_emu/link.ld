/* 代码、只读数据、数据、bss 连续放在 0x08000000；仿真里整块可读写，不建模存储器时序。 */
ENTRY(h_mount)
SECTIONS
{
  . = 0x08000000;
  .text : { *(.text*) *(.rodata*) }
  .data : { *(.data*) }
  .bss  : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.ARM.*) *(.comment) *(.note*) }
}
