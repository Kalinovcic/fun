EnterApplicationNamespace


enum
{
    IMAGE_DOS_SIGNATURE = 0x5A4D,      // MZ
    IMAGE_NT_SIGNATURE  = 0x00004550,  // PE00
};

struct IMAGE_DOS_HEADER
{
    u16 e_magic;                     // Magic number
    u16 e_cblp;                      // Bytes on last page of file
    u16 e_cp;                        // Pages in file
    u16 e_crlc;                      // Relocations
    u16 e_cparhdr;                   // Size of header in paragraphs
    u16 e_minalloc;                  // Minimum extra paragraphs needed
    u16 e_maxalloc;                  // Maximum extra paragraphs needed
    u16 e_ss;                        // Initial (relative) SS value
    u16 e_sp;                        // Initial SP value
    u16 e_csum;                      // Checksum
    u16 e_ip;                        // Initial IP value
    u16 e_cs;                        // Initial (relative) CS value
    u16 e_lfarlc;                    // File address of relocation table
    u16 e_ovno;                      // Overlay number
    u16 e_res[4];                    // Reserved words
    u16 e_oemid;                     // OEM identifier (for e_oeminfo)
    u16 e_oeminfo;                   // OEM information; e_oemid specific
    u16 e_res2[10];                  // Reserved words
    u32 e_lfanew;                    // File address of new exe header
};


enum
{
    IMAGE_FILE_RELOCS_STRIPPED         = 0x0001,
    IMAGE_FILE_EXECUTABLE_IMAGE        = 0x0002,
    IMAGE_FILE_LINE_NUMS_STRIPPED      = 0x0004,
    IMAGE_FILE_LOCAL_SYMS_STRIPPED     = 0x0008,
    IMAGE_FILE_AGGRESIVE_WS_TRIM       = 0x0010,
    IMAGE_FILE_LARGE_ADDRESS_AWARE     = 0x0020,
    IMAGE_FILE_16BIT_MACHINE           = 0x0040,
    IMAGE_FILE_BYTES_REVERSED_LO       = 0x0080,
    IMAGE_FILE_32BIT_MACHINE           = 0x0100,
    IMAGE_FILE_DEBUG_STRIPPED          = 0x0200,
    IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP = 0x0400,
    IMAGE_FILE_NET_RUN_FROM_SWAP       = 0x0800,
    IMAGE_FILE_SYSTEM                  = 0x1000,
    IMAGE_FILE_DLL                     = 0x2000,
    IMAGE_FILE_UP_SYSTEM_ONLY          = 0x4000,
    IMAGE_FILE_BYTES_REVERSED_HI       = 0x8000,

    IMAGE_FILE_MACHINE_UNKNOWN     = 0,
    IMAGE_FILE_MACHINE_TARGET_HOST = 0x0001,
    IMAGE_FILE_MACHINE_I860        = 0x014d,
    IMAGE_FILE_MACHINE_I386        = 0x014c,
    IMAGE_FILE_MACHINE_R3000       = 0x0162,
    IMAGE_FILE_MACHINE_R4000       = 0x0166,
    IMAGE_FILE_MACHINE_R10000      = 0x0168,
    IMAGE_FILE_MACHINE_WCEMIPSV2   = 0x0169,
    IMAGE_FILE_MACHINE_ALPHA       = 0x0184,
    IMAGE_FILE_MACHINE_SH3         = 0x01a2,
    IMAGE_FILE_MACHINE_SH3DSP      = 0x01a3,
    IMAGE_FILE_MACHINE_SH3E        = 0x01a4,
    IMAGE_FILE_MACHINE_SH4         = 0x01a6,
    IMAGE_FILE_MACHINE_SH5         = 0x01a8,
    IMAGE_FILE_MACHINE_ARM         = 0x01c0,
    IMAGE_FILE_MACHINE_THUMB       = 0x01c2,
    IMAGE_FILE_MACHINE_ARMNT       = 0x01c4,
    IMAGE_FILE_MACHINE_ARM64       = 0xaa64,
    IMAGE_FILE_MACHINE_AM33        = 0x01d3,
    IMAGE_FILE_MACHINE_POWERPC     = 0x01f0,
    IMAGE_FILE_MACHINE_POWERPCFP   = 0x01f1,
    IMAGE_FILE_MACHINE_IA64        = 0x0200,
    IMAGE_FILE_MACHINE_MIPS16      = 0x0266,
    IMAGE_FILE_MACHINE_ALPHA64     = 0x0284,
    IMAGE_FILE_MACHINE_MIPSFPU     = 0x0366,
    IMAGE_FILE_MACHINE_MIPSFPU16   = 0x0466,
    IMAGE_FILE_MACHINE_AXP64       = IMAGE_FILE_MACHINE_ALPHA64,
    IMAGE_FILE_MACHINE_TRICORE     = 0x0520,
    IMAGE_FILE_MACHINE_CEF         = 0x0cef,
    IMAGE_FILE_MACHINE_EBC         = 0x0ebc,
    IMAGE_FILE_MACHINE_AMD64       = 0x8664,
    IMAGE_FILE_MACHINE_M32R        = 0x9041,
    IMAGE_FILE_MACHINE_CEE         = 0xc0ee,
};

