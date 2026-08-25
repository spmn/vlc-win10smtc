#pragma once

#if defined(_M_IX86) || defined(_M_ARM)
	typedef __int32 ssize_t;
#elif defined(_M_X64) || defined(_M_ARM64)
	typedef __int64 ssize_t;
#else
	#error "Unknown arch"
#endif
