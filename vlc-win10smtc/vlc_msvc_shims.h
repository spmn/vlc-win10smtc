#pragma once

#include <basetsd.h>

#define restrict __restrict

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
using ssize_t = SSIZE_T;
#endif

struct pollfd;
int poll(struct pollfd *, unsigned, int);
