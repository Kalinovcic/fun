#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#define OutputDebugStringA(str) printf("%s", str)

#include "library_info.h"

EnterApplicationNamespace


static HMODULE dbghelp;

enum MINIDUMP_TYPE {
    MiniDumpNormal,
    MiniDumpWithDataSegs,
    MiniDumpWithFullMemory,
};

struct MINIDUMP_EXCEPTION_INFORMATION;
struct MINIDUMP_USER_STREAM_INFORMATION;
struct MINIDUMP_CALLBACK_INFORMATION;

static BOOL(_stdcall *MiniDumpWriteDump)(
    HANDLE                            hProcess,
    DWORD                             ProcessId,
    HANDLE                            hFile,
    MINIDUMP_TYPE                     DumpType,
    MINIDUMP_EXCEPTION_INFORMATION*   ExceptionParam,
    MINIDUMP_USER_STREAM_INFORMATION* UserStreamParam,
    MINIDUMP_CALLBACK_INFORMATION*    CallbackParam
);



constexpr int MAX_SYM_NAME = 2000;

struct IMAGEHLP_SYMBOL64
{
    DWORD   SizeOfStruct;
    DWORD64 Address;
    DWORD   Size;
    DWORD   Flags;
    DWORD   MaxNameLength;
    CHAR    Name[1];
};

struct IMAGEHLP_LINE64
{
    DWORD    SizeOfStruct;
    PVOID    Key;
    DWORD    LineNumber;
    PCHAR    FileName;
    DWORD64  Address;
};

enum ADDRESS_MODE
{
    AddrMode1616,
    AddrMode1632,
    AddrModeReal,
    AddrModeFlat
};

struct ADDRESS64
{
    DWORD64       Offset;
    WORD          Segment;
    ADDRESS_MODE  Mode;
};

struct KDHELP64
{
    DWORD64   Thread;
    DWORD     ThCallbackStack;
    DWORD     ThCallbackBStore;
    DWORD     NextCallback;
    DWORD     FramePointer;
    DWORD64   KiCallUserMode;
    DWORD64   KeUserCallbackDispatcher;
    DWORD64   SystemRangeStart;
    DWORD64   KiUserExceptionDispatcher;
    DWORD64   StackBase;
    DWORD64   StackLimit;
    DWORD     BuildVersion;
    DWORD     Reserved0;
    DWORD64   Reserved1[4];
};


struct STACKFRAME64
{
    ADDRESS64   AddrPC;
    ADDRESS64   AddrReturn;
    ADDRESS64   AddrFrame;
    ADDRESS64   AddrStack;
    ADDRESS64   AddrBStore;
    PVOID       FuncTableEntry;
    DWORD64     Params[4];
    BOOL        Far;
    BOOL        Virtual;
    DWORD64     Reserved[3];
    KDHELP64    KdHelp;
};

typedef BOOL   (__stdcall* PREAD_PROCESS_MEMORY_ROUTINE64)(HANDLE hProcess, DWORD64 qwBaseAddress, PVOID lpBuffer, DWORD nSize, LPDWORD lpNumberOfBytesRead);
typedef PVOID  (__stdcall* PFUNCTION_TABLE_ACCESS_ROUTINE64)(HANDLE ahProcess, DWORD64 AddrBase);
typedef DWORD64(__stdcall* PGET_MODULE_BASE_ROUTINE64)(HANDLE hProcess, DWORD64 Address);
typedef DWORD64(__stdcall* PTRANSLATE_ADDRESS_ROUTINE64)(HANDLE hProcess, HANDLE hThread, ADDRESS64* lpaddr);

static BOOL(__stdcall *SymInitialize)(HANDLE hProcess, PCSTR UserSearchPath, BOOL fInvadeProcess);
static BOOL(__stdcall *SymGetSymFromAddr64)(HANDLE hProcess, DWORD64 qwAddr, PDWORD64 pdwDisplacement, IMAGEHLP_SYMBOL64* Symbol);
static BOOL(__stdcall *SymGetLineFromAddr64)(HANDLE hProcess, DWORD64 qwAddr, PDWORD pdwDisplacement, IMAGEHLP_LINE64* Line64);
static BOOL(__stdcall *StackWalk64)(
    DWORD MachineType, HANDLE hProcess, HANDLE hThread, STACKFRAME64* StackFrame, PVOID ContextRecord,
    PREAD_PROCESS_MEMORY_ROUTINE64 ReadMemoryRoutine, PFUNCTION_TABLE_ACCESS_ROUTINE64 FunctionTableAccessRoutine,
    PGET_MODULE_BASE_ROUTINE64 GetModuleBaseRoutine, PTRANSLATE_ADDRESS_ROUTINE64 TranslateAddress);
