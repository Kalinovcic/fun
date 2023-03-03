#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"
#include <stdio.h>

#if defined(OS_LINUX)
#include "security_linux.inl"
#else
#error "Unsupported platform"
#endif
