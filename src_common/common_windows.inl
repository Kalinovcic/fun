#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <iphlpapi.h>
#include <shellapi.h>

EnterApplicationNamespace


////////////////////////////////////////////////////////////////////////////////
// Atomic locks
////////////////////////////////////////////////////////////////////////////////


void spin(Spinner* spinner)
{
    u32 delay = spinner->count;
    if (delay <= spinner->fast_count)
    {
        while (delay--)
            pause();
        spinner->count *= 2;
    }
    else
    {
        Sleep(0);
    }
}


////////////////////////////////////////////////////////////////////////////////
// Memory allocation
////////////////////////////////////////////////////////////////////////////////


void* allocate_virtual_memory(umm size, bool high_address_range)
{
    DWORD type = MEM_RESERVE | MEM_COMMIT;
    if (high_address_range)
        type |= MEM_TOP_DOWN;
    return VirtualAlloc(NULL, size, type, PAGE_READWRITE);
}

void release_virtual_memory(void* base, umm size)
{
    (void) size;  // unused
    VirtualFree(base, 0, MEM_RELEASE);
}


////////////////////////////////////////////////////////////////////////////////
// File path utilities.
////////////////////////////////////////////////////////////////////////////////


Path_Comparison_Result compare_paths(String a, String b)
{
    String16 a16 = make_windows_path_string16(a);
    String16 b16 = make_windows_path_string16(b);
    if (a16 == b16)  // common case: exactly the same path
        return PATHS_POINT_TO_THE_SAME_FILE;

    DWORD access = 0;  // we can still query file information without having any access to it
    DWORD share  = FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD flags  = FILE_FLAG_BACKUP_SEMANTICS;  // to allow opening directories
    HANDLE file_a = CreateFileW((WCHAR*) a16.data, access, share, NULL, OPEN_EXISTING, flags, NULL);
    HANDLE file_b = CreateFileW((WCHAR*) b16.data, access, share, NULL, OPEN_EXISTING, flags, NULL);

    Defer({ if (file_a != INVALID_HANDLE_VALUE) CloseHandle(file_a); });
    Defer({ if (file_b != INVALID_HANDLE_VALUE) CloseHandle(file_b); });

    if (file_a == INVALID_HANDLE_VALUE && file_b == INVALID_HANDLE_VALUE) return COULD_NOT_FIND_EITHER_FILE;
    if (file_a == INVALID_HANDLE_VALUE) return COULD_NOT_FIND_FILE_A;
    if (file_b == INVALID_HANDLE_VALUE) return COULD_NOT_FIND_FILE_B;

    BY_HANDLE_FILE_INFORMATION info_a = {};
    BY_HANDLE_FILE_INFORMATION info_b = {};
    if (!GetFileInformationByHandle(file_a, &info_a) ||
        !GetFileInformationByHandle(file_b, &info_b))
    {
        return COULD_NOT_CHECK_PATHS_POINT_TO_THE_SAME_FILE;
    }

    if (info_a.dwVolumeSerialNumber == info_b.dwVolumeSerialNumber &&
        info_a.nFileIndexLow        == info_b.nFileIndexLow &&
        info_a.nFileIndexHigh       == info_b.nFileIndexHigh)
    {
        return PATHS_POINT_TO_THE_SAME_FILE;
    }

    return PATHS_DO_NOT_POINT_TO_THE_SAME_FILE;
}


////////////////////////////////////////////////////////////////////////////////
// Logging.
////////////////////////////////////////////////////////////////////////////////


void report_win32_error(String subsystem, u32 error_code, String while_doing_what)
{
    LANGID language = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);

    LPWSTR error16 = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, error_code, language, (LPWSTR) &error16, 0, NULL);
    if (error16)
    {
        String error = trim(make_utf8_path(error16));
        LocalFree(error16);

        LogError(subsystem, "%\n .. Error code 0x%\n .. %", error, hex_format(error_code, 8), while_doing_what);
    }
    else
    {
        LogError(subsystem, "Error code 0x%\n .. %", hex_format(error_code, 8), while_doing_what);
    }
}

void report_last_win32_error(String subsystem, String while_doing_what)
{
    report_win32_error(subsystem, GetLastError(), while_doing_what);
}


////////////////////////////////////////////////////////////////////////////////
// IOCP
////////////////////////////////////////////////////////////////////////////////


static const String iocp_subsystem = "IOCP"_s;

static HANDLE iocp;
static umm iocp_worker_count;

void set_io_worker_count(umm count)
{
    assert(!iocp);
    assert(!iocp_worker_count);
    iocp_worker_count = count;
}

umm get_io_worker_count()
{
    launch_iocp();
    return iocp_worker_count;
}

static void iocp_worker(void*)
{
    while (true)
    {
        Scope_Region_Cursor temp_scope(temp);

        DWORD length = 0;
        ULONG_PTR completion_key = 0;
        OVERLAPPED* overlapped = NULL;

        BOOL success = GetQueuedCompletionStatus(iocp, &length, &completion_key, &overlapped, INFINITE);
        if (!overlapped)
        {
            report_last_win32_error(iocp_subsystem, "In GetQueuedCompletionStatus"_s);
            continue;
        }

        IOCP_Callback* callback = (IOCP_Callback*) completion_key;
        if (callback)
        {
            DWORD error = success ? 0 : GetLastError();
            callback(error, overlapped, length);
        }
    }
}

void* launch_iocp()
{
    if (iocp) return iocp;
    SynchronizedScope();
    if (iocp) return iocp;

    fence();

    if (!iocp_worker_count)
    {
        iocp_worker_count = get_hardware_parallelism();
        if (iocp_worker_count > 4)
            iocp_worker_count = 4;
    }

    if (iocp_worker_count < 1  ) iocp_worker_count = 1;
    if (iocp_worker_count > 100) iocp_worker_count = 100;

    iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, iocp_worker_count);
    for (umm i = 0; i < iocp_worker_count; i++)
        spawn_thread("iocp"_s, NULL, iocp_worker);

    return iocp;
}

bool post_to_iocp(IOCP_Callback* callback, _OVERLAPPED* overlapped, u32 length)
{
    if (!iocp) launch_iocp();

    if (!PostQueuedCompletionStatus(iocp, (DWORD) length, (umm) callback, overlapped))
    {
        report_last_win32_error(iocp_subsystem, "In PostQueuedCompletionStatus."_s);
        return false;
    }

    return true;
}

bool associate_handle_with_iocp(void* handle, IOCP_Callback* callback)
{
    if (!iocp) launch_iocp();

    if (CreateIoCompletionPort((HANDLE) handle, iocp, (umm) callback, 0) != iocp)
    {
        report_last_win32_error(iocp_subsystem, "In CreateIoCompletionPort."_s);
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////
// File system
////////////////////////////////////////////////////////////////////////////////


bool check_if_file_exists(String path)
{
    LPCWSTR path16 = make_windows_path(path);
    DWORD attributes = GetFileAttributesW(path16);
    return ((attributes != INVALID_FILE_ATTRIBUTES) && !(attributes & FILE_ATTRIBUTE_DIRECTORY));
}

bool check_if_directory_exists(String path)
{
    LPCWSTR path16 = make_windows_path(path);
    DWORD attributes = GetFileAttributesW(path16);
    return ((attributes != INVALID_FILE_ATTRIBUTES) && (attributes & FILE_ATTRIBUTE_DIRECTORY));
}


bool check_disk(String path, u64* out_available, u64* out_total)
{
    *out_available = 0;
    *out_total     = 0;
    if (!check_if_directory_exists(path)) return false;

    LPCWSTR path16 = make_windows_path(path);
    ULARGE_INTEGER available = {};
    ULARGE_INTEGER total = {};
    BOOL success = GetDiskFreeSpaceExW(path16, &available, &total, NULL);
    if (!success) return false;

    *out_available = available.QuadPart;
    *out_total     = total    .QuadPart;
    return true;
}


bool create_directory(String path)
{
    LPCWSTR path16 = make_windows_path(path);

    SetLastError(0);
    bool success = CreateDirectoryW(path16, NULL);
    if (!success)
    {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS)
        {
            ReportLastWin32(subsystem_files, "While creating directory %", path);
            return false;
        }
    }

    return true;
}


static bool open_file_for_reading(String path, u64 offset, HANDLE* out_file, u64* out_remaining_file_length)
{
    LPCWSTR path16 = make_windows_path(path);
    HANDLE file = CreateFileW(path16, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        ReportLastWin32(subsystem_files, "While opening file % for reading", path);
        CloseHandle(file);
        return false;
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file, &file_size))
    {
        ReportLastWin32(subsystem_files, "While getting size of file % for reading", path);
        CloseHandle(file);
        return false;
    }

    if (offset > file_size.QuadPart)
    {
        LogWarn(subsystem_files, "Failed to read file %, because offset is greater than file size", path);
        CloseHandle(file);
        return false;
    }

    if (offset)
    {
        LARGE_INTEGER offset_large_integer;
        offset_large_integer.QuadPart = offset;
        if (!SetFilePointerEx(file, offset_large_integer, NULL, FILE_BEGIN))
        {
            ReportLastWin32(subsystem_files, "While setting the offset in file % for reading", path);
            CloseHandle(file);
            return false;
        }

        file_size.QuadPart -= offset;
    }

    *out_file = file;
    *out_remaining_file_length = file_size.QuadPart;
    return true;
}

