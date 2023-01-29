#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>


#if defined(_WIN32)
  #define OS_WINDOWS
#elif defined(__linux__)
  #define OS_LINUX
  #if defined(ANDROID) || defined(__ANDROID__)
    #define OS_ANDROID
  #endif
#elif defined(__APPLE__)
  #include <TargetConditionals.h>
  #if TARGET_IPHONE_SIMULATOR
     #define OS_IOS
  #elif TARGET_OS_IPHONE
    #define OS_IOS
  #elif TARGET_OS_MAC
    #define OS_MAC
  #else
    #error "Unsupported Apple platform!"
  #endif
#else
  #error "Unsupported platform!"
#endif

#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) || defined(__x86_64) || defined(_M_X64) || defined(_M_AMD64)
  #define ARCHITECTURE_X64 1
#elif defined(i386) || defined(__i386) || defined(__i386__) || defined(_X86_) || defined(_M_IX86)
  #define ARCHITECTURE_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
  #define ARCHITECTURE_ARM64 1
#elif defined(__arm__) && defined(__ARM_ARCH_7A__)
  #define ARCHITECTURE_ARM_V7 1
#else
  #error "Unsupported architecture!"
#endif

#if defined(ARCHITECTURE_ARM_V7)
#define ARCHITECTURE_SUPPORTS_UNALIGNED_MEMORY_ACCESS 0
#else
#define ARCHITECTURE_SUPPORTS_UNALIGNED_MEMORY_ACCESS 1
#endif

#if defined(_MSC_VER)
  #define COMPILER_MSVC
#elif defined(__clang__)
  #define COMPILER_CLANG
#elif defined(__GNUC__)
  #define COMPILER_GCC
#elif defined(__INTEL_COMPILER)
  #define COMPILER_INTEL
#elif defined(__MINGW32__)
  #define COMPILER_MINGW
#else
  #error "Unsupported compiler."
#endif

#if defined(COMPILER_MSVC)
#include <intrin.h>
#elif defined(COMPILER_GCC) || defined(COMPILER_CLANG)
  #if defined(ARCHITECTURE_X86) || defined(ARCHITECTURE_X64)
#include <cpuid.h>
#include <wmmintrin.h>
  #endif
extern "C" long syscall(long, ...) __THROW;
#endif

#ifndef LEAKCHECK
#include "libraries/lk_region.h"
#endif

#define ApplicationNamespace            wc
#define EnterApplicationNamespace       namespace ApplicationNamespace {
#define ExitApplicationNamespace        }


#include "colorbars.h"
#include "debug.h"

EnterApplicationNamespace


////////////////////////////////////////////////////////////////////////////////
// Helper macros
////////////////////////////////////////////////////////////////////////////////


#define Concatenate__(x, y) x##y
#define Concatenate_(x, y)  Concatenate__(x, y)
#define Concatenate(x, y)   Concatenate_(x, y)

#define UniqueIdentifier(name) Concatenate(_##name##_, __COUNTER__)

#define ArrayCount(array) (sizeof(array) / sizeof(*(array)))
#define ZeroStruct(address) memset(address, 0, sizeof(*(address)));
#define ZeroStaticArray(array) memset(array, 0, sizeof(array));

#if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
#define Unreachable    { assert(false && "unreachable code"); __builtin_unreachable(); }
#define NotImplemented { assert(false && "not implemented");  __builtin_unreachable(); }
#define IllegalDefaultCase default: { assert(false && "illegal default case - unreachable code"); __builtin_unreachable(); break; }
#else
#define Unreachable assert(false && "unreachable code");
#define NotImplemented assert(false && "not implemented");
#define IllegalDefaultCase default: { assert(false && "illegal default case - unreachable code"); break; }
#endif

#define MemberSize(Type, member) sizeof(((Type*) NULL)->member)
#define MemberOffset(Type, member) ((umm) &((Type*) NULL)->member)


// Almost equivalent to R"XXX(what you pass in)XXX", except it collapses whitespace
// into a single space character. Use in macros, to avoid the MSVC line number bug.
#define CodeString(...) #__VA_ARGS__

#define CodeString_s(...) #__VA_ARGS__##_s


#define CACHE_SIZE 64
#define CacheAlign alignas(CACHE_SIZE)


#if defined(COMPILER_MSVC)
#define ForceInline __forceinline
#elif defined(COMPILER_GCC) || defined(COMPILER_CLANG)
#define ForceInline inline __attribute__((always_inline))
#else
#define ForceInline inline
#endif

#define IgnoreUnused(x) ((void)(x))

// Defer

template <typename F>
struct Defer_RAII
{
    F f;
    Defer_RAII(F f): f(f) {}
    ~Defer_RAII() { f(); }
};

template <typename F>
Defer_RAII<F> defer_function(F f)
{
    return Defer_RAII<F>(f);
}

#define Defer(code)   auto UniqueIdentifier(defer) = defer_function([&] () { code; })


#define For(container) for (auto* it : container)

#define Loop(name)                 \
    if (0)                         \
    {                              \
        name##_break:    break;    \
        name##_continue: continue; \
    } else

#define BreakLoop(name)    goto name##_break
#define ContinueLoop(name) goto name##_continue



#define CompileTimeAssert(test) static_assert((test), "Compile-time assertion (" #test ") failed!")


// Placing a constexpr expression in a constexpr context is the only way to force evaluation.
// And this is also really necessary in practice, because C++ compilers are ass.
// Hence, this garbage:
#define CompileTimeU32(x) (sizeof(byte(&)[(x) + 1]) - 1)
// +1 is there because C++ doesn't allow zero-sized objects.
// Also keep in mind, because of compiler-imposed static array size limits, the value of
// this integer is limited to 2^31 - 2 inclusive.


#define GlobalBlock GlobalBlock_(UniqueIdentifier(global_block))
#define GlobalBlock_(name) GlobalBlock__(name)
#define GlobalBlock__(name) \
    static struct name##_struct \
    { \
        typedef void Lambda(); \
        inline name##_struct(Lambda* f) { f(); } \
    } name##_instance = (name##_struct::Lambda*) []()

#define GlobalCleanupBlock GlobalCleanupBlock_(UniqueIdentifier(global_cleanup_block))
#define GlobalCleanupBlock_(name) GlobalCleanupBlock__(name)
#define GlobalCleanupBlock__(name) \
    static struct name##_struct \
    { \
        typedef void Lambda(); \
        void(*f)(); \
        inline name##_struct(void(*f)()): f(f) {} \
        inline ~name##_struct() { f(); } \
    } name##_instance = (name##_struct::Lambda*) []()

#if defined(COMPILER_MSVC)
// Thread-local initializer
// Note: Very poorly implemented on non-MSVC compilers. Constructors and destructors
// might not get called. Therefore, this is not defined for those compilers.

#define ThreadLocalBlock ThreadLocalBlock_(UniqueIdentifier(thread_local_block))
#define ThreadLocalBlock_(name) ThreadLocalBlock__(name)
#define ThreadLocalBlock__(name) \
    static thread_local struct name##_struct \
    { \
        typedef void Lambda(); \
        inline name##_struct(void(*f)()) { f(); } \
    } name##_instance = (name##_struct::Lambda*) []()

#define ThreadLocalCleanupBlock ThreadLocalCleanupBlock_(UniqueIdentifier(thread_local_cleanup_block))
#define ThreadLocalCleanupBlock_(name) ThreadLocalCleanupBlock__(name)
#define ThreadLocalCleanupBlock__(name) \
    static thread_local struct name##_struct \
    { \
        typedef void Lambda(); \
        Lambda* f; \
        inline name##_struct(Lambda* f): f(f) {} \
        inline ~name##_struct() { f(); } \
    } name##_instance = (name##_struct::Lambda*) []()

#endif

// Only once

