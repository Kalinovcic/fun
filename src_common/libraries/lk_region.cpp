#ifndef LEAKCHECK

#include <stdlib.h>

#define LK_REGION_IMPLEMENTATION
#define LK_REGION_CUSTOM_PAGE_ALLOCATOR
#include "lk_region.h"

void* lk_region_os_alloc(size_t size, const char* caller_name)
{
    return calloc(1, size);
}

void lk_region_os_free(void* memory, size_t size)
{
    free(memory);
}

#endif