// requirement: remaining file length from the cursor >= buffer_length
static bool read_file_into_preallocated_buffer(HANDLE file, u8* buffer, umm buffer_length)
{
    umm remaining = buffer_length;
    while (remaining)
    {
        u32 max_read_length = 512 * 1024;
        u32 read_length = (remaining > max_read_length) ? max_read_length : (u32) remaining;

        DWORD amount_read;
        BOOL success = ReadFile(file, buffer, read_length, &amount_read, NULL);
        if (!success || (amount_read != read_length))
        {
            ReportLastWin32(subsystem_files, "Failed to read file.");
            return false;
        }

        buffer    += read_length;
        remaining -= read_length;
    }

    return true;
}

bool read_file(String* out_content,
               String path, u64 offset,
               umm min_size, umm max_size,
               void* preallocated_destination, Region* memory)
{
    *out_content = {};
    if (!max_size) return true;
    assert(min_size <= max_size);

    HANDLE file;
    u64    length;
    if (!open_file_for_reading(path, offset, &file, &length))
        return false;

    Defer(CloseHandle(file));

    if (min_size > length)
    {
        LogWarn(subsystem_files, "Failed to read file %, because the requested size doesn't fit in memory", path);
        *out_content = {};
        return false;
    }
    if (length > max_size)
        length = max_size;

    String string;
    string.length = length;
    if (preallocated_destination)
    {
        string.data = (u8*) preallocated_destination;
    }
    else
    {
        string.data = alloc<u8, false>(memory, length + 1);
        string.data[string.length] = 0;
    }

    if (!read_file_into_preallocated_buffer(file, string.data, string.length))
    {
        ReportLastWin32(subsystem_files, "While reading from file %", path);

        if (!preallocated_destination && !memory)
            free(string.data);
        return false;
    }

    *out_content = string;
    return true;
}

bool get_file_length(String path, u64* out_file_length)
{
    HANDLE file;
    if (!open_file_for_reading(path, 0, &file, out_file_length))
        return false;
    CloseHandle(file);
    return true;
}

static bool write_loop(HANDLE file, String content)
{
    u8* buffer = content.data;
    umm remaining = content.length;
    while (remaining)
    {
        u32 max_write_length = 512 * 1024;
        u32 write_length = (remaining > max_write_length) ? max_write_length : (u32) remaining;

        DWORD amount_written;
        BOOL success = WriteFile(file, buffer, write_length, &amount_written, NULL);
        if (!success || (amount_written != write_length))
            return false;

        buffer += write_length;
        remaining -= write_length;
    }

    return true;
}

bool write_to_file(String path, u64 offset, String content, bool must_exist)
{
    LPCWSTR path16 = make_windows_path(path);
    HANDLE file = CreateFileW(path16, GENERIC_WRITE, FILE_SHARE_READ, NULL, must_exist ? OPEN_EXISTING : OPEN_ALWAYS, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        ReportLastWin32(subsystem_files, "While opening file % for writing", path);
        return false;
    }
    Defer(CloseHandle(file));

    if (offset == U64_MAX)
    {
        LARGE_INTEGER offset_large_integer;
        offset_large_integer.QuadPart = 0;
        if (!SetFilePointerEx(file, offset_large_integer, NULL, FILE_END))
        {
            ReportLastWin32(subsystem_files, "While setting offset to EOF in file % for writing", path);
            return false;
        }
    }
    else if (offset)
    {
        LARGE_INTEGER offset_large_integer;
        offset_large_integer.QuadPart = offset;
        if (!SetFilePointerEx(file, offset_large_integer, NULL, FILE_BEGIN))
        {
            ReportLastWin32(subsystem_files, "While setting offset in file % for writing", path);
            return false;
        }
    }

    bool success = write_loop(file, content);
    if (!success)
        ReportLastWin32(subsystem_files, "While writing to file %", path);

    return success;
}

bool write_entire_file(String path, String content)
{
    LPCWSTR path16 = make_windows_path(path);
    HANDLE file = CreateFileW(path16, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        ReportLastWin32(subsystem_files, "While opening file % for writing", path);
        return false;
    }
    Defer(CloseHandle(file));

    bool success = write_loop(file, content);
    if (!success)
        ReportLastWin32(subsystem_files, "While writing to %", path);
    return success;
}



static Array<String> generic_search(String search, String extension, bool allow_files, bool allow_directores)
{
    LPCWSTR search16 = make_windows_path(search);

    WIN32_FIND_DATAW find_data;
    HANDLE find = FindFirstFileW(search16, &find_data);
    if (find == INVALID_HANDLE_VALUE)
        return {};

    Defer(FindClose(find));

    Dynamic_Array<String> files = {};
    Defer(free_heap_array(&files));

    while (true)
    {
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (allow_directores)
            {
                String name = make_utf8_path(find_data.cFileName);
                if (!extension || suffix_equals(name, extension))
                    if (name != "."_s && name != ".."_s)
                        add_item(&files, &name);
            }
        }
        else
        {
            if (allow_files)
            {
                String name = make_utf8_path(find_data.cFileName);
                if (!extension || suffix_equals(name, extension))
                    add_item(&files, &name);
            }
        }

        bool has_next = FindNextFileW(find, &find_data);
        if (!has_next)
            break;
    }

    return allocate_array(temp, &files);
}


Array<String> list_files(String parent_path)
{
    String search = concatenate(parent_path, "/*"_s);
    return generic_search(search, {}, true, false);
}

Array<String> list_files(String parent_path, String extension)
{
    String dot_extension = concatenate("."_s, extension);
    String search = concatenate(parent_path, "/*."_s, extension);
    return generic_search(search, dot_extension, true, false);
}

Array<String> list_directories(String parent_path)
{
    String search = concatenate(parent_path, "/*"_s);
    return generic_search(search, {}, false, true);
}



bool move_file(String destination_path, String path)
{
    LPCWSTR path16        = make_windows_path(path);
    LPCWSTR destination16 = make_windows_path(destination_path);
    DWORD flags = MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH | MOVEFILE_REPLACE_EXISTING;
    BOOL success = MoveFileExW(path16, destination16, flags);
    return success;
}

bool delete_file(String path)
{
    LPCWSTR path16 = make_windows_path(path);
    BOOL success = DeleteFileW(path16);
    return success;
}

