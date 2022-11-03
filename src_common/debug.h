#ifdef LEAKCHECK
// sigh...
#include <vcruntime_typeinfo.h>
#endif



EnterApplicationNamespace



#ifdef assert
#undef assert
#endif

void assertion_failure(const char* test, const char* file, int line);

#define assert(test)                                                                \
    do                                                                              \
    {                                                                               \
        if (!(test))                                                                \
            ::ApplicationNamespace::assertion_failure(#test, __FILE__, __LINE__);   \
    }                                                                               \
    while (false)



extern bool output_full_dumps;

extern void(*after_crash_dump)();

void install_unhandled_exception_handler();


#ifdef LEAKCHECK

inline void* crt_malloc (size_t size)                           { return malloc   (size);             }
inline void* crt_calloc (size_t items, size_t size)             { return calloc   (items, size);      }
inline void* crt_realloc(void* ptr, size_t size)                { return realloc  (ptr, size);        }
inline void* crt_recalloc(void* ptr, size_t items, size_t size) { return _recalloc(ptr, items, size); }
inline void  crt_free   (void* ptr)                             {        free     (ptr);              }

void* debug_malloc  (size_t size);
void* debug_calloc  (size_t items, size_t size);
void* debug_realloc (void* ptr, size_t size);
void* debug_recalloc(void* ptr, size_t items, size_t size);
void  debug_free    (void* ptr);

#define malloc(...)    ApplicationNamespace::debug_malloc  (__VA_ARGS__)
#define calloc(...)    ApplicationNamespace::debug_calloc  (__VA_ARGS__)
#define realloc(...)   ApplicationNamespace::debug_realloc (__VA_ARGS__)
#define _recalloc(...) ApplicationNamespace::debug_recalloc(__VA_ARGS__)
#define free(...)      ApplicationNamespace::debug_free    (__VA_ARGS__)


extern thread_local bool        the_next_allocation_is_deliberately_leaked;
extern thread_local char const* the_next_allocation_type_name;
extern thread_local size_t      the_next_allocation_type_size;

inline void deliberately_leak_next_allocation()
{
    the_next_allocation_is_deliberately_leaked = true;
}

inline void leakcheck_set_next_allocation_type(char const* name, size_t size)
{
    the_next_allocation_type_name = name;
    the_next_allocation_type_size = size;
}

#define LeakcheckNextAllocationType(T) \
    leakcheck_set_next_allocation_type(typeid(T).name(), sizeof(T))

void leakcheck_report_alive_memory();


struct Leakcheck_Region_Item
{
    Leakcheck_Region_Item* previous;
    unsigned long long guard_qword;
    unsigned char data[0];
};

struct LK_Region
{
    alignas(32)
    Leakcheck_Region_Item* top;
    size_t pop_count;
    size_t junk_[4];  // just to match actual LK_Region size
};

struct LK_Region_Cursor
{
    size_t pop_count;
    size_t junk_[2];  // just to match actual LK_Region_Cursor size
};


void* lk_region_alloc(LK_Region* region, size_t size, size_t alignment);
void lk_region_free(LK_Region* region);
void lk_region_cursor(LK_Region* region, LK_Region_Cursor* cursor);
void lk_region_rewind(LK_Region* region, LK_Region_Cursor* cursor);

#define LK_RegionValue(region_ptr, type)                          (LeakcheckNextAllocationType(type), (type*) lk_region_alloc((region_ptr), sizeof(type),           alignof(type)))
#define LK_RegionArray(region_ptr, type, count)                   (LeakcheckNextAllocationType(type), (type*) lk_region_alloc((region_ptr), sizeof(type) * (count), alignof(type)))
#define LK_RegionValueAligned(region_ptr, type, alignment)        (LeakcheckNextAllocationType(type), (type*) lk_region_alloc((region_ptr), sizeof(type),           (alignment)))
#define LK_RegionArrayAligned(region_ptr, type, count, alignment) (LeakcheckNextAllocationType(type), (type*) lk_region_alloc((region_ptr), sizeof(type) * (count), (alignment)))



#else

#define deliberately_leak_next_allocation(...)  ((void) 0)
#define leakcheck_report_alive_memory(...)      ((void) 0)
#define LeakcheckNextAllocationType(...)        ((void) 0)

#endif  // LEAKCHECK

#define leakcheck_ignore_next_allocation(...)   deliberately_leak_next_allocation(__VA_ARGS__)


ExitApplicationNamespace