#define OnlyOnce OnlyOnce_(UniqueIdentifier(only_once))
#define OnlyOnce_(name) OnlyOnce__(name)
#define OnlyOnce__(name)                                         \
    static Atomic32 name;                                        \
    static Atomic32 name##_lock;                                 \
    if (!load(&name))                                            \
    {                                                            \
        while (!compare_exchange(&name##_lock, 0, 1));           \
        if (!load(&name))                                        \
        {                                                        \
            goto name##_start;                                   \
            name##_end:                                          \
            exchange(&name, 1);                                  \
        }                                                        \
        exchange(&name##_lock, 0);                               \
    }                                                            \
    if (0) while (1)                                             \
        if (1) goto name##_end;                                  \
        else name##_start:


template<typename T>
inline void swap(T* a, T* b)
{
    T temp = *a;
    *a = *b;
    *b = temp;
}


////////////////////////////////////////////////////////////////////////////////
// Primitive types
////////////////////////////////////////////////////////////////////////////////


typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
CompileTimeAssert(sizeof(s8)  == 1);
CompileTimeAssert(sizeof(s16) == 2);
CompileTimeAssert(sizeof(s32) == 4);
CompileTimeAssert(sizeof(s64) == 8);

constexpr s8  S8_MAX  = 0x7fl;
constexpr s16 S16_MAX = 0x7fffl;
constexpr s32 S32_MAX = 0x7fffffffl;
constexpr s64 S64_MAX = 0x7fffffffffffffffll;

constexpr s8  S8_MIN  = (s8)  0x80ul;
constexpr s16 S16_MIN = (s16) 0x8000ul;
constexpr s32 S32_MIN = (s32) 0x80000000ul;
constexpr s64 S64_MIN = (s64) 0x8000000000000000ull;

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
CompileTimeAssert(sizeof(u8)  == 1);
CompileTimeAssert(sizeof(u16) == 2);
CompileTimeAssert(sizeof(u32) == 4);
CompileTimeAssert(sizeof(u64) == 8);

constexpr u8  U8_MAX  = 0xfful;
constexpr u16 U16_MAX = 0xfffful;
constexpr u32 U32_MAX = 0xfffffffful;
constexpr u64 U64_MAX = 0xffffffffffffffffull;

#if defined(ARCHITECTURE_X86) || defined(ARCHITECTURE_ARM_V7)
typedef s32 smm;
typedef u32 umm;
constexpr smm SMM_MIN = S32_MIN;
constexpr smm SMM_MAX = S32_MAX;
constexpr umm UMM_MAX = U32_MAX;
#else
typedef s64 smm;
typedef u64 umm;
constexpr smm SMM_MIN = S64_MIN;
constexpr smm SMM_MAX = S64_MAX;
constexpr umm UMM_MAX = U64_MAX;
#endif

CompileTimeAssert(sizeof(smm) == sizeof(void*));
CompileTimeAssert(sizeof(umm) == sizeof(void*));

typedef u8 byte;

typedef u8  bool8;
typedef u16 bool16;
typedef u32 bool32;
typedef u64 bool64;

typedef u8  flags8;
typedef u16 flags16;
typedef u32 flags32;
typedef u64 flags64;

typedef u8  mask8;
typedef u16 mask16;
typedef u32 mask32;
typedef u64 mask64;

typedef float  f32;
typedef double f64;
CompileTimeAssert(sizeof(f32) == 4);
CompileTimeAssert(sizeof(f64) == 8);

constexpr double F64_MAX = 1.7976931348623158e308;
constexpr float  F32_MAX = 3.402823466e38f;

#define Low8(v16)   (u8 )(v16)
#define Low16(v32)  (u16)(v32)
#define Low32(v64)  (u32)(v64)
#define High8(v16)  (u8 )(((u16)(v16)) >> 8)
#define High16(v32) (u16)(((u32)(v32)) >> 16)
#define High32(v64) (u32)(((u64)(v64)) >> 32)


typedef u64 File_Time;
constexpr File_Time PAST_TIME   = 1ull << 16;  // 01 Jan 1601 00:00:01
constexpr File_Time FUTURE_TIME = 1ull << 61;  // 05 Dec 8907 18:42:01


#define IsPowerOfTwo(x) ((x & (x - 1)) == 0)  // zero considered pow2


inline f32 f32_from_bits(u32 x) { union { u32 i; f32 f; } u; u.i = x; return u.f; }
inline f64 f64_from_bits(u64 x) { union { u64 i; f64 f; } u; u.i = x; return u.f; }
inline u32 bits_from_f32(f32 x) { union { u32 i; f32 f; } u; u.f = x; return u.i; }
inline u64 bits_from_f64(f64 x) { union { u64 i; f64 f; } u; u.f = x; return u.i; }

inline bool is_f64_finite(double x) { return (bits_from_f64(x) & 0x7FFFFFFFFFFFFFFFull) < 0x7FF0000000000000ull; }



////////////////////////////////////////////////////////////////////////////////
// Atomics
////////////////////////////////////////////////////////////////////////////////


union alignas(2) Atomic16
{
    u16 volatile v;
    u8  volatile v8[2];
};

union alignas(4) Atomic32
{
    u32 volatile v;
    u16 volatile v16[2];
    u8  volatile v8 [4];
};

template <typename T>
struct alignas(void*) Atomic_Pointer
{
    T* volatile v;
};

#if defined(ARCHITECTURE_X64) || defined(ARCHITECTURE_ARM64)
union alignas(8) Atomic64
{
    u64 volatile v;
    u32 volatile v32[2];
    u16 volatile v16[4];
    u8  volatile v8 [8];
};

typedef Atomic64 AtomicMM;
#else
typedef Atomic32 AtomicMM;
#endif

#if defined(ARCHITECTURE_X64)
union alignas(16) Atomic128
{
    u64 volatile v64[2];
    u32 volatile v32[4];
    u16 volatile v16[8];
    u8  volatile v8 [16];
};
#endif


inline void pause()
{
#if defined(ARCHITECTURE_X86) || defined(ARCHITECTURE_X64)
    _mm_pause();
#elif defined(ARCHITECTURE_ARM_V7) || defined(ARCHITECTURE_ARM64)
    __asm__ __volatile__("isb\n");
#else
#error "Unsupported"
#endif
}


#if defined(COMPILER_MSVC)
////////////////////////////////////////
// MSVC specific code

#define CompilerFence() _ReadWriteBarrier()
inline void fence() { _mm_mfence(); }

// Atomic16
inline u16 load(Atomic16* atomic)
{
    CompilerFence();
    u16 value = atomic->v;
    CompilerFence();
    return value;
}

inline void store(Atomic16* atomic, u16 value)
{
    CompilerFence();
    atomic->v = value;
    CompilerFence();
}

inline u16 exchange(Atomic16* atomic, u16 new_value)
{
    return (u16) _InterlockedExchange16((short volatile*) &atomic->v, (short) new_value);
}

inline bool compare_exchange(Atomic16* atomic, u16 old_value, u16 new_value)
{
    return _InterlockedCompareExchange16((short volatile*) &atomic->v, (short) new_value, (short) old_value) == (short) old_value;
}

inline u16 compare_exchange_and_return_previous(Atomic16* atomic, u16 old_value, u16 new_value)
{
    return (u16) _InterlockedCompareExchange16((short volatile*) &atomic->v, (short) new_value, (short) old_value);
}

inline void increment                    (Atomic16* atomic) {              _InterlockedIncrement16((short volatile*) &atomic->v);     }
inline u16  increment_and_return_previous(Atomic16* atomic) { return (u16) _InterlockedIncrement16((short volatile*) &atomic->v) - 1; }
inline u16  increment_and_return_new     (Atomic16* atomic) { return (u16) _InterlockedIncrement16((short volatile*) &atomic->v);     }

inline void decrement                    (Atomic16* atomic) {              _InterlockedDecrement16((short volatile*) &atomic->v);     }
inline u16  decrement_and_return_previous(Atomic16* atomic) { return (u16) _InterlockedDecrement16((short volatile*) &atomic->v) + 1; }
inline u16  decrement_and_return_new     (Atomic16* atomic) { return (u16) _InterlockedDecrement16((short volatile*) &atomic->v);     }

inline void add                    (Atomic16* atomic, u16 value) {              _InterlockedExchangeAdd16((short volatile*) &atomic->v, (short) value);         }
inline u16  add_and_return_previous(Atomic16* atomic, u16 value) { return (u16) _InterlockedExchangeAdd16((short volatile*) &atomic->v, (short) value);         }
inline u16  add_and_return_new     (Atomic16* atomic, u16 value) { return (u16) _InterlockedExchangeAdd16((short volatile*) &atomic->v, (short) value) + value; }

// Atomic32
inline u32 load(Atomic32* atomic)
{
    CompilerFence();
    u32 value = atomic->v;
    CompilerFence();
    return value;
}

inline void store(Atomic32* atomic, u32 value)
{
    CompilerFence();
    atomic->v = value;
    CompilerFence();
}

inline u32 exchange(Atomic32* atomic, u32 new_value)
{
    return (u32) _InterlockedExchange((long volatile*) &atomic->v, (long) new_value);
}

inline bool compare_exchange(Atomic32* atomic, u32 old_value, u32 new_value)
{
    return _InterlockedCompareExchange((long volatile*) &atomic->v, (long) new_value, (long) old_value) == (long) old_value;
}

inline u32 compare_exchange_and_return_previous(Atomic32* atomic, u32 old_value, u32 new_value)
{
    return (u32) _InterlockedCompareExchange((long volatile*) &atomic->v, (long) new_value, (long) old_value);
}

inline void increment                    (Atomic32* atomic) {              _InterlockedIncrement((long volatile*) &atomic->v);     }
inline u32  increment_and_return_previous(Atomic32* atomic) { return (u32) _InterlockedIncrement((long volatile*) &atomic->v) - 1; }
inline u32  increment_and_return_new     (Atomic32* atomic) { return (u32) _InterlockedIncrement((long volatile*) &atomic->v);     }

inline void decrement                    (Atomic32* atomic) {              _InterlockedDecrement((long volatile*) &atomic->v);     }
inline u32  decrement_and_return_previous(Atomic32* atomic) { return (u32) _InterlockedDecrement((long volatile*) &atomic->v) + 1; }
inline u32  decrement_and_return_new     (Atomic32* atomic) { return (u32) _InterlockedDecrement((long volatile*) &atomic->v);     }

inline void add                    (Atomic32* atomic, u32 value) {              _InterlockedExchangeAdd((long volatile*) &atomic->v, (long) value);         }
inline u32  add_and_return_previous(Atomic32* atomic, u32 value) { return (u32) _InterlockedExchangeAdd((long volatile*) &atomic->v, (long) value);         }
inline u32  add_and_return_new     (Atomic32* atomic, u32 value) { return (u32) _InterlockedExchangeAdd((long volatile*) &atomic->v, (long) value) + value; }


// Atomic64
#if defined(ARCHITECTURE_X64)

inline u64 load(Atomic64* atomic)
{
    CompilerFence();
    u64 value = atomic->v;
    CompilerFence();
    return value;
}

inline void store(Atomic64* atomic, u64 value)
{
    CompilerFence();
    atomic->v = value;
    CompilerFence();
}

inline u64 exchange(Atomic64* atomic, u64 new_value)
{
    return (u64) _InterlockedExchange64((__int64 volatile*) &atomic->v, (__int64) new_value);
}

inline bool compare_exchange(Atomic64* atomic, u64 old_value, u64 new_value)
{
    return _InterlockedCompareExchange64((__int64 volatile*) &atomic->v, (__int64) new_value, (__int64) old_value) == (__int64) old_value;
}

inline u64 compare_exchange_and_return_previous(Atomic64* atomic, u64 old_value, u64 new_value)
{
    return (u64) _InterlockedCompareExchange64((__int64 volatile*) &atomic->v, (__int64) new_value, (__int64) old_value);
}

inline void increment                    (Atomic64* atomic) {              _InterlockedIncrement64((__int64 volatile*) &atomic->v);     }
inline u64  increment_and_return_previous(Atomic64* atomic) { return (u64) _InterlockedIncrement64((__int64 volatile*) &atomic->v) - 1; }
inline u64  increment_and_return_new     (Atomic64* atomic) { return (u64) _InterlockedIncrement64((__int64 volatile*) &atomic->v);     }

inline void decrement                    (Atomic64* atomic) {              _InterlockedDecrement64((__int64 volatile*) &atomic->v);     }
inline u64  decrement_and_return_previous(Atomic64* atomic) { return (u64) _InterlockedDecrement64((__int64 volatile*) &atomic->v) + 1; }
inline u64  decrement_and_return_new     (Atomic64* atomic) { return (u64) _InterlockedDecrement64((__int64 volatile*) &atomic->v);     }

inline void add                    (Atomic64* atomic, u64 value) {              _InterlockedExchangeAdd64((__int64 volatile*) &atomic->v, (__int64) value);         }
inline u64  add_and_return_previous(Atomic64* atomic, u64 value) { return (u64) _InterlockedExchangeAdd64((__int64 volatile*) &atomic->v, (__int64) value);         }
inline u64  add_and_return_new     (Atomic64* atomic, u64 value) { return (u64) _InterlockedExchangeAdd64((__int64 volatile*) &atomic->v, (__int64) value) + value; }

#endif


// Atomic_Pointer
template <typename T>
inline T* load(Atomic_Pointer<T>* atomic)
{
    CompilerFence();
    T* value = atomic->v;
    CompilerFence();
    return value;
}

template <typename T>
inline void store(Atomic_Pointer<T>* atomic, T* value)
{
    CompilerFence();
    atomic->v = value;
    CompilerFence();
}

template <typename T>
inline T* exchange(Atomic_Pointer<T>* atomic, T* new_value)
{
    return (T*) _InterlockedExchangePointer((void* volatile*) &atomic->v, new_value);
}

template <typename T>
inline bool compare_exchange(Atomic_Pointer<T>* atomic, T* old_value, T* new_value)
{
    return _InterlockedCompareExchangePointer((void* volatile*) &atomic->v, new_value, old_value) == old_value;
}

template <typename T>
inline T* compare_exchange_and_return_previous(Atomic_Pointer<T>* atomic, T* old_value, T* new_value)
{
    return (T*) _InterlockedCompareExchangePointer((void* volatile*) &atomic->v, new_value, old_value);
}


// Atomic128
#if defined(ARCHITECTURE_X64)
// Returns true if the exchange was performed.
// old_value is overwritten with the previous 128-bit value.
inline bool compare_exchange_and_overwrite_with_previous(Atomic128* atomic, void* old_value, void* new_value)
{
    __int64* new64 = (__int64*) new_value;
    return _InterlockedCompareExchange128((__int64 volatile*) atomic, new64[1], new64[0], (__int64*) old_value);
}

inline bool compare_exchange(Atomic128* atomic, void* old_value, void* new_value)
{
    __int64* old64 = (__int64*) old_value;
    __int64 old_value_copy[2] = { old64[0], old64[1] };
    return compare_exchange_and_overwrite_with_previous(atomic, old_value_copy, new_value);
}
#endif

// MSVC specific code
////////////////////////////////////////
#elif defined(COMPILER_GCC) || defined(COMPILER_CLANG)
////////////////////////////////////////
// GCC/CLANG specific code

#define CompilerFence() { asm volatile ("" : : : "memory"); }
inline void fence() { __sync_synchronize(); }

// Atomic16
inline u16 load(Atomic16* atomic)
{
    CompilerFence();
    u16 value = atomic->v;
    CompilerFence();
    return value;
}

inline void store(Atomic16* atomic, u16 value)
{
    CompilerFence();
    atomic->v = value;
    CompilerFence();
}

inline u16 exchange(Atomic16* atomic, u16 new_value)
{
    // @Optimization - InterlockedExchange is a full memory barrier, but
    // __sync_lock_test_and_set is only Acquire. I didn't want to think about this
    // so I just slapped full synchronizations on both sides.
    __sync_synchronize();
    u16 result = __sync_lock_test_and_set(&atomic->v, new_value);
    __sync_synchronize();
    return result;
}

inline bool compare_exchange(Atomic16* atomic, u16 old_value, u16 new_value)
{
    return __sync_bool_compare_and_swap(&atomic->v, old_value, new_value);
}

inline u16 compare_exchange_and_return_previous(Atomic16* atomic, u16 old_value, u16 new_value)
{
    return __sync_val_compare_and_swap(&atomic->v, old_value, new_value);
}

inline void increment                    (Atomic16* atomic) {        __sync_fetch_and_add(&atomic->v, 1); }
inline u16  increment_and_return_previous(Atomic16* atomic) { return __sync_fetch_and_add(&atomic->v, 1); }
inline u16  increment_and_return_new     (Atomic16* atomic) { return __sync_add_and_fetch(&atomic->v, 1); }

inline void decrement                    (Atomic16* atomic) {        __sync_fetch_and_sub(&atomic->v, 1); }
inline u16  decrement_and_return_previous(Atomic16* atomic) { return __sync_fetch_and_sub(&atomic->v, 1); }
inline u16  decrement_and_return_new     (Atomic16* atomic) { return __sync_sub_and_fetch(&atomic->v, 1); }

inline void add                    (Atomic16* atomic, u16 value) {        __sync_fetch_and_add(&atomic->v, value); }
inline u16  add_and_return_previous(Atomic16* atomic, u16 value) { return __sync_fetch_and_add(&atomic->v, value); }
inline u16  add_and_return_new     (Atomic16* atomic, u16 value) { return __sync_add_and_fetch(&atomic->v, value); }

// Atomic32
inline u32 load(Atomic32* atomic)
{
    CompilerFence();
    u32 value = atomic->v;
    CompilerFence();
    return value;
}

inline void store(Atomic32* atomic, u32 value)
{
    CompilerFence();
    atomic->v = value;
    CompilerFence();
}

inline u32 exchange(Atomic32* atomic, u32 new_value)
{
    // @Optimization - InterlockedExchange is a full memory barrier, but
    // __sync_lock_test_and_set is only Acquire. I didn't want to think about this
    // so I just slapped full synchronizations on both sides.
    __sync_synchronize();
    u16 result = __sync_lock_test_and_set(&atomic->v, new_value);
    __sync_synchronize();
    return result;
}

inline bool compare_exchange(Atomic32* atomic, u32 old_value, u32 new_value)
{
    return __sync_bool_compare_and_swap(&atomic->v, old_value, new_value);
}

inline u32 compare_exchange_and_return_previous(Atomic32* atomic, u32 old_value, u32 new_value)
{
    return __sync_val_compare_and_swap(&atomic->v, old_value, new_value);
}

inline void increment                    (Atomic32* atomic) {        __sync_fetch_and_add(&atomic->v, 1); }
inline u32  increment_and_return_previous(Atomic32* atomic) { return __sync_fetch_and_add(&atomic->v, 1); }
inline u32  increment_and_return_new     (Atomic32* atomic) { return __sync_add_and_fetch(&atomic->v, 1); }

inline void decrement                    (Atomic32* atomic) {        __sync_fetch_and_sub(&atomic->v, 1); }
inline u32  decrement_and_return_previous(Atomic32* atomic) { return __sync_fetch_and_sub(&atomic->v, 1); }
inline u32  decrement_and_return_new     (Atomic32* atomic) { return __sync_sub_and_fetch(&atomic->v, 1); }

inline void add                    (Atomic32* atomic, u32 value) {        __sync_fetch_and_add(&atomic->v, value); }
inline u32  add_and_return_previous(Atomic32* atomic, u32 value) { return __sync_fetch_and_add(&atomic->v, value); }
inline u32  add_and_return_new     (Atomic32* atomic, u32 value) { return __sync_add_and_fetch(&atomic->v, value); }


// Atomic64
#if defined(ARCHITECTURE_X64) || defined(ARCHITECTURE_ARM64)

inline u64 load(Atomic64* atomic)
{
    CompilerFence();
    u64 value = atomic->v;
    CompilerFence();
    return value;
}

inline void store(Atomic64* atomic, u64 value)
{
    CompilerFence();
    atomic->v = value;
    CompilerFence();
}

inline u64 exchange(Atomic64* atomic, u64 new_value)
{
    // @Optimization - InterlockedExchange is a full memory barrier, but
    // __sync_lock_test_and_set is only Acquire. I didn't want to think about this
    // so I just slapped full synchronizations on both sides.
    __sync_synchronize();
    u16 result = __sync_lock_test_and_set(&atomic->v, new_value);
    __sync_synchronize();
    return result;
}

inline bool compare_exchange(Atomic64* atomic, u64 old_value, u64 new_value)
{
    return __sync_bool_compare_and_swap(&atomic->v, old_value, new_value);
}

inline u64 compare_exchange_and_return_previous(Atomic64* atomic, u64 old_value, u64 new_value)
{
    return __sync_val_compare_and_swap(&atomic->v, old_value, new_value);
}

inline void increment                    (Atomic64* atomic) {        __sync_fetch_and_add(&atomic->v, 1); }
inline u64  increment_and_return_previous(Atomic64* atomic) { return __sync_fetch_and_add(&atomic->v, 1); }
inline u64  increment_and_return_new     (Atomic64* atomic) { return __sync_add_and_fetch(&atomic->v, 1); }

inline void decrement                    (Atomic64* atomic) {        __sync_fetch_and_sub(&atomic->v, 1); }
inline u64  decrement_and_return_previous(Atomic64* atomic) { return __sync_fetch_and_sub(&atomic->v, 1); }
inline u64  decrement_and_return_new     (Atomic64* atomic) { return __sync_sub_and_fetch(&atomic->v, 1); }

inline void add                    (Atomic64* atomic, u64 value) {        __sync_fetch_and_add(&atomic->v, value); }
inline u64  add_and_return_previous(Atomic64* atomic, u64 value) { return __sync_fetch_and_add(&atomic->v, value); }
inline u64  add_and_return_new     (Atomic64* atomic, u64 value) { return __sync_add_and_fetch(&atomic->v, value); }

#endif


// Atomic_Pointer
template <typename T>
inline T* load(Atomic_Pointer<T>* atomic)
{
    CompilerFence();
    T* value = atomic->v;
    CompilerFence();
    return value;
}

template <typename T>
inline void store(Atomic_Pointer<T>* atomic, T* value)
{
    CompilerFence();
    atomic->v = value;
    CompilerFence();
}

template <typename T>
inline T* exchange(Atomic_Pointer<T>* atomic, T* new_value)
{
    // @Optimization - InterlockedExchange is a full memory barrier, but
    // __sync_lock_test_and_set is only Acquire. I didn't want to think about this
    // so I just slapped full synchronizations on both sides.
    __sync_synchronize();
    T* result = __sync_lock_test_and_set(&atomic->v, new_value);
    __sync_synchronize();
    return result;
}

template <typename T>
inline bool compare_exchange(Atomic_Pointer<T>* atomic, T* old_value, T* new_value)
{
    return __sync_bool_compare_and_swap(&atomic->v, old_value, new_value);
}

template <typename T>
inline T* compare_exchange_and_return_previous(Atomic_Pointer<T>* atomic, T* old_value, T* new_value)
{
    return __sync_val_compare_and_swap(&atomic->v, old_value, new_value);
}

// GCC/CLANG specific code
////////////////////////////////////////
#else
#error "Unsupported"
#endif



////////////////////////////////////////////////////////////////////////////////
// Atomic locks
////////////////////////////////////////////////////////////////////////////////



struct Spinner
{
    u32 count = 1;
    u32 fast_count = 16;
};

void spin(Spinner* spinner);

#define SpinWhile(condition)    \
    if (condition)              \
    {                           \
        Spinner my_spinner;     \
        while (condition)       \
            spin(&my_spinner);  \
    }


inline void atomic_lock(Atomic32* atomic)
{
    SpinWhile(!compare_exchange(atomic, 0, 1));
}

inline void atomic_unlock(Atomic32* atomic)
{
    store(atomic, 0);
}


// Usage:
//
// static Atomic32 atomic = {};
// if (only_once(&atomic))
// {
//     ... do the thing once
//     end_only_once(&atomic);
// }
// ... this code is only reached when the thing already happened

inline bool only_once(Atomic32* atomic)
{
    if (load(atomic) == 1) return false;
    if (compare_exchange(atomic, 0, 2)) return true;
    SpinWhile(load(atomic) == 2);
    return false;
}

inline void end_only_once(Atomic32* atomic)
{
    store(atomic, 1);
}


struct Ticket_Spinlock
{
    Atomic32 ticket;
    Atomic32 owner;
};

inline void lock(Ticket_Spinlock* lock)
{
    u32 ticket = increment_and_return_previous(&lock->ticket);
    SpinWhile(load(&lock->owner) != ticket);
}

inline void unlock(Ticket_Spinlock* lock)
{
    increment(&lock->owner);
}



#define SynchronizedScope() SynchronizedScope_(UniqueIdentifier(synchronized))
#define SynchronizedScope_(name) \
    static Ticket_Spinlock name; \
    lock(&name);                 \
    Defer(unlock(&name));

#define StaticCleanup(code) StaticCleanup_(UniqueIdentifier(static_cleanup), code)
#define StaticCleanup_(name, code) StaticCleanup__(name, code)
#define StaticCleanup__(name, code) static struct name##_struct { ~name##_struct() { code; } } name;



/////////////////////////////////////////////////////////////////////////////////////
// Atomic stack.
/////////////////////////////////////////////////////////////////////////////////////

#if defined(ARCHITECTURE_X64)


#define AtomicStack(T, member) Atomic_Stack_<T, &T::member>

template <typename T, T* T::* M>
struct alignas(2 * sizeof(void*)) Atomic_Stack_
{
    T* volatile top;
    umm volatile pop_count;
};

template <typename T, T* T::* M>
inline void atomic_push(Atomic_Stack_<T, M>* stack, T* value)
{
    Atomic_Pointer<T>* atomic = (Atomic_Pointer<T>*) &stack->top;
    while (true)
    {
        T* top = load(atomic);
        value->*M = top;
        if (compare_exchange(atomic, top, value))
            return;
    }
}



template <typename T, T* T::* M>
inline bool atomic_push(Atomic_Stack_<T, M>* stack, T* value, umm expected_pop_count)
{
    Atomic128* atomic = (Atomic128*) stack;
    Atomic_Stack_<T, M> desire = { value, expected_pop_count };
    Atomic_Stack_<T, M> assume = *stack;
    while (assume.pop_count == expected_pop_count)
    {
        value->*M = assume.top;
        if (compare_exchange_and_overwrite_with_previous(atomic, &assume, &desire))
            return true;
    }

    return false;
}

template <typename T, T* T::* M>
inline T* atomic_pop(Atomic_Stack_<T, M>* stack)
{
    Atomic128* atomic = (Atomic128*) stack;
    Atomic_Stack_<T, M> assume = *stack;
    Atomic_Stack_<T, M> desire;
    while (true)
    {
        T* top = assume.top;
        if (!top) return NULL;

        desire.top = (T*)(top->*M);
        desire.pop_count = assume.pop_count + 1;
        if (compare_exchange_and_overwrite_with_previous(atomic, &assume, &desire))
            return top;
    }
}

template <typename T, T* T::* M>
inline T* atomic_pop_all(Atomic_Stack_<T, M>* stack)
{
    Atomic128* atomic = (Atomic128*) stack;
    Atomic_Stack_<T, M> assume = *stack;
    Atomic_Stack_<T, M> desire;
    desire.top = NULL;
    while (true)
    {
        desire.pop_count = assume.pop_count + 1;
        if (compare_exchange_and_overwrite_with_previous(atomic, &assume, &desire))
            return assume.top;
    }
}

template <typename T, T* T::* M>
inline T* atomic_pop_all_without_incremeting_pop_count(Atomic_Stack_<T, M>* stack)
{
    Atomic128* atomic = (Atomic128*) stack;
    Atomic_Stack_<T, M> assume = *stack;
    Atomic_Stack_<T, M> desire;
    desire.top = NULL;
    while (true)
    {
        if (!assume.top) return NULL;
        desire.pop_count = assume.pop_count;
        if (compare_exchange_and_overwrite_with_previous(atomic, &assume, &desire))
            return assume.top;
    }
}


// This junk is here just so we can use the ranged for syntax on Atomic_Stacks.
// Ughh...

template <typename T, T* T::* M>
struct Atomic_Stack_Iterator
{
    Atomic_Stack_<T, M>* stack;
    T* current;

    inline bool operator!=(Atomic_Stack_Iterator<T, M> other) { return current != other.current; }
    inline void operator++() { current = atomic_pop(stack); }
    inline T*   operator* () { return current; }
};

template <typename T, T* T::* M>
inline Atomic_Stack_Iterator<T, M> begin(Atomic_Stack_<T, M>& stack) { return { &stack, atomic_pop(&stack) }; }

template <typename T, T* T::* M>
inline Atomic_Stack_Iterator<T, M> end(Atomic_Stack_<T, M>& stack) { return { &stack, NULL }; }



#endif


////////////////////////////////////////////////////////////////////////////////
// Memory allocation
////////////////////////////////////////////////////////////////////////////////


#define Kilobyte(count) ((count) * 1024)
#define Megabyte(count) ((count) * 1024 * 1024)
#define Gigabyte(count) ((count) * 1024 * 1024 * 1024)


typedef LK_Region Region;
typedef LK_Region_Cursor Region_Cursor;

extern thread_local Region temporary_memory;
extern thread_local Region* temp;

struct Scope_Region_Cursor
{
    Region* region;
    Region_Cursor cursor;

    inline Scope_Region_Cursor(Region* region)
    : region(region)
    {
        lk_region_cursor(region, &cursor);
    }

    inline ~Scope_Region_Cursor()
    {
        lk_region_rewind(region, &cursor);
    }
};

#define PushValue(region_ptr, type)                    LK_RegionValue(region_ptr, type)
#define PushArray(region_ptr, type, count)             LK_RegionArray(region_ptr, type, count)
#define PushValueA(region_ptr, type, alignment)        LK_RegionValueAligned(region_ptr, type, alignment)
#define PushArrayA(region_ptr, type, count, alignment) LK_RegionArrayAligned(region_ptr, type, count, alignment)


template <typename T, bool Zeroed = true>
inline T* alloc(Region* memory, umm count = 1)
{
#if defined(ARCHITECTURE_X64) || defined(ARCHITECTURE_ARM64)
    #define ALIGNMENT 16
#elif defined(ARCHITECTURE_X86) || defined(ARCHITECTURE_ARM_V7)
    #define ALIGNMENT 8
#else
#error "Unsupported"
#endif

    LeakcheckNextAllocationType(T);
    T* result;
    if (memory)
        result = PushArrayA(memory, T, count, ALIGNMENT);
    else if (Zeroed)
        result = (T*) calloc(sizeof(T) * count, 1);
    else
        result = (T*) malloc(sizeof(T) * count);
    assert(result || !(sizeof(T) * count));
    return result;
}


void* allocate_virtual_memory(umm size, bool high_address_range);  // May return NULL on failure!
void release_virtual_memory(void* base, umm size);


////////////////////////////////////////////////////////////////////////////////
// Atomic tree
////////////////////////////////////////////////////////////////////////////////


template <typename T, umm Indirection, umm BitsPerLayer = 10>
struct Atomic_Tree
{
    using Next = Atomic_Tree<T, Indirection - 1, BitsPerLayer>;

    union
    {
        Atomic_Pointer<Next> link[1 << BitsPerLayer];
        T data[sizeof(link) / sizeof(T)];
        CompileTimeAssert(ArrayCount(data) > 0);
    };

    inline T* at(umm index)
    {
        if constexpr (Indirection == 0)
        {
            assert(index < ArrayCount(data));
            return &data[index];
        }
        else
        {
            umm slot = (index / ArrayCount(data)) >> (BitsPerLayer * (Indirection - 1));
            assert(slot < ArrayCount(link));
            index -= (slot << (BitsPerLayer * (Indirection - 1))) * ArrayCount(data);

            Next* next = load(&link[slot]);
            if (!next)
            {
                next = alloc<Atomic_Tree<T, Indirection - 1, BitsPerLayer>>(NULL);
                if (Next* old = compare_exchange_and_return_previous(&link[slot], (Next*) NULL, next))
                {
                    free(next);
                    next = old;
                }
            }
            return next->at(index);
        }
    }

    inline void clear()
    {
        if constexpr (Indirection > 0)
            for (umm i = 0; i < ArrayCount(link); i++)
                if (Next* next = exchange(&link[i], (Next*) NULL))
                {
                    next->clear();
                    free(next);
                }
    }
};


////////////////////////////////////////////////////////////////////////////////
// Hierarchical memory allocator.
////////////////////////////////////////////////////////////////////////////////


// :HierarchialFreeingRace
// As a rule, you're allowed to free a child only if you're holding a reference
// to the parent. This is to prevent both of them getting deallocated simultaneously,
// which would be a race condition.

void* halloc_(void* parent, umm size, bool zero = true);
void hfree(void* node);
void reference(void* node, u32 to_add = 1);
void on_free_(void* node, void(*callback)(void* memory));

template <typename T, bool Zero = true>
T* halloc(void* parent, umm count = 1)
{
    LeakcheckNextAllocationType(T);
    return (T*) halloc_(parent, sizeof(T) * count, Zero);
}

template <typename T, typename G>
void on_free(T* node, G&& callback)
{
    on_free_(node, (void(*)(void*)) (void*) (void(*)(T*)) callback);
}


////////////////////////////////////////////////////////////////////////////////
// Array
////////////////////////////////////////////////////////////////////////////////



template <typename T>
struct Array
{
    umm count;
    T* address;

    inline T& operator[](umm index)
    {
        assert(index < count);
        return address[index];
    }
};

template<typename T>
inline umm index_from_item_address(Array<T>* array, T* item)
{
    umm index = item - array->address;
    assert(index < array->count);
    return index;
}

template <typename T>
inline Array<T> make_array(T* address, umm count)
{
    Array<T> result;
    result.count = count;
    result.address = address;
    return result;
}

template <typename T, umm N>
inline Array<T> make_array(T(&address)[N])
{
    Array<T> result;
    result.count = N;
    result.address = address;
    return result;
}

template <typename T, umm N>
inline void copy_static_array(T(&address_dest)[N], T const(&address_source)[N])
{
    memcpy(address_dest, address_source, N * sizeof(T));
}

template <typename T, bool Zeroed = true>
inline Array<T> allocate_array(Region* memory, umm count)
{
    Array<T> result;
    result.count = count;
    result.address = alloc<T, Zeroed>(memory, count);
    return result;
}

template <typename T>
inline Array<T> allocate_array(Region* memory, T* data, umm count)
{
    Array<T> result;
    result.count = count;
    result.address = alloc<T>(memory, count);
    memcpy(result.address, data, sizeof(T) * count);
    return result;
}


template <typename T, umm Count>
inline Array<T> allocate_array(Region* memory, T(&data)[Count])
{
    Array<T> result;
    result.count = Count;
    result.address = alloc<T>(memory, Count);
    memcpy(result.address, data, sizeof(T) * Count);
    return result;
}

template <typename T>
inline Array<T> allocate_array(Region* memory, Array<T>* array)
{
    Array<T> result;
    result.count = array->count;
    result.address = alloc<T>(memory, array->count);
    memcpy(result.address, array->address, sizeof(T) * array->count);
    return result;
}

template<typename T>
void free_heap_array(Array<T>* array)
{
    free(array->address);
    array->address = 0;
    array->count = 0;
}

template<typename T>
void unordered_remove_item(Array<T>* array, umm index)
{
    assert(index < array->count);

    T* removed = &array->address[index];
    T* last = &array->address[array->count - 1];

    if (index != array->count - 1)
    {
        memcpy(removed, last, sizeof(T));
    }

    memset(last, 0, sizeof(T));
    array->count--;
}

template<typename T>
void unordered_remove_item(Array<T>* array, T* pointer_to_item)
{
    u32 index = index_from_item_address(array, pointer_to_item);
    unordered_remove_item(array, index);
}

template<typename T>
void ordered_remove_item(Array<T>* array, umm index)
{
    assert(index < array->count);

    if (index != array->count - 1)
    {
        T* removed = &array->address[index];
        memmove(removed, removed + 1, (array->count - index - 1) * sizeof(T));
    }

    T* last = &array->address[array->count - 1];
    memset(last, 0, sizeof(T));
    array->count--;
}

template<typename T>
void ordered_remove_range(Array<T>* array, umm index, umm count)
{
    umm one_past_last = index + count;
    assert(one_past_last <= array->count);
    if (count == 0) return;

    if (one_past_last != array->count)
    {
        T* removed_start     = &array->address[index];
        T* not_removed_start = &array->address[one_past_last];
        memmove(removed_start, not_removed_start, (array->count - one_past_last) * sizeof(T));
    }

    T* zero_start = &array->address[array->count - count];
    memset(zero_start, 0, count * sizeof(T));
    array->count -= count;
}

template<typename T>
void ordered_remove_item(Array<T>* array, T* pointer_to_item)
{
    umm index = index_from_item_address(array, pointer_to_item);
    ordered_remove_item(array, index);
}

template <typename T>
Array<T> subarray(Array<T> a, umm from, umm length)
{
    assert(from <= a.count);
    assert(a.count - from >= length);
    return { length, a.address + from };
}


template <typename T>
struct Array_Iterator
{
    T* current;
    inline bool operator!=(Array_Iterator<T> other) { return current != other.current; }
    inline void operator++() { current++; }
    inline void operator--() { current--; }
    inline T*   operator* () { return current; }
};

template <typename T>
inline Array_Iterator<T> begin(const Array<T>& array) { return { array.address }; }

template <typename T>
inline Array_Iterator<T> end(const Array<T>& array) { return { array.address + array.count }; }

template <typename T>
inline Array_Iterator<T> begin(const Array<T>&& array) { return { array.address }; }

template <typename T>
inline Array_Iterator<T> end(const Array<T>&& array) { return { array.address + array.count }; }



////////////////////////////////////////////////////////////////////////////////
// Dynamic_Array
////////////////////////////////////////////////////////////////////////////////


template<typename T, bool Zeroed = true>
struct Dynamic_Array: Array<T>
{
    umm capacity;
};

template<typename T, bool Zeroed>
void free_heap_array(Dynamic_Array<T, Zeroed>* array)
{
    free(array->address);
    array->address = 0;
    array->count = 0;
    array->capacity = 0;
}

template<typename T, bool Zeroed>
void set_capacity(Dynamic_Array<T, Zeroed>* array, umm new_capacity)
{
    LeakcheckNextAllocationType(T);
    umm old_capacity = array->capacity;
    array->address = (T*) realloc(array->address, new_capacity * sizeof(T));
    assert(array->address || !new_capacity);
    if (Zeroed)
        memset(array->address + old_capacity, 0, (new_capacity - old_capacity) * sizeof(T));
#if defined(DEBUG_BUILD)
    else
        memset(array->address + old_capacity, 0xCD, (new_capacity - old_capacity) * sizeof(T));
#endif
    array->capacity = new_capacity;
}

template<typename T, bool Zeroed>
T* reserve_items(Dynamic_Array<T, Zeroed>* array, umm count)
{
    umm new_count = array->count + count;
    if (new_count > array->capacity)
    {
        umm new_capacity = array->capacity ? array->capacity : 128;
        while (new_count > new_capacity) new_capacity *= 2;
        set_capacity(array, new_capacity);
    }

    T* result = &array->address[array->count];
    array->count = new_count;
    return result;
}

template<typename T, bool Zeroed>
T* reserve_item(Dynamic_Array<T, Zeroed>* array)
{
    return reserve_items(array, 1);
}

template<typename T, bool Zeroed>
void add_items(Dynamic_Array<T, Zeroed>* array, Array<T>* items)
{
    T* destination = reserve_items(array, items->count);
    for (umm i = 0; i < items->count; i++)
        destination[i] = items->address[i];
}

template<typename T, bool Zeroed>
void add_items(Dynamic_Array<T, Zeroed>* array, T* items, umm count)
{
    T* destination = reserve_items(array, count);
    for (umm i = 0; i < count; i++)
        destination[i] = items[i];
}

template<typename T, bool Zeroed>
void add_item(Dynamic_Array<T, Zeroed>* array, T* item)
{
    *reserve_item(array) = *item;
}

template<typename T, bool Zeroed>
void insert_item(Dynamic_Array<T, Zeroed>* array, T* item, umm index)
{
    if (array->count >= array->capacity)
    {
        umm new_capacity = array->capacity * 2;
        if (!new_capacity) new_capacity = 128;

        set_capacity(array, new_capacity);
    }

    if (index < array->count)
    {
        umm move_size = (array->count - index) * sizeof(T);
        T* move_src = array->address + index;
        T* move_dest = move_src + 1;
        memmove(move_dest, move_src, move_size);
    }

    array->address[index] = *item;
    array->count++;
}

template<typename T, bool Zeroed>
void clear_array(Dynamic_Array<T, Zeroed>* array)
{
    if (Zeroed)
        memset(array->address, 0, sizeof(T) * array->count);
#if defined(DEBUG_BUILD)
    else
        memset(array->address, 0xCD, sizeof(T) * array->count);
#endif
    array->count = 0;
}



////////////////////////////////////////////////////////////////////////////////
// Fixed Circular Buffer
////////////////////////////////////////////////////////////////////////////////

template<typename T, umm N>
struct Fixed_Circular_Buffer
{
    CompileTimeAssert(N % 2 == 0);

    umm read_cursor;
    umm write_cursor;
    T data[N];
};

template<typename T, umm N>
bool read_item(Fixed_Circular_Buffer<T, N>* buffer, T* out_item)
{
    if (buffer->read_cursor == buffer->write_cursor)
        return false;

    *out_item = buffer->data[buffer->read_cursor % N];
    buffer->read_cursor++;
    return true;
}

template<typename T, umm N>
bool write_item(Fixed_Circular_Buffer<T, N>* buffer, T* item)
{
    if (buffer->read_cursor + N == buffer->write_cursor)
        return false;

    buffer->data[buffer->write_cursor % N] = *item;
    buffer->write_cursor++;
    return true;
}


////////////////////////////////////////////////////////////////////////////////
// 8-bit strings
// When treated as text, UTF-8 encoding is assumed.
////////////////////////////////////////////////////////////////////////////////


struct String
{
    umm length;
    u8* data;

    inline u8& operator[](umm index)
    {
        assert(index < length);
        return data[index];
    }

    inline operator bool()
    {
        return length != 0;
    }
};

#define StringArgs(string)  (int)((string).length), (const char*)((string).data)

inline String operator ""_s(const char* c_string, umm length) { return { length, (u8*) c_string }; }


String make_string(const char* c_string);                           // Allocates.
char* make_c_style_string(String string, Region* memory = temp);    // Allocates.
char* make_c_style_string_on_heap(String string);                   // Allocates.
String wrap_string(const char* c_string);

void copy_string_to_c_style_buffer(char* destination, umm size, String string);

template <umm N>
inline void copy_string_to_c_style_buffer(char(&destination)[N], String string)
{
    copy_string_to_c_style_buffer(destination, N, string);
}

template <typename T>
inline String memory_as_string(T* value)
{
    String result;
    result.length = sizeof(T);
    result.data = (u8*) value;
    return result;
}

template <typename T>
inline String array_as_string(Array<T> value)
{
    String result;
    result.length = sizeof(T) * value.count;
    result.data = (u8*) value.address;
    return result;
}

template <typename T, umm N>
inline String static_array_as_string(T(&value)[N])
{
    String result;
    result.length = sizeof(T) * N;
    result.data = (u8*) value;
    return result;
}

String allocate_string(Region* memory, String string);
String allocate_zero_string(Region* memory, umm length);
String allocate_uninitialized_string(Region* memory, umm length);
String allocate_string_on_heap(String string);  // The result is heap-allocated and null-terminated.

String make_lowercase_copy(Region* memory, String string);
String make_uppercase_copy(Region* memory, String string);

inline void free_heap_string(String* string)
{
    free(string->data);
    ZeroStruct(string);
}

String concatenate(Region* memory, String first, String second, String third = {}, String fourth = {}, String fifth = {}, String sixth = {});  // Allocates.
String concatenate(String first, String second, String third = {}, String fourth = {}, String fifth = {}, String sixth = {});  // Allocates.
String concatenate(Array<String>* strings, Region* memory);
String substring(String string, umm start_index, umm length);

bool operator==(String lhs, String rhs);
bool operator!=(String lhs, String rhs);

bool prefix_equals(String string, String prefix);
bool suffix_equals(String string, String suffix);

bool compare_case_insensitive(const void* m1, const void* m2, umm length);
bool compare_case_insensitive(String lhs, String rhs);
bool prefix_equals_case_insensitive(String string, String prefix);
bool suffix_equals_case_insensitive(String string, String suffix);

int lexicographic_order(String a, String b);

constexpr umm NOT_FOUND = ~(umm) 0;

umm find_first_occurance(String string, u8 of);
umm find_first_occurance(String string, String of);
umm find_first_occurance_case_insensitive(String string, String of);
umm find_first_occurance_of_any(String string, String any_of);
inline umm find_first_occurance(String string, char of) { return find_first_occurance(string, (u8) of); }

umm find_last_occurance(String string, u8 of);
umm find_last_occurance(String string, String of);
umm find_last_occurance_of_any(String string, String any_of);
inline umm find_last_occurance(String string, char of) { return find_last_occurance(string, (u8) of); }

void replace_all_occurances(String string, u8 what, u8 with_what);
String replace_all_occurances(String string, String what, String with_what, Region* memory);

// Unlike replace_all_occurances, replace_all_occurances_or_return_input doesn't allocate
// if 'what' doesn't appear in 'string'.
String replace_all_occurances_or_return_input(String string, String what, String with_what, Region* memory);

u32 compute_crc32(String data);

// for input "akp", output is "616b70"
String hex_from_bytes(Region* memory, String bytes);

// for input "616b70", output is "akp". on error, output is empty string
String bytes_from_hex(Region* memory, String hex);


////////////////////////////////////////////////////////////////////////////////
// Text reading utilities
////////////////////////////////////////////////////////////////////////////////

#define IsAlpha(c)        (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z') || ((c) == '_'))
#define IsAlphaNumeric(c) (((c) >= '0' && (c) <= '9') || IsAlpha(c))
#define IsSpaceByte(x)    (((x) == ' ') || ((x) >= 9 && (x) <= 13))


String consume(String string, umm amount = 1);
void consume(String* string, umm amount);
String take(String* string, umm amount);
void consume_whitespace(String* string);

String consume_line(String* string);
String consume_line_preserve_whitespace(String* string);
String consume_until(String* string, u8 until_what);
String consume_until(String* string, String until_what);
String consume_until_preserve_whitespace(String* string, u8 until_what);
String consume_until_preserve_whitespace(String* string, String until_what);
String consume_until_any(String* string, String until_any_of);
String consume_until_whitespace(String* string);
String consume_until_last(String* string, u8 until_what);
String consume_until_last(String* string, String until_what);
String consume_until_if_exists(String* string, String until_what);
String consume_identifier(String* string);

String trim(String string);
String trim_front(String string);
String trim_back(String string);
String trim_null_back(String string);

u32 consume_u32(String* string, u32 base = 10);
u64 consume_u64(String* string, u32 base = 10);
s32 consume_s32(String* string, u32 base = 10);
s64 consume_s64(String* string, u32 base = 10);
f32 consume_f32(String* string);
f64 consume_f64(String* string);

u32 u32_from_string(String string, u32 base = 10);
u64 u64_from_string(String string, u32 base = 10);
s32 s32_from_string(String string, u32 base = 10);
s64 s64_from_string(String string, u32 base = 10);
f32 f32_from_string(String string);
f64 f64_from_string(String string);

static constexpr u32 MAX_UTF8_SEQUENCE_LENGTH = 6;
u32 get_utf8_sequence_length(u32 code_point);
void encode_utf8_sequence(u32 code_point, u8* target, u32 length);

bool decode_utf8_sequence(String* string, u32* out_code_point, String* out_consumed = NULL);

String utf8_safe_truncate(String string, umm maximum_bytes);


////////////////////////////////////////////////////////////////////////////////
// Endian utilities
////////////////////////////////////////////////////////////////////////////////


#if defined(COMPILER_MSVC)

ForceInline u16 endian_swap16(u16 value) { return _byteswap_ushort(value); }
ForceInline u32 endian_swap32(u32 value) { return _byteswap_ulong (value); }
ForceInline u64 endian_swap64(u64 value) { return _byteswap_uint64(value); }

#elif defined(COMPILER_GCC) || defined(COMPILER_CLANG)

ForceInline u16 endian_swap16(u16 value) { return __builtin_bswap16(value); }
ForceInline u32 endian_swap32(u32 value) { return __builtin_bswap32(value); }
ForceInline u64 endian_swap64(u64 value) { return __builtin_bswap64(value); }

#else

inline u16 endian_swap16(u16 value)
{
    return (value << 8) | (value >> 8);
}

inline u32 endian_swap32(u32 value)
{
    return (value << 24) | (value >> 24) |
           ((value << 8) & 0xff0000ul) |
           ((value >> 8) & 0x00ff00ul);
}

inline u64 endian_swap64(u64 value)
{
    return ((u64) endian_swap32((u32)(value >>  0)) << 32)
         | ((u64) endian_swap32((u32)(value >> 32)) <<  0);
}

#endif


#if defined(BIG_ENDIAN)
#define IS_LITTLE_ENDIAN 0
#define IS_BIG_ENDIAN 1

// Swaps value to/from LE.
#define endian_swap16_le(value) endian_swap16(value)
#define endian_swap32_le(value) endian_swap32(value)
#define endian_swap64_le(value) endian_swap64(value)

// Swaps value to/from BE.
#define endian_swap16_be(value) (value)
#define endian_swap32_be(value) (value)
#define endian_swap64_be(value) (value)

inline u16 u16le(u16 value) { return endian_swap16(value); }
inline u32 u32le(u32 value) { return endian_swap32(value); }
inline u64 u64le(u64 value) { return endian_swap64(value); }
inline u16 u16be(u16 value) { return value;                }
inline u32 u32be(u32 value) { return value;                }
inline u64 u64be(u64 value) { return value;                }

#define load_u16(addr)          load_u16be(addr)
#define load_u32(addr)          load_u32be(addr)
#define load_u64(addr)          load_u64be(addr)
#define store_u16(addr, value)  store_u16be(addr, value)
#define store_u32(addr, value)  store_u32be(addr, value)
#define store_u64(addr, value)  store_u64be(addr, value)

#else
#define IS_LITTLE_ENDIAN 1
#define IS_BIG_ENDIAN 0

// Swaps value to/from LE.
#define endian_swap16_le(value) (value)
#define endian_swap32_le(value) (value)
#define endian_swap64_le(value) (value)

// Swaps value to/from BE.
#define endian_swap16_be(value) endian_swap16(value)
#define endian_swap32_be(value) endian_swap32(value)
#define endian_swap64_be(value) endian_swap64(value)

inline u16 u16le(u16 value) { return value;                }
inline u32 u32le(u32 value) { return value;                }
inline u64 u64le(u64 value) { return value;                }
inline u16 u16be(u16 value) { return endian_swap16(value); }
inline u32 u32be(u32 value) { return endian_swap32(value); }
inline u64 u64be(u64 value) { return endian_swap64(value); }

#define load_u16(addr)          load_u16le(addr)
#define load_u32(addr)          load_u32le(addr)
#define load_u64(addr)          load_u64le(addr)
#define store_u16(addr, value)  store_u16le(addr, value)
#define store_u32(addr, value)  store_u32le(addr, value)
#define store_u64(addr, value)  store_u64le(addr, value)

#endif


#if ARCHITECTURE_SUPPORTS_UNALIGNED_MEMORY_ACCESS

#define load_u16le(addr)           endian_swap16_le(*(u16 const*)(addr))
#define load_u32le(addr)           endian_swap32_le(*(u32 const*)(addr))
#define load_u64le(addr)           endian_swap64_le(*(u64 const*)(addr))
#define load_u16be(addr)           endian_swap16_be(*(u16 const*)(addr))
#define load_u32be(addr)           endian_swap32_be(*(u32 const*)(addr))
#define load_u64be(addr)           endian_swap64_be(*(u64 const*)(addr))
#define store_u16le(addr, value)   (*(u16*)(addr) = endian_swap16_le((u16)(value)))
#define store_u32le(addr, value)   (*(u32*)(addr) = endian_swap32_le((u32)(value)))
#define store_u64le(addr, value)   (*(u64*)(addr) = endian_swap64_le((u64)(value)))
#define store_u16be(addr, value)   (*(u16*)(addr) = endian_swap16_be((u16)(value)))
#define store_u32be(addr, value)   (*(u32*)(addr) = endian_swap32_be((u32)(value)))
#define store_u64be(addr, value)   (*(u64*)(addr) = endian_swap64_be((u64)(value)))

#else

#define B(T, i, s) (((T) ((u8*) a)[i]) << (s * 8))
inline u16 load_u16le(void const* a) { return B(u16,0,0) | B(u16,1,1); }
inline u32 load_u32le(void const* a) { return B(u32,0,0) | B(u32,1,1) | B(u32,2,2) | B(u32,3,3); }
inline u64 load_u64le(void const* a) { return B(u64,0,0) | B(u64,1,1) | B(u64,2,2) | B(u64,3,3) | B(u64,4,4) | B(u64,5,5) | B(u64,6,6) | B(u64,7,7); }
inline u16 load_u16be(void const* a) { return B(u16,0,1) | B(u16,1,0); }
inline u32 load_u32be(void const* a) { return B(u32,0,3) | B(u32,1,2) | B(u32,2,1) | B(u32,3,0); }
inline u64 load_u64be(void const* a) { return B(u64,0,7) | B(u64,1,6) | B(u64,2,5) | B(u64,3,4) | B(u64,4,3) | B(u64,5,2) | B(u64,6,1) | B(u64,7,0); }
#undef B

#define B(i, s) ((u8*) a)[i] = (u8)(v >> (s * 8));
inline void store_u16le(void* a, u16 v) { B(0,0); B(1,1); }
inline void store_u32le(void* a, u32 v) { B(0,0); B(1,1); B(2,2); B(3,3); }
inline void store_u64le(void* a, u64 v) { B(0,0); B(1,1); B(2,2); B(3,3); B(4,4); B(5,5); B(6,6); B(7,7); }
inline void store_u16be(void* a, u16 v) { B(0,1); B(1,0); }
inline void store_u32be(void* a, u32 v) { B(0,3); B(1,2); B(2,1); B(3,0); }
inline void store_u64be(void* a, u64 v) { B(0,7); B(1,6); B(2,5); B(3,4); B(4,3); B(5,2); B(6,1); B(7,0); }
#undef B

#endif


////////////////////////////////////////////////////////////////////////////////
// Binary reading utilities
////////////////////////////////////////////////////////////////////////////////


bool read_bytes(String* string, void* result, umm count);

bool read_u8 (String* string, u8*  result);
bool read_u16(String* string, u16* result);
bool read_u32(String* string, u32* result);
bool read_u64(String* string, u64* result);

bool read_s8 (String* string, s8*  result);
bool read_s16(String* string, s16* result);
bool read_s32(String* string, s32* result);
bool read_s64(String* string, s64* result);

bool read_f32(String* string, f32* result);
bool read_f64(String* string, f64* result);

// Assuming the architecture is little endian.
inline bool read_u16le(String* string, u16* result) { return read_u16(string, result); }
inline bool read_u32le(String* string, u32* result) { return read_u32(string, result); }
inline bool read_u64le(String* string, u64* result) { return read_u64(string, result); }
inline bool read_s16le(String* string, s16* result) { return read_s16(string, result); }
inline bool read_s32le(String* string, s32* result) { return read_s32(string, result); }
inline bool read_s64le(String* string, s64* result) { return read_s64(string, result); }
inline bool read_f32le(String* string, f32* result) { return read_f32(string, result); }
inline bool read_f64le(String* string, f64* result) { return read_f64(string, result); }

bool read_string(String* string, String* result);

// The following set of functions return 0 if the reading operation fails.
// There is no way to tell if it failed or not by looking at the return value.
// You should check that some other way.

u8  read_u8 (String* string);
u16 read_u16(String* string);
u32 read_u32(String* string);
u64 read_u64(String* string);

s8  read_s8 (String* string);
s16 read_s16(String* string);
s32 read_s32(String* string);
s64 read_s64(String* string);

f32 read_f32(String* string);
f64 read_f64(String* string);

// Assuming the architecture is little endian.
inline u16 read_u16le(String* string) { return read_u16(string); }
inline u32 read_u32le(String* string) { return read_u32(string); }
inline u64 read_u64le(String* string) { return read_u64(string); }
inline s16 read_s16le(String* string) { return read_s16(string); }
inline s32 read_s32le(String* string) { return read_s32(string); }
inline s64 read_s64le(String* string) { return read_s64(string); }
inline f32 read_f32le(String* string) { return read_f32(string); }
inline f64 read_f64le(String* string) { return read_f64(string); }

String read_string(String* string);


inline u16 read_u16be(String* string) { return endian_swap16(read_u16le(string)); }
inline u32 read_u32be(String* string) { return endian_swap32(read_u32le(string)); }
inline u64 read_u64be(String* string) { return endian_swap64(read_u64le(string)); }


////////////////////////////////////////////////////////////////////////////////
// File path utilities
// Convention: directory paths *don't* end with a trailing slash.
////////////////////////////////////////////////////////////////////////////////


String get_file_name(String path);
String get_file_name_without_extension(String path);
String get_file_extension(String path);

String get_parent_directory_path(String path);

bool is_path_absolute(String path);
bool is_path_relative(String path);

String concatenate_path(Region* memory, String a, String b);

enum Path_Comparison_Result
{
    PATHS_POINT_TO_THE_SAME_FILE,
    PATHS_DO_NOT_POINT_TO_THE_SAME_FILE,
    COULD_NOT_CHECK_PATHS_POINT_TO_THE_SAME_FILE,  // recommended: assume they are different
    COULD_NOT_FIND_FILE_A,                         // maybe A doesn't exist, or we don't have permission, or IO error
    COULD_NOT_FIND_FILE_B,                         // maybe B doesn't exist, or we don't have permission, or IO error
    COULD_NOT_FIND_EITHER_FILE,                    // maybe neither files exist, or we don't have permission, or IO error
};

// Function is slow, because it opens files synchronously!
// Despite result enum names, it also works with directories.
Path_Comparison_Result compare_paths(String a, String b);


////////////////////////////////////////////////////////////////////////////////
// 16-bit strings. These mostly exist for interfacing with Windows.
// That's why conversion routines return null terminated strings, and why
// we don't really support any operations on them.
////////////////////////////////////////////////////////////////////////////////


struct String16
{
    umm length;
    u16* data;

    inline u16& operator[](umm index)
    {
        assert(index < length);
        return data[index];
    }

    inline operator bool()
    {
        return length != 0;
    }
};


inline String16 operator ""_s(const wchar_t* c_string, umm length)
{
    String16 result;
    result.length = length;
    result.data = (u16*) c_string;
    return result;
}


String16 make_string16(const u16* c_string);  // Allocates. The returned string is null terminated.
String16 wrap_string16(u16* c_string);

void copy_string_to_c_style_buffer(u16* destination, umm size, String16 string);

// These conversion functions return the length in **code units**! (u8s for UTF-8, u16s for UTF-16)
// If 'target' isn't an empty string, it's expected to point to a sufficiently large buffer.
umm convert_utf8_to_utf16(String16 target, String source);
umm convert_utf16_to_utf8(String target, String16 source);

String16 convert_utf8_to_utf16(String string, Region* memory);  // Allocates. The returned string is null terminated.
String convert_utf16_to_utf8(String16 string, Region* memory);  // Allocates. The returned string is null terminated.


String16 allocate_string(Region* memory, String16 string);
String16 allocate_string_on_heap(String16 string);

String16 concatenate(Region* memory, String16 first, String16 second, String16 third = {}, String16 fourth = {}, String16 fifth = {}, String16 sixth = {});  // Allocates.
String16 concatenate(String16 first, String16 second, String16 third = {}, String16 fourth = {}, String16 fifth = {}, String16 sixth = {});  // Allocates.
String16 substring(String16 string, umm start_index, umm length);

bool operator==(String16 lhs, String16 rhs);
bool prefix_equals(String16 string, String16 prefix);
bool suffix_equals(String16 string, String16 suffix);

umm find_first_occurance(String16 string, u16 of);
umm find_first_occurance(String16 string, String16 of);
umm find_first_occurance_of_any(String16 string, String16 any_of);

umm find_last_occurance(String16 string, u16 of);
umm find_last_occurance(String16 string, String16 of);
umm find_last_occurance_of_any(String16 string, String16 any_of);

bool match_wildcard_string(String wildcard, String string);


// Windows path conversion functions.
wchar_t* make_windows_path(String path);
String16 make_windows_path_string16(String path);  // null-terminated
String make_utf8_path(wchar_t* path);
String make_utf8_path(String16 path);


////////////////////////////////////////////////////////////////////////////////
// 32-bit strings.
// UTF-32 encoding is assumed.
////////////////////////////////////////////////////////////////////////////////


struct String32
{
    umm length;
    u32* data;

    inline u32& operator[](umm index)
    {
        assert(index < length);
        return data[index];
    }

    inline operator bool()
    {
        return length != 0;
    }
};

inline String32 operator ""_s(const char32_t* c_string, umm length)
{
    String32 result;
    result.length = length;
    result.data = (u32*) c_string;
    return result;
}


String32 allocate_string32(Region* memory, String32 string);
String32 allocate_zero_string32(Region* memory, umm length);
String32 allocate_uninitialized_string32(Region* memory, umm length);


// These conversion functions return the length in **code units**! (u8s for UTF-8, u32s for UTF-32)
// If 'target' isn't an empty string, it's expected to point to a sufficiently large buffer.
umm convert_utf8_to_utf32(String32 target, String source);
umm convert_utf32_to_utf8(String target, String32 source);

String32 convert_utf8_to_utf32(String string, Region* memory);  // Allocates. The returned string is null terminated.
String convert_utf32_to_utf8(String32 string, Region* memory);  // Allocates. The returned string is null terminated.



////////////////////////////////////////////////////////////////////////////////
// Region-based string building
// In order for this allocator to be efficient, the string should be relatively small (smaller than
// a region page), and nothing else should be allocated from the Region while building a string.
// If the allocator reaches the end of a page, or if something has been allocated in between,
// the entire string is relocated. This leaves residue in the region.
////////////////////////////////////////////////////////////////////////////////


void* region_builder_reserve(Region* memory, void** base, umm size, umm reservation_size, umm alignment);
void region_builder_append(Region* memory, void** base, umm size, const void* data, umm data_size, umm alignment);
void region_builder_insert(Region* memory, void** base, umm size, umm offset, const void* data, umm data_size, umm alignment);

// String building functions

void append(String* string, Region* memory, String to_append);
void append(String* string, Region* memory, const char* c_string);
void append(String* string, Region* memory, const void* data, umm length);

void insert(String* string, Region* memory, umm at_offset, String to_insert);
void insert(String* string, Region* memory, umm at_offset, const char* c_string);
void insert(String* string, Region* memory, umm at_offset, const void* data, umm length);

// Array building functions

template <typename T>
inline void region_array_append(Region* memory, Array<T>* array, T* items, umm count = 1)
{
    region_builder_append(memory, (void**) &array->address, array->count * sizeof(T), items, count * sizeof(T), alignof(T));
    array->count += count;
}

template <typename T>
inline T* region_array_reserve(Region* memory, Array<T>* array, umm count = 1)
{
    T* result = (T*) region_builder_reserve(memory, (void**) &array->address, array->count * sizeof(T), count * sizeof(T), alignof(T));
    array->count += count;
    return result;
}


////////////////////////////////////////////////////////////////////////////////
// Retirement list
////////////////////////////////////////////////////////////////////////////////


#define RetirementLink      void* next_retired;

template <typename T>
struct Retired
{
    T* head;
};

template <typename T>
void retire(Retired<T>* list, T* value)
{
    value->next_retired = list->head;
    list->head = value;
}

template <typename T>
T* allocate(Region* region, Retired<T>* list)
{
    T* value = list->head;
    if (value)
    {
        list->head = (T*) value->next_retired;
        ZeroStruct(value);
    }
    else
    {
        value = alloc<T>(region);
    }

    return value;
}


////////////////////////////////////////////////////////////////////////////////
// Doubly-linked intrusive list
////////////////////////////////////////////////////////////////////////////////


struct Link
{
    void* prev;
    void* next;
};

#define List(T, member) List_<T, &T::member>

template <typename T, Link T::* M>
struct List_
{
    T* head;
    T* tail;
};

template <typename T, Link T::* M>
inline void link(List_<T, M>* list, T* object)
{
    T* tail = list->tail;
    if (tail)
        (tail->*M).next = object;
    else
        list->head = object;

    (object->*M).prev = tail;
    (object->*M).next = NULL;
    list->tail = object;
}

template <typename T, Link T::* M>
inline void link_head(List_<T, M>* list, T* object)
{
    T* head = list->head;
    if (head)
        (head->*M).prev = object;
    else
        list->tail = object;

    (object->*M).prev = NULL;
    (object->*M).next = head;
    list->head = object;
}

template <typename T, Link T::* M>
inline void link_after(List_<T, M>* list, T* object, T* after_what)
{
    T* next = (T*)(after_what->*M).next;
    if (next)
        (next->*M).prev = object;
    else
        list->tail = object;

    (after_what->*M).next = object;
    (object->*M).prev = after_what;
    (object->*M).next = next;
}

template <typename T, Link T::* M>
inline void link_before(List_<T, M>* list, T* object, T* before_what)
{
    T* prev = (T*)(before_what->*M).prev;
    if (prev)
        (prev->*M).next = object;
    else
        list->head = object;

    (before_what->*M).prev = object;
    (object->*M).prev = prev;
    (object->*M).next = before_what;
}

template <typename T, Link T::* M>
inline void unlink(List_<T, M>* list, T* object)
{
    T* prev = (T*)(object->*M).prev;
    T* next = (T*)(object->*M).next;

    if (prev)
        (prev->*M).next = next;
    else
        list->head = next;

    if (next)
        (next->*M).prev = prev;
    else
        list->tail = prev;
}

template <typename T, Link T::* M>
inline bool in_list(List_<T, M>* list, T* object)
{
    if ((object->*M).prev) return true;
    if ((object->*M).next) return true;
    if (list->head == object) return true;
    if (list->tail == object) return true;
    return false;
}

template <typename T, Link T::* M>
inline void link(List_<T, M>* list, List_<T, M>* to_append)
{
    T* tail = list->tail;
    if (!tail)
    {
        *list = *to_append;
    }
    else
    {
        T* head = to_append->head;
        if (head)
        {
            (tail->*M).next = head;
            (head->*M).prev = tail;
            list->tail = to_append->tail;
        }
    }
}


// This junk is here just so we can use the ranged for syntax on Lists.
// Ughh...

// IT IS SAFE to unlink, retire, free, or do anything with THE CURRENT ITERATOR while iterating over a list.
// IT IS UNSAFE to unlink or retire OTHER ELEMENTS of the list while iterating over a list!

template <typename T, Link T::* M>
struct List_Iterator
{
    T* current;
    T* next;

    inline List_Iterator (T* ptr) { current = ptr; next = ptr ? (T*)(ptr->*M).next : NULL; }
    inline void operator=(T* ptr) { current = ptr; next = ptr ? (T*)(ptr->*M).next : NULL; }

    inline bool operator!=(List_Iterator<T, M> other) { return current != other.current; }
    inline void operator++() { *this = next; }
    inline T*   operator* () { return current; }
};

template <typename T, Link T::* M>
inline List_Iterator<T, M> begin(const List_<T, M>& list) { return list.head; }

template <typename T, Link T::* M>
inline List_Iterator<T, M> end(const List_<T, M>& list) { return NULL; }

template <typename T, Link T::* M>
inline List_Iterator<T, M> begin(const List_<T, M>&& list) { return list.head; }

template <typename T, Link T::* M>
inline List_Iterator<T, M> end(const List_<T, M>&& list) { return NULL; }



////////////////////////////////////////////////////////////////////////////////
// Input buffer
////////////////////////////////////////////////////////////////////////////////

struct Input_Buffer
{
    const byte* start;
    const byte* end;
    const byte* cursor;
    // invariant: start <= cursor < end

    const char* error;

    bool(*refill)(Input_Buffer* buffer);
    // pre-condition: cursor == end (not enforced)
    // post-condition: start == cursor < end
    // returns false if error != NULL
};

inline bool refill(Input_Buffer* buffer)
{
    return buffer->refill(buffer);
}

void skip(Input_Buffer* source, umm size);

void copy_from_buffer(void* target, Input_Buffer* source, umm size);

template <typename T>
inline T read(Input_Buffer* source)
{
    if (source->end - source->cursor >= sizeof(T))
    {
        T* result = (T*) source->cursor;
        source->cursor += sizeof(T);

#if ARCHITECTURE_SUPPORTS_UNALIGNED_MEMORY_ACCESS
        return *result;
#else
        T value;
        memcpy(&value, result, sizeof(T));
        return value;
#endif
    }
    else
    {
        T result;
        copy_from_buffer(&result, source, sizeof(T));
        return result;
    }
}

inline u8 peek_u8(Input_Buffer* source)
{
    if (source->cursor >= source->end) source->refill(source);
    return *source->cursor;
}

// @Endianness @Incomplete

inline u8  read_u8   (Input_Buffer* source) { return read<u8 >(source); }
inline u16 read_u16le(Input_Buffer* source) { return read<u16>(source); }
inline u32 read_u32le(Input_Buffer* source) { return read<u32>(source); }
inline u64 read_u64le(Input_Buffer* source) { return read<u64>(source); }
inline s8  read_s8   (Input_Buffer* source) { return read<s8 >(source); }
inline s16 read_s16le(Input_Buffer* source) { return read<s16>(source); }
inline s32 read_s32le(Input_Buffer* source) { return read<s32>(source); }
inline s64 read_s64le(Input_Buffer* source) { return read<s64>(source); }
inline f32 read_f32le(Input_Buffer* source) { return read<f32>(source); }
inline f64 read_f64le(Input_Buffer* source) { return read<f64>(source); }
inline u16 read_u16be(Input_Buffer* source) { return endian_swap16(read<u16>(source)); }
inline u32 read_u32be(Input_Buffer* source) { return endian_swap32(read<u32>(source)); }
inline u64 read_u64be(Input_Buffer* source) { return endian_swap64(read<u64>(source)); }
inline s16 read_s16be(Input_Buffer* source) { return endian_swap16(read<s16>(source)); }
inline s32 read_s32be(Input_Buffer* source) { return endian_swap32(read<s32>(source)); }
inline s64 read_s64be(Input_Buffer* source) { return endian_swap64(read<s64>(source)); }
inline f32 read_f32be(Input_Buffer* source) { return f32_from_bits(endian_swap32(read<u32>(source))); }
inline f64 read_f64be(Input_Buffer* source) { return f64_from_bits(endian_swap64(read<u64>(source))); }

String read_entire_buffer(Input_Buffer* source, Region* memory);
void skip_entire_buffer(Input_Buffer* source);

inline String read_string(Input_Buffer* source, Region* memory)
{
    u64 count = read_u64le(source);
    String data = allocate_uninitialized_string(memory, count);
    copy_from_buffer(data.data, source, data.length);
    return data;
}


bool fail(Input_Buffer* buffer, const char* error);  // zero refill, sets the error, returns false
bool fail_eof(Input_Buffer* buffer);                 // fails with EOF, returns false
bool failed_eof(Input_Buffer* buffer);               // returns true if buffer failed with EOF

void make_zero_input_buffer(Input_Buffer* buffer);
void make_memory_input_buffer(Input_Buffer* buffer, void* base, umm size);
Input_Buffer make_memory_input_buffer(String data);


struct Substring_Input_Buffer: Input_Buffer
{
    Input_Buffer* source;
    umm remaining;
};

void make_substring_input_buffer(Substring_Input_Buffer* buffer, Input_Buffer* source, umm length);


struct Concatenator_Input_Buffer: Input_Buffer
{
    Array<String> strings;  // up to the user how the array and strings are allocated
    umm next_string_index;
};

void make_concatenator_input_buffer(Concatenator_Input_Buffer* buffer, Array<String> strings);

// If true, *prefix contains everything before matched 'until_what', and the *in cursor is
// right after the matched 'until_what', in a non-error state.
// If false, *prefix is zeroed, *in was consumed up until reaching an error state,
// or until 'seek_limit' bytes were read.
//
// prefix->strings is allocated from 'memory' which may be NULL meaning heap.
// If copy_strings is set, the strings are copied to 'memory', otherwise they point to memory
// returned by the source buffer.
bool consume_until(Input_Buffer* in, Concatenator_Input_Buffer* prefix, String until_what,
                   Region* memory, bool copy_strings, umm seek_limit);


////////////////////////////////////////////////////////////////////////////////
// Output buffer
////////////////////////////////////////////////////////////////////////////////

struct Output_Buffer
{
    byte* start;
    byte* end;
    byte* cursor;
    // invariant: start <= cursor < end

    const char* error;  // check AFTER CLOSING, since closing may commit

    bool(*commit)(Output_Buffer* buffer);
    // post-condition: start == cursor < end
    // returns false if error != NULL
};

inline bool commit(Output_Buffer* buffer)
{
    bool ok = buffer->commit(buffer);
    assert(buffer->cursor < buffer->end);
    return ok;
}

void copy_to_buffer(Output_Buffer* target, const void* source, umm size);

template <typename T>
inline void write(Output_Buffer* target, T value)
{
    if (target->end - target->cursor >= sizeof(T))
    {
#if ARCHITECTURE_SUPPORTS_UNALIGNED_MEMORY_ACCESS
        *(T*) target->cursor = value;
#else
        memcpy(target->cursor, &value, sizeof(T));
#endif
        target->cursor += sizeof(T);
    }
    else
    {
        copy_to_buffer(target, &value, sizeof(T));
    }
}

inline void write(Output_Buffer* target, String string)
{
    copy_to_buffer(target, string.data, string.length);
}

inline void write_u8   (Output_Buffer* target, u8  v) { write<u8 >(target, v); }
inline void write_u16le(Output_Buffer* target, u16 v) { write<u16>(target, v); }
inline void write_u32le(Output_Buffer* target, u32 v) { write<u32>(target, v); }
inline void write_u64le(Output_Buffer* target, u64 v) { write<u64>(target, v); }
inline void write_f32le(Output_Buffer* target, f32 v) { write<f32>(target, v); }
inline void write_f64le(Output_Buffer* target, f64 v) { write<f64>(target, v); }
inline void write_u16be(Output_Buffer* target, u16 v) { write<u16>(target, endian_swap16(v)); }
inline void write_u32be(Output_Buffer* target, u32 v) { write<u32>(target, endian_swap32(v)); }
inline void write_u64be(Output_Buffer* target, u64 v) { write<u64>(target, endian_swap64(v)); }
inline void write_f32be(Output_Buffer* target, f32 v) { write<u32>(target, endian_swap32(bits_from_f32(v))); }
inline void write_f64be(Output_Buffer* target, f64 v) { write<u64>(target, endian_swap64(bits_from_f64(v))); }

inline void write_string(Output_Buffer* target, String string)
{
    write_u64le(target, string.length);
    write(target, string);
}

bool fail(Output_Buffer* buffer, const char* error);  // void commit, sets the error, returns false

void make_void_output_buffer(Output_Buffer* buffer);
void make_memory_output_buffer(Output_Buffer* buffer, void* base, umm size);



struct String_Concatenator_Output_Buffer: Output_Buffer
{
    struct Chunk
    {
        Chunk* next;
        umm size;
    };

    Chunk* head;
    umm total_size;
};

void make_string_concatenator_output_buffer(String_Concatenator_Output_Buffer* buffer);
String resolve_to_string_and_free(String_Concatenator_Output_Buffer* buffer, Region* memory);


////////////////////////////////////////////////////////////////////////////////
// Speculative input/output stream
////////////////////////////////////////////////////////////////////////////////


// I/O on SPSC_Buffered_Stream is speculative,
// meaning the read or write can be either committed or dropped.
//
// When speculatively reading:
//  - start at current_buffer = head
//  - return one or two ranges with data (two since it's circular)
//  - when reached the end of current_buffer, current_buffer = current_buffer->next
//  - if current_buffer is null, reading has failed (insufficient data)
//  - repeat
// When committing a read:
//  - update read_cursor in current_buffer
//  - if current_buffer != head, free all buffers from head to current_buffer, not including
//    the current buffer, and not including the last buffer if current_buffer is null
// When dropping a read:
//  - do nothing
//
// When speculatively writing:
//  - start at current_buffer = tail
//  - return one or two ranges that can be written to
//  - when reached the end, current_buffer = new buffer, not attached to tail yet
//  - repeat
// When committing a write:
//  - update write_cursor in tail (this must happen first!)
//  - attach new buffers, if any, which will update tail->next and tail
// When dropping a write:
//  - free new buffers, if any

struct SPSC_Buffered_Stream
{
    struct Buffer
    {
        // cursor overflow is ok, because buffer_size is power of 2
        Atomic32 read_cursor;
        Atomic32 write_cursor;
        Atomic_Pointer<Buffer> next;  // once set, write_cursor may not change
        byte data[0];
    };

    u32 buffer_size;  // must be power of 2
    Atomic_Pointer<Buffer> head;
    Atomic_Pointer<Buffer> tail;
    AtomicMM available;
};

struct SPSC_Buffered_Stream_Input: Input_Buffer
{
    SPSC_Buffered_Stream::Buffer* buffer;
    SPSC_Buffered_Stream::Buffer* next;
    u32 read_cursor;
    u32 write_cursor;
    u32 buffer_size;
    u64 total_length;
};

struct SPSC_Buffered_Stream_Output: Output_Buffer
{
    SPSC_Buffered_Stream::Buffer* tail_next;
    u32 tail_write_cursor;

    SPSC_Buffered_Stream::Buffer* buffer;
    u32 read_cursor;
    u32 write_cursor;
    u32 buffer_size;
    u64 total_length;
};


void make_spsc_buffered_stream(SPSC_Buffered_Stream* stream, umm buffer_size);
void free_spsc_buffered_stream(SPSC_Buffered_Stream* stream);

void begin_input(SPSC_Buffered_Stream* stream, SPSC_Buffered_Stream_Input* input);
void commit_input(SPSC_Buffered_Stream* stream, SPSC_Buffered_Stream_Input* input);
// drop_input doesn't need to exist, you just stop reading and don't do anything special

void begin_output(SPSC_Buffered_Stream* stream, SPSC_Buffered_Stream_Output* output);
void commit_output(SPSC_Buffered_Stream* stream, SPSC_Buffered_Stream_Output* output);
void drop_output(SPSC_Buffered_Stream* stream, SPSC_Buffered_Stream_Output* output);


////////////////////////////////////////////////////////////////////////////////
// Concatenator
////////////////////////////////////////////////////////////////////////////////



template <typename T>
struct Concatenator
{
    struct Chunk
    {
        Chunk* next;
        umm count;
        T array[0];
    };

    Region* memory;

    Chunk* head;
    Chunk* tail;

    T*  base;
    umm count;
    umm remaining;
    umm capacity;
};

template <typename T>
void free_concatenator(Concatenator<T>* cat)
{
    if (cat->memory)
    {
        ZeroStruct(cat);
        return;
    }

    typedef typename Concatenator<T>::Chunk Chunk;
    Chunk* tail = cat->tail;
    if (!tail) return;

    Chunk* chunk = cat->head;
    while (chunk != tail)
    {
        Chunk* next = chunk->next;
        free(chunk);
        chunk = next;
    }
    free(tail);

    ZeroStruct(cat);
}

// After stealing, the source concatenator is left empty. There's no need to free it.
template <typename T>
inline void steal(Concatenator<T>* cat, Concatenator<T>* source)
{
    if (!source->head) return;

    if (cat->tail)
    {
        cat->tail->next  = source->head;
        cat->tail->count = cat->capacity - cat->remaining;
    }
    else
    {
        cat->head = source->head;
    }

    cat->tail      = source->tail;
    cat->base      = source->base - cat->count;
    cat->remaining = source->remaining;
    cat->capacity  = source->capacity;
    cat->count    += source->count;

    ZeroStruct(source);
}

template <typename T>
void append_chunk(Concatenator<T>* cat, umm minimum_capacity)
{
    umm new_capacity = cat->capacity;
    if (!new_capacity) new_capacity = 64;

    do { new_capacity *= 2; }
    while (new_capacity < minimum_capacity);

    typedef typename Concatenator<T>::Chunk Chunk;
    umm size = new_capacity * sizeof(T) + sizeof(Chunk);
    Chunk* chunk = (Chunk*) alloc<u8, false>(cat->memory, size);

    if (cat->tail)
    {
        cat->tail->next = chunk;
        cat->tail->count = cat->capacity - cat->remaining;
    }
    else
    {
        cat->head = chunk;
    }
    cat->tail = chunk;

    cat->base      = chunk->array - cat->count;
    cat->remaining = new_capacity;
    cat->capacity  = new_capacity;
}



template <typename T>
inline void ensure_space(Concatenator<T>* cat, umm count)
{
    if (count > cat->remaining)
        append_chunk(cat, count);
}

template <typename T>
inline T* reserve_items_without_increasing_count(Concatenator<T>* cat, umm count)
{
    ensure_space(cat, count);
    return cat->base + cat->count;
}

// Remember to ensure enough space is available in the current concatenator chunk!
// IMPORTANT!  Memory allocated by Concatenators is not zeroed!
template <typename T>
inline T* reserve_items_unchecked(Concatenator<T>* cat, umm count)
{
    assert(count <= cat->remaining);
    T* items = cat->base + cat->count;
    cat->remaining -= count;
    cat->count += count;
    return items;
}

// Remember to ensure enough space is available in the current concatenator chunk!
// IMPORTANT!  Memory allocated by Concatenators is not zeroed!
template <typename T>
inline T* reserve_item_unchecked(Concatenator<T>* cat)
{
    return reserve_items_unchecked(cat, 1);
}

// Remember to ensure enough space is available in the current concatenator chunk!
template <typename T>
inline void add_item_unchecked(Concatenator<T>* cat, T* item)
{
    *reserve_items_unchecked(cat, 1) = *item;
}



// IMPORTANT!  Memory allocated by Concatenators is not zeroed!
template <typename T>
inline T* reserve_items(Concatenator<T>* cat, umm count)
{
    ensure_space(cat, count);
    return reserve_items_unchecked(cat, count);
}

// IMPORTANT!  Memory allocated by Concatenators is not zeroed!
template <typename T>
inline T* reserve_item(Concatenator<T>* cat)
{
    ensure_space(cat, 1);
    return reserve_items_unchecked(cat, 1);
}

template <typename T>
inline void add_item(Concatenator<T>* cat, T* item)
{
    ensure_space(cat, 1);
    return add_item_unchecked(cat, item);
}

template <typename T>
inline void add_items(Concatenator<T>* cat, T* item, umm count)
{
    ensure_space(cat, count);
    T* destination = reserve_items_unchecked(cat, count);
    memcpy(destination, item, sizeof(T) * count);
}

template <typename T>
inline void add_items(Concatenator<T>* cat, Array<T> items)
{
    add_items(cat, items.address, items.count);
}



template <typename T> struct Remove_Pointer      { typedef T type; };
template <typename T> struct Remove_Pointer<T*>  { typedef T type; };
template <typename T> struct Remove_Pointer<T[]> { typedef T type; };
template <typename T, umm N> struct Remove_Pointer<T[N]> { typedef T type; };

#define ForConcatenatorChunks(cat_ptr, it_base, it_count, code)   \
{                                                                 \
    auto* cat_ = (cat_ptr);                                       \
    /* cat_ is a Concatenator<T>*, we need to get to Chunk<T> */  \
    typedef typename Remove_Pointer<decltype(cat_)>::type::Chunk Chunk_; \
                                                                  \
    Chunk_* tail_ = cat_->tail;                                   \
    if (tail_)                                                    \
    {                                                             \
        Chunk_* chunk_ = cat_->head;                              \
        for (; chunk_ != tail_; chunk_ = chunk_->next)            \
        {                                                         \
            auto* it_base = chunk_->array;                        \
            umm it_count = chunk_->count;                         \
            { code; }                                             \
        }                                                         \
        /* dummy loop so we can break/continue in code */         \
        while (chunk_ == tail_)                                   \
        {                                                         \
            auto* it_base = tail_->array;                         \
            umm it_count = cat_->capacity - cat_->remaining;      \
            { code; }                                             \
            break;                                                \
        }                                                         \
    }                                                             \
}

#define ForEachInConcatenator(cat_ptr, it, code)                          \
{                                                                         \
    auto* cat_ = (cat_ptr);                                               \
    /* cat_ is a Concatenator<T>*, we need to get to Chunk<T> */          \
    typedef typename Remove_Pointer<decltype(cat_)>::type::Chunk Chunk_;  \
                                                                          \
    Chunk_* tail_ = cat_->tail;                                           \
    if (tail_)                                                            \
    {                                                                     \
        Chunk_* chunk_ = cat_->head;                                      \
        for (; chunk_ != tail_; chunk_ = chunk_->next)                    \
        {                                                                 \
            auto* it = chunk_->array;                                     \
            auto* _it_end = it + chunk_->count;                           \
            for (; it != _it_end; it++)                                   \
                { code; }                                                 \
            if (it != _it_end) break;                                     \
        }                                                                 \
        if (chunk_ == tail_)                                              \
        {                                                                 \
            auto* it = tail_->array;                                      \
            auto* _it_end = it + (cat_->capacity - cat_->remaining);      \
            for (; it != _it_end; it++)                                   \
                { code; }                                                 \
        }                                                                 \
    }                                                                     \
}


template <typename T>
void resolve_to_array(Concatenator<T> const* cat, Array<T> array)
{
    assert(array.count >= cat->count);
    T* cursor = array.address;
    ForConcatenatorChunks(cat, array, count,
    {
        memcpy(cursor, array, count * sizeof(T));
        cursor += count;
    })
}

template <typename T>
inline void resolve_to_array_and_free(Concatenator<T>* cat, Array<T> array)
{
    resolve_to_array(cat, array);
    free_concatenator(cat);
}

template <typename T>
Array<T> resolve_to_array(Concatenator<T> const* cat, Region* memory)
{
    Array<T> array;
    array.count = cat->count;
    array.address = alloc<T, false>(memory, array.count);
    resolve_to_array(cat, array);
    return array;
}

template <typename T>
inline Array<T> resolve_to_array_and_free(Concatenator<T>* cat, Region* memory)
{
    Array<T> result = resolve_to_array(cat, memory);
    free_concatenator(cat);
    return result;
}




using String_Concatenator = Concatenator<u8>;

// IMPORTANT!  Memory allocated by Concatenators is not zeroed!
template <typename T>
inline T* reserve(String_Concatenator* cat) { return (T*) reserve_items(cat, sizeof(T)); }
inline void* reserve(String_Concatenator* cat, umm length) { return reserve_items(cat, length); }

inline void add(String_Concatenator* cat, const void* data, umm length) { memcpy(reserve_items(cat, length), data, length); }
inline void add(String_Concatenator* cat, const char* c_string) { add(cat, c_string, strlen(c_string)); }
inline void add(String_Concatenator* cat, String string) { add(cat, string.data, string.length); }

inline void add_u8 (String_Concatenator* cat, u8  value) { add(cat, &value, sizeof(value)); }
inline void add_u16(String_Concatenator* cat, u16 value) { add(cat, &value, sizeof(value)); }
inline void add_u32(String_Concatenator* cat, u32 value) { add(cat, &value, sizeof(value)); }
inline void add_u64(String_Concatenator* cat, u64 value) { add(cat, &value, sizeof(value)); }

inline void add_s8 (String_Concatenator* cat, s8  value) { add(cat, &value, sizeof(value)); }
inline void add_s16(String_Concatenator* cat, s16 value) { add(cat, &value, sizeof(value)); }
inline void add_s32(String_Concatenator* cat, s32 value) { add(cat, &value, sizeof(value)); }
inline void add_s64(String_Concatenator* cat, s64 value) { add(cat, &value, sizeof(value)); }

inline void add_f32(String_Concatenator* cat, f32 value) { add(cat, &value, sizeof(value)); }
inline void add_f64(String_Concatenator* cat, f64 value) { add(cat, &value, sizeof(value)); }

inline void add_u16le(String_Concatenator* cat, u16 value) { add_u16(cat, value); }
inline void add_u32le(String_Concatenator* cat, u32 value) { add_u32(cat, value); }
inline void add_u64le(String_Concatenator* cat, u64 value) { add_u64(cat, value); }
inline void add_s16le(String_Concatenator* cat, s16 value) { add_u16(cat, (u16) value); }
inline void add_s32le(String_Concatenator* cat, s32 value) { add_u32(cat, (u32) value); }
inline void add_s64le(String_Concatenator* cat, s64 value) { add_u64(cat, (u64) value); }
inline void add_f32le(String_Concatenator* cat, f32 value) { add_u32(cat, bits_from_f32(value)); }
inline void add_f64le(String_Concatenator* cat, f64 value) { add_u64(cat, bits_from_f64(value)); }

inline void add_u16be(String_Concatenator* cat, u16 value) { add_u16(cat, endian_swap16(value)); }
inline void add_u32be(String_Concatenator* cat, u32 value) { add_u32(cat, endian_swap32(value)); }
inline void add_u64be(String_Concatenator* cat, u64 value) { add_u64(cat, endian_swap64(value)); }
inline void add_s16be(String_Concatenator* cat, s16 value) { add_u16(cat, endian_swap16((u16) value)); }
inline void add_s32be(String_Concatenator* cat, s32 value) { add_u32(cat, endian_swap32((u32) value)); }
inline void add_s64be(String_Concatenator* cat, s64 value) { add_u64(cat, endian_swap64((u64) value)); }
inline void add_f32be(String_Concatenator* cat, f32 value) { add_u32(cat, endian_swap32(bits_from_f32(value))); }
inline void add_f64be(String_Concatenator* cat, f64 value) { add_u64(cat, endian_swap64(bits_from_f64(value))); }


inline void add_string(String_Concatenator* cat, String value)
{
    add_u64(cat, (u64) value.length);
    add(cat, value);
}

inline String resolve_to_string(String_Concatenator const* cat, Region* memory)
{
    Array<u8> array = resolve_to_array(cat, memory);
    String result;
    result.length = array.count;
    result.data = array.address;
    return result;
};

inline String resolve_to_string_and_free(String_Concatenator* cat, Region* memory)
{
    String result = resolve_to_string(cat, memory);
    free_concatenator(cat);
    return result;
}


template <u8 Threshold = 128, u8 ForcedLiteralBytes = 0>
inline void add_variable_length_u64(String_Concatenator* cat, u64 value)
{
    CompileTimeAssert(Threshold > 0 && Threshold <= 255);
    constexpr u8 DIVISOR = 256 - Threshold;

    for (u8 i = 0; i < ForcedLiteralBytes; i++)
    {
        add_u8(cat, value & 0xFF);
        value >>= 8;
    }

    while (value >= Threshold)
    {
        value -= Threshold;
        add_u8(cat, Threshold + (value % DIVISOR));
        value /= DIVISOR;
    }
    add_u8(cat, value);
}

template <u8 Threshold = 128, u8 ForcedLiteralBytes = 0>
inline u64 read_variable_length_u64(String* data)
{
    CompileTimeAssert(Threshold > 0 && Threshold <= 255);
    constexpr u8 DIVISOR = 256 - Threshold;

    u64 value = 0;
    for (u8 i = 0; i < ForcedLiteralBytes; i++)
        value = (value << 8) | read_u8(data);

    u64 multiplier = 1;
    while (true)
    {
        u8 b = read_u8(data);
        value += b * multiplier;
        if (b < Threshold) break;
        multiplier *= DIVISOR;
    }
    return value;
}

// Preprocessing: sign bit is placed in LSB place (rotate left by 1),
// and if sign bit is set, the other bits are inverted.
// The preprocessed number is then encoded as u64.
template <u8 Threshold = 128, u8 ForcedLiteralBytes = 0>
inline void add_variable_length_s64(String_Concatenator* cat, s64 value)
{
    u64 preprocessed = (u64) value;
    preprocessed = (preprocessed << 1) | (preprocessed >> 63);
    if (preprocessed & 1)
        preprocessed = ~preprocessed | 1;
    add_variable_length_u64<Threshold, ForcedLiteralBytes>(cat, preprocessed);
}

template <u8 Threshold = 128, u8 ForcedLiteralBytes = 0>
inline u64 read_variable_length_s64(String* data)
{
    u64 preprocessed = read_variable_length_u64<Threshold, ForcedLiteralBytes>(data);
    return (s64)((preprocessed & 1) ? ~(preprocessed >> 1) : (preprocessed >> 1));
}



////////////////////////////////////////////////////////////////////////////////
// Text formatting utilities
////////////////////////////////////////////////////////////////////////////////


#if defined(COMPILER_MSVC) && defined(ARCHITECTURE_X64)
    #define GetLog2_64(destination, value, return_if_zero)          \
    {                                                               \
        if (!_BitScanReverse64(&(destination), (value)))            \
            return (return_if_zero);  /* value == 0 */              \
    }
#elif defined(COMPILER_MSVC) && defined(ARCHITECTURE_X86)
    #define GetLog2_64(destination, value, return_if_zero)          \
    {                                                               \
        if (_BitScanReverse(&(destination), (u32)((value) >> 32)))  \
            (destination) += 32;                                    \
        else if (!_BitScanReverse(&(destination), (u32)(value)))    \
            return (return_if_zero);  /* value == 0 */              \
    }
#elif defined(COMPILER_GCC) || defined(COMPILER_CLANG)
    #define GetLog2_64(destination, value, return_if_zero)          \
    {                                                               \
        if (!(value))                                               \
            return (return_if_zero);  /* value == 0 */              \
        (destination) = 63 - __builtin_clzll((value));              \
    }
#else
#error "Unsupported"
#endif


inline umm digits_base16_u64(u64 value)
{
    unsigned long log2;
    GetLog2_64(log2, value, 1);
    return (log2 >> 2) + 1;
}

inline umm digits_base256_u64(u64 value)
{
    unsigned long log2;
    GetLog2_64(log2, value, 1);
    return (log2 >> 3) + 1;
}


static constexpr u64 POWERS_OF_TEN[20] = {
    1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull, 10000000ull,
    100000000ull, 1000000000ull, 10000000000ull, 100000000000ull, 1000000000000ull,
    10000000000000ull, 100000000000000ull, 1000000000000000ull, 10000000000000000ull,
    100000000000000000ull, 1000000000000000000ull, 10000000000000000000ull,
};

inline umm digits_base10_u64(u64 value)
{
    unsigned long log2;
    GetLog2_64(log2, value, 1);
    umm digits = ((log2 + 1) * 1233) >> 12;  // 1/log2(10) ~ 1233/4096
    return 1 + digits - (value < POWERS_OF_TEN[digits]);
}

// digits MUST be equal to digits_base10_u64(value),
// and destination MUST have at least that many bytes.
void write_base10_u64(u8* destination, umm digits, u64 value);

inline void add_base10_u64(String_Concatenator* cat, u64 value)
{
    umm digits = digits_base10_u64(value);
    write_base10_u64((u8*) reserve(cat, digits), digits, value);
}

inline void add_base10_s64(String_Concatenator* cat, s64 value)
{
    if (value < 0)
    {
        u64 abs = (u64) -value;
        umm digits = digits_base10_u64(abs);
        u8* memory = (u8*) reserve(cat, digits + 1);
        memory[0] = '-';
        write_base10_u64(memory + 1, digits, abs);
    }
    else
    {
        u64 abs = (u64) value;
        umm digits = digits_base10_u64(abs);
        u8* memory = (u8*) reserve(cat, digits);
        write_base10_u64(memory, digits, abs);
    }
}



////////////////////////////////////////////////////////////////////////////////
// Safe text formatting.
////////////////////////////////////////////////////////////////////////////////


// Format makes a single allocation and writes formatted text to it.
// 'memory' can be a Region* or NULL (heap-allocated). It's safe to write large text to regions.
// 'string' has to be a literal. Arguments are inserted on % (short form) or %~ (long form).
// Literal % is escaped as %%.
#define Format(memory, string, ...)                                               \
    ApplicationNamespace::format<count_percent_signs(string)>(memory, string,     \
        CompileTimeU32(format_literal_length(string)),                            \
        ##__VA_ARGS__)

// FormatC is the same as Format, but returns a char*
#define FormatC(memory, string, ...) ((char*) (Format(memory, string, ##__VA_ARGS__)).data)

// FormatAppend makes a single region-builder reservation and writes formatted text to it.
// 'destination_string' is a String* being built.
// 'memory' is a Region* that 'destination_string' is being built in.
// It is safe to make a SINGLE large append, but NOT repeated appends!
// 'string' has to be a literal. Look at Format() for the syntax description.
#define FormatAppend(destination_string, memory, string, ...)                                             \
    ApplicationNamespace::format_append<count_percent_signs(string)>(destination_string, memory, string,  \
        CompileTimeU32(format_literal_length(string)), ##__VA_ARGS__)

// FormatAdd makes a single concatenator reservation and writes formatted text to it.
// 'destination_cat' is a String_Concatenator*.
// 'string' has to be a literal. Look at Format() for the syntax description.
#define FormatAdd(destination_cat, string, ...)                                             \
    ApplicationNamespace::format_add<count_percent_signs(string)>(destination_cat, string,  \
        CompileTimeU32(format_literal_length(string)), ##__VA_ARGS__)

// FormatWrite writes formatted text to an Output_Buffer.
// 'destination_buffer' is an Output_Buffer*.
// 'string' has to be a literal. Look at Format() for the syntax description.
#define FormatWrite(destination_buffer, string, ...)                                             \
    ApplicationNamespace::format_write<count_percent_signs(string)>(destination_buffer, string,  \
        CompileTimeU32(format_literal_length(string)), ##__VA_ARGS__)




struct Hexadecimal_Format
{
    u64 value;
    umm digits;
    bool capital;
};

inline Hexadecimal_Format hex_format(u64 value, umm digits = 0, bool capital = false)
{
    return { value, digits, capital };
}


struct U64_Format
{
    u64 value;
    umm digits;
};

inline U64_Format u64_format(u64 value, umm digits = 0)
{
    return { value, digits };
}


struct S64_Format
{
    s64 value;
    umm characters;
    bool force_sign;
};

inline S64_Format s64_format(s64 value, umm characters = 0, bool force_sign = false)
{
    return { value, characters, force_sign };
}


struct F64_Format
{
    f64 value;
    umm precision;
};

inline F64_Format f64_format(f64 value, umm precision = 6)
{
    return { value, precision };
}


struct String_Format
{
    String value;
    umm    desired_length;
};

inline String_Format string_format(String value, umm desired_length)
{
    return { value, desired_length };
}


struct Plural_Format
{
    bool   is_plural;
    String singular_form;
    String plural_form;
};

/* default plural_form is to append "s" */
inline Plural_Format plural(s64 quantity, String singular_form, String plural_form = {})
{
    bool is_plural = (quantity != 1 && quantity != -1);
    return { is_plural, singular_form, plural_form };
}


// In order to reduce code bloat, most format item functions are not defined in the header.
#define FormatItemFunctionDeclarations(T) \
    umm format_item_length(T v);          \
    void format_item(Output_Buffer* buffer, T v);

FormatItemFunctionDeclarations(signed char)
FormatItemFunctionDeclarations(signed short)
FormatItemFunctionDeclarations(signed int)
FormatItemFunctionDeclarations(signed long)
FormatItemFunctionDeclarations(signed long long)

FormatItemFunctionDeclarations(unsigned char)
FormatItemFunctionDeclarations(unsigned short)
FormatItemFunctionDeclarations(unsigned int)
FormatItemFunctionDeclarations(unsigned long)
FormatItemFunctionDeclarations(unsigned long long)

FormatItemFunctionDeclarations(bool)
FormatItemFunctionDeclarations(double)
FormatItemFunctionDeclarations(String)
FormatItemFunctionDeclarations(String16)
FormatItemFunctionDeclarations(String32)
FormatItemFunctionDeclarations(const char*)
FormatItemFunctionDeclarations(const void*)
FormatItemFunctionDeclarations(Hexadecimal_Format)
FormatItemFunctionDeclarations(U64_Format)
FormatItemFunctionDeclarations(S64_Format)
FormatItemFunctionDeclarations(F64_Format)
FormatItemFunctionDeclarations(String_Format)
FormatItemFunctionDeclarations(Plural_Format)

#undef FormatItemFunctionDeclarations


template <typename T>
inline umm format_item_length(Array<T> array)
{
    if (array.count == 0) return 2;              // []
    umm result = 2 + (array.count - 1) * 2 + 2;  // [ a, b ]
    For (array)
        result += format_item_length(*it);
    return result;
}

template <typename T>
inline void format_item(Output_Buffer* buffer, Array<T> array)
{
    write_u8(buffer, '[');
    if (array.count)
    {
        write_u8(buffer, ' ');
        For (array)
        {
            if (it > array.address)
            {
                write_u8(buffer, ',');
                write_u8(buffer, ' ');
            }
            format_item(buffer, *it);
        }
        write_u8(buffer, ' ');
    }
    write_u8(buffer, ']');
}



inline constexpr umm count_percent_signs(const char* string)
{
    umm result = 0;
    while (string[0])
    {
             if (string[0] == '%' && string[1] == '%') string++;  // escaped
        else if (string[0] == '%') result++;                      // short or long form
        string++;
    }
    return result;
}

inline constexpr umm format_literal_length(const char* string)
{
    umm result = 0;
    while (string[0])
    {
             if (string[0] == '%' && string[1] == '%') result++, string++;  // escaped
        else if (string[0] == '%' && string[1] == '~') string++;            // long form
        else if (string[0] != '%') result++;                                // not short form
        string++;
    }
    return result;
}

// To reduce code bloat, format_next_item() is not defined in the header.
void format_next_item(Output_Buffer* buffer, u8** read, umm* remaining_literal);

template <typename T, typename... Ts>
inline void format_next_item(Output_Buffer* buffer, u8** read, umm* remaining_literal, T first, Ts... args)
{
    format_next_item(buffer, read, remaining_literal);
    format_item(buffer, first);
    format_next_item(buffer, read, remaining_literal, args...);
}

inline umm format_items_length_total() { return 0; }

template <typename T, typename... Ts>
inline umm format_items_length_total(T first, Ts... args)
{
    return format_item_length(first) + format_items_length_total(args...);
}

template <umm ExpectedArgumentCount, typename... Ts>
String format(Region* memory, const char* string, umm literal_length, Ts... args)
{
    CompileTimeAssert(sizeof...(Ts) == ExpectedArgumentCount);

    String result;
    result.length = literal_length + format_items_length_total(args...);
    result.data = alloc<u8>(memory, result.length + 1);
    result.data[result.length] = 0;

    Output_Buffer buffer;
    make_memory_output_buffer(&buffer, result.data, result.length);

    u8* read_cursor = (u8*) string;
    umm remaining_literal = literal_length;
    format_next_item(&buffer, &read_cursor, &remaining_literal, args...);
    assert(!buffer.error && buffer.cursor == buffer.end);

    return result;
}

template <umm ExpectedArgumentCount, typename... Ts>
void format_append(String* destination, Region* memory, const char* string, umm literal_length, Ts... args)
{
    CompileTimeAssert(sizeof...(Ts) == ExpectedArgumentCount);

    umm reservation_size = literal_length + format_items_length_total(args...);
    void* reserved = region_builder_reserve(memory, (void**) &destination->data, destination->length, reservation_size, 1);
    destination->length += reservation_size;

    Output_Buffer buffer;
    make_memory_output_buffer(&buffer, reserved, reservation_size);

    u8* read_cursor = (u8*) string;
    umm remaining_literal = literal_length;
    format_next_item(&buffer, &read_cursor, &remaining_literal, args...);
    assert(!buffer.error && buffer.cursor == buffer.end);
}

template <umm ExpectedArgumentCount, typename... Ts>
void format_add(String_Concatenator* destination, const char* string, umm literal_length, Ts... args)
{
    CompileTimeAssert(sizeof...(Ts) == ExpectedArgumentCount);

    umm reservation_size = literal_length + format_items_length_total(args...);
    void* reserved = reserve(destination, reservation_size);

    Output_Buffer buffer;
    make_memory_output_buffer(&buffer, reserved, reservation_size);

    u8* read_cursor = (u8*) string;
    umm remaining_literal = literal_length;
    format_next_item(&buffer, &read_cursor, &remaining_literal, args...);
    assert(!buffer.error && buffer.cursor == buffer.end);
}

template <umm ExpectedArgumentCount, typename... Ts>
void format_write(Output_Buffer* destination, const char* string, umm literal_length, Ts... args)
{
    CompileTimeAssert(sizeof...(Ts) == ExpectedArgumentCount);

    u8* read_cursor = (u8*) string;
    umm remaining_literal = literal_length;
    format_next_item(destination, &read_cursor, &remaining_literal, args...);
}



////////////////////////////////////////////////////////////////////////////////
// Logging.
////////////////////////////////////////////////////////////////////////////////


extern String LOG_DEBUG;
extern String LOG_INFO;
extern String LOG_WARN;
extern String LOG_ERROR;

String format_log_message_as_line(Region* memory, String severity, String subsystem, String message, File_Time timestamp);
void log(String severity, String subsystem, String message);

void add_log_handler(void(*handler)(String severity, String subsystem, String msg));

// 'string' has to be a literal
#define Debug(string, ...)               log(LOG_DEBUG, {},        Format(temp, string, ##__VA_ARGS__))
#define LogInfo(subsystem, string, ...)  log(LOG_INFO,  subsystem, Format(temp, string, ##__VA_ARGS__))
#define LogWarn(subsystem, string, ...)  log(LOG_WARN,  subsystem, Format(temp, string, ##__VA_ARGS__))
#define LogError(subsystem, string, ...) log(LOG_ERROR, subsystem, Format(temp, string, ##__VA_ARGS__))



#if defined(OS_WINDOWS)

void report_win32_error(String subsystem, u32 error_code, String while_doing_what);
void report_last_win32_error(String subsystem, String while_doing_what);

#define ReportWin32(subsystem, error_code, string, ...) \
    report_win32_error(subsystem, error_code, Format(temp, string, ##__VA_ARGS__))
#define ReportLastWin32(subsystem, string, ...) \
    report_last_win32_error(subsystem, Format(temp, string, ##__VA_ARGS__))

#elif defined(OS_LINUX)

bool report_errno(String subsystem, int error_code, String while_doing_what); // returns false
bool report_last_errno(String subsystem, String while_doing_what);            // returns false

#define ReportErrno(subsystem, error_code, string, ...) \
    report_errno(subsystem, error_code, Format(temp, string, ##__VA_ARGS__))
#define ReportLastErrno(subsystem, string, ...) \
    report_last_errno(subsystem, Format(temp, string, ##__VA_ARGS__))

#define CheckFatalErrno(subsystem, error_code, string, ...)                 \
    while (error_code)                                                      \
    {                                                                       \
        ReportErrno((subsystem), (error_code), (string), ##__VA_ARGS__);    \
        assert(false && "fatal error");                                     \
    }

#define CheckFatalLastErrno(subsystem, string, ...)                         \
    while (errno)                                                           \
    {                                                                       \
        ReportLastErrno((subsystem), (string), ##__VA_ARGS__);              \
        assert(false && "fatal error");                                     \
    }

#endif


#define TRACING 0

#if TRACING
#define TRACE_FUNCTION       Debug("Enter % (thread %)", __FUNCTION__, current_thread_name); Defer(Debug("Exit % (thread %)", __FUNCTION__, current_thread_name));
#define TRACE_LINE(msg, ...) Debug("Line % (thread %): " msg, __LINE__, current_thread_name, ##__VA_ARGS__);
#else
#define TRACE_FUNCTION
#define TRACE_LINE(msg, ...)
#endif


////////////////////////////////////////////////////////////////////////////////
// Hierarchial allocator utilities.
////////////////////////////////////////////////////////////////////////////////


template <typename T>
inline Array<T> halloc_array(void* parent, umm count)
{
    Array<T> result;
    result.address = halloc<T>(parent, count);
    result.count = count;
    return result;
}

template <typename T>
inline Array<T> halloc_clone(void* parent, Array<T> source)
{
    Array<T> result;
    result.address = halloc<T>(parent, source.count);
    result.count = source.count;
    memcpy(result.address, source.address, source.count * sizeof(T));
    return result;
}

inline String halloc_clone(void* parent, String source)
{
    String result;
    result.data = halloc<u8>(parent, source.length);
    result.length = source.length;
    memcpy(result.data, source.data, source.length);
    return result;
}

inline void hfree_string(String* string)
{
    hfree(string->data);
}



////////////////////////////////////////////////////////////////////////////////
// Sorting.
////////////////////////////////////////////////////////////////////////////////


void radix_sort(void* address, umm count, umm size, umm key_offset, umm key_size);

template <typename T, typename U, U T::* M>
void radix_sort(T* address, umm count)
{
    umm key_offset = (umm) &(((T*) 0)->*M);
    radix_sort(address, count, sizeof(T), key_offset, sizeof(U));
}



////////////////////////////////////////////////////////////////////////////////
// IOCP
////////////////////////////////////////////////////////////////////////////////


ExitApplicationNamespace
struct _OVERLAPPED;  // This is the struct defined by Win32, OVERLAPPED is a typedef of it.
EnterApplicationNamespace

typedef void IOCP_Callback(u32 error, _OVERLAPPED* overlapped, u32 length);

// If you want to set this, you must set it before any IO happens.
// Workers are created the first time launch_iocp() is called, which happens
// on any IOCP IO request. Default worker count is min(cpu count, 4)
void set_io_worker_count(umm count);
umm get_io_worker_count();  // starts IOCP if not started

void* launch_iocp();  // Returns the IOCP handle.

bool post_to_iocp(IOCP_Callback* callback, _OVERLAPPED* overlapped, u32 length);
bool associate_handle_with_iocp(void* handle, IOCP_Callback* callback);



////////////////////////////////////////////////////////////////////////////////
// File system
////////////////////////////////////////////////////////////////////////////////


bool check_if_file_exists(String path);
bool check_if_directory_exists(String path);

// *out_available and *out_total are set to zero in case of failure
bool check_disk(String path, u64* out_available, u64* out_total);

bool create_directory(String path);
bool create_directory_recursive(String path);

// General file reading function.
// Succeeds if it reads anywhere between min_size and max_size bytes from the file.
// If 'preallocated_destination' is not NULL, the buffer is expected to be able to
// hold up to 'max_size' bytes. Otherwise, the exact amount of memory required
// is allocated from 'memory', along with a trailing null byte (not included in length).
//
// On Linux: If 'min_size' and 'max_size' are set to UMM_MAX, the function will not query
// the file size, and instead read until EOF and append to concatenator.
// In this case, 'offset' and 'preallocated_destination' are unused.
bool read_file(String* out_content,
               String path, u64 offset,
               umm min_size, umm max_size,
               void* preallocated_destination, Region* memory);

bool read_file(String path, u64 offset, umm size, void* destination);
bool read_file(String path, u64 offset, umm size, String* content, Region* memory);
bool read_file(String path, u64 offset, umm destination_size, void* destination, umm* bytes_read);
bool read_entire_file(String path, String* content, Region* memory);
String read_entire_file(String path, Region* memory = temp);

bool get_file_length(String path, u64* out_file_length);

bool write_to_file(String path, u64 offset, umm size, void* content, bool must_exist);  // Modifies, the rest of the file stays the same. Setting offset to U64_MAX appends content to the end of the file.
bool write_to_file(String path, u64 offset, String content, bool must_exist);           // Modifies, the rest of the file stays the same. Setting offset to U64_MAX appends content to the end of the file.
bool write_entire_file(String path, String content);                                    // Doesn't modify, overwrites the previous file.

Array<String> list_files(String parent_path);
Array<String> list_files(String parent_path, String extension);
Array<String> list_directories(String parent_path);

bool move_file(String destination_path, String path);

bool delete_file(String path);
bool delete_directory_with_contents(String path);

enum Delete_Action
{
    DO_NOT_DELETE,
    DELETE_FILE_OR_DIRECTORY,
    RECURSE_BUT_DO_NOT_DELETE,
};

bool delete_directory_conditional(String path, bool delete_directory, Delete_Action(*should_delete)(String parent, String name, bool is_file, void* userdata), void* userdata);

bool get_file_time(String path, File_Time* out_creation, File_Time* out_write);

bool copy_file(String from, String to, bool error_on_overwrite, bool write_through = false);


#if defined(OS_LINUX)
// Same as ::close, but will retry on EINTR. Returns 0 or errno.
int close_file_descriptor(int fd);
#endif


#if defined(OS_WINDOWS)
struct Memory_Mapped_String: String
{
    void* os_handle;
};

Memory_Mapped_String open_memory_mapped_file_readonly(String path);
void close_memory_mapped_file(Memory_Mapped_String* file);
#endif



////////////////////////////////////////////////////////////////////////////////
// Transactional file utilities
////////////////////////////////////////////////////////////////////////////////


struct Safe_Filesystem
{
    void* open (bool* out_success, u64* out_size, String path, bool share_read = true, bool report_open_failures = true);
    void  close(void* handle);
    void  flush(void* handle);
    void  read (void* handle, u64 size, void* destination);
    void  write(void* handle, u64 size, void const* source);
    void  seek (void* handle, u64 offset);
    void  trim (void* handle);
    void  erase(String path);
};

extern Safe_Filesystem the_sfs;


struct File;  // operations on File are transactional

File* open_exclusive(String path, bool share_read = true, bool report_open_failures = true);
void close(File* file);

u64    size(File* file);
void resize(File* file, u64 new_size);
void   read(File* file, u64 offset, umm size, void* data);
void  write(File* file, u64 offset, umm size, void const* data, bool truncate_after_written_data = false);

struct Data_To_Write
{
    u64 offset;
    umm size;
    void const* data;
};

void write(File* file, Array<Data_To_Write> data);

inline String read(File* file, u64 offset, umm size, Region* memory)
{
    byte* buffer = alloc<byte>(memory, size);
    read(file, offset, size, buffer);
    return { size, buffer };
}

bool safe_read_file(String path, String* content, Region* memory);
void safe_write_file(String path, String content);


////////////////////////////////////////////////////////////////////////////////
// Log files
////////////////////////////////////////////////////////////////////////////////


struct Log_File;

Log_File* open_log_file(String path, umm days_to_keep = 5);
void close(Log_File* file);
void append(Log_File* file, String data, bool flush = false);
void flush_log(Log_File* file);


////////////////////////////////////////////////////////////////////////////////
// File dialog
////////////////////////////////////////////////////////////////////////////////


#if defined(OS_WINDOWS)

enum
{
    FILE_DIALOG_OPEN           = 0x0000,
    FILE_DIALOG_SAVE           = 0x0001,
    FILE_DIALOG_MULTIPLE_FILES = 0x0002,
    FILE_DIALOG_DIRECTORIES    = 0x0004,
};

struct File_Dialog_Options
{
    void* parent_window;  // HWND on Windows

    flags32 flags;
    String initial_directory;

    // You can use either one of these to specify extensions.
    // Extension strings are semicolon-separated, for example "jpg;jpeg;png"
    // Extension arrays just contain the extension strings, for example { "jpg", "jpeg", "png" }
    String extensions;
    Array<String> extensions_array;
};

Array<String> file_dialog(File_Dialog_Options* options);

String        save_file_dialog       (String extensions = {}, String initial_directory = {}, void* parent_window = NULL);
String        open_file_dialog       (String extensions = {}, String initial_directory = {}, void* parent_window = NULL);
Array<String> open_files_dialog      (String extensions = {}, String initial_directory = {}, void* parent_window = NULL);
String        open_directory_dialog  (String initial_directory = {}, void* parent_window = NULL);
Array<String> open_directories_dialog(String initial_directory = {}, void* parent_window = NULL);

#endif



////////////////////////////////////////////////////////////////////////////////
// Time
////////////////////////////////////////////////////////////////////////////////


static constexpr File_Time UNIX_EPOCH          = 116444736000000000ULL;

static constexpr File_Time FILETIME_FREQUENCY  = 10000000;
static constexpr File_Time FILETIME_ONE_SECOND = FILETIME_FREQUENCY;
static constexpr File_Time FILETIME_ONE_MINUTE = 60 * FILETIME_ONE_SECOND;
static constexpr File_Time FILETIME_ONE_HOUR   = 60 * FILETIME_ONE_MINUTE;
static constexpr File_Time FILETIME_ONE_DAY    = 24 * FILETIME_ONE_HOUR;

void rough_sleep(double seconds);

File_Time current_filetime();
double seconds_from_filetime(File_Time filetime);
File_Time filetime_from_seconds(double seconds);
File_Time filetime_from_integer_seconds(s64 seconds);

File_Time filetime_from_unix(u64 unix_time);
u64 unix_from_filetime(File_Time filetime);

String    ISO_8601_date_string_from_filetime(File_Time filetime, Region* memory);
File_Time filetime_from_ISO_8601_date_string(String date); // returns 0 when date is an invalid ISO 8601 string.

String human_readable_timestamp(Region* memory, File_Time filetime);


// Returns -1 if a is before b, 0 if they are equal, or 1 if a is after b.
inline int compare_filetimes(File_Time a, File_Time b)
{
    s64 delta = (s64) a - (s64) b;
    return (int)(delta > 0) - (int)(delta < 0);
}

// utc = local - offset
s64 get_utc_offset();  // in minutes


// A high-resolution clock that is not synchronized to any external clock.
typedef s64 QPC;
QPC current_qpc();
double seconds_from_qpc(QPC qpc);
QPC qpc_from_seconds(double seconds);


struct Date
{
    u32 year;
    u32 month;
    u32 day;
    u32 hour;
    u32 minute;
    u32 second;
    u32 nanosecond;
    u32 week_day;  // 0 is Sunday. This field is ignored in filetime_from_utc_date.
};

void       utc_date_from_filetime(File_Time filetime, Date* out_date);
bool maybe_filetime_from_utc_date(Date* date, File_Time* out_filetime);
File_Time  filetime_from_utc_date(Date* date);
File_Time  filetime_from_utc_date_parts(u32 year, u32 month, u32 day, u32 hour = 0, u32 minute = 0, u32 second = 0, u32 nanosecond = 0);

// Format string:
//
//   Y for year, M for month, D for day,
//   h for hour, m for minute, s for second,
//   f for fractions of seconds,
//
//   for YMDhms:
//     a single character means the number will be fully printed, with as many digits as it needs
//     a series of N characters means the number will be exactly N digits (mod 10^N) and zero-left-padded
//     additionally for M:
//       MMM will use month names (by default English abbreviations)
//   for f:
//     a series of N characters means N digits after the decimal point will be printed and zero-right-padded
//     so for example, fff is millisecond precision
//
//   if you want 12-hour formatting, include a or A
//     a for lowercase am/pm string
//     A for uppercase AM/PM string
//
//   if you want the weekday, use w (by default English abbreviations)
//
//   all other characters are emitted back as literals

extern String the_english_abbreviated_month_names  [12];
extern String the_english_full_month_names         [12];
extern String the_english_abbreviated_weekday_names[ 7];
extern String the_english_full_weekday_names       [ 7];

String format_date(Region* memory, String format, Date* date,
                   String months[12]  = the_english_abbreviated_month_names,
                   String weekdays[7] = the_english_abbreviated_weekday_names);
String format_timestamp(Region* memory, String format, File_Time filetime,
                        String months[12]  = the_english_abbreviated_month_names,
                        String weekdays[7] = the_english_abbreviated_weekday_names);

// Format string:
//
//   Same characters as format strings used in format_date and format_timestamp,
//   except for w for weekday, which is not supported.
//
//   Whenever the function has to parse a number, it will first skip all non-number characters.
//   Then, if the format character is repeated once, it reads digits until it reaches a non-digit.
//   Otherwise, if it's repeated N times, it reads N digits.
//
//   If MMM is used in the format string, the function will skip characters until the remaining string
//   starts with one of the provided month names (by default English abbreviations, which also work for
//   matching full month names).
//
//   If a or A is used, the function will skip characters until the remaining string starts with AM or PM,
//   case insensitive (regardless of which character was used in the format string).
//
//   If * is used in the string, the function parses and discards the next number.
//
//   If > is used in the string, the will skip characters until it reaches the literal character after >,
//   and then skip that character too (basically call consume_until with the character after > as 'until_what').
//
//   Any other character that appears in the format string is handled by expecting it as the next
//   character in the string, and returning false if it's not there.
//
//   The function returns false if it can't parse everything required by the format string,
//   or if the date can't be converted into filetime (for functions with out_filetime)
//
//   In the output date, fields that were not filled are by default set to 1601/01/01 00:00:00.000,
//   which is the filetime epoch, which makes parsing times without dates into filetime units convenient.
//   week_day is not filled in.

bool consume_date    (Date*      out_date,     String format, String* string, String months[12] = the_english_abbreviated_month_names);
bool consume_filetime(File_Time* out_filetime, String format, String* string, String months[12] = the_english_abbreviated_month_names);

bool parse_date    (Date*      out_date,     String format, String string, String months[12] = the_english_abbreviated_month_names);
bool parse_filetime(File_Time* out_filetime, String format, String string, String months[12] = the_english_abbreviated_month_names);



struct Country_Code
{
    u8 data[2];

    inline          operator bool()   const { return data[0] != 0 && data[1] != 0; }
    inline explicit operator String() const { return { ArrayCount(data), (u8*)data }; }
};

inline bool operator==(Country_Code const& a, Country_Code const& b) { return a.data[0] == b.data[0] && a.data[1] == b.data[1]; }
inline bool operator!=(Country_Code const& a, Country_Code const& b) { return a.data[0] != b.data[0] || a.data[1] != b.data[1]; }

inline Country_Code country_code_from_string(String s)
{
    if (s.length != 2) return {};

    Country_Code code = { { s.data[0], s.data[1] } };

    if ('A' <= code.data[0] && code.data[0] <= 'Z') code.data[0] += ('a' - 'A');
    if ('A' <= code.data[1] && code.data[1] <= 'Z') code.data[1] += ('a' - 'A');

    if (('a' <= code.data[0] && code.data[0] <= 'z') &&
        ('a' <= code.data[1] && code.data[1] <= 'z'))
        return code;

    return {};
}

static String string_from_country_code(Country_Code code, Region* memory = temp)
{
    return code ? allocate_string(memory, (String) code) : String {};
}

inline void add_country_code(String_Concatenator* cat, Country_Code cc)
{
    add_u8(cat, cc.data[0]);
    add_u8(cat, cc.data[1]);
}

inline bool read_country_code(String* data, Country_Code* cc)
{
    if (data->length < 2) return false;
    cc->data[0] = read_u8(data);
    cc->data[1] = read_u8(data);
    return true;
}



////////////////////////////////////////////////////////////////////////////////
// Profiling helper
////////////////////////////////////////////////////////////////////////////////


struct Profile_Statistics
{
    Dynamic_Array<QPC> durations;
    QPC start_time;
};

static inline void begin_profile(Profile_Statistics* prof)
{
    prof->start_time = current_qpc();
}

static inline void end_profile(Profile_Statistics* prof)
{
    QPC duration = current_qpc() - prof->start_time;
    add_item(&prof->durations, &duration);
}

static inline void steal(Profile_Statistics* target, Profile_Statistics* source)
{
    add_items(&target->durations, &source->durations);
    free_heap_array(&source->durations);
}

// Prints the statistics to stdout and to log(), and returns the printed string.
String profile_statistics(Profile_Statistics* prof, String title,
                          double scale = 1, umm precision = 5, umm top_discard = 0);



////////////////////////////////////////////////////////////////////////////////
// Threading utilities
////////////////////////////////////////////////////////////////////////////////


struct Pipe
{
    String name;
#if defined(OS_WINDOWS)
    void* handle;
#else
    int fd;
#endif
};

void make_local_pipe(Pipe* in, Pipe* out, u32 buffer_size);
void make_pipe_in   (Pipe* pipe, u32 buffer_size, String suffix = {});
void make_pipe_out  (Pipe* pipe, u32 buffer_size, String suffix = {});

#if defined(OS_WINDOWS)
void make_pipe_duplex   (Pipe* pipe, u32 buffer_size, String suffix = {});
bool connect_pipe_duplex(Pipe* pipe, String name);
#elif defined(OS_LINUX)
void set_pipe(Pipe* pipe, int fd, String name);
#endif

void free_pipe(Pipe* pipe);

bool seek    (Pipe* pipe, u32 size);
bool read    (Pipe* pipe, void* data, u32 size);
bool try_read(Pipe* pipe, void* data, u32 size, bool* out_error);  // reads all or nothing
bool write   (Pipe* pipe, void* data, u32 size);

inline bool write(Pipe* pipe, String data)
{
    return write(pipe, data.data, data.length);
}

#if defined(OS_WINDOWS)
// @Deprecated - Seriously, do not use this function. It is slow, and abuses a lot of the
// pipe features which are only available on Windows, and will never be ported anywhere else.
Array<String> read_lines(Pipe* pipe, Region* memory);
#endif



struct Lock
{
#if defined(OS_WINDOWS)
    void* handle;
#elif defined(OS_LINUX)
  #if defined(ARCHITECTURE_X86) || defined(ARCHITECTURE_ARM_V7)
    umm data[1];
  #else
    umm data[40 / sizeof(umm)];
  #endif
#else
#error "Unsupported"
#endif
};

void make_lock(Lock* lock);
void free_lock(Lock* lock);
void acquire(Lock* lock);
void release(Lock* lock);

#if defined(OS_WINDOWS) && defined(ARCHITECTURE_X64)
void acquire_read(Lock* lock);
bool acquire_read_timeout(Lock* lock, QPC timeout);
void release_read(Lock* lock);
#endif

#define LockedScope(lock_ptr)   \
    acquire(lock_ptr);          \
    Defer(release(lock_ptr))

#define LockedReadScope(lock_ptr) \
    acquire_read(lock_ptr);       \
    Defer(release_read(lock_ptr))


#if defined(OS_WINDOWS)
struct IPC_Lock
{
    void* handle;
};

void make_ipc_lock(IPC_Lock* ipc_lock, String name);
void free_ipc_lock(IPC_Lock* ipc_lock);
void ipc_acquire(IPC_Lock* ipc_lock);
void ipc_release(IPC_Lock* ipc_lock);
#endif


struct Semaphore
{
#if defined(OS_WINDOWS)
    void* handle;
#elif defined(OS_LINUX)
    // @Incomplete - probably not the same on 32-bit
    umm data[32 / sizeof(umm)];
#else
#error "Unsupported"
#endif
};

void make_semaphore(Semaphore* sem, u32 initial = 0);
void free_semaphore(Semaphore* sem);
void post(Semaphore* sem);
void wait(Semaphore* sem);
bool wait(Semaphore* sem, double timeout_seconds);



#if !defined(OS_WINDOWS) || !defined(ARCHITECTURE_X86)

struct Condition_Variable
{
#if defined(OS_WINDOWS)
    void* handle;
#elif defined(OS_LINUX)
  #if defined(ARCHITECTURE_X86) || defined(ARCHITECTURE_ARM_V7)
    umm data[1];
  #else
    umm data[48 / sizeof(umm)];
  #endif
#else
#error "Unsupported"
#endif
};

void make_condition_variable(Condition_Variable* variable);
void free_condition_variable(Condition_Variable* variable);
void signal    (Condition_Variable* variable);
void signal_all(Condition_Variable* variable);
void wait(Condition_Variable* variable, Lock* lock);
bool wait(Condition_Variable* variable, Lock* lock, double timeout_seconds);

#endif



struct Event
{
#if defined(OS_WINDOWS)
    void* handle;
#elif defined(OS_LINUX)
    Lock               lock;
    Condition_Variable cv;
    bool               signal;
#else
#error "Unsupported"
#endif
};

void make_event(Event* event);
void free_event(Event* event);
void signal(Event* event);
void wait(Event* event);
bool wait(Event* event, double timeout_seconds);



struct Thread
{
    void* handle;
#if defined(OS_WINDOWS)
    u32 thread_id;
#elif defined(OS_LINUX)
    bool valid_handle;
#endif
};

extern thread_local String current_thread_name;

void spawn_thread(String name, void* userdata, void(*entry)(void*), Thread* out_thread = NULL);
void wait(Thread* thread);
#if defined(OS_WINDOWS)
bool is_thread_running(Thread* thread);
#endif

// Not supported on Linux, because making the gettid() syscall crashes Android!
#if defined(OS_WINDOWS)
inline u32 current_thread_id()
{
#if defined(ARCHITECTURE_X64)
    return __readgsdword(0x48);
#elif defined(ARCHITECTURE_X86)
    return __readfsdword(0x24);
#else
    #error "Unsupported architecture."
#endif
}
#endif


umm get_hardware_parallelism();



////////////////////////////////////////////////////////////////////////////////
// Process utilities
////////////////////////////////////////////////////////////////////////////////


void get_cpu_and_memory_usage(double* out_cpu_usage, u64* out_physical_use, u64* out_physical_max);

#if defined(OS_WINDOWS)
void prevent_sleep_mode();
#endif

void get_network_performance(double* out_read_bps, double* out_write_bps);

struct System_Information
{
    String motherboard;
    String system_name;
    String bios;
    String cpu_name;
};

void get_system_information(System_Information* info);


u32 current_process_id();
void terminate_current_process(u32 exit_code = 0);
String get_executable_path();
String get_executable_name();
String get_executable_directory();


struct Process
{
    String path;
    Array<String> arguments;
    String file_stdin;
    String file_stdout;
    String file_stderr;

    String current_directory;

    bool detached;
    bool prohibit_console_window;
    bool new_console_window;
    bool do_not_escape_arguments;
    bool start_minimized;

#if defined(OS_WINDOWS)
    void* handle;
#else
    u32 pid;
#endif
};

bool run_process(Process* process);
bool get_process_id(Process* process, u32* out_process_id);
void terminate_process_without_waiting(Process* process);

#if defined(OS_WINDOWS)
constexpr double WAIT_FOR_PROCESS_FOREVER = -1234567890;
u32 wait_for_process(Process* process, double seconds = WAIT_FOR_PROCESS_FOREVER);
bool wait_for_process(Process* process, double seconds, u32* out_exit_code);
u32 terminate_and_wait_for_process(Process* process);
bool is_process_running(Process* process);
#endif

bool wait_for_process_by_id(u32 id);
bool terminate_and_wait_for_process(u32 id);

Array<String> command_line_arguments();
bool   get_command_line_bool   (String name);   // -name
s64    get_command_line_integer(String name);   // -name:return
String get_command_line_string (String name);   // -name:return


String get_os_name();

void error_box(String message, String title = {}, void* window = NULL);

#if defined(OS_WINDOWS)
bool run_process_through_shell(String exe, String arguments, void** out_handle = NULL, bool minimized = false);

bool add_app_to_run_at_startup(String name);
bool prevent_app_running_at_startup(String name);
bool set_should_app_run_at_startup(String name, bool should_run);

bool is_wow64();

String get_windows_directory();
void restart_windows_explorer();

void copy_text_to_clipboard(String text);
#endif


////////////////////////////////////////////////////////////////////////////////
// Random
////////////////////////////////////////////////////////////////////////////////


#if defined(COMPILER_MSVC)
#define RotL32(value, rot) (_rotl  ((value), (rot)))
#define RotL64(value, rot) (_rotl64((value), (rot)))
#define RotR32(value, rot) (_rotr  ((value), (rot)))
#define RotR64(value, rot) (_rotr64((value), (rot)))
#else
#define RotL32(value, rot) (((u32)(value) << (u32)(rot)) | ((u32)(value) >> ((-(u32)(rot)) & 31)))
#define RotL64(value, rot) (((u32)(value) << (u32)(rot)) | ((u32)(value) >> ((-(u32)(rot)) & 63)))
#define RotR32(value, rot) (((u32)(value) >> (u32)(rot)) | ((u32)(value) << ((-(u32)(rot)) & 31)))
#define RotR64(value, rot) (((u32)(value) >> (u32)(rot)) | ((u32)(value) << ((-(u32)(rot)) & 63)))
#endif

inline u32 rotl32(u32 value, u32 rot) { return RotL32(value, rot); }
inline u64 rotl64(u64 value, u64 rot) { return RotL64(value, rot); }
inline u32 rotr32(u32 value, u32 rot) { return RotR32(value, rot); }
inline u64 rotr64(u64 value, u32 rot) { return RotR64(value, rot); }

inline float  lerp(float  a, float  b, float  t) { return a * (1 - t) + b * t; }
inline double lerp(double a, double b, double t) { return a * (1 - t) + b * t; }

inline float  unlerp(float  a, float  b, float  x) { return (x - a) / (b - a); }
inline double unlerp(double a, double b, double x) { return (x - a) / (b - a); }


// A very expensive function, but should return data from a reasonable set of sources of entropy.
// Used for seeding cryptographically secure RNGs (which are in turn used to seed regular RNGs).
void entropy_source_callback(void* userdata, void(*callback)(void* userdata, byte* data, umm size));


struct Random
{
    u64 state;
    u64 increment;
};

void seed(Random* random, u64 seed1, u64 seed2);
void seed(Random* random);

u32 next_u32(Random* random);
u64 next_u64(Random* random);
u32 next_u32(Random* random, u32 low, u32 high);  // [ low, high >
u64 next_u64(Random* random, u64 low, u64 high);  // [ low, high >

s32 next_s32(Random* random);
s64 next_s64(Random* random);
s32 next_s32(Random* random, s32 low, s32 high);  // [ low, high >
s64 next_s64(Random* random, s64 low, s64 high);  // [ low, high >

float next_float    (Random* random);                         // [   0,    1 >
float next_float_pm1(Random* random);                         // [  -1,    1 >
float next_float    (Random* random, float low, float high);  // [ low, high >
bool  next_chance   (Random* random, float chance);

double next_double    (Random* random);                           // [   0,    1 >
double next_double_pm1(Random* random);                           // [  -1,    1 >
double next_double    (Random* random, double low, double high);  // [ low, high >

template <typename T>
void shuffle_array(Random* rng, Array<T> a)
{
    for (umm i = 0; i < a.count - 1; i++)
    {
        umm j = next_u64(rng, i, a.count);
        T temp = a[j];
        a[j] = a[i];
        a[i] = temp;
    }
}



union GUID
{
    struct
    {
        u64 q1;
        u64 q2;
    };
    struct
    {
        u32 d1;
        u16 d2;
        u16 d3;
        u8  d4[8];
    };

    inline operator bool() const { return q1 || q2; }
};

inline bool operator==(GUID a, GUID b) { return a.q1 == b.q1 && a.q2 == b.q2; }
inline bool operator!=(GUID a, GUID b) { return a.q1 != b.q1 || a.q2 != b.q2; }

GUID generate_guid();
GUID parse_guid(String string);
bool parse_guid_checked(String string, GUID* out_guid);
String print_guid(GUID guid);

inline GUID read_guid(String* string)
{
    GUID guid;
    if (!read_u32le(string, &guid.d1)) return {};
    if (!read_u16le(string, &guid.d2)) return {};
    if (!read_u16le(string, &guid.d3)) return {};
    if (!read_u64le(string, &guid.q2)) return {};
    return guid;
}

inline void add_guid(String_Concatenator* cat, GUID guid)
{
    add_u32le(cat, guid.d1);
    add_u16le(cat, guid.d2);
    add_u16le(cat, guid.d3);
    add_u64le(cat, guid.q2);
}


ExitApplicationNamespace