bool delete_directory_conditional(String path, bool delete_directory, Delete_Action(*should_delete)(String parent, String name, bool is_file, void* userdata), void* userdata)
{
    WIN32_FIND_DATAW find_data;
    HANDLE find = FindFirstFileW(make_windows_path(concatenate_path(temp, path, "*"_s)), &find_data);
    if (find == INVALID_HANDLE_VALUE)
        return true;

    bool success = true;
    bool has_more = true;
    while (has_more)
    {
        Scope_Region_Cursor temp_scope(temp);

        bool is_file = !(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        String name = make_utf8_path(find_data.cFileName);
        has_more = FindNextFileW(find, &find_data);
        if (name == "."_s || name == ".."_s) continue;

        Delete_Action action = should_delete(path, name, is_file, userdata);
        if (action == DO_NOT_DELETE) continue;

        String child = concatenate_path(temp, path, name);
        if (is_file)
        {
            if (action == DELETE_FILE_OR_DIRECTORY)
                if (!DeleteFileW(make_windows_path(child)))
                    success = false;
        }
        else
        {
            if (!delete_directory_conditional(child, action == DELETE_FILE_OR_DIRECTORY, should_delete, userdata))
                success = false;
        }
    }

    FindClose(find);

    if (delete_directory)
    {
        if (!RemoveDirectoryW(make_windows_path(path)))
            success = false;
    }

    return success;
}


bool get_file_time(String path, File_Time* out_creation, File_Time* out_write)
{
    LPCWSTR path16 = make_windows_path(path);
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;  // to allow opening directories
    HANDLE handle = CreateFileW(path16, GENERIC_READ, share, NULL, OPEN_EXISTING, flags, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return false;

    Defer(CloseHandle(handle));

    FILETIME creation_time;
    FILETIME write_time;
    if (!GetFileTime(handle, &creation_time, NULL, &write_time))
        return false;

    u64 creation64 = ((u64) creation_time.dwHighDateTime << 32) | creation_time.dwLowDateTime;
    u64 write64    = ((u64) write_time   .dwHighDateTime << 32) | write_time   .dwLowDateTime;

    if (out_creation) *out_creation = creation64;
    if (out_write)    *out_write    = write64;
    return true;
}


bool copy_file(String from, String to, bool error_on_overwrite, bool write_through)
{
    DWORD flags = 0;
    if (error_on_overwrite) flags |= COPY_FILE_FAIL_IF_EXISTS;
    if (write_through)      flags |= COPY_FILE_NO_BUFFERING;

    BOOL cancel = false;
    return CopyFileExW(make_windows_path(from), make_windows_path(to), NULL, NULL, &cancel, flags);
}


Memory_Mapped_String open_memory_mapped_file_readonly(String path)
{
    String log_system = "open_memory_mapped_file_readonly"_s;

    LPCWSTR path16 = make_windows_path(path);
    HANDLE handle = CreateFileW(path16, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    Defer(CloseHandle(handle));

    LARGE_INTEGER size;
    if (!GetFileSizeEx(handle, &size))
    {
        LogError(log_system, "Failed to open % - GetFileSizeEx failed!", path);
        return {};
    }

    if (size.QuadPart != (SIZE_T) size.QuadPart || size.QuadPart != (umm) size.QuadPart)
    {
        LogError(log_system, "Failed to open % - file is too big!", path);
        return {};
    }

    HANDLE map = CreateFileMapping(handle, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!map)
    {
        LogError(log_system, "Failed to open % - CreateFileMapping failed!", path);
        return {};
    }

    void* data = MapViewOfFile(map, FILE_MAP_READ, 0, 0, (SIZE_T) size.QuadPart);
    if (!data)
    {
        LogError(log_system, "Failed to open % - MapViewOfFile failed!", path);
        CloseHandle(map);
        return {};
    }

    Memory_Mapped_String result = {};
    result.length = (umm) size.QuadPart;
    result.data   = (u8*) data;
    result.os_handle = map;
    return result;
}

void close_memory_mapped_file(Memory_Mapped_String* file)
{
    if (file->os_handle)
        UnmapViewOfFile(file->os_handle);
    ZeroStruct(file);
}


////////////////////////////////////////////////////////////////////////////////
// Transactional file utilities
////////////////////////////////////////////////////////////////////////////////


void* Safe_Filesystem::open(bool* out_success, u64* out_size, String path, bool share_read, bool report_open_failures)
{
again:
    if (out_success) *out_success = false;
    if (out_size) *out_size = 0;

    LPCWSTR path16 = make_windows_path(path);
    DWORD disposition = (out_size ? OPEN_ALWAYS : CREATE_ALWAYS);
    DWORD access      = GENERIC_READ | GENERIC_WRITE;
    DWORD share       = share_read ? FILE_SHARE_READ : 0;
    HANDLE handle = CreateFileW(path16, access, share, NULL, disposition, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE)
    {
        if (report_open_failures)
            ReportLastWin32(subsystem_files, "CreateFileW failed in Safe_Filesystem::open(), path = %", path);
        if (out_success) return NULL;
        goto again;
    }

    if (out_size)
    {
        LARGE_INTEGER size;
        if (!GetFileSizeEx(handle, &size))
        {
            if (report_open_failures)
                ReportLastWin32(subsystem_files, "GetFileSizeEx failed in Safe_Filesystem::open(), path = %", path);
            CloseHandle(handle);
            if (out_success) return NULL;
            goto again;
        }

        *out_size = size.QuadPart;
    }

    if (out_success) *out_success = true;
    return handle;
}

void Safe_Filesystem::close(void* file)
{
    CloseHandle(file);
}

void Safe_Filesystem::flush(void* file)
{
    u64 failures = 0;
    while (!FlushFileBuffers(file))
        if (++failures == 100)
            ReportLastWin32(subsystem_files, "FlushFileBuffers failed in Safe_Filesystem::flush()");
}

void Safe_Filesystem::read(void* file, u64 size, void* data)
{
    u64 failures = 0;
    TRACE_LINE("reading % % %", file, size, data);
    while (size)
    {
        u32 max_length = 65536 * BLOCK_SIZE;  // 32 MB
        u32 length = (size > max_length) ? max_length : (u32) size;

        DWORD read;
        BOOL success = ReadFile(file, data, length, &read, NULL);
        if (!success || read > length)
            read = 0;

        if (!success)
            if (++failures == 100)
                ReportLastWin32(subsystem_files, "ReadFile failed in Safe_Filesystem::read()");

        size -= read;
        data = (byte*) data + read;
    }
}

void Safe_Filesystem::write(void* file, u64 size, void const* data)
{
    u64 failures = 0;
    while (size)
    {
        u32 max_length = 65536 * BLOCK_SIZE;  // 32 MB
        u32 length = (size > max_length) ? max_length : (u32) size;

        DWORD written;
        BOOL success = WriteFile(file, data, length, &written, NULL);
        if (!success || written > length)
            written = 0;

        if (!success)
            if (++failures == 100)
                ReportLastWin32(subsystem_files, "WriteFile failed in Safe_Filesystem::write()");

        size -= written;
        data = (byte const*) data + written;
    }
}

void Safe_Filesystem::seek(void* file, u64 offset)
{
    u64 failures = 0;
    LARGE_INTEGER offset_large_integer;
    offset_large_integer.QuadPart = offset;
    while (!SetFilePointerEx(file, offset_large_integer, NULL, FILE_BEGIN))
        if (++failures == 100)
            ReportLastWin32(subsystem_files, "SetFilePointerEx failed in Safe_Filesystem::seek()");
}

void Safe_Filesystem::trim(void* file)
{
    u64 failures = 0;
    while (!SetEndOfFile(file))
        if (++failures == 100)
            ReportLastWin32(subsystem_files, "SetEndOfFile() failed in Safe_Filesystem::trim()");
}

void Safe_Filesystem::erase(String path)
{
    u64 failures = 0;
    while (!delete_file(path))
        if (++failures == 100)
            ReportLastWin32(subsystem_files, "delete_file() failed in Safe_Filesystem::erase() for %", path);
}


////////////////////////////////////////////////////////////////////////////////
// File dialog
////////////////////////////////////////////////////////////////////////////////


static String subsystem_file_dialog = "file dialog"_s;


static String get_filter_string(File_Dialog_Options* options)
{
    String filter = {};
    if (options->extensions_array.count)
    {
        For (options->extensions_array)
        {
            append(&filter, temp, filter.length ? ";*."_s : "*."_s);
            append(&filter, temp, *it);
        }
    }
    else if (options->extensions)
    {
        String extensions = options->extensions;
        while (extensions)
        {
            String extension = consume_until(&extensions, ';');
            append(&filter, temp, filter.length ? ";*."_s : "*."_s);
            append(&filter, temp, extension);
        }
    }
    return filter;
}


#if ARCHITECTURE_X64

static HRESULT windows_file_dialog(File_Dialog_Options* options, Array<String>* results)
{
    HRESULT status;

    status = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (status != S_FALSE && FAILED(status)) return status;
    Defer(CoUninitialize());

    REFCLSID class_id = (options->flags & FILE_DIALOG_SAVE) ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
    IFileDialog* dialog = NULL;
    status = CoCreateInstance(class_id, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(status)) return status;
    Defer(dialog->Release());

    DWORD flags;
    status = dialog->GetOptions(&flags);
    if (FAILED(status)) return status;

    flags |= FOS_NOCHANGEDIR;
    flags |= FOS_FORCEFILESYSTEM;
    if (options->flags & FILE_DIALOG_DIRECTORIES)
        flags |= FOS_PICKFOLDERS;
    if (options->flags & FILE_DIALOG_MULTIPLE_FILES)
        flags |= FOS_ALLOWMULTISELECT;

    status = dialog->SetOptions(flags);
    if (FAILED(status)) return status;

    if (options->initial_directory)
    {
        String path = allocate_string(temp, options->initial_directory);
        replace_all_occurances(path, '/', '\\');
        LPCWSTR path16 = make_windows_path(path);

        IShellItem* folder;
        status = SHCreateItemFromParsingName(path16, NULL, IID_PPV_ARGS(&folder));
        if (FAILED(status)) return status;
        Defer(folder->Release());

        status = dialog->SetFolder(folder);
        if (FAILED(status)) return status;
    }

    String filter = get_filter_string(options);
    if (filter)
    {
        LPCWSTR filter16 = (LPCWSTR) convert_utf8_to_utf16(filter, temp).data;

        COMDLG_FILTERSPEC file_types[2];
        file_types[0].pszName = L"All Files";
        file_types[0].pszSpec = L"*.*";
        file_types[1].pszName = L"Filtered";
        file_types[1].pszSpec = filter16;
        status = dialog->SetFileTypes(ArrayCount(file_types), file_types);
        if (FAILED(status)) return status;

        status = dialog->SetFileTypeIndex(2);
        if (FAILED(status)) return status;
    }

    HWND owner_hwnd = (HWND) options->parent_window;
    status = dialog->Show(owner_hwnd);
    if (status == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return S_OK;
    if (FAILED(status)) return status;

    auto shell_item_to_full_path = [&status](IShellItem* item) -> String
    {
        PWSTR path16;
        status = item->GetDisplayName(SIGDN_FILESYSPATH, &path16);
        if (FAILED(status)) return {};
        Defer(CoTaskMemFree(path16));

        return make_utf8_path(path16);
    };

    if (options->flags & FILE_DIALOG_SAVE)
    {
        IShellItem* item;
        status = dialog->GetResult(&item);
        if (FAILED(status)) return status;
        Defer(item->Release());

        *results = allocate_array<String>(temp, 1);
        String path = shell_item_to_full_path(item);
        if (!path) return status;
        (*results)[0] = path;
    }
    else
    {
        IFileOpenDialog* open_dialog;
        status = dialog->QueryInterface(IID_PPV_ARGS(&open_dialog));
        if (FAILED(status)) return status;
        Defer(open_dialog->Release());

        IShellItemArray* items;
        status = open_dialog->GetResults(&items);
        if (FAILED(status)) return status;
        Defer(items->Release());

        DWORD item_count;
        status = items->GetCount(&item_count);
        if (FAILED(status)) return status;

        *results = allocate_array<String>(temp, item_count);
        for (DWORD index = 0; index < item_count; index++)
        {
            IShellItem* item;
            status = items->GetItemAt(index, &item);
            if (FAILED(status)) return status;
            Defer(item->Release());

            String path = shell_item_to_full_path(item);
            if (!path) return status;
            (*results)[index] = path;
        }
    }

    return status;
}

#else

static HRESULT windows_file_dialog(File_Dialog_Options* options, Array<String>* results)
{
    HWND hwnd = (HWND) options->parent_window;

    WCHAR file_name[MAX_PATH];
    file_name[0] = 0;

    if (options->flags & FILE_DIALOG_DIRECTORIES)
    {
        BROWSEINFOW info = {};
        info.ulFlags   = BIF_NEWDIALOGSTYLE;
        info.hwndOwner = hwnd;
        PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&info);
        if (!id)
        {
            *results = {};
            return S_OK;
        }

        if (!SHGetPathFromIDListW(id, file_name))
            return E_FAIL;
    }
    else if (options->flags & FILE_DIALOG_SAVE)
    {
        return E_FAIL;
    }
    else
    {
        OPENFILENAMEW open = {};
        open.lStructSize   = sizeof(open);
        open.hwndOwner     = hwnd;
        open.lpstrFile     = file_name;
        open.nMaxFile      = ArrayCount(file_name);
        open.Flags         = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

        String filter = get_filter_string(options);
        if (filter)
        {
            filter = concatenate(temp, "All Files\0*.*\0Filtered\0"_s, filter, "\0"_s);
            open.lpstrFilter = (LPCWSTR) convert_utf8_to_utf16(filter, temp).data;
            open.nFilterIndex = 2;
        }

        if (!GetOpenFileNameW(&open))
            return E_FAIL;
    }

    *results = allocate_array<String>(temp, 1);
    (*results)[0] = convert_utf16_to_utf8(wrap_string16((u16*) file_name), temp);
    return S_OK;
}

#endif



Array<String> file_dialog(File_Dialog_Options* options)
{
    flags32 flags = options->flags;
    assert(!((flags & FILE_DIALOG_SAVE) && (flags & FILE_DIALOG_DIRECTORIES)) && "Can't open a save dialog that selects directories.");
    assert(!((flags & FILE_DIALOG_SAVE) && (flags & FILE_DIALOG_MULTIPLE_FILES)) && "Can't open a save dialog that selects multiple files.");
    assert(!((flags & FILE_DIALOG_DIRECTORIES) && (options->extensions || options->extensions_array.count)) && "Can't specify extensions for opening directories.");

    Array<String> results = {};
    HRESULT status = windows_file_dialog(options, &results);
    if (FAILED(status))
    {
        LogError(subsystem_file_dialog, "Error in file dialog. Status: 0x%", hex_format(status));
        return {};
    }
    return results;
}

String save_file_dialog(String extensions, String initial_directory, void* parent_window)
{
    File_Dialog_Options options = {};
    options.parent_window = parent_window;
    options.flags = FILE_DIALOG_SAVE;
    options.extensions = extensions;
    options.initial_directory = initial_directory;
    Array<String> results = file_dialog(&options);
    return results.count ? results[0] : ""_s;
}

String open_file_dialog(String extensions, String initial_directory, void* parent_window)
{
    File_Dialog_Options options = {};
    options.parent_window = parent_window;
    options.flags = FILE_DIALOG_OPEN;
    options.extensions = extensions;
    options.initial_directory = initial_directory;
    Array<String> results = file_dialog(&options);
    return results.count ? results[0] : ""_s;
}

Array<String> open_files_dialog(String extensions, String initial_directory, void* parent_window)
{
    File_Dialog_Options options = {};
    options.parent_window = parent_window;
    options.flags = FILE_DIALOG_OPEN | FILE_DIALOG_MULTIPLE_FILES;
    options.extensions = extensions;
    options.initial_directory = initial_directory;
    return file_dialog(&options);
}

String open_directory_dialog(String initial_directory, void* parent_window)
{
    File_Dialog_Options options = {};
    options.parent_window = parent_window;
    options.flags = FILE_DIALOG_OPEN | FILE_DIALOG_DIRECTORIES;
    options.initial_directory = initial_directory;
    Array<String> results = file_dialog(&options);
    return results.count ? results[0] : ""_s;
}

Array<String> open_directories_dialog(String initial_directory, void* parent_window)
{
    File_Dialog_Options options = {};
    options.parent_window = parent_window;
    options.flags = FILE_DIALOG_OPEN | FILE_DIALOG_MULTIPLE_FILES | FILE_DIALOG_DIRECTORIES;
    options.initial_directory = initial_directory;
    return file_dialog(&options);
}


////////////////////////////////////////////////////////////////////////////////
// Time
////////////////////////////////////////////////////////////////////////////////



void rough_sleep(double seconds)
{
    Sleep((DWORD)(seconds * 1000 + 0.5));
}


File_Time current_filetime()
{
    SYSTEMTIME system_time;
    GetSystemTime(&system_time);

    union
    {
        FILETIME file_time;
        File_Time result;
    };
    SystemTimeToFileTime(&system_time, &file_time);

    return result;
}

s64 get_utc_offset()
{
    SYSTEMTIME utc;
    SYSTEMTIME local;

    FILETIME ft_utc;
    FILETIME ft_local;

    GetSystemTime(&utc);
    if (!SystemTimeToTzSpecificLocalTime(NULL, &utc, &local)) return 0;
    if (!SystemTimeToFileTime(&utc,   &ft_utc  )) return 0;
    if (!SystemTimeToFileTime(&local, &ft_local)) return 0;

    u64 utc64   = ft_utc  .dwLowDateTime | ((u64) ft_utc  .dwHighDateTime << 32);
    u64 local64 = ft_local.dwLowDateTime | ((u64) ft_local.dwHighDateTime << 32);
    return (s64)(local64 - utc64) / 600000000;
}



QPC qpc_frequency;
GlobalBlock
{
    LARGE_INTEGER frequency;
    BOOL ok = QueryPerformanceFrequency(&frequency);
    assert(ok);
    qpc_frequency = frequency.QuadPart;
};

QPC current_qpc()
{
    LARGE_INTEGER counter;
    BOOL ok = QueryPerformanceCounter(&counter);
    assert(ok);
    return counter.QuadPart;
}

double seconds_from_qpc(QPC qpc)
{
    return (double) qpc / (double) qpc_frequency;
}

QPC qpc_from_seconds(double seconds)
{
    return (QPC)(seconds * (double) qpc_frequency);
}



////////////////////////////////////////////////////////////////////////////////
// Threading utilities
////////////////////////////////////////////////////////////////////////////////


static String subsystem_pipe = "pipe"_s;

static String make_stupid_pipe_name(String suffix)
{
    static u32 global_serial_number;
    u32 serial = InterlockedIncrement(&global_serial_number);
    u32 process_id = GetCurrentProcessId();
    String name = Format(temp, "//./pipe/PipeOwnedByGlobalMMK.%.%.%",
                         hex_format(process_id, 8), hex_format(serial, 8), suffix);
    return name;
}

static void make_pipe(Pipe* pipe, u32 buffer_size, DWORD access, String suffix)
{
    pipe->name = allocate_string(NULL, make_stupid_pipe_name(suffix));

    LPCWSTR name16 = make_windows_path(pipe->name);
    pipe->handle = CreateNamedPipeW(name16, access, PIPE_TYPE_BYTE, 1, buffer_size, buffer_size, 0, NULL);
    if (!pipe->handle)
    {
        ReportLastWin32(subsystem_pipe, "FAILED TO CREATE A PIPE! make_pipe(). Name: %", pipe->name);
        assert(false);
    }
}

void make_local_pipe(Pipe* in, Pipe* out, u32 buffer_size)
{
    make_pipe(out, buffer_size, PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED, {});

    LPCWSTR path16 = make_windows_path(out->name);
    in->name = allocate_string(NULL, out->name);
    in->handle = CreateFileW(path16, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (in->handle == INVALID_HANDLE_VALUE)
    {
        ReportLastWin32(subsystem_pipe, "FAILED TO CREATE A PIPE! make_local_pipe(). Name: %", out->name);
        assert(false);
    }
}

void make_pipe_in(Pipe* pipe, u32 buffer_size, String suffix)
{
    make_pipe(pipe, buffer_size, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, suffix);
}

void make_pipe_out(Pipe* pipe, u32 buffer_size, String suffix)
{
    make_pipe(pipe, buffer_size, PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED, suffix);
}

void make_pipe_duplex(Pipe* pipe, u32 buffer_size, String suffix)
{
    make_pipe(pipe, buffer_size, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, suffix);
}

bool connect_pipe_duplex(Pipe* pipe, String name)
{
    LPCWSTR name16 = make_windows_path(name);

    pipe->name = allocate_string(NULL, name);
    pipe->handle = CreateFileW(name16, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (pipe->handle == INVALID_HANDLE_VALUE)
    {
        pipe->handle = 0;
        ReportLastWin32(subsystem_pipe, "While connecting to a named pipe '%'", name);
        return false;
    }

    return true;
}

void free_pipe(Pipe* pipe)
{
    if (pipe->handle) CloseHandle(pipe->handle);
    free_heap_string(&pipe->name);
    ZeroStruct(pipe);
}

bool seek(Pipe* pipe, u32 size)
{
    static u8 buffer[1024];
    while (size)
    {
        DWORD read;
        DWORD to_read = (size < 1024 ? size : 1024);
        if (!ReadFile(pipe->handle, buffer, to_read, &read, NULL))
            return false;
        size -= read;
    }

    return true;
}

bool read(Pipe* pipe, void* data, u32 size)
{
    while (size)
    {
        OVERLAPPED overlapped = {};

        BOOL done = ReadFile(pipe->handle, data, size, NULL, &overlapped);
        if (!done && GetLastError() != ERROR_IO_PENDING)
            return false;

        DWORD read;
        if (!GetOverlappedResult(pipe->handle, &overlapped, &read, true))
            return false;

        data = (BYTE*) data + read;
        size -= read;
    }

    return true;
}

static u32 available(Pipe* pipe)
{
    DWORD available = 0;
    PeekNamedPipe(pipe->handle, NULL, 0, NULL, &available, NULL);
    return available;
}

bool try_read(Pipe* pipe, void* data, u32 size, bool* out_error)
{
    if (out_error) *out_error = false;
    if (available(pipe) < size) return false;
    bool ok = read(pipe, data, size);
    if (!ok && out_error) *out_error = true;
    return ok;
}

bool write(Pipe* pipe, void* data, u32 size)
{
    while (size)
    {
        OVERLAPPED overlapped = {};

        BOOL done = WriteFile(pipe->handle, data, size, NULL, &overlapped);
        if (!done && GetLastError() != ERROR_IO_PENDING)
            return false;

        DWORD written;
        if (!GetOverlappedResult(pipe->handle, &overlapped, &written, true))
            return false;

        data = (BYTE*) data + written;
        size -= written;
    }

    return true;
}


Array<String> read_lines(Pipe* pipe, Region* memory)
{
    auto peek = [](Pipe* pipe, void* data, u32 size) -> bool
    {
        DWORD read;
        if (!PeekNamedPipe(pipe->handle, data, size, &read, NULL, NULL))
            return false;
        return read == size;
    };

    u32 amount = available(pipe);
    if (!amount)
        return {};

    String data = allocate_zero_string(memory, amount);
    peek(pipe, data.data, data.length);

    u8* base = data.data;
    Array<String> lines = {};
    while (true)
    {
        consume_whitespace(&data);
        umm length = find_first_occurance(data, '\n');
        if (length == NOT_FOUND) break;

        String line = take(&data, length);
        region_array_append(memory, &lines, &line, 1);
    }

    seek(pipe, data.data - base);
    return lines;
}



#if ARCHITECTURE_X64

CompileTimeAssert(sizeof(SRWLOCK) == sizeof(void*));

void make_lock(Lock* lock)
{
    InitializeSRWLock((SRWLOCK*) &lock->handle);
}

void free_lock(Lock* lock)
{
}

void acquire(Lock* lock)
{
    AcquireSRWLockExclusive((SRWLOCK*) &lock->handle);
}

void release(Lock* lock)
{
    ReleaseSRWLockExclusive((SRWLOCK*) &lock->handle);
}

void acquire_read(Lock* lock)
{
    AcquireSRWLockShared((SRWLOCK*) &lock->handle);
}

bool acquire_read_timeout(Lock* lock, QPC timeout)
{
    QPC start_time = current_qpc();
    QPC wait_time = qpc_from_seconds(0.001);
    while (!TryAcquireSRWLockShared((SRWLOCK*) &lock->handle))
    {
        QPC remaining = timeout - (current_qpc() - start_time);
        if (remaining < 0)
            return false;

        wait_time *= 2;
        if (wait_time > remaining)
            wait_time = remaining;
        Sleep((DWORD)(seconds_from_qpc(wait_time) * 1000) + 1);
    }

    return true;
}

void release_read(Lock* lock)
{
    ReleaseSRWLockShared((SRWLOCK*) &lock->handle);
}

#else

void make_lock(Lock* lock)
{
    CRITICAL_SECTION* crit = alloc<CRITICAL_SECTION>(NULL);
    lock->handle = crit;
    InitializeCriticalSection(crit);
}

void free_lock(Lock* lock)
{
    CRITICAL_SECTION* crit = (CRITICAL_SECTION*) lock->handle;
    DeleteCriticalSection(crit);
    free(crit);
}

void acquire(Lock* lock)
{
    CRITICAL_SECTION* crit = (CRITICAL_SECTION*) lock->handle;
    EnterCriticalSection(crit);
}

void release(Lock* lock)
{
    CRITICAL_SECTION* crit = (CRITICAL_SECTION*) lock->handle;
    LeaveCriticalSection(crit);
}

#endif



void make_ipc_lock(IPC_Lock* ipc_lock, String name)
{
    name = concatenate("Global\\mmk_funny_pun_"_s, name);
    String16 name16 = convert_utf8_to_utf16(name, temp);
    HANDLE handle = CreateMutexW(NULL, false, (LPCWSTR)name16.data);
    assert(handle != INVALID_HANDLE_VALUE);
    ipc_lock->handle = handle;
}

void free_ipc_lock(IPC_Lock* ipc_lock)
{
    CloseHandle(ipc_lock->handle);
    ZeroStruct(ipc_lock);
}

void ipc_acquire(IPC_Lock* ipc_lock)
{
    DWORD status = WaitForSingleObject(ipc_lock->handle, INFINITE);
    assert(status == WAIT_OBJECT_0);
}

void ipc_release(IPC_Lock* ipc_lock)
{
    BOOL ok = ReleaseMutex(ipc_lock->handle);
    assert(ok);
}




void make_semaphore(Semaphore* sem, u32 initial)
{
    sem->handle = CreateSemaphoreA(NULL, initial, 0x7FFFFFF, NULL);
    assert(sem->handle);
}

void free_semaphore(Semaphore* sem)
{
    CloseHandle(sem->handle);
}

void post(Semaphore* sem)
{
    BOOL success = ReleaseSemaphore(sem->handle, 1, NULL);
    assert(success);
}

void wait(Semaphore* sem)
{
    DWORD result = WaitForSingleObject(sem->handle, INFINITE);
    assert(result == WAIT_OBJECT_0);
}

bool wait(Semaphore* sem, double timeout_seconds)
{
    double milliseconds = timeout_seconds * 1000.0;

    DWORD dw_milliseconds;
    if (milliseconds <= 0.0)
        dw_milliseconds = 0;
    else if (milliseconds < S32_MAX)
        dw_milliseconds = (DWORD)(milliseconds + 0.5);
    else
        dw_milliseconds = INFINITE;

    DWORD result = WaitForSingleObject(sem->handle, dw_milliseconds);
    assert(result == WAIT_OBJECT_0 || result == WAIT_TIMEOUT);
    return result == WAIT_OBJECT_0;
}



#if ARCHITECTURE_X64

CompileTimeAssert(sizeof(CONDITION_VARIABLE) == sizeof(void*));

void make_condition_variable(Condition_Variable* variable)
{
    InitializeConditionVariable((CONDITION_VARIABLE*) &variable->handle);
}

void free_condition_variable(Condition_Variable* variable)
{
}

void signal(Condition_Variable* variable)
{
    WakeConditionVariable((CONDITION_VARIABLE*) &variable->handle);
}

void signal_all(Condition_Variable* variable)
{
    WakeAllConditionVariable((CONDITION_VARIABLE*) &variable->handle);
}

void wait(Condition_Variable* variable, Lock* lock)
{
    SleepConditionVariableSRW((CONDITION_VARIABLE*) &variable->handle,
                              (SRWLOCK*) &lock->handle,
                              INFINITE, 0);
}

bool wait(Condition_Variable* variable, Lock* lock, double timeout_seconds)
{
    double milliseconds = timeout_seconds * 1000.0;

    DWORD dw_milliseconds;
    if (milliseconds <= 0.0)
        dw_milliseconds = 0;
    else if (milliseconds < S32_MAX)
        dw_milliseconds = (DWORD)(milliseconds + 0.5);
    else
        dw_milliseconds = INFINITE;

    BOOL ok = SleepConditionVariableSRW((CONDITION_VARIABLE*) &variable->handle,
                                        (SRWLOCK*) &lock->handle,
                                        dw_milliseconds, 0);

    return ok;
}

#endif



void make_event(Event* event)
{
    event->handle = CreateEventW(NULL, FALSE, FALSE, NULL);
    assert(event->handle);
}

void free_event(Event* event)
{
    CloseHandle(event->handle);
}

void signal(Event* event)
{
    BOOL success = SetEvent(event->handle);
    assert(success);
}

void wait(Event* event)
{
    DWORD result = WaitForSingleObject(event->handle, INFINITE);
    assert(result == WAIT_OBJECT_0);
}

bool wait(Event* event, double timeout_seconds)
{
    double milliseconds = timeout_seconds * 1000.0;

    DWORD dw_milliseconds;
    if (milliseconds <= 0.0)
        dw_milliseconds = 0;
    else if (milliseconds < S32_MAX)
        dw_milliseconds = (DWORD)(milliseconds + 0.5);
    else
        dw_milliseconds = INFINITE;

    DWORD result = WaitForSingleObject(event->handle, dw_milliseconds);
    assert(result == WAIT_OBJECT_0 || result == WAIT_TIMEOUT);
    return result == WAIT_OBJECT_0;
}



static void set_thread_name(const char* name)
{
    //
    // Set the thread name by throwing an exception that Visual Studio recognizes.
    //

    constexpr DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push,8)
    struct THREADNAME_INFO
    {
        DWORD  dwType;     // Must be 0x1000.
        LPCSTR szName;     // Pointer to name (in user addr space).
        DWORD  dwThreadID; // Thread ID (-1 = caller thread).
        DWORD  dwFlags;    // Reserved for future use, must be zero.
    };
#pragma pack(pop)

    THREADNAME_INFO info;
    info.dwType     = 0x1000;
    info.szName     = name;
    info.dwThreadID = (DWORD) -1;
    info.dwFlags    = 0;

#pragma warning(push)
#pragma warning(disable: 6320 6322)
    __try
    {
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*) &info);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
#pragma warning(pop)

    //
    // Set the thread name using SetThreadDescription (since Windows 10, 1607)
    //

    HMODULE kernel32 = LoadLibraryA("kernel32.dll");
    if (kernel32)
    {
        HRESULT(WINAPI *SetThreadDescription)(HANDLE hThread, PCWSTR lpThreadDescription);
        *(FARPROC*) &SetThreadDescription = GetProcAddress(kernel32, "SetThreadDescription");
        if (SetThreadDescription)
        {
            leakcheck_ignore_next_allocation();
            WCHAR* name16 = (WCHAR*) convert_utf8_to_utf16(wrap_string(name), temp).data;
            SetThreadDescription(GetCurrentThread(), name16);
        }
        FreeLibrary(kernel32);
    }
}

thread_local String current_thread_name;

void spawn_thread(String name, void* userdata, void(*entry)(void*), Thread* out_thread)
{
    struct Info
    {
        String name;
        void* userdata;
        void(*entry)(void*);
    };

    leakcheck_ignore_next_allocation();
    Info* info = alloc<Info>(NULL);
    leakcheck_ignore_next_allocation();
    info->name = allocate_string_on_heap(name);
    info->userdata = userdata;
    info->entry = entry;

    DWORD thread_id;
    HANDLE thread = CreateThread(NULL, 0, [](void* info_ptr) -> DWORD
    {
        Info info = *(Info*) info_ptr;
        leakcheck_ignore_next_allocation();
        current_thread_name = Format(NULL, "% (ID %)", info.name, GetCurrentThreadId());
        leakcheck_ignore_next_allocation();
        char* name = make_c_style_string(info.name);
        free_heap_string(&info.name);
        free(info_ptr);

        set_thread_name(name);
        info.entry(info.userdata);

        free_heap_string(&current_thread_name);
        lk_region_free(temp);
        return 0;
    }, info, 0, &thread_id);
    if (!thread)
        ReportLastWin32("spawn_thread"_s, "FAILED TO CREATE A THREAD! CreateThread failed.");
    assert(thread);

    if (out_thread)
    {
        out_thread->handle    = thread;
        out_thread->thread_id = thread_id;
    }
    else
    {
        CloseHandle(thread);
    }
}

void wait(Thread* thread)
{
    if (!thread || !thread->handle) return;
    DWORD result = WaitForSingleObject(thread->handle, INFINITE);
    assert(result == WAIT_OBJECT_0);
    CloseHandle(thread->handle);
    thread->handle = NULL;
}

bool is_thread_running(Thread* thread)
{
    if (!thread || !thread->handle) return false;
    DWORD result = WaitForSingleObject(thread->handle, 0);
    if (result == WAIT_OBJECT_0)
    {
        CloseHandle(thread->handle);
        thread->handle = NULL;
        return false;
    }

    assert(result == WAIT_TIMEOUT);
    return true;
}



umm get_hardware_parallelism()
{
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    return system_info.dwNumberOfProcessors;
}



////////////////////////////////////////////////////////////////////////////////
// Process utilities
////////////////////////////////////////////////////////////////////////////////



void prevent_sleep_mode()
{
    if (SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED) == NULL)
    {
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
    }
}


void get_cpu_and_memory_usage(double* out_cpu_usage, u64* out_physical_use, u64* out_physical_max)
{
    static QPC last_query_time;
    static MEMORYSTATUSEX last_mem_info;
    static u64 last_idle;
    static u64 last_kernel;
    static u64 last_user;
    static double last_cpu_use;

    QPC now = current_qpc();
    if (now - last_query_time > qpc_from_seconds(1))
    {
        last_query_time = now;

        MEMORYSTATUSEX mem_info = {};
        mem_info.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&mem_info))
            last_mem_info = mem_info;

        u64 now_idle, now_kernel, now_user;
        if (GetSystemTimes((FILETIME*) &now_idle, (FILETIME*) &now_kernel, (FILETIME*) &now_user))
        {
            u64 d_idle   = now_idle   - last_idle;
            u64 d_kernel = now_kernel - last_kernel;
            u64 d_user   = now_user   - last_user;

            u64 d_system = d_kernel + d_user;
            if (d_system == 0)
                last_cpu_use = 0;
            else
                last_cpu_use = (d_system - d_idle) / (double) d_system;

            last_idle   = now_idle;
            last_kernel = now_kernel;
            last_user   = now_user;
        }
    }

    *out_cpu_usage    = last_cpu_use;
    *out_physical_use = last_mem_info.ullTotalPhys - last_mem_info.ullAvailPhys;
    *out_physical_max = last_mem_info.ullTotalPhys;
}


