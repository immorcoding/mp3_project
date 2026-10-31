/* “如果 ff_wtoupper 对 CJK 区走快速路径会怎样”的假设实验（-Fast 开关），不是对仓库的改动建议。
 * 构建时 cc936.c 以 -Dff_wtoupper=ff_wtoupper_orig 编译，本文件提供同名 ff_wtoupper：
 * U+3000..U+A63F 在 cc936.c 的 cvt2 表里没有任何大小写映射，直接返回原值；其余转调原实现。
 * fastupper_selfcheck() 逐值（0..65535）核对与原实现一致，返回不一致的个数。 */
#include "ff.h"

WCHAR ff_wtoupper_orig(WCHAR chr);

/**
 * @brief 实验用：CJK 区快速路径，其余转调原实现
 * @param[in] chr UTF-16 字符
 * @return 大写字符
 */
WCHAR ff_wtoupper(WCHAR chr)
{
    if (chr >= 0x3000 && chr < 0xA640) return chr;
    return ff_wtoupper_orig(chr);
}

/**
 * @brief 逐值核对与原 ff_wtoupper 一致
 * @return 不一致的个数
 */
int fastupper_selfcheck(void)
{
    unsigned c;
    int bad = 0;
    for (c = 0; c < 0x10000; c++)
        if (ff_wtoupper((WCHAR)c) != ff_wtoupper_orig((WCHAR)c)) bad++;
    return bad;
}