static PVOID  (__stdcall *SymFunctionTableAccess64)(HANDLE hProcess, DWORD64 AddrBase);
static DWORD64(__stdcall *SymGetModuleBase64)(HANDLE hProcess, DWORD64 qwAddr);




static void init_dbghelp()
{
    OnlyOnce
    {
        dbghelp = LoadLibraryA("dbghelp.dll");
        if (dbghelp)
        {
            *(FARPROC*) &MiniDumpWriteDump        = GetProcAddress(dbghelp, "MiniDumpWriteDump");
            *(FARPROC*) &SymInitialize            = GetProcAddress(dbghelp, "SymInitialize");
            *(FARPROC*) &SymGetSymFromAddr64      = GetProcAddress(dbghelp, "SymGetSymFromAddr64");
            *(FARPROC*) &SymGetLineFromAddr64     = GetProcAddress(dbghelp, "SymGetLineFromAddr64");
            *(FARPROC*) &StackWalk64              = GetProcAddress(dbghelp, "StackWalk64");
            *(FARPROC*) &SymFunctionTableAccess64 = GetProcAddress(dbghelp, "SymFunctionTableAccess64");
            *(FARPROC*) &SymGetModuleBase64       = GetProcAddress(dbghelp, "SymGetModuleBase64");

            if (!MiniDumpWriteDump || !SymInitialize || !SymGetSymFromAddr64 ||
                !SymGetLineFromAddr64 || !StackWalk64 || !SymFunctionTableAccess64 ||
                !SymGetModuleBase64)
            {
                LogError("debug"_s, "Failed to find all required functions in DbgHelp.");
                FreeLibrary(dbghelp);
                dbghelp = NULL;
            }
            else
            {
                SymInitialize(GetCurrentProcess(), NULL, true);
            }
        }
        else
        {
            ReportLastWin32("debug"_s, "While loading DbgHelp.");
        }
    }
}


bool output_full_dumps = true;
void(*after_crash_dump)();

static void dump()
{
    init_dbghelp();
    if (!dbghelp) return;

    Thread thread;
    spawn_thread("dumper"_s, NULL, [](void*)
    {
        HANDLE process    = GetCurrentProcess();
        DWORD  process_id = GetCurrentProcessId();

        LPCWSTR path16 = make_windows_path(Format(temp, "full-%.dmp", current_filetime()));
        DWORD access = GENERIC_READ | GENERIC_WRITE;
        DWORD share  = FILE_SHARE_READ;
        HANDLE file = CreateFileW(path16, access, share, NULL, CREATE_ALWAYS, 0, NULL);

        MINIDUMP_TYPE type = MiniDumpWithFullMemory;
        BOOL success = MiniDumpWriteDump(process, process_id, file, type, NULL, NULL, NULL);

        FlushFileBuffers(file);
        CloseHandle(file);
    }, &thread);
    wait(&thread);
}

static void capture_stack(void** trace, umm trace_count, umm frames_to_skip)
{
    init_dbghelp();

    memset(trace, 0, sizeof(void*) * trace_count);
    if (dbghelp)
    {
        CONTEXT context;
        context.ContextFlags = CONTEXT_FULL;
        RtlCaptureContext(&context);

        DWORD machine_type;
        STACKFRAME64 stack_frame = {};

#if defined(ARCHITECTURE_X64)
        machine_type = IMAGE_FILE_MACHINE_AMD64;
        stack_frame.AddrPC.Offset = context.Rip;
        stack_frame.AddrPC.Mode = AddrModeFlat;
        stack_frame.AddrFrame.Offset = context.Rsp;
        stack_frame.AddrFrame.Mode = AddrModeFlat;
        stack_frame.AddrStack.Offset = context.Rsp;
        stack_frame.AddrStack.Mode = AddrModeFlat;
#elif defined(ARCHITECTURE_X86)
        machine_type = IMAGE_FILE_MACHINE_I386;
        stack_frame.AddrPC.Offset = context.Eip;
        stack_frame.AddrPC.Mode = AddrModeFlat;
        stack_frame.AddrFrame.Offset = context.Ebp;
        stack_frame.AddrFrame.Mode = AddrModeFlat;
        stack_frame.AddrStack.Offset = context.Esp;
        stack_frame.AddrStack.Mode = AddrModeFlat;
#else
#error "New CPU architecture"
#endif

        HANDLE process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();

        for (umm cursor = 0; cursor < trace_count; cursor++)
        {
            if (!StackWalk64(machine_type, process, thread, &stack_frame, &context, NULL,
                             SymFunctionTableAccess64, SymGetModuleBase64, NULL))
                break;

            trace[cursor] = (void*)(stack_frame.AddrPC.Offset);
        }

        if (trace[0])
            return;  // probably good data
    }

    // fallback
    CaptureStackBackTrace(frames_to_skip + 1, trace_count, trace, NULL);
}