static MIB_IFTABLE* get_network_table()
{
    static DWORD(__stdcall *GetIfTable)(PMIB_IFTABLE pIfTable, PULONG pdwSize, BOOL bOrder);
    OnlyOnce
    {
        HMODULE iphlpapi = LoadLibraryW(L"iphlpapi.dll");
        if (iphlpapi)
            *(FARPROC*) &GetIfTable = GetProcAddress(iphlpapi, "GetIfTable");
    }

    if (!GetIfTable)
        return NULL;

    DWORD table_size = sizeof(MIB_IFTABLE);
    MIB_IFTABLE* table = (MIB_IFTABLE*) alloc<byte>(NULL, table_size);
    if (GetIfTable(table, &table_size, TRUE) == ERROR_INSUFFICIENT_BUFFER)
    {
        free(table);
        table = (MIB_IFTABLE*) alloc<byte>(NULL, table_size);
    }
    if (GetIfTable(table, &table_size, TRUE) != NO_ERROR)
    {
        free(table);
        table = NULL;
    }
    return table;
}

void get_network_performance(double* out_read_bps, double* out_write_bps)
{
    SynchronizedScope();

    *out_read_bps  = 0;
    *out_write_bps = 0;

    MIB_IFTABLE* table = get_network_table();
    File_Time now = current_filetime();
    if (!table) return;

    static MIB_IFTABLE* previous_table = NULL;
    static File_Time previous_filetime = 0;
    if (previous_table)
    {
        for (umm new_it = 0; new_it < table->dwNumEntries; new_it++)
        {
            MIB_IFROW* new_row = &table->table[new_it];
            for (umm old_it = 0; old_it < previous_table->dwNumEntries; old_it++)
            {
                MIB_IFROW* old_row = &previous_table->table[old_it];
                if (new_row->dwIndex != old_row->dwIndex) continue;

                double delta = seconds_from_filetime(now - previous_filetime);
                double in  = (double)(new_row->dwInOctets  - old_row->dwInOctets ) * 8 / delta;
                double out = (double)(new_row->dwOutOctets - old_row->dwOutOctets) * 8 / delta;
                if (in > *out_read_bps)
                {
                    *out_read_bps  = in;
                    *out_write_bps = out;
                }
            }
        }
    }

    previous_table = table;
    previous_filetime = now;
}



