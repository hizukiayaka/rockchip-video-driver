#ifndef _ROCKCHIP_DEBUG_H_
#define _ROCKCHIP_DEBUG_H_
#include "common.h"

#define WARN_ONCE(...) do {				\
		static bool g_once = true;				\
		if (g_once) {						\
			g_once = false;					\
			fprintf(stderr, "WARNING: " __VA_ARGS__);	\
		}							\
	} while(0)

void rk_error_msg(const char *msg, ...);
void rk_info_msg(const char *msg, ...);

#endif
