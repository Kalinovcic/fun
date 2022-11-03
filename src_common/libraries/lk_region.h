/*  lk_region.h - public domain stack-based allocator */
/*  no warranty is offered or implied */

/*********************************************************************************************

Include this file in all places you need to refer to it. In one of your compilation units, write:
    #define LK_REGION_IMPLEMENTATION
before including lk_region.h, in order to paste in the source code.

QUICK NOTES
    @Incomplete

LICENSE
    This software is in the public domain. Anyone can use it, modify it,
    roll'n'smoke hardcopies of the source code, sell it to the terrorists, etc.
    No warranty is offered or implied; use this code at your own risk!

    See end of file for license information.

DOCUMENTATION
    @Incomplete

 *********************************************************************************************/

#ifndef LK_REGION_HEADER
#define LK_REGION_HEADER

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* This entire preprocessor mess is here just because
   C and C++ don't really provide a simple way to set alignment,
   or inquire about alignment. We need alignment info to allocate,
   and we would also like LK_Region to be cache-aligned. */

#define LK__REGION_CACHE_SIZE 32
#if defined(__cplusplus) && (__cplusplus>=201103L)
/* Do it with C++11 features, if available. */
#define LK__REGION_CACHE_ALIGN(m) alignas(LK__REGION_CACHE_SIZE) m
#define LK__REGION_ALIGNOF(type) alignof(type)
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__>=201112L)
/* Do it with C11 features, if available. */
#define LK__REGION_CACHE_ALIGN(m) _Alignas(LK__REGION_CACHE_SIZE) m
#define LK__REGION_ALIGNOF(type) _Alignof(type)
#elif defined(_MSC_VER)
/* Do it with MSVC features, if available. */
#define LK__REGION_CACHE_ALIGN(m) __declspec(align(LK__REGION_CACHE_SIZE)) m
#define LK__REGION_ALIGNOF(type) __alignof(type)
#elif defined(__GNUC__) || defined(__GNUG__)
/* Do it with GCC features, if available. */
#define LK__REGION_CACHE_ALIGN(m) m __attribute__((aligned(LK__REGION_CACHE_SIZE)))
#define LK__REGION_ALIGNOF(type) __alignof__(type)
#else
/* Oh well, best of luck to you! */
#define LK__REGION_CACHE_ALIGN(m) m
#define LK__REGION_ALIGNOF(type) (sizeof(type) > 4 ? 8 : (sizeof(type) > 2 ? 4 : (sizeof(type) == 2 ? 2 : 1)))
#endif

/* LK_Region struct.
   You shouldn't need to care about the members of this struct,
   it is only in the header so that you can allocate it. */
typedef struct
{
    LK__REGION_CACHE_ALIGN(uintptr_t page_size);
    uint8_t*  page_end;
    uint8_t*  cursor;
    void*     big_list;
    uint8_t*  next_page_end;
    uintptr_t total_size;
} LK_Region;

/* Use this macro to initialize region variables. Like this:
       LK_Region region = LK_RegionInit;
   If you're using C++, you can also do:
       LK_Region region = { 0 };
       LK_Region region = {}; // C++11 */
#define LK_RegionInit { 0, 0, 0, 0, 0, 0 }

#ifdef LK_REGION_COLLECT_CALLER_INFO
#define lk_region_alloc(...) (lk_region_alloc_(__VA_ARGS__, __FUNCTION__))
void* lk_region_alloc_(LK_Region* region, size_t size, size_t alignment, const char* caller_name);
#else
void* lk_region_alloc(LK_Region* region, size_t size, size_t alignment);
#endif

void lk_region_free(LK_Region* region);

/* Helper macros. */
#define LK_RegionValue(region_ptr, type)                          ((type*) lk_region_alloc((region_ptr), sizeof(type),           LK__REGION_ALIGNOF(type)))
#define LK_RegionArray(region_ptr, type, count)                   ((type*) lk_region_alloc((region_ptr), sizeof(type) * (count), LK__REGION_ALIGNOF(type)))
#define LK_RegionValueAligned(region_ptr, type, alignment)        ((type*) lk_region_alloc((region_ptr), sizeof(type),           (alignment)))
#define LK_RegionArrayAligned(region_ptr, type, count, alignment) ((type*) lk_region_alloc((region_ptr), sizeof(type) * (count), (alignment)))