static String read_registry_key(HKEY key, LPCWSTR value_name, Region* memory)
{
    LSTATUS status;
    while (true)
    {
        DWORD length;
        status = RegQueryValueExW(key, value_name, NULL, NULL, NULL, &length);
        if (status != ERROR_SUCCESS)
            return {};

        u16* data = (u16*) alloc<u8>(NULL, length + 2);
        Defer(free(data));

        status = RegQueryValueExW(key, value_name, NULL, NULL, (LPBYTE) data, &length);
        if (status != ERROR_SUCCESS)
        {
            if (status == ERROR_MORE_DATA) continue;
            return {};
        }

        return convert_utf16_to_utf8(wrap_string16(data), memory);
    }
}

void get_system_information(System_Information* info)
{
    ZeroStruct(info);
    HKEY key;
    DWORD flags = KEY_QUERY_VALUE | KEY_WOW64_32KEY | KEY_ENUMERATE_SUB_KEYS;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", 0, flags, &key))
    {
        Defer(RegCloseKey(key));
        info->motherboard = concatenate(read_registry_key(key, L"BaseBoardManufacturer", temp), " "_s,
                                        read_registry_key(key, L"BaseBoardProduct", temp), " "_s,
                                        read_registry_key(key, L"BaseBoardVersion", temp));
        info->system_name = read_registry_key(key, L"SystemProductName", temp);
        info->bios = concatenate(read_registry_key(key, L"BIOSVendor", temp), " "_s,
                                 read_registry_key(key, L"BIOSVersion", temp));
    }
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, flags, &key))
    {
        Defer(RegCloseKey(key));
        info->cpu_name = read_registry_key(key, L"ProcessorNameString", temp);
    }
}