static String format_stack(void* const* trace, umm trace_count)
{
    init_dbghelp();

    String_Concatenator cat = {};

    static HMODULE exe_module;
    static MODULEINFO exe_module_info;
    static Library_Info* library_info;
    OnlyOnce
    {
        /*
        String exe_path = get_executable_path();
        String pdb_path = concatenate(get_parent_directory_path(exe_path),
                                      "/"_s, get_file_name_without_extension(exe_path), ".pdb"_s);
        library_info = load_library_info_from_pdb(pdb_path);
        */

        exe_module = GetModuleHandleW(NULL);
        GetModuleInformation(GetCurrentProcess(), exe_module, &exe_module_info, sizeof(exe_module_info));
    }

    for (umm i = 0; i < trace_count; i++) Loop(trace_loop)
    {
        void* addr = trace[i];
        if (!addr) break;

        if (dbghelp)
        {
            char symbol_buffer[sizeof(IMAGEHLP_SYMBOL64) + MAX_SYM_NAME + 1];
            IMAGEHLP_SYMBOL64* symbol = (IMAGEHLP_SYMBOL64*) &symbol_buffer[0];
            symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
            symbol->MaxNameLength = MAX_SYM_NAME;

            HANDLE process = GetCurrentProcess();
            DWORD64 address = (DWORD64) addr;
            DWORD64 symbol_displacement;
            BOOL success = SymGetSymFromAddr64(process, address, &symbol_displacement, symbol);
            if (success)
            {
                FormatAdd(&cat, " .. %", symbol->Name);

                DWORD line_displacement;
                IMAGEHLP_LINE64 location = {};
                location.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
                BOOL success = SymGetLineFromAddr64(process, address, &line_displacement, &location);
                if (success)
                {
                    FormatAdd(&cat, " (%:%)", location.FileName, location.LineNumber);
                }

                add(&cat, "\n"_s);
                ContinueLoop(trace_loop);
            }
        }

        if (library_info)
        {
            umm rel_addr = (umm) addr - (umm) exe_module;
            if (rel_addr < exe_module_info.SizeOfImage)
            {
                For (library_info->functions)
                {
                    if (rel_addr >= it->address + it->size) continue;
                    if (rel_addr < it->address) break;

                    FormatAdd(&cat, " .. %", it->name);
                    add(&cat, "\n"_s);
                    ContinueLoop(trace_loop);
                }
            }
        }

        FormatAdd(&cat, " .. 0x%", addr);

        HMODULE module = NULL;
        if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCTSTR) addr, &module))
        {
            char module_name[MAX_PATH + 1] = {};
            if (GetModuleFileNameA(module, module_name, ArrayCount(module_name)))
                FormatAdd(&cat, " (% +%)", module_name, (umm) addr - (umm) module);
        }

        add(&cat, "\n"_s);
    }

    add(&cat, "\0"_s);
    String result = resolve_to_string_and_free(&cat, temp);
    result.length--;
    return result;
}

static void debug_print_stack(void* const* trace, umm trace_count)
{
    String text = format_stack(trace, trace_count);
    OutputDebugStringA((char*) text.data);
}





static thread_local bool already_crashing;

void assertion_failure(const char* test, const char* file, int line)
{
    if (already_crashing)
    {
        TerminateProcess(GetCurrentProcess(), 1);  // crashed again during crashing
        return;
    }
    already_crashing = true;

    void* frames[64];
    capture_stack(frames, ArrayCount(frames), 0);
    String stack = format_stack(frames, ArrayCount(frames));

    printf("Assertion %s failed!\n%s:%d (thread %.*s)\nStack:\n%.*s\n", test, file, line, StringArgs(current_thread_name), StringArgs(stack));
    LogError("debug system"_s, "Assertion % failed!\n%:% (thread %)\nStack:\n%\n", test, file, line, current_thread_name, stack);
    if (after_crash_dump)
        after_crash_dump();
    TerminateProcess(GetCurrentProcess(), 1);
}