struct IMAGE_FILE_HEADER
{
    u16 Machine;
    u16 NumberOfSections;
    u32 TimeDateStamp;
    u32 PointerToSymbolTable;
    u32 NumberOfSymbols;
    u16 SizeOfOptionalHeader;
    u16 Characteristics;
};


enum
{
    IMAGE_DIRECTORY_ENTRY_EXPORT         = 0,
    IMAGE_DIRECTORY_ENTRY_IMPORT         = 1,
    IMAGE_DIRECTORY_ENTRY_RESOURCE       = 2,
    IMAGE_DIRECTORY_ENTRY_EXCEPTION      = 3,
    IMAGE_DIRECTORY_ENTRY_SECURITY       = 4,
    IMAGE_DIRECTORY_ENTRY_BASERELOC      = 5,
    IMAGE_DIRECTORY_ENTRY_DEBUG          = 6,
    IMAGE_DIRECTORY_ENTRY_ARCHITECTURE   = 7,
    IMAGE_DIRECTORY_ENTRY_GLOBALPTR      = 8,
    IMAGE_DIRECTORY_ENTRY_TLS            = 9,
    IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG    = 10,
    IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT   = 11,
    IMAGE_DIRECTORY_ENTRY_IAT            = 12,
    IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT   = 13,
    IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR = 14,

    IMAGE_NUMBEROF_DIRECTORY_ENTRIES     = 16,
};

struct IMAGE_DATA_DIRECTORY
{
    u32 VirtualAddress;
    u32 Size;
};


enum
{
    IMAGE_NT_OPTIONAL_HDR32_MAGIC = 0x10b,
    IMAGE_NT_OPTIONAL_HDR64_MAGIC = 0x20b,
    IMAGE_ROM_OPTIONAL_HDR_MAGIC  = 0x107,

    IMAGE_SUBSYSTEM_UNKNOWN                  = 0,
    IMAGE_SUBSYSTEM_NATIVE                   = 1,
    IMAGE_SUBSYSTEM_WINDOWS_GUI              = 2,
    IMAGE_SUBSYSTEM_WINDOWS_CUI              = 3,
    IMAGE_SUBSYSTEM_OS2_CUI                  = 5,
    IMAGE_SUBSYSTEM_POSIX_CUI                = 7,
    IMAGE_SUBSYSTEM_NATIVE_WINDOWS           = 8,
    IMAGE_SUBSYSTEM_WINDOWS_CE_GUI           = 9,
    IMAGE_SUBSYSTEM_EFI_APPLICATION          = 10,
    IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER  = 11,
    IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER       = 12,
    IMAGE_SUBSYSTEM_EFI_ROM                  = 13,
    IMAGE_SUBSYSTEM_XBOX                     = 14,
    IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION = 16,

    IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA       = 0x0020,
    IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE          = 0x0040,
    IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY       = 0x0080,
    IMAGE_DLLCHARACTERISTICS_NX_COMPAT             = 0x0100,
    IMAGE_DLLCHARACTERISTICS_NO_ISOLATION          = 0x0200,
    IMAGE_DLLCHARACTERISTICS_NO_SEH                = 0x0400,
    IMAGE_DLLCHARACTERISTICS_NO_BIND               = 0x0800,
    IMAGE_DLLCHARACTERISTICS_APPCONTAINER          = 0x1000,
    IMAGE_DLLCHARACTERISTICS_WDM_DRIVER            = 0x2000,
    IMAGE_DLLCHARACTERISTICS_GUARD_CF              = 0x4000,
    IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE = 0x8000,
};

struct IMAGE_OPTIONAL_HEADER64
{
    u16 Magic;
    u8  MajorLinkerVersion;
    u8  MinorLinkerVersion;
    u32 SizeOfCode;
    u32 SizeOfInitializedData;
    u32 SizeOfUninitializedData;
    u32 AddressOfEntryPoint;
    u32 BaseOfCode;
    u64 ImageBase;
    u32 SectionAlignment;
    u32 FileAlignment;
    u16 MajorOperatingSystemVersion;
    u16 MinorOperatingSystemVersion;
    u16 MajorImageVersion;
    u16 MinorImageVersion;
    u16 MajorSubsystemVersion;
    u16 MinorSubsystemVersion;
    u32 Win32VersionValue;
    u32 SizeOfImage;
    u32 SizeOfHeaders;
    u32 CheckSum;
    u16 Subsystem;
    u16 DllCharacteristics;
    u64 SizeOfStackReserve;
    u64 SizeOfStackCommit;
    u64 SizeOfHeapReserve;
    u64 SizeOfHeapCommit;
    u32 LoaderFlags;
    u32 NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
};