/* LK_Region_Cursor struct.
   You shouldn't need to care about the members of this struct,
   it is only in the header so that you can allocate it. */
typedef struct
{
    uint8_t* page_end;
    uint8_t* cursor;
    void*    big_list;
} LK_Region_Cursor;

void lk_region_cursor(LK_Region* region, LK_Region_Cursor* cursor);
void lk_region_rewind(LK_Region* region, LK_Region_Cursor* cursor);

#ifdef __cplusplus
}
#endif

#endif /* LK_REGION_HEADER */

/*********************************************************************************************

  END OF HEADER - BEGINNING OF IMPLEMENTATION

 *********************************************************************************************/

#ifdef LK_REGION_IMPLEMENTATION
#ifndef LK_REGION_IMPLEMENTED
#define LK_REGION_IMPLEMENTED

#ifdef __cplusplus
extern "C"
{
#endif

void* lk_region_os_alloc(size_t size, const char* caller_name);
void lk_region_os_free(void* memory, size_t size);

#ifdef _WIN32
/*********************************************************************************************
  Windows-specific
 *********************************************************************************************/
#ifndef LK_REGION_DEFAULT_PAGE_SIZE
#define LK_REGION_DEFAULT_PAGE_SIZE 0x10000 /* 64 kB */
#endif

#include <windows.h>

#ifndef LK_REGION_CUSTOM_PAGE_ALLOCATOR

void* lk_region_os_alloc(size_t size, const char* caller_name)
{
    return VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void lk_region_os_free(void* memory, size_t size)
{
    VirtualFree(memory, 0, MEM_RELEASE);
}

#endif  // ifndef LK_REGION_CUSTOM_PAGE_ALLOCATOR

#elif defined(__linux__)
/*********************************************************************************************
  Linux-specific
 *********************************************************************************************/
#ifndef LK_REGION_DEFAULT_PAGE_SIZE
#define LK_REGION_DEFAULT_PAGE_SIZE 0x10000 /* 64 kB */
#endif

#include <stdlib.h>

#ifndef LK_REGION_CUSTOM_PAGE_ALLOCATOR

void* lk_region_os_alloc(size_t size, const char* caller_name)
{
    // @Reconsider
    return calloc(1, size);
}

void lk_region_os_free(void* memory, size_t size)
{
    return free(memory);
}

#endif  // ifndef LK_REGION_CUSTOM_PAGE_ALLOCATOR

#else
#error Unrecognized operating system
#endif

/*********************************************************************************************
  Cross-platform
 *********************************************************************************************/

#if defined(__GNUC__) || defined(__clang__)
#define LK__HintProbablyFalse(x) (__builtin_expect((x), 0))
#else
#define LK__HintProbablyFalse(x) (x)
#endif

#include <string.h>

typedef struct LK_Page_Node
{
    struct LK_Page_Node* previous;
} LK_Page_Node;

typedef struct LK_Big_Node
{
    struct LK_Big_Node* previous;
    uintptr_t size;
    uint8_t*  page_end;  // page_end at the time of allocation
    uint8_t*  cursor;    // cursor at the time of allocation
} LK_Big_Node;

void* lk_region_alloc(LK_Region* region, size_t size, size_t alignment)
{
    typedef uint8_t byte;
    typedef uintptr_t umm;

retry:
    umm page_size = region->page_size;
    if LK__HintProbablyFalse(size > (page_size >> 2))  // big allocation (or page_size is not set)
    {
        if (!page_size)
        {
            region->page_size = LK_REGION_DEFAULT_PAGE_SIZE;
            goto retry;
        }

        umm allocation_size = sizeof(LK_Big_Node) + alignment + size;
        LK_Big_Node* node = (LK_Big_Node*) lk_region_os_alloc(allocation_size, NULL);
        node->previous = (LK_Big_Node*) region->big_list;
        node->size     = allocation_size;
        node->page_end = region->page_end;
        node->cursor   = region->cursor;
        region->big_list = node;
        region->total_size += allocation_size;

        byte* result = (byte*)(node + 1);
        result += -(umm)(result) & (umm)(alignment - 1);
        return result;
    }

    byte* cursor = region->cursor;
    cursor += -(umm)(cursor) & (umm)(alignment - 1);

    byte* end_cursor = cursor + (umm) size;
    byte* page_end   = region->page_end;
    if LK__HintProbablyFalse(end_cursor > page_end)  // reached the end of the page
    {
        LK_Page_Node* previous = (LK_Page_Node*)(page_end - (page_end ? page_size : 0));

        byte* next_page_end = region->next_page_end;
        if (next_page_end)
        {
            LK_Page_Node* node = (LK_Page_Node*)(next_page_end - page_size);
            node->previous = previous;

            region->page_end = next_page_end;
            region->cursor   = (byte*)(node + 1);
            region->next_page_end = 0;
        }
        else
        {
            LK_Page_Node* node = (LK_Page_Node*) lk_region_os_alloc(page_size, NULL);
            node->previous = previous;

            region->page_end = (byte*) node + page_size;
            region->cursor   = (byte*)(node + 1);
            region->total_size += page_size;
        }

        cursor = region->cursor;
        cursor += -(umm)(cursor) & (umm)(alignment - 1);
        end_cursor = cursor + (umm) size;
    }

    region->cursor = end_cursor;
    return cursor;
}

void lk_region_free(LK_Region* region)
{
    uintptr_t page_size = region->page_size;
    if (region->page_end)
    {
        LK_Page_Node* page = (LK_Page_Node*)(region->page_end - page_size);
        while (page)
        {
            LK_Page_Node* previous = page->previous;
            lk_region_os_free(page, page_size);
            page = previous;
        }
    }

    if (region->next_page_end)
    {
        LK_Page_Node* next_page = (LK_Page_Node*)(region->next_page_end - page_size);
        lk_region_os_free(next_page, page_size);
    }

    LK_Big_Node* big = (LK_Big_Node*) region->big_list;
    while (big)
    {
        LK_Big_Node* previous = big->previous;
        lk_region_os_free(big, big->size);
        big = previous;
    }

    region->page_end      = NULL;
    region->cursor        = NULL;
    region->big_list      = NULL;
    region->next_page_end = NULL;
    region->total_size    = 0;
}

void lk_region_cursor(LK_Region* region, LK_Region_Cursor* region_cursor)
{
    region_cursor->page_end = region->page_end;
    region_cursor->cursor   = region->cursor;
    region_cursor->big_list = region->big_list;
}

void lk_region_rewind(LK_Region* region, LK_Region_Cursor* region_cursor)
{
    typedef uint8_t byte;
    typedef uintptr_t umm;

    byte* target_page_end = region_cursor->page_end;
    byte* target_cursor   = region_cursor->cursor;
    void* target_big_list = region_cursor->big_list;

    umm          page_size = region->page_size;
    byte*        page_end  = region->page_end;
    byte*        cursor    = region->cursor;
    LK_Big_Node* big       = (LK_Big_Node*) region->big_list;

    while (big != target_big_list)
    {
        region->total_size -= big->size;
        LK_Big_Node* previous = big->previous;
        lk_region_os_free(big, big->size);
        big = previous;
    }

    while (page_end != target_page_end)
    {
        LK_Page_Node* page = (LK_Page_Node*)(page_end - page_size);
        LK_Page_Node* previous = page->previous;

        if (region->next_page_end)
        {
            lk_region_os_free(page, page_size);
            region->total_size -= page_size;
        }
        else
        {
            region->next_page_end = page_end;
            memset(page, 0, cursor - (byte*) page);
        }

        page_end = (byte*) previous + (previous ? page_size : 0);
        cursor   = page_end;
    }

    if (cursor != target_cursor)
    {
        memset(target_cursor, 0, cursor - target_cursor);
        cursor = target_cursor;
    }

    region->page_end  = page_end;
    region->cursor    = cursor;
    region->big_list  = big;
}

#ifdef __cplusplus
}
#endif

#endif /* LK_REGION_IMPLEMENTED */
#endif /* LK_REGION_IMPLEMENTATION */


/*********************************************************************************************

THE UNLICENCE (http://unlicense.org)

    This is free and unencumbered software released into the public domain.

    Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
    software, either in source code form or as a compiled binary, for any purpose,
    commercial or non-commercial, and by any means.

    In jurisdictions that recognize copyright laws, the author or authors of this
    software dedicate any and all copyright interest in the software to the public
    domain. We make this dedication for the benefit of the public at large and to
    the detriment of our heirs and successors. We intend this dedication to be an
    overt act of relinquishment in perpetuity of all present and future rights to
    this software under copyright law.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
    ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
    WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 *********************************************************************************************/