static LONG WINAPI unhandled_exception_handler(EXCEPTION_POINTERS* info)
{
    ScopeZone("unhandled_exception_handler");

    if (already_crashing)
    {
        TerminateProcess(GetCurrentProcess(), 1);  // crashed again during crashing
        return EXCEPTION_EXECUTE_HANDLER;
    }
    already_crashing = true;

    void* frames[64];
    capture_stack(frames, ArrayCount(frames), 0);
    String stack = format_stack(frames, ArrayCount(frames));

    u32 code  = info->ExceptionRecord->ExceptionCode;
    u32 flags = info->ExceptionRecord->ExceptionFlags;

    String description = "unknown exception code"_s;
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
    {
        umm   access  = (umm)   info->ExceptionRecord->ExceptionInformation[0];
        void* address = (void*) info->ExceptionRecord->ExceptionInformation[1];

        const char* verbs[] = { "reading from", "writing to", "executing at", "operating on" };
        if (access >= ArrayCount(verbs))
            access = ArrayCount(verbs) - 1;

        description = Format(temp, "Access violation caused by % address 0x%.", verbs[access], address);
    } break;
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    description = "EXCEPTION_ARRAY_BOUNDS_EXCEEDED"_s;    break;
    case EXCEPTION_BREAKPOINT:               description = "EXCEPTION_BREAKPOINT"_s;               break;
    case EXCEPTION_DATATYPE_MISALIGNMENT:    description = "EXCEPTION_DATATYPE_MISALIGNMENT"_s;    break;
    case EXCEPTION_FLT_DENORMAL_OPERAND:     description = "EXCEPTION_FLT_DENORMAL_OPERAND"_s;     break;
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       description = "EXCEPTION_FLT_DIVIDE_BY_ZERO"_s;       break;
    case EXCEPTION_FLT_INEXACT_RESULT:       description = "EXCEPTION_FLT_INEXACT_RESULT"_s;       break;
    case EXCEPTION_FLT_INVALID_OPERATION:    description = "EXCEPTION_FLT_INVALID_OPERATION"_s;    break;
    case EXCEPTION_FLT_OVERFLOW:             description = "EXCEPTION_FLT_OVERFLOW"_s;             break;
    case EXCEPTION_FLT_STACK_CHECK:          description = "EXCEPTION_FLT_STACK_CHECK"_s;          break;
    case EXCEPTION_FLT_UNDERFLOW:            description = "EXCEPTION_FLT_UNDERFLOW"_s;            break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:      description = "Executed illegal instruction."_s;      break;
    case EXCEPTION_IN_PAGE_ERROR:            description = "EXCEPTION_IN_PAGE_ERROR"_s;            break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       description = "Integer division by zero."_s;          break;
    case EXCEPTION_INT_OVERFLOW:             description = "EXCEPTION_INT_OVERFLOW"_s;             break;
    case EXCEPTION_INVALID_DISPOSITION:      description = "EXCEPTION_INVALID_DISPOSITION"_s;      break;
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: description = "EXCEPTION_NONCONTINUABLE_EXCEPTION"_s; break;
    case EXCEPTION_PRIV_INSTRUCTION:         description = "EXCEPTION_PRIV_INSTRUCTION"_s;         break;
    case EXCEPTION_SINGLE_STEP:              description = "EXCEPTION_SINGLE_STEP"_s;              break;
    case EXCEPTION_STACK_OVERFLOW:           description = "Stack overflow."_s;                    break;
    }

    printf("%.*s (code %08x, flags %08x)\nStack (thread %.*s):\n%.*s\n", StringArgs(description), code, flags, StringArgs(current_thread_name), StringArgs(stack));
    LogError("debug system"_s, "% (code %, flags %)\nStack (thread %):\n%\n", description, hex_format(code, 8), hex_format(flags, 8), current_thread_name, stack);
    if (output_full_dumps)
        dump();
    if (after_crash_dump)
        after_crash_dump();

    return EXCEPTION_EXECUTE_HANDLER;
}

void install_unhandled_exception_handler()
{
    init_dbghelp();
    SetUnhandledExceptionFilter(unhandled_exception_handler);
}




// The linker merges sections with equal names before $, and sorts them alphabetically.
// Example: .CRT$XCA and .CRT$XCU are merged, and .CRT$XCA comes before .CRT$XCU.
//
// The CRT places sentinel pointers in .CRT$XCA and .CRT$XCZ.
// Global initialization pointers go in an array between these sentinels.
// By default, they go to .CRT$XCU (CINIT_SECTION_DEFAULT), but you can force some
// initializers to execute before or after most other ones by placing them
// in different sections, like .CRT$XCB (CINIT_SECTION_EARLY) or .CRT$XCY (CINIT_SECTION_LATE)

#define CINIT_SECTION_EARLY   ".CRT$XCB"
#define CINIT_SECTION_DEFAULT ".CRT$XCU"
#define CINIT_SECTION_LATE    ".CRT$XCY"

// Create these sections, readonly.
#pragma section(CINIT_SECTION_EARLY, read)
#pragma section(CINIT_SECTION_LATE,  read)

typedef void(__cdecl* Global_Init_Call)();

#define InitializeEarly(...) \
    __declspec(allocate(CINIT_SECTION_EARLY)) \
    static Global_Init_Call UniqueIdentifier(early_init)[] = { __VA_ARGS__ }

#define InitializeLate(...) \
    __declspec(allocate(CINIT_SECTION_LATE)) \
    static Global_Init_Call UniqueIdentifier(late_init)[] = { __VA_ARGS__ }


