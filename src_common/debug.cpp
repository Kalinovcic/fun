#include "common.h"

#if defined(OS_WINDOWS)
#include "debug_windows.inl"
#elif defined(OS_LINUX)
#include "debug_linux.inl"
#else
#error "Unsupported"
#endif