u32 current_process_id()
{
    return GetCurrentProcessId();
}

void terminate_current_process(u32 exit_code)
{
    TerminateProcess(GetCurrentProcess(), exit_code);
    Unreachable;
}

static WCHAR* get_module_directory()
{
    // get the exe path
    SIZE_T size = MAX_PATH + 1;
    WCHAR* path = (WCHAR*) LocalAlloc(LMEM_FIXED, sizeof(WCHAR) * size);
    if (!path) return 0;
    while (GetModuleFileNameW(0, path, size) == size)
    {
        size *= 2;
        LocalFree(path);
        path = (WCHAR*) LocalAlloc(LMEM_FIXED, sizeof(WCHAR) * size);
        if (!path) return 0;
    }

    return path;
}

String get_executable_path()
{
    WCHAR* path16 = get_module_directory();
    if (!path16)
        return ""_s;
    String path = make_utf8_path(path16);
    LocalFree(path16);
    return path;
}



static HANDLE the_job_object;

GlobalBlock
{
    HANDLE job = CreateJobObjectA(NULL, NULL);
    assert(job);

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    BOOL ok = SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
    assert(ok);

    the_job_object = job;
};



static void escape_command_argument(String* out, Region* mem, String s)
{
    bool needs_quoting = !s;
    for (umm i = 0; i < s.length && !needs_quoting; i++)
        if (s[i] == ' ' || s[i] == '\t' || s[i] == '"')
            needs_quoting = true;

    if (needs_quoting)
        append(out, mem, "\""_s);

    umm backslashes = 0;
    for (umm i = 0; i < s.length; i++)
    {
        if (s[i] == '\\')
        {
            append(out, mem, "\\"_s);
            backslashes++;
        }
        else
        {
            if (s[i] == '"')
            {
                backslashes++;
                while (backslashes--)
                    append(out, mem, "\\"_s);
            }
            backslashes = 0;
            append(out, mem, &s[i], 1);
        }
    }

    if (needs_quoting)
        append(out, mem, "\""_s);
}


