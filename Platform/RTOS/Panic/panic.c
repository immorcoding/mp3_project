static const char *volatile assert_file;
static volatile uint32_t assert_line;

void vAssertCalled(const char *file, uint32_t line)
{
    assert_file = file;
    assert_line = line;

    __disable_irq();
    if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U) //调试器连接
    {
        __BKPT(0);//硬件断点
    }

    while (1);
}