static bool is_in_cinit;

static void enter_cinit() { is_in_cinit = true;  }
static void leave_cinit() { is_in_cinit = false; }

InitializeEarly(enter_cinit);
InitializeLate (leave_cinit);

#define AssertAfterGlobalInitialization() { if (is_in_cinit) __fastfail(0); }





#ifdef LEAKCHECK



thread_local bool        the_next_allocation_is_deliberately_leaked;
thread_local char const* the_next_allocation_type_name;
thread_local size_t      the_next_allocation_type_size;


struct Allocation_Info
{
    bool deliberately_leaked;
    void* trace[16] = {};

    bool operator<(Allocation_Info const& other) const
    {
        for (umm i = 0; i < ArrayCount(trace); i++)
        {
            if ((byte*) trace[i] < (byte*) other.trace[i]) return true;
            if ((byte*) trace[i] > (byte*) other.trace[i]) return false;
        }
        return false;
    }
};

// Ughhhhhh.....
// We can't use Table because that would allocate using the debug allocator.
// We need a table that allocates memory using some other allocator.
// So, fuck it, use the standard one.
ExitApplicationNamespace
#include <map>
EnterApplicationNamespace

std::map<void*, Allocation_Info*> the_allocation_table;




// Encodes/decodes a variable-length integer.
//
// 1+max in:  1 byte  2 bytes  3 bytes  4 bytes  5 bytes  6 bytes  7 bytes  8 bytes  9 bytes
// bits 0:       255,     510,     765,    1020,    1275,    1530,    1785,    2040,    2295
// bits 1:       254,     762,    1778,    3810,    7874,   16002,   32258,   64770,  129794
// bits 2:       252,    1260,    5292,   21420,   85932,  343980
// bits 3:       248,    2232,   18104,  145080
// bits 4:       240,    4080,   65520, 1048560
// bits 5:       224,    7392,  236768
// bits 6:       192,   12480,  798912
// bits 7:       128,   16512, 2113664

template <typename T, u32 Bits>
u8* encode_mod_pow2(u8* to, T val)
{
    static_assert(Bits < 8, "Bits should be between 0 and 7");
    constexpr T mod   = (1 << Bits);
    constexpr T limit = 256 - mod;

more:
    if (val < limit)
    {
        *(to++) = (u8)(mod + val);
        return to;
    }

    val -= limit;
    *(to++) = (u8)(val & (mod - 1));
    val >>= Bits;
    goto more;
}

template <typename T, u32 Bits>
u8* decode_mod_pow2(u8* from, T* out)
{
    static_assert(Bits < 8, "Bits should be between 0 and 7");
    constexpr T mod   = (1 << Bits);
    constexpr T limit = 256 - mod;
    u32 shift = 0;
    T   val   = 0;

more:
    int byte = *(from++);
    if (byte >= mod)
    {
        val += (byte - mod) << shift;
        *out = val;
        return from;
    }

    val += (byte + limit) << shift;
    shift += Bits;
    goto more;
}





static Lock the_leakcheck_lock;

static void initialize_leakcheck()
{
    make_lock(&the_leakcheck_lock);
}

InitializeEarly(initialize_leakcheck);



#if 0

ExitApplicationNamespace
#include <stdio.h>
EnterApplicationNamespace

