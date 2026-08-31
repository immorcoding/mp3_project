/**
 * @file test_user_diskio_fallback.c
 * @brief 未链接强后端时，生成 USER Glue 必须安全拒绝读写。
 */

#include "ff_gen_drv.h"
#include "FATFS/Target/user_diskio.h"
#include <assert.h>
#include <string.h>

/**
 * @brief 验证没有强后端时 USER Glue 安全拒绝初始化、读写和同步。
 * @return 全部断言通过返回 0；失败由断言终止进程。
 * @note 同时核对失败读取不修改输出缓冲，防止弱默认实现伪报成功。
 */
int main(void)
{
    BYTE data[512];
    memset(data, 0xA5, sizeof(data));
    assert(USER_Driver.disk_initialize(0) & STA_NOINIT);
    assert(USER_Driver.disk_status(0) & STA_NOINIT);
    assert(USER_Driver.disk_read(0, data, 0, 1) == RES_NOTRDY);
    assert(USER_Driver.disk_write(0, data, 0, 1) == RES_NOTRDY);
    assert(USER_Driver.disk_ioctl(0, CTRL_SYNC, 0) == RES_NOTRDY);
    for (unsigned i = 0; i < sizeof(data); i++)
    {
        assert(data[i] == 0xA5);
    }
    return 0;
}