static String subsystem_process = "process"_s;

bool run_process(Process* process)
{
    Scope_Region_Cursor temp_scope(temp);

    //
    // Prepare command line.
    //

    String command_line = {};
    escape_command_argument(&command_line, temp, process->path);
    For (process->arguments)
    {
        append(&command_line, temp, " "_s);

        if (process->do_not_escape_arguments)
            append(&command_line, temp, *it);
        else
            escape_command_argument(&command_line, temp, *it);
    }

    //Debug("CMD: %", command_line);
    LPWSTR command_line16 = (LPWSTR) convert_utf8_to_utf16(command_line, temp).data;


    //
    // Prepare standard pipes.
    //

    STARTUPINFOW startup_info = {};
    startup_info.cb         = sizeof(STARTUPINFOW);
    startup_info.dwFlags    = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup_info.wShowWindow = process->start_minimized ? SW_MINIMIZE : SW_RESTORE;

    auto set_pipe = [&](String path, HANDLE* out_pipe, bool output) -> bool
    {
        if (!path)
        {
            *out_pipe = NULL;
            return true;
        }

        SECURITY_ATTRIBUTES attribs = {};
        attribs.nLength = sizeof(SECURITY_ATTRIBUTES);
        attribs.bInheritHandle = TRUE;
        attribs.lpSecurityDescriptor = NULL;

        LPCWSTR path16 = make_windows_path(path);
        *out_pipe = CreateFileW(path16, output ? GENERIC_WRITE : GENERIC_READ,
                                FILE_SHARE_READ, &attribs,
                                output ? OPEN_ALWAYS : OPEN_EXISTING, 0, NULL);
        if (*out_pipe == INVALID_HANDLE_VALUE)
        {
            report_last_win32_error(subsystem_process, "While creating a pipe for a process"_s);
            return false;
        }

        return true;
    };

    if (!set_pipe(process->file_stdin,  &startup_info.hStdInput, false)  ||
        !set_pipe(process->file_stdout, &startup_info.hStdOutput, true) ||
        !set_pipe(process->file_stderr, &startup_info.hStdError,  true))
        return false;

    //
    // Create the process.
    //

    LPCWSTR directory = NULL;
    if (process->current_directory)
        directory = make_windows_path(process->current_directory);

    PROCESS_INFORMATION process_info = {};
    BOOL inherit_handles = true;

    DWORD creation_flags = 0;
    if (!process->detached)
        creation_flags |= CREATE_SUSPENDED;
    if (process->prohibit_console_window)
        creation_flags |= CREATE_NO_WINDOW;
    if (process->new_console_window)
        creation_flags |= CREATE_NEW_CONSOLE;
    else if (process->detached)
        creation_flags |= DETACHED_PROCESS;

    bool ok = CreateProcessW(NULL, command_line16, NULL, NULL, inherit_handles,
                             creation_flags, NULL, directory,
                             &startup_info, &process_info);
    DWORD process_error = GetLastError();

    if (startup_info.hStdInput)  CloseHandle(startup_info.hStdInput);
    if (startup_info.hStdOutput) CloseHandle(startup_info.hStdOutput);
    if (startup_info.hStdError)  CloseHandle(startup_info.hStdError);

    if (ok)
    {
        if (!process->detached)
        {
            // Add the process to the job.
            BOOL already_in_job = false;
            if (!IsProcessInJob(process_info.hProcess, NULL, &already_in_job))
                already_in_job = false;

            if (!already_in_job)
                ok = AssignProcessToJobObject(the_job_object, process_info.hProcess);
            if (!ok)
            {
                report_last_win32_error(subsystem_process, "While assigning process to job object"_s);
            }
            else if (ResumeThread(process_info.hThread) == (DWORD) -1)
            {
                report_last_win32_error(subsystem_process, "While resuming thread of started process"_s);
                ok = false;
            }

            if (!ok)
            {
                TerminateProcess(process_info.hProcess, 0);
                CloseHandle(process_info.hProcess);
            }
        }

        CloseHandle(process_info.hThread);
    }
    else
    {
        report_win32_error(subsystem_process, process_error, "While creating a process"_s);
    }

    process->handle = ok ? process_info.hProcess : NULL;
    return ok;
}

bool get_process_id(Process* process, u32* out_process_id)
{
    if (!process->handle) return false;

    DWORD process_id = GetProcessId((HANDLE)process->handle);
    *out_process_id = (u32)process_id;
    return (process_id != 0);
}

bool wait_for_process(Process* process, double seconds, u32* out_exit_code)
{
    if (!process->handle)
    {
        *out_exit_code = -1;
        return false;
    }

    HANDLE process_handle = (HANDLE) process->handle;

    DWORD wait_milliseconds = (seconds == WAIT_FOR_PROCESS_FOREVER) ? INFINITE : (DWORD)(seconds * 1000.0);
    DWORD wait_status = WaitForSingleObject(process_handle, wait_milliseconds);
    if (wait_status == WAIT_TIMEOUT || wait_status == WAIT_FAILED)
    {
        *out_exit_code = -1;
        return false;
    }

    DWORD exit_code;
    GetExitCodeProcess(process_handle, &exit_code);
    CloseHandle(process_handle);

    process->handle = NULL;

    *out_exit_code = exit_code;
    return true;
}

u32 terminate_and_wait_for_process(Process* process)
{
    if (!process->handle) return 0;
    TerminateProcess(process->handle, 0);
    return wait_for_process(process);
}

void terminate_process_without_waiting(Process* process)
{
    TerminateProcess(process->handle, 1);
    CloseHandle(process->handle);
}

bool is_process_running(Process* process)
{
    if (!process->handle)
        return false;

    DWORD wait_status = WaitForSingleObject((HANDLE)process->handle, 0);
    return wait_status != WAIT_OBJECT_0;
}



bool wait_for_process_by_id(u32 id, u32* exit_code)
{
    HANDLE process = OpenProcess(SYNCHRONIZE, false, id);
    if (!process) return false;
    DWORD wait_status = WaitForSingleObject(process, INFINITE);
    CloseHandle(process);
    return wait_status != WAIT_FAILED;
}

bool terminate_and_wait_for_process(u32 id)
{
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, false, id);
    if (!process) return false;
    TerminateProcess(process, 0);
    DWORD wait_status = WaitForSingleObject(process, INFINITE);
    CloseHandle(process);
    return wait_status != WAIT_FAILED;
}