GlobalBlock
{
    struct Allocation
    {
        // type_index == 0     is reserved for release records
        // type_index == 12345 is reserved for declaration records
        u64 type_index;
        u64 address;
        u64 size;
        u64 thread;
    };

    int* i = new int;
    Allocation alloc = { 200, (u64) i, 42, 123 };
    printf("%p\n\n\n", i);

    // add_u64(&cat, alloc.type_index);
    // add_u64(&cat, alloc.address);
    // add_u64(&cat, alloc.size);
    // add_u64(&cat, alloc.thread);


    u8 data[256];

    printf("\n\n  Naive:\n  ");
    {
        u64* x = (u64*) data;

        *(x++) = alloc.type_index;
        *(x++) = alloc.address;
        *(x++) = alloc.size;
        *(x++) = alloc.thread;

        for (u8* a = data; a < (u8*) x; a++)
            printf("%02x ", *a);
        printf("\n");
    }

    printf("\n\n  Compressed (mod 32):\n  ");
    {
        u8* x = data;
        x = encode_mod_pow2<u64, 5>(x, alloc.type_index);
        x = encode_mod_pow2<u64, 5>(x, alloc.address);
        x = encode_mod_pow2<u64, 5>(x, alloc.size);
        x = encode_mod_pow2<u64, 5>(x, alloc.thread);
        for (u8* a = data; a < x; a++)
            printf("%02x ", *a);
        printf("\n");
    }

    printf("\n\n  Compressed (mod 128):\n  ");
    {
        u8* x = data;
        x = encode_mod_pow2<u64, 7>(x, alloc.type_index);
        x = encode_mod_pow2<u64, 7>(x, alloc.address);
        x = encode_mod_pow2<u64, 7>(x, alloc.size);
        x = encode_mod_pow2<u64, 7>(x, alloc.thread);
        for (u8* a = data; a < x; a++)
            printf("%02x ", *a);
        printf("\n");
    }

    printf("\n\n  Compressed (mixed):\n  ");
    {
        u8* x = data;
        x = encode_mod_pow2<u64, 5>(x, alloc.type_index);
        x = encode_mod_pow2<u64, 7>(x, alloc.address);
        x = encode_mod_pow2<u64, 5>(x, alloc.size);
        x = encode_mod_pow2<u64, 5>(x, alloc.thread);
        for (u8* a = data; a < x; a++)
            printf("%02x ", *a);
        printf("\n");


        u8* y = data;
        y = decode_mod_pow2<u64, 5>(y, &alloc.type_index);
        y = decode_mod_pow2<u64, 7>(y, &alloc.address);
        y = decode_mod_pow2<u64, 5>(y, &alloc.size);
        y = decode_mod_pow2<u64, 5>(y, &alloc.thread);
        printf("  alloc.type_index: %llu\n", alloc.type_index);
        printf("  alloc.address:    %llu\n", alloc.address);
        printf("  alloc.size:       %llu\n", alloc.size);
        printf("  alloc.thread:     %llu\n", alloc.thread);
    }

    printf("\n\n  Compressed address:\n  ");
    {
        u8* x = data;
        x = encode_mod_pow2<u64, 7>(x, alloc.address);
        for (u8* a = data; a < x; a++)
            printf("%02x ", *a);
        printf("\n");
    }
};
#endif


// When you free an allocation, we decommit it, but don't release it immediately.
// Instead, its address goes to this circular backlog. When the backlog is full,
// the oldest address is actually released.
// This is to help detect use-after-free shortly after freeing the memory.
// If we actually released the memory, it could get reserved and committed again,
// leading to reading junk or corrupting unrelated memory.
static umm   volatile the_reserved_backlog_count;
static umm   volatile the_reserved_backlog_index;
static byte* volatile the_reserved_backlog[1048576];


static u8 GUARD_BYTE   = 0xCD;
static u8 GARBAGE_BYTE = 0xAF;
static umm PAGE_SIZE = 4 * 1024;


static void* debug_allocate(umm size, umm alignment)
{
    Defer(the_next_allocation_is_deliberately_leaked = false);
    Defer(the_next_allocation_type_name = NULL);
    Defer(the_next_allocation_type_size = 0);

    if (!size)
        return NULL;


    if (!the_next_allocation_type_size)
        the_next_allocation_type_size = 1;
    if (!the_next_allocation_type_name)
        the_next_allocation_type_name = "<unknown>";

#if 0
    char msg[512];
    sprintf(msg, "alloc: %s [%d]\n", the_next_allocation_type_name, (int)(size / the_next_allocation_type_size));
    OutputDebugString(msg);
#endif


    while (size % alignment) size++;

    umm actual_size = size + 2 * PAGE_SIZE;  // allocate two extra reserved pages
    if (actual_size & (PAGE_SIZE - 1))       // round to multiple of page size
        actual_size += PAGE_SIZE - (actual_size & (PAGE_SIZE - 1));

    byte* memory = (byte*) VirtualAlloc(NULL, actual_size, MEM_RESERVE, PAGE_NOACCESS);
    assert(memory);

    byte* rw_start = memory + PAGE_SIZE;
    byte* rw_end   = memory + actual_size - PAGE_SIZE;
    assert(rw_start <= rw_end);
    byte* rw_ok = (byte*) VirtualAlloc(rw_start, rw_end - rw_start, MEM_COMMIT, PAGE_READWRITE);
    assert(rw_ok);

    byte* user_start = rw_end - size;
    // Fill extra space at the bottom of the page with guard bytes,
    // and user-allocated space with garbage bytes.
    memset(rw_start, GUARD_BYTE, user_start - rw_start);
    memset(user_start, GARBAGE_BYTE, size);

    // store allocation info
    acquire(&the_leakcheck_lock);
    {
        Allocation_Info* info = new Allocation_Info;
        info->deliberately_leaked = the_next_allocation_is_deliberately_leaked;
        capture_stack(info->trace, ArrayCount(info->trace), 2);
        the_allocation_table[user_start] = info;
    }
    release(&the_leakcheck_lock);

    return user_start;
}

