/********************************** (C) COPYRIGHT *******************************
* File Name          : bsp_syscalls.c
* Description        : newlib 系统调用裁剪：_write() 直接进调试口环形缓冲，
*                      所以即使误用 printf 也不会死等
*******************************************************************************/
#include "bsp_uart.h"
#include <stddef.h>

__attribute__((used)) int _write(int fd, char *buf, int size)
{
    (void)fd;
    if(size <= 0)
    {
        return 0;
    }
    DBG_Write((const uint8_t *)buf, (uint16_t)size);
    return size;
}

__attribute__((used)) void *_sbrk(ptrdiff_t incr)
{
    extern char _end[];
    extern char _heap_end[];
    static char *curbrk = _end;

    if((curbrk + incr < _end) || (curbrk + incr > _heap_end))
    {
        return (void *)-1;
    }
    curbrk += incr;
    return (void *)(curbrk - incr);
}