enum
{
    IMAGE_SIZEOF_SHORT_NAME = 8,

    IMAGE_SCN_TYPE_NO_PAD            = 0x00000008,
    IMAGE_SCN_CNT_CODE               = 0x00000020,
    IMAGE_SCN_CNT_INITIALIZED_DATA   = 0x00000040,
    IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080,
    IMAGE_SCN_LNK_OTHER              = 0x00000100,
    IMAGE_SCN_LNK_INFO               = 0x00000200,
    IMAGE_SCN_LNK_REMOVE             = 0x00000800,
    IMAGE_SCN_LNK_COMDAT             = 0x00001000,
    IMAGE_SCN_MEM_FARDATA            = 0x00008000,
    IMAGE_SCN_MEM_PURGEABLE          = 0x00020000,
    IMAGE_SCN_MEM_16BIT              = 0x00020000,
    IMAGE_SCN_MEM_LOCKED             = 0x00040000,
    IMAGE_SCN_MEM_PRELOAD            = 0x00080000,
    IMAGE_SCN_ALIGN_1BYTES           = 0x00100000,
    IMAGE_SCN_ALIGN_2BYTES           = 0x00200000,
    IMAGE_SCN_ALIGN_4BYTES           = 0x00300000,
    IMAGE_SCN_ALIGN_8BYTES           = 0x00400000,
    IMAGE_SCN_ALIGN_16BYTES          = 0x00500000,
    IMAGE_SCN_ALIGN_32BYTES          = 0x00600000,
    IMAGE_SCN_ALIGN_64BYTES          = 0x00700000,
    IMAGE_SCN_ALIGN_128BYTES         = 0x00800000,
    IMAGE_SCN_ALIGN_256BYTES         = 0x00900000,
    IMAGE_SCN_ALIGN_512BYTES         = 0x00A00000,
    IMAGE_SCN_ALIGN_1024BYTES        = 0x00B00000,
    IMAGE_SCN_ALIGN_2048BYTES        = 0x00C00000,
    IMAGE_SCN_ALIGN_4096BYTES        = 0x00D00000,
    IMAGE_SCN_ALIGN_8192BYTES        = 0x00E00000,
    IMAGE_SCN_ALIGN_MASK             = 0x00F00000,
    IMAGE_SCN_LNK_NRELOC_OVFL        = 0x01000000,
    IMAGE_SCN_MEM_DISCARDABLE        = 0x02000000,
    IMAGE_SCN_MEM_NOT_CACHED         = 0x04000000,
    IMAGE_SCN_MEM_NOT_PAGED          = 0x08000000,
    IMAGE_SCN_MEM_SHARED             = 0x10000000,
    IMAGE_SCN_MEM_EXECUTE            = 0x20000000,
    IMAGE_SCN_MEM_READ               = 0x40000000,
    IMAGE_SCN_MEM_WRITE              = 0x80000000,
};

struct IMAGE_SECTION_HEADER
{
    u8 Name[IMAGE_SIZEOF_SHORT_NAME];
    union
    {
        u32 PhysicalAddress;
        u32 VirtualSize;
    } Misc;
    u32 VirtualAddress;
    u32 SizeOfRawData;
    u32 PointerToRawData;
    u32 PointerToRelocations;
    u32 PointerToLinenumbers;
    u16 NumberOfRelocations;
    u16 NumberOfLinenumbers;
    u32 Characteristics;
};

struct IMAGE_IMPORT_BY_NAME
{
    u16 Hint;
    u8  Name[1];
};

union IMAGE_THUNK_DATA32
{
    u32 ForwarderString;
    u32 Function;
    u32 Ordinal;
    u32 AddressOfData;
};

union IMAGE_THUNK_DATA64
{
    u64 ForwarderString;
    u64 Function;
    u64 Ordinal;
    u64 AddressOfData;
};

struct IMAGE_IMPORT_DESCRIPTOR
{
    union
    {
        u32 Characteristics;
        u32 OriginalFirstThunk;
    };
    u32 TimeDateStamp;
    u32 ForwarderChain;
    u32 Name;
    u32 FirstThunk;
};


ExitApplicationNamespace