Array<String> command_line_arguments()
{
    static Array<String> cached = {};
    OnlyOnce
    {
        LPWSTR command_line = GetCommandLineW();

        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(command_line, &argc);
        if (argv)
        {
            cached = allocate_array<String>(NULL, argc);
            for (int i = 0; i < argc; i++)
            {
                leakcheck_ignore_next_allocation();
                cached[i] = convert_utf16_to_utf8(wrap_string16((u16*) argv[i]), NULL);
            }
            LocalFree(argv);
        }
    }
    return cached;
}



void error_box(String message, String title, void* window)
{
    if (!title)
        title = "World2Capture"_s;

    MessageBoxA((HWND) window,
                make_c_style_string(message),
                make_c_style_string(title),
                MB_OK | MB_ICONERROR);
}



bool run_process_through_shell(String exe, String arguments, void** out_handle, bool minimized)
{
    SHELLEXECUTEINFOW info = {};
    info.cbSize       = sizeof(SHELLEXECUTEINFOW);
    info.fMask        = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb       = L"open";
    info.lpFile       = (LPWSTR) convert_utf8_to_utf16(exe, temp).data;
    info.lpParameters = (LPWSTR) convert_utf8_to_utf16(arguments, temp).data;
    info.nShow        = minimized ? SW_MINIMIZE : SW_RESTORE;

    if (!ShellExecuteExW(&info))
        return false;

    if (out_handle)
        *out_handle = info.hProcess;
    else if (info.hProcess)
        CloseHandle(info.hProcess);
    return true;
}



bool add_app_to_run_at_startup(String name)
{
    HKEY key;
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_WRITE, &key);
    if (status != ERROR_SUCCESS)
        return false;

    String path = get_executable_path();
    String cmd  = Format(temp, "\"%\" -auto_start", path);

    LPWSTR c_name = (LPWSTR) convert_utf8_to_utf16(name, temp).data;
    LPWSTR c_cmd  = make_windows_path(cmd);
    DWORD  length = wrap_string16((u16*) c_cmd).length * 2 + 2;
    status = RegSetValueExW(key, c_name, 0, REG_SZ, (BYTE*) c_cmd, length);

    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool prevent_app_running_at_startup(String name)
{
    HKEY key;
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_WRITE, &key);
    if (status != ERROR_SUCCESS)
        return false;

    LPWSTR c_name = (LPWSTR) convert_utf8_to_utf16(name, temp).data;
    status = RegDeleteValueW(key, c_name);

    RegCloseKey(key);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

bool set_should_app_run_at_startup(String name, bool should_run)
{
    if (should_run)
        return add_app_to_run_at_startup(name);
    else
        return prevent_app_running_at_startup(name);
}



String get_os_name()
{
    static DWORD(WINAPI *GetFileVersionInfoSizeA)(LPCSTR lptstrFilename, LPDWORD lpdwHandle);
    static BOOL (WINAPI *GetFileVersionInfoA    )(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
    static BOOL (WINAPI *VerQueryValueA         )(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen);

    OnlyOnce
    {
        HMODULE dll = LoadLibraryA("version.dll");
        if (dll)
        {
            *(FARPROC*) &GetFileVersionInfoSizeA = GetProcAddress(dll, "GetFileVersionInfoSizeA");
            *(FARPROC*) &GetFileVersionInfoA     = GetProcAddress(dll, "GetFileVersionInfoA");
            *(FARPROC*) &VerQueryValueA          = GetProcAddress(dll, "VerQueryValueA");
        }
    }

    if (!GetFileVersionInfoSizeA || !GetFileVersionInfoA || !VerQueryValueA)
        return "Windows (unknown)"_s;


    DWORD useless;
    DWORD size = GetFileVersionInfoSizeA("kernel32.dll", &useless);
    if (!size)
        return "Windows (unknown)"_s;

    void* block = alloc<byte, false>(NULL, size);
    Defer(free(block));
    if (!GetFileVersionInfoA("kernel32.dll", NULL, size, block))
        return "Windows (unknown)"_s;

    struct Language
    {
        WORD language;
        WORD code_page;
    };

    Language* lang;
    UINT lang_length;
    if (!VerQueryValueA(block, "\\VarFileInfo\\Translation",
                        (LPVOID*) &lang, &lang_length))
        return "Windows (unknown)"_s;

    for (umm i = 0; i < lang_length / sizeof(Language); i++)
    {
        char* subblock = (char*) Format(temp,
            "\\StringFileInfo\\%~%~\\ProductVersion",
            hex_format(lang[i].language, 4, true), hex_format(lang[i].code_page, 4, true)).data;

        u8* name;
        UINT name_length;
        if (VerQueryValueA(block, subblock, (LPVOID*) &name, &name_length)
         && name_length > 1)
        {
            String name_str = { name_length - 1, name };
            return concatenate("Windows "_s, name_str);
        }
    }

    return "Windows (unknown)"_s;
}

bool is_wow64()
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32");
    BOOL(WINAPI *IsWow64Process)(HANDLE, PBOOL) = NULL;
    *(FARPROC*) &IsWow64Process = GetProcAddress(kernel32, "IsWow64Process");
    if (!IsWow64Process)
        return false;

    BOOL is_wow64;
    if (!IsWow64Process(GetCurrentProcess(), &is_wow64))
        return false;
    return is_wow64;
}

String get_windows_directory()
{
    WCHAR path[MAX_PATH] = {};
    if (!GetSystemWindowsDirectoryW(path, ArrayCount(path)))
    {
        report_last_win32_error("Win32"_s, "GetSystemWindowsDirectoryW failed."_s);
        return allocate_string(temp, "C:/Windows"_s);
    }
    return make_utf8_path(path);
}

void restart_windows_explorer()
{
    QPC start_time = current_qpc();
    QPC timeout = qpc_from_seconds(120);

    String subsystem = "windows_explorer"_s;
    LogInfo(subsystem, "Restarting explorer.exe");
    while (HWND tray = FindWindowW(L"Shell_TrayWnd", NULL))
    {
        // This post is the equivalent to clicking "Exit Explorer" in the Ctrl+Shift+RMB menu.
        PostMessageW(tray, WM_USER + 436, NULL, NULL);
        Sleep(1000);

        if (current_qpc() - start_time > timeout)
            return;
    }


    LogInfo(subsystem, "Exit explorer.exe");

    String explorer = concatenate_path(temp, get_windows_directory(), "explorer.exe"_s);
    if (run_process_through_shell(explorer, {}))
        LogInfo(subsystem, "Launched explorer.exe");
    else
        LogError(subsystem, "Failed to launch explorer.exe");
}


void copy_text_to_clipboard(String text)
{
    String16 text16 = convert_utf8_to_utf16(text, temp);

    OpenClipboard(NULL);
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, (text16.length + 1) * 2);
    WCHAR* buffer = (WCHAR*) GlobalLock(handle);
    memcpy(buffer, text16.data, (text16.length + 1) * 2);
    GlobalUnlock(handle);
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, handle);
    CloseClipboard();
}



////////////////////////////////////////////////////////////////////////////////
// Random
////////////////////////////////////////////////////////////////////////////////



void entropy_source_callback(void* userdata, void(*callback)(void* userdata, byte* data, umm size))
{
    #define AddEntropyV(value) callback(userdata, (byte*) &value, sizeof(value));
    #define AddEntropy(value)                      \
    {                                              \
        u64 v = (u64) value;                       \
        callback(userdata, (byte*) &v, sizeof(v)); \
    }

    LARGE_INTEGER qpc1;
    QueryPerformanceCounter(&qpc1);
    AddEntropy(qpc1.QuadPart);
    AddEntropy(__rdtsc());

#if defined(ARCHITECTURE_X64)
    int cpuid_data[4] = {};
    __cpuid(cpuid_data, 1);
    AddEntropyV(cpuid_data);
    if ((cpuid_data[2] >> 30) & 1)  // RDRAND supported
    {
        u64 cpu_random_numbers[4];
        _rdrand64_step(&cpu_random_numbers[0]);
        _rdrand64_step(&cpu_random_numbers[1]);
        _rdrand64_step(&cpu_random_numbers[2]);
        _rdrand64_step(&cpu_random_numbers[3]);
        AddEntropyV(cpu_random_numbers);
    }
#endif

    AddEntropy(GetTickCount());
    AddEntropy(GetMessagePos());
    AddEntropy(GetMessageTime());
    AddEntropy(GetInputState());
    AddEntropy(GetCurrentProcessId());
    AddEntropy(GetCurrentThreadId());

    AddEntropy((umm) &qpc1);
    AddEntropy((umm) userdata);
    AddEntropy((umm) callback);
    AddEntropy((umm) &entropy_source_callback);

    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    AddEntropyV(system_info);

    MEMORYSTATUSEX memory_info;
    GlobalMemoryStatusEx(&memory_info);
    AddEntropyV(memory_info);

    POINT cursor;
    GetCursorPos(&cursor);
    AddEntropyV(cursor);

    POINT caret;
    GetCaretPos(&caret);
    AddEntropyV(caret);

    LARGE_INTEGER qpc2;
    QueryPerformanceCounter(&qpc2);
    AddEntropy(qpc2.QuadPart);
    AddEntropy(__rdtsc());

    #undef AddEntropy
    #undef AddEntropyV
}



ExitApplicationNamespace