static void get_allocation_boundaries(byte* user_start, byte** out_base, byte** out_end)
{
    byte* rw_start = (byte*)((umm) user_start & ~(umm)(PAGE_SIZE - 1));

    MEMORY_BASIC_INFORMATION information = {};
    umm count = VirtualQuery(rw_start, &information, sizeof(information));
    assert(count == sizeof(information));

    assert(information.AllocationBase == rw_start - PAGE_SIZE);
    assert(information.AllocationProtect == PAGE_NOACCESS);
    assert(information.State != MEM_FREE);
    assert(information.Type == MEM_PRIVATE);

    *out_base = rw_start - PAGE_SIZE;
    if (information.State == MEM_RESERVE)
    {
        assert(information.AllocationBase == user_start - PAGE_SIZE);
        *out_end = user_start + PAGE_SIZE;
    }
    else
    {
        *out_end = rw_start + information.RegionSize + PAGE_SIZE;
    }

    for (byte* guard_byte = rw_start; guard_byte < user_start; guard_byte++)
        assert(*guard_byte == GUARD_BYTE);
}

static void debug_release(void* object)
{
    byte* user_start = (byte*) object;
    if (!user_start)
        return;

    // free allocation info
    acquire(&the_leakcheck_lock);
    {
        Allocation_Info* info = the_allocation_table[user_start];
        assert(info && "You're freeing an invalid pointer!");
        the_allocation_table.erase(user_start);
        delete info;
    }
    release(&the_leakcheck_lock);

    // decommit the pages
    byte* base;
    byte* end;
    get_allocation_boundaries(user_start, &base, &end);

    byte* rw_start = base + PAGE_SIZE;
    byte* rw_end   = end  - PAGE_SIZE;
    assert(rw_start <= rw_end);

    BOOL ok = VirtualFree(rw_start, rw_end - rw_start, MEM_DECOMMIT);
    assert(ok);

    // add to reserved backlog
    byte* to_release = NULL;
    acquire(&the_leakcheck_lock);
    {
        umm capacity = ArrayCount(the_reserved_backlog);
        if (the_reserved_backlog_count == capacity)
            to_release = the_reserved_backlog[the_reserved_backlog_index];
        else
            the_reserved_backlog_count++;

        the_reserved_backlog[the_reserved_backlog_index] = base;
        the_reserved_backlog_index = (the_reserved_backlog_index + 1) % capacity;
    }
    release(&the_leakcheck_lock);

    // maybe release the oldest reserved allocation
    if (to_release)
    {
        BOOL ok = VirtualFree(to_release, 0, MEM_RELEASE);
        assert(ok);
    }
}

static void* debug_reallocate(void* object, umm size, bool zeroed, umm alignment)
{
    if (!object)
        return debug_allocate(size, alignment);


    byte* base;
    byte* end;
    byte* user_start = (byte*) object;
    get_allocation_boundaries(user_start, &base, &end);

    byte* rw_end = end - PAGE_SIZE;
    assert(user_start <= rw_end);
    umm old_size = rw_end - user_start;


    void* result = debug_allocate(size, alignment);
    if (zeroed)
        memset(result, 0x00, size);

    memcpy(result, object, (size < old_size ? size : old_size));

    debug_release(object);
    return result;
}



void* debug_malloc(size_t size)
{
    return debug_allocate(size, 16);
}

void* debug_calloc(size_t items, size_t size)
{
    void* result = debug_allocate(items * size, 16);
    memset(result, 0, items * size);
    return result;
}

void* debug_realloc(void* ptr, size_t size)
{
    return debug_reallocate(ptr, size, false, 16);
}

void* debug_recalloc(void* ptr, size_t items, size_t size)
{
    return debug_reallocate(ptr, items * size, true, 16);
}

void debug_free(void* ptr)
{
    debug_release(ptr);
}




constexpr u64 REGION_GUARD = 0xCDCDCDCDCDCDCDCDull;

void* lk_region_alloc(LK_Region* region, size_t size, size_t alignment)
{
    Leakcheck_Region_Item* item = (Leakcheck_Region_Item*) debug_allocate(sizeof(Leakcheck_Region_Item) + size, alignment);
    item->previous = region->top;
    item->guard_qword = REGION_GUARD;
    region->top = item;
    region->pop_count++;
    memset(item->data, 0, size);
    return item->data;
}

void lk_region_free(LK_Region* region)
{
    LK_Region_Cursor cursor = {};
    cursor.pop_count = 0;
    lk_region_rewind(region, &cursor);
    assert(!region->top);
    assert(!region->pop_count);
}

void lk_region_cursor(LK_Region* region, LK_Region_Cursor* cursor)
{
    cursor->pop_count = region->pop_count;
}

void lk_region_rewind(LK_Region* region, LK_Region_Cursor* cursor)
{
    while (cursor->pop_count < region->pop_count)
    {
        Leakcheck_Region_Item* item = region->top;
        assert(item->guard_qword == REGION_GUARD);
        region->top = item->previous;
        debug_release(item);
        region->pop_count--;
    }
    assert(cursor->pop_count == region->pop_count);
}



