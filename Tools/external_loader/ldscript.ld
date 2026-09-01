/* STM32CubeProgrammer loader: 信息表置于 0，全部可执行代码与数据驻留 AXI SRAM。 */
MEMORY
{
    INFO (r)   : ORIGIN = 0x00000000, LENGTH = 0x100
    RAM  (rwx) : ORIGIN = 0x24000004, LENGTH = 0x0007FFFC
}

SECTIONS
{
    .info :
    {
        KEEP(*(.info))
    } > INFO

    .text :
    {
        . = ALIGN(4);
        /* CubeProgrammer 按符号调用这些入口，不能因 Init 未直接引用而被 GC。 */
        KEEP(*(.text.Init))
        KEEP(*(.text.Write))
        KEEP(*(.text.SectorErase))
        KEEP(*(.text.MassErase))
        KEEP(*(.text.CheckSum))
        KEEP(*(.text.Verify))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > RAM

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > RAM
    .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > RAM

    .data :
    {
        . = ALIGN(4);
        *(.data .data.*)
        . = ALIGN(4);
    } > RAM

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
    } > RAM

    /DISCARD/ : { *(.comment .note .eh_frame) }
}
