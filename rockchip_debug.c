#include <stdarg.h>
#include "rockchip_debug.h"

void rk_error_msg(const char *msg, ...)
{
    va_list args;

    fprintf(stderr, "rockchip_drv_video ERROR: ");
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
}

void rk_info_msg(const char *msg, ...)
{
    va_list args;

    fprintf(stderr, "rockchip_drv_video: ");
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
}