void leakcheck_report_alive_memory()
{
    acquire(&the_leakcheck_lock);

    std::map<Allocation_Info, u32> counts;
    for (auto const& kv : the_allocation_table)
        if (!kv.second->deliberately_leaked)
            counts[*kv.second]++;

    release(&the_leakcheck_lock);

    for (auto const& kv : counts)
    {
        Allocation_Info const& info = kv.first;
        u32 count = kv.second;

        char buf[256];
        sprintf(buf, "\nThis leaked %u time%s.\n", count, (count == 1) ? "" : "s");
        OutputDebugStringA(buf);
        debug_print_stack(info.trace, ArrayCount(info.trace));
    }
}


GlobalCleanupBlock
{
    leakcheck_report_alive_memory();
};



#if 0
static constexpr byte GUARD_BYTE = 0xCD;
static constexpr umm  GUARD_SIZE = 32;

struct alignas(16) Allocation
{
    Link  link;
    void* trace[16];
    umm   size;
};

static SRWLOCK live_allocations_lock = SRWLOCK_INIT;
static List(Allocation, link) live_allocations;


static void* debug_allocate(size_t size)
{
    void* memory = crt_malloc(sizeof(Allocation) + 2 * GUARD_SIZE + size);

    // set up guards
    Allocation* allocation = (Allocation*) memory;
    byte* object = (byte*) allocation + sizeof(Allocation) + GUARD_SIZE;

    ZeroStruct(allocation);
    allocation->size = size;
    memset((byte*) allocation + sizeof(Allocation), GUARD_BYTE, 2 * GUARD_SIZE + size);

    // get stack trace
    capture_stack(allocation->trace, ArrayCount(allocation->trace), 2);

    // record allocation
    AcquireSRWLockExclusive(&live_allocations_lock);
    link(&live_allocations, allocation);
    ReleaseSRWLockExclusive(&live_allocations_lock);

    return object;
}

static void debug_release(void* object)
{
    if (!object)
        return;

    Allocation* allocation = (Allocation*)((byte*) object - GUARD_SIZE - sizeof(Allocation));

    // check guards
    byte* low_guard  = (byte*) allocation + sizeof(Allocation);
    byte* high_guard = (byte*) allocation + sizeof(Allocation) + GUARD_SIZE + allocation->size;
    for (umm i = 0; i < GUARD_SIZE; i++)
        if (low_guard[i] != GUARD_BYTE || high_guard[i] != GUARD_BYTE)
        {
            OutputDebugStringA(" --- Guard found allocation misuse! --- \n");
            debug_print_stack(allocation->trace, ArrayCount(allocation->trace));
            __debugbreak();
        }

    // forget the allocation
    AcquireSRWLockExclusive(&live_allocations_lock);
    unlink(&live_allocations, allocation);
    ReleaseSRWLockExclusive(&live_allocations_lock);

    crt_free(allocation);
}

static void* debug_reallocate(void* ptr, size_t size, bool zeroed)
{
    if (!ptr)
        return debug_allocate(size);

    Allocation* allocation = (Allocation*)((byte*) ptr - GUARD_SIZE - sizeof(Allocation));
    size_t old_size = allocation->size;
    void* result = debug_allocate(size);
    if (zeroed)
        memset(result, 0, size);

    memcpy(result, ptr, (size < old_size ? size : old_size));

    debug_release(ptr);
    return result;
}



void* debug_malloc(size_t size)
{
    return debug_allocate(size);
}

void* debug_calloc(size_t items, size_t size)
{
    void* result = debug_allocate(items * size);
    memset(result, 0, items * size);
    return result;
}

void* debug_realloc(void* ptr, size_t size)
{
    return debug_reallocate(ptr, size, false);
}

void* debug_recalloc(void* ptr, size_t items, size_t size)
{
    return debug_reallocate(ptr, items * size, true);
}

void debug_free(void* ptr)
{
    debug_release(ptr);
}


GlobalCleanupBlock
{
    AcquireSRWLockExclusive(&live_allocations_lock);

    while (live_allocations.head)
    {
        auto* it = live_allocations.head;

        u32 count = 0;
        for (auto* other : live_allocations)
        {
            if (memcmp(it->trace, other->trace, sizeof(it->trace)) != 0)
                continue;

            unlink(&live_allocations, other);
            count++;
        }

        char buf[256];
        sprintf(buf, "\nThis leaked %u time%s.\n", count, (count == 1) ? "" : "s");
        OutputDebugStringA(buf);
        debug_print_stack(it->trace, ArrayCount(it->trace));
    }

    ReleaseSRWLockExclusive(&live_allocations_lock);
};
#endif


#endif  // LEAKCHECK



ExitApplicationNamespace
