#define _GNU_SOURCE 1
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sched.h>
#include <pthread.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <semaphore.h>
#include <poll.h>
#include <signal.h>
#include <syscall.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/sendfile.h>
#include <sys/prctl.h>
#include <sys/wait.h>


EnterApplicationNamespace



static void set_timespec_from_seconds(timespec* time, double seconds)
{
    if (seconds <= 0)
        seconds = 0;

    double integer;
    double fraction = modf(seconds, &integer);

    time->tv_sec = (time_t) integer;
    time->tv_nsec = (long)(fraction * 1e9);
    // sanity clamps
    if (time->tv_sec  < 0)         time->tv_sec = 0;
    if (time->tv_nsec < 0)         time->tv_nsec = 0;
    if (time->tv_nsec > 999999999) time->tv_nsec = 999999999;
}

static void set_timespec_absolute_timeout(timespec* time, clockid_t clock, double timeout_seconds)
{
    timespec delta_time;
    set_timespec_from_seconds(&delta_time, timeout_seconds);

    clock_gettime(clock, time);
    time->tv_nsec += delta_time.tv_nsec;
    time->tv_sec  += delta_time.tv_sec + (time->tv_nsec / 1000000000);
    time->tv_nsec %= 1000000000;
}

static File_Time filetime_from_timespec(timespec* time)
{
    u64 unix_100ns = (u64) time->tv_sec * 10000000ull + (u64)(time->tv_nsec + 50) / 100;
    return unix_100ns + 116444736000000000ull;
}


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
        sched_yield();
    }
}


////////////////////////////////////////////////////////////////////////////////
// Memory allocation
////////////////////////////////////////////////////////////////////////////////


void* allocate_virtual_memory(umm size, bool high_address_range)
{
    (void) high_address_range;  // unused

    int prot  = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void* result = mmap(/*base*/ NULL, size, prot, flags, /*fd*/ -1, /*offset*/ 0);
    if (result == 0 || result == MAP_FAILED)
    {
        report_last_errno("memory"_s, "While callling mmap()"_s);
        return NULL;
    }

    return result;
}

void release_virtual_memory(void* base, umm size)
{
    if (munmap(base, size))
    {
        report_last_errno("memory"_s, "While callling munmap()"_s);
    }
}


////////////////////////////////////////////////////////////////////////////////
// File path utilities.
////////////////////////////////////////////////////////////////////////////////


Path_Comparison_Result compare_paths(String a, String b)
{
    if (a == b)  // common case: exactly the same path
        return PATHS_POINT_TO_THE_SAME_FILE;

    struct stat sa = {}, sb = {};
    int fail_a = stat(make_c_style_string(a), &sa);
    int fail_b = stat(make_c_style_string(b), &sb);
    if (fail_a && fail_b) return COULD_NOT_FIND_EITHER_FILE;
    if (fail_a) return COULD_NOT_FIND_FILE_A;
    if (fail_b) return COULD_NOT_FIND_FILE_B;

    if (sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino)
        return PATHS_POINT_TO_THE_SAME_FILE;
    return PATHS_DO_NOT_POINT_TO_THE_SAME_FILE;
}


////////////////////////////////////////////////////////////////////////////////
// Logging.
////////////////////////////////////////////////////////////////////////////////


bool report_errno(String subsystem, int error_code, String while_doing_what)
{
    char message_buffer[512];
    message_buffer[sizeof(message_buffer) - 1] = 0;
#if (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && ! _GNU_SOURCE
    int result = strerror_r(error_code, message_buffer, sizeof(message_buffer) - 1);
    if (result == 0)
        LogError(subsystem, "%\n .. Error code 0x%\n .. %", message_buffer, hex_format(error_code, 8), while_doing_what);
    else
        LogError(subsystem, "Error code 0x%\n .. %", hex_format(error_code, 8), while_doing_what);
#else
    char* message = strerror_r(error_code, message_buffer, sizeof(message_buffer) - 1);
    LogError(subsystem, "%\n .. Error code 0x%\n .. %", message, hex_format(error_code, 8), while_doing_what);
#endif
    return false;
}

bool report_last_errno(String subsystem, String while_doing_what)
{
    report_errno(subsystem, errno, while_doing_what);
    return false;
}


////////////////////////////////////////////////////////////////////////////////
// File system
////////////////////////////////////////////////////////////////////////////////


int close_file_descriptor(int fd)
{
again:
    if (!::close(fd)) return 0;
    if (errno == EINTR) goto again;
    return errno;
}


bool check_if_file_exists(String path)
{
    struct stat s = {};
    if (stat(make_c_style_string(path), &s)) return false;
    return !S_ISDIR(s.st_mode);
}

bool check_if_directory_exists(String path)
{
    struct stat s = {};
    if (stat(make_c_style_string(path), &s)) return false;
    return S_ISDIR(s.st_mode);
}


bool check_disk(String path, u64* out_available, u64* out_total)
{
    *out_available = 0;
    *out_total     = 0;
    struct statfs s;
    if (statfs(make_c_style_string(path), &s)) return false;
    *out_available = (u64) s.f_bsize * (u64) s.f_bavail;
    *out_total     = (u64) s.f_bsize * (u64) s.f_blocks;
    return true;
}


bool create_directory(String path)
{
    if (mkdir(make_c_style_string(path), 0777))
    {
        if (errno == EEXIST) return true;
        return ReportLastErrno(subsystem_files, "While creating directory %", path);
    }
    return true;
}


static bool read_fd(int fd, String destination, String name)
{
    u8* cursor = destination.data;
    umm size = destination.length;
    while (size)
    {
        s64 amount = ::read(fd, cursor, size);
        if (amount < 0)
        {
            if (errno == EINTR) continue;
            return ReportLastErrno(subsystem_files, "Failed to read %", name);
        }
        if (amount == 0 || amount > size)
        {
            LogWarn(subsystem_files, "Failed to read %, unexpected EOF", name);
            return false;
        }
        cursor += amount;
        size   -= amount;
    }

    return true;
}

static bool read_fd(int fd, u64 offset, String destination, String name)
{
    if (lseek(fd, offset, SEEK_SET) != offset)
        return ReportLastErrno(subsystem_files, "Failed to set offset while reading %", name);
    return read_fd(fd, destination, name);
}

static bool write_fd(int fd, String source, String name)
{
    u8* cursor = source.data;
    umm size = source.length;
    while (size)
    {
        s64 amount = ::write(fd, cursor, size);
        if (amount <= 0 || amount > size)
        {
            if (errno == EINTR) continue;
            return ReportLastErrno(subsystem_files, "Failed to write %", name);
        }
        cursor += amount;
        size   -= amount;
    }

    return true;
}

static bool write_fd(int fd, u64 offset, String source, String name)
{
    if (lseek(fd, offset, SEEK_SET) != offset)
        return ReportLastErrno(subsystem_files, "Failed to set offset while writing %", name);
    return write_fd(fd, source, name);
}


bool read_file(String* out_content,
               String path, u64 offset,
               umm min_size, umm max_size,
               void* preallocated_destination, Region* memory)
{
    *out_content = {};
    if (!max_size) return true;
    assert(min_size <= max_size);

    try_open_again:
    int fd = open(make_c_style_string(path), O_CLOEXEC | O_RDONLY);
    if (fd == -1)
    {
        if (errno == EINTR) goto try_open_again;
        return ReportLastErrno(subsystem_files, "While opening file % for reading", path);
    }
    Defer(close_file_descriptor(fd));

    // Special case: read until EOF
    if (min_size == UMM_MAX && max_size == UMM_MAX)
    {
        assert(preallocated_destination == NULL && offset == 0);

        String_Concatenator cat = {};
        Defer(free_concatenator(&cat));

        u8 buffer[128];
        s64 amount;
    more:
        while ((amount = ::read(fd, buffer, sizeof(buffer))) > 0)
            add(&cat, buffer, amount);
        if (amount < 0)
        {
            if (errno == EINTR) goto more;
            return ReportLastErrno(subsystem_files, "Failed to read file %", path);
        }

        *out_content = resolve_to_string(&cat, memory);
        return true;
    }

    // Normal case: check size and offset
    struct stat s = {};
    if (fstat(fd, &s))
        return ReportLastErrno(subsystem_files, "While getting the size of file % for reading", path);
    u64 size = s.st_size;
    if (min_size > size)
    {
        LogWarn(subsystem_files, "Failed to read file %, because the requested size doesn't fit in memory", path);
        return false;
    }
    if (size > max_size)
        size = max_size;

    if (offset > s.st_size)
    {
        LogWarn(subsystem_files, "Failed to read file %, because offset is greater than file size", path);
        return false;
    }

    String string;
    string.length = size;
    if (preallocated_destination)
    {
        string.data = (u8*) preallocated_destination;
    }
    else
    {
        string.data = alloc<u8, false>(memory, size + 1);
        string.data[string.length] = 0;
    }

    if (!read_fd(fd, offset, string, path))
    {
        if (!preallocated_destination && !memory)
            free(string.data);
        return false;
    }

    *out_content = string;
    return true;
}

bool get_file_length(String path, u64* out_file_length)
{
    *out_file_length = 0;
    struct stat s = {};
    if (stat(make_c_style_string(path), &s))
        return false;
    *out_file_length = s.st_size;
    return true;
}


static bool general_write_to_file(String path, u64 offset, String content, int flags)
{
try_open_again:
    int fd = open(make_c_style_string(path), O_CLOEXEC | flags, 0777);
    if (fd == -1)
    {
        if (errno == EINTR) goto try_open_again;
        return ReportLastErrno(subsystem_files, "While opening file % for writing", path);
    }
    Defer(close_file_descriptor(fd));

    if (offset == U64_MAX)
    {
        struct stat s = {};
        if (fstat(fd, &s))
            return ReportLastErrno(subsystem_files, "While getting the size of file % for writing", path);
        offset = s.st_size;
    }

    if (!write_fd(fd, offset, content, path))
        return false;
    return true;
}

bool write_to_file(String path, u64 offset, String content, bool must_exist)
{
    return general_write_to_file(path, offset, content, O_WRONLY | (must_exist ? 0 : O_CREAT));
}

bool write_entire_file(String path, String content)
{
    return general_write_to_file(path, 0, content, O_WRONLY | O_CREAT | O_TRUNC);
}


static Array<String> list_directory_entries(String parent_path, bool want_files, String suffix)
{
    DIR* dir = opendir(make_c_style_string(parent_path));
    if (!dir) return {};
    Defer(closedir(dir));

    int fd = dirfd(dir);
    if (fd == -1) return {};

    Dynamic_Array<String> result = {};
    Defer(free_heap_array(&result));
    while (struct dirent* entry = readdir(dir))
    {
        String name = wrap_string(entry->d_name);
        if (name == "."_s || name == ".."_s) continue;
        if (!suffix_equals(name, suffix)) continue;

        bool is_file;
        if (entry->d_type == DT_UNKNOWN)
        {
            struct stat s = {};
            fstatat(fd, entry->d_name, &s, AT_SYMLINK_NOFOLLOW);
            is_file = !S_ISDIR(s.st_mode);
        }
        else
        {
            is_file = (entry->d_type != DT_DIR);
        }
        if (is_file != want_files) continue;

        *reserve_item(&result) = allocate_string(temp, name);
    }

    return allocate_array(temp, &result);
}

Array<String> list_files(String parent_path)
{
    return list_directory_entries(parent_path, true, {});
}

Array<String> list_files(String parent_path, String extension)
{
    return list_directory_entries(parent_path, true, concatenate("."_s, extension));
}

Array<String> list_directories(String parent_path)
{
    return list_directory_entries(parent_path, false, {});
}


bool move_file(String destination_path, String path)
{
    return rename(make_c_style_string(path), make_c_style_string(destination_path)) == 0;
}

bool delete_file(String path)
{
    return ::unlink(make_c_style_string(path)) == 0;
}

bool delete_directory_conditional(String path, bool delete_directory, Delete_Action(*should_delete)(String parent, String name, bool is_file, void* userdata), void* userdata)
{
    char const* c_path = make_c_style_string(path);
    DIR* dir = opendir(c_path);
    if (!dir) return true;

    int fd = dirfd(dir);
    if (fd == -1) return false;

    bool success = true;
    while (struct dirent* entry = readdir(dir))
    {
        Scope_Region_Cursor temp_scope(temp);

        String name = wrap_string(entry->d_name);
        if (name == "."_s || name == ".."_s) continue;

        bool is_file;
        if (entry->d_type == DT_UNKNOWN)
        {
            struct stat s = {};
            fstatat(fd, entry->d_name, &s, AT_SYMLINK_NOFOLLOW);
            is_file = !S_ISDIR(s.st_mode);
        }
        else
        {
            is_file = (entry->d_type != DT_DIR);
        }

        Delete_Action action = should_delete(path, name, is_file, userdata);
        if (action == DO_NOT_DELETE) continue;

        String child = concatenate_path(temp, path, name);
        if (is_file)
        {
            if (action == DELETE_FILE_OR_DIRECTORY)
                if (unlinkat(fd, entry->d_name, 0))
                    success = false;
        }
        else
        {
            if (!delete_directory_conditional(child, action == DELETE_FILE_OR_DIRECTORY, should_delete, userdata))
                success = false;
        }
    }

    closedir(dir);
    if (delete_directory)
        if (rmdir(c_path))
            success = false;
    return success;
}

bool get_file_time(String path, File_Time* out_creation, File_Time* out_write)
{
    struct stat s = {};
    if (stat(make_c_style_string(path), &s)) return false;
    if (out_creation) *out_creation = filetime_from_timespec(&s.st_ctim);
    if (out_write)    *out_write    = filetime_from_timespec(&s.st_mtim);
    return true;
}

bool copy_file(String from, String to, bool error_on_overwrite, bool write_through)
{
    (void) write_through;  // ignored

    int input = open(make_c_style_string(from), O_CLOEXEC | O_RDONLY);
    if (input == -1) return false;
    Defer(close_file_descriptor(input));

    int flags = O_CLOEXEC | O_CREAT | O_TRUNC | O_WRONLY;
    if (error_on_overwrite) flags |= O_EXCL;
    int output = open(make_c_style_string(to), flags, 0777);
    if (output == -1) return false;
    Defer(close_file_descriptor(output));

    struct stat s = {};
    if (fstat(input, &s))
        return false;

    size_t size = s.st_size;
    off_t offset = 0;
    while (size)
    {
        ssize_t amount = sendfile(output, input, &offset, size);
        if (amount < 0 || amount > size)
            return false;

        offset += amount;
        size  -= amount;
    }

    return true;
}




////////////////////////////////////////////////////////////////////////////////
// Transactional file utilities
////////////////////////////////////////////////////////////////////////////////




void* Safe_Filesystem::open(bool* out_success, u64* out_size, String path, bool share_read, bool report_open_failures)
{
    (void) share_read;  // ignored
    if (out_success) *out_success = false;
    if (out_size) *out_size = 0;

again:
    int fd = ::open(make_c_style_string(path), O_CLOEXEC | O_CREAT | O_RDWR | (out_size ? 0 : O_TRUNC), 0777);
    if (fd == -1)
    {
        if (report_open_failures)
            ReportLastErrno(subsystem_files, "open failed in Safe_Filesystem::open(), path = %", path);
        if (out_success) return NULL;
        goto again;
    }

    if (out_size)
    {
        struct stat s = {};
        if (fstat(fd, &s))
        {
            if (report_open_failures)
                ReportLastErrno(subsystem_files, "fstat failed in Safe_Filesystem::open(), path = %", path);
            close_file_descriptor(fd);
            if (out_success) return NULL;
            goto again;
        }
        *out_size = s.st_size;
    }

    if (out_success) *out_success = true;
    return (void*)(smm) fd;
}

void Safe_Filesystem::close(void* fd_ptr)
{
    int fd = (int)(smm) fd_ptr;
    close_file_descriptor(fd);
}

void Safe_Filesystem::flush(void* fd_ptr)
{
    int fd = (int)(smm) fd_ptr;
    // @Reconsider - is fsync() enough?
    u64 failures = 0;
    while (fsync(fd))
        if (++failures == 100)
            ReportLastErrno(subsystem_files, "fsync failed in Safe_Filesystem::flush()");
}

void Safe_Filesystem::read(void* fd_ptr, u64 size, void* data)
{
    int fd = (int)(smm) fd_ptr;

    u64 failures = 0;
    u64 offset;
    while ((offset = lseek(fd, 0, SEEK_CUR)) == (off_t) -1)
        if (++failures == 100)
            ReportLastErrno(subsystem_files, "lseek failed in Safe_Filesystem::read()");

    while (!read_fd(fd, offset, { (umm) size, (u8*) data }, "in Safe_Filesystem::read()"_s))
        continue;
}

void Safe_Filesystem::write(void* fd_ptr, u64 size, void const* data)
{
    int fd = (int)(smm) fd_ptr;

    u64 failures = 0;
    u64 offset;
    while ((offset = lseek(fd, 0, SEEK_CUR)) == (off_t) -1)
        if (++failures == 100)
            ReportLastErrno(subsystem_files, "lseek failed in Safe_Filesystem::write()");

    while (!write_fd(fd, offset, { (umm) size, (u8*) data }, "in Safe_Filesystem::write()"_s))
        continue;
}

void Safe_Filesystem::seek(void* fd_ptr, u64 offset)
{
    int fd = (int)(smm) fd_ptr;

    u64 failures = 0;
    while (lseek(fd, offset, SEEK_SET) != offset)
        if (++failures == 100)
            ReportLastErrno(subsystem_files, "lseek failed in Safe_Filesystem::seek()");
}

void Safe_Filesystem::trim(void* fd_ptr)
{
    int fd = (int)(smm) fd_ptr;

    u64 failures = 0;
    u64 offset;
    while ((offset = lseek(fd, 0, SEEK_CUR)) == (off_t) -1)
        if (++failures == 100)
            ReportLastErrno(subsystem_files, "lseek failed in Safe_Filesystem::trim()");

    failures = 0;
    while (ftruncate(fd, offset))
        if (++failures == 100)
            ReportLastErrno(subsystem_files, "ftruncate failed in Safe_Filesystem::trim()");
}

void Safe_Filesystem::erase(String path)
{
    u64 failures = 0;
    while (!delete_file(path))
        if (++failures == 100)
            ReportLastErrno(subsystem_files, "delete_file failed in Safe_Filesystem::erase() for %", path);
}



////////////////////////////////////////////////////////////////////////////////
// Time
////////////////////////////////////////////////////////////////////////////////


void rough_sleep(double seconds)
{
    if (seconds <= 0)
        return;

    timespec time;
    set_timespec_from_seconds(&time, seconds);
    while (true)
    {
        timespec remaining;
        if (!nanosleep(&time, &remaining) || errno != EINVAL)
            break;
        time = remaining;
    }
}

File_Time current_filetime()
{
    timespec time;
    int error = clock_gettime(CLOCK_REALTIME, &time);
    assert(!error);
    return filetime_from_timespec(&time);
}

s64 get_utc_offset()
{
    time_t t = time(NULL);
    struct tm local = {0};
    localtime_r(&t, &local);
    return local.tm_gmtoff / 60;
}

QPC current_qpc()
{
    timespec time;
    int error = clock_gettime(CLOCK_MONOTONIC_RAW, &time);
    assert(!error);
    return (u64) time.tv_sec * 1000000000ull + (u64) time.tv_nsec;
}

double seconds_from_qpc(QPC qpc)
{
    return (double) qpc / (double) 1000000000.0;
}

QPC qpc_from_seconds(double seconds)
{
    return (QPC)(seconds * 1000000000.0);
}


////////////////////////////////////////////////////////////////////////////////
// Threading utilities
////////////////////////////////////////////////////////////////////////////////


static String subsystem_thread = "thread"_s;



void set_pipe(Pipe* pipe, int fd, String name)
{
    ZeroStruct(pipe);
    pipe->fd = fd;
    if (name) pipe->name = allocate_string_on_heap(name);
}

void make_local_pipe(Pipe* in, Pipe* out, u32 buffer_size)
{
    (void) buffer_size;  // ignored
    int fd[2];
    if (pipe(fd))
        CheckFatalLastErrno(subsystem_thread, "pipe() failed in make_local_pipe()");
    set_pipe(in,  fd[0], {});
    set_pipe(out, fd[1], {});
}

String make_pipe(u32 buffer_size, String suffix)
{
    (void) buffer_size;  // ignored
    static Atomic32 global_serial_number;
    String name = Format(temp, "/tmp/PipeOwnedByGlobalMMK.%.%.%",
                         hex_format(getpid(), 8),
                         hex_format(increment_and_return_previous(&global_serial_number), 8),
                         suffix);
    if (mkfifo((char*) name.data, 0777))
        CheckFatalLastErrno(subsystem_thread, "mkfifo() failed in make_pipe(), name %", name);
    return name;
}

bool connect_pipe(Pipe* pipe, String name, int flags)
{
    ZeroStruct(pipe);
    int fd = open(make_c_style_string(name), O_CLOEXEC | flags);
    if (fd == -1)
    {
        ReportLastErrno(subsystem_thread, "open() failed in connect_pipe(), name %", name);
        return false;
    }
    pipe->fd   = fd;
    pipe->name = allocate_string_on_heap(name);
    return true;
}

bool connect_pipe_in(Pipe* pipe, String name)
{
    return connect_pipe(pipe, name, O_RDONLY);
}

bool connect_pipe_out(Pipe* pipe, String name)
{
    return connect_pipe(pipe, name, O_WRONLY);
}

void free_pipe(Pipe* pipe)
{
    close_file_descriptor(pipe->fd);
    free_heap_string(&pipe->name);
    ZeroStruct(pipe);
}

bool seek(Pipe* pipe, u32 size)
{
    u8 buffer[128];
    while (size)
    {
        u32 amount = sizeof(buffer);
        if (amount > size)
            amount = size;
        if (!read(pipe, buffer, amount))
            return false;
        size -= amount;
    }
    return true;
}

bool read(Pipe* pipe, void* data, u32 size)
{
    return read_fd(pipe->fd, { size, (u8*) data }, "pipe"_s);
}

bool try_read(Pipe* pipe, void* data, u32 size, bool* out_error)
{
    if (out_error) *out_error = false;

    struct pollfd pfd = {};
    pfd.fd = pipe->fd;
    pfd.events = POLLIN;

    int count = poll(&pfd, 1, 0);
    if (count == 0) return false;
    if (count != 1)
    {
        ReportLastErrno(subsystem_thread, "poll() failed in try_read()");
        if (out_error) *out_error = true;
        return false;
    }
    bool ok = read(pipe, data, size);
    if (!ok && out_error) *out_error = true;
    return ok;
}

bool write(Pipe* pipe, void* data, u32 size)
{
    return write_fd(pipe->fd, { size, (u8*) data }, "pipe"_s);
}



CompileTimeAssert(sizeof (Lock) == sizeof (pthread_mutex_t));
CompileTimeAssert(alignof(Lock) >= alignof(pthread_mutex_t));

void make_lock(Lock* lock)
{
    int error = pthread_mutex_init((pthread_mutex_t*) lock, NULL);
    CheckFatalErrno(subsystem_thread, error, "pthread_mutex_init() failed in make_lock()");
}

void free_lock(Lock* lock)
{
    int error = pthread_mutex_destroy((pthread_mutex_t*) lock);
    CheckFatalErrno(subsystem_thread, error, "pthread_mutex_destroy() failed in free_lock()");
}

void acquire(Lock* lock)
{
    int error = pthread_mutex_lock((pthread_mutex_t*) lock);
    CheckFatalErrno(subsystem_thread, error, "pthread_mutex_lock() failed in acquire()");
}

void release(Lock* lock)
{
    int error = pthread_mutex_unlock((pthread_mutex_t*) lock);
    CheckFatalErrno(subsystem_thread, error, "pthread_mutex_unlock() failed in release()");
}



CompileTimeAssert(sizeof (Semaphore) == sizeof (sem_t));
CompileTimeAssert(alignof(Semaphore) >= alignof(sem_t));

void make_semaphore(Semaphore* sem, u32 initial)
{
    assert(initial <= SEM_VALUE_MAX);
    if (sem_init((sem_t*) sem, 0, initial))
        CheckFatalLastErrno(subsystem_thread, "sem_init() failed!");
}

void free_semaphore(Semaphore* sem)
{
    sem_destroy((sem_t*) sem);
}

void post(Semaphore* sem)
{
    if (sem_post((sem_t*) sem))
        CheckFatalLastErrno(subsystem_thread, "sem_post() failed!");
}

void wait(Semaphore* sem)
{
    if (sem_wait((sem_t*) sem))
        CheckFatalLastErrno(subsystem_thread, "sem_wait() failed!");
}

bool wait(Semaphore* sem, double timeout_seconds)
{
    timespec time;
    set_timespec_absolute_timeout(&time, CLOCK_REALTIME, timeout_seconds);
    sem_timedwait((sem_t*) sem, &time);
    if (errno == ETIMEDOUT) return false;
    CheckFatalLastErrno(subsystem_thread, "sem_timedwait() failed!");
    return true;
}



CompileTimeAssert(sizeof (Condition_Variable) == sizeof (pthread_cond_t));
CompileTimeAssert(alignof(Condition_Variable) >= alignof(pthread_cond_t));

void make_condition_variable(Condition_Variable* variable)
{
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);

    int error = pthread_cond_init((pthread_cond_t*) variable, &attr);
    CheckFatalErrno(subsystem_thread, error, "pthread_cond_init() failed in make_condition_variable()");
}

void free_condition_variable(Condition_Variable* variable)
{
    int error = pthread_cond_destroy((pthread_cond_t*) variable);
    CheckFatalErrno(subsystem_thread, error, "pthread_cond_destroy() failed in free_condition_variable()");
}

void signal(Condition_Variable* variable)
{
    int error = pthread_cond_signal((pthread_cond_t*) variable);
    CheckFatalErrno(subsystem_thread, error, "pthread_cond_signal() failed in signal()");
}

void signal_all(Condition_Variable* variable)
{
    int error = pthread_cond_broadcast((pthread_cond_t*) variable);
    CheckFatalErrno(subsystem_thread, error, "pthread_cond_broadcast() failed in signal_all()");
}

void wait(Condition_Variable* variable, Lock* lock)
{
    int error = pthread_cond_wait((pthread_cond_t*) variable, (pthread_mutex_t*) lock);
    CheckFatalErrno(subsystem_thread, error, "pthread_cond_wait() failed in wait()");
}

static bool wait(Condition_Variable* variable, Lock* lock, timespec* abs_timeout)
{
    int error = pthread_cond_timedwait((pthread_cond_t*) variable, (pthread_mutex_t*) lock, abs_timeout);
    if (error == ETIMEDOUT) return false;
    CheckFatalErrno(subsystem_thread, error, "pthread_cond_timedwait() failed in wait()");
    return true;
}

bool wait(Condition_Variable* variable, Lock* lock, double timeout_seconds)
{
    timespec time;
    set_timespec_absolute_timeout(&time, CLOCK_MONOTONIC, timeout_seconds);
    return wait(variable, lock, &time);
}



void make_event(Event* event)
{
    make_lock(&event->lock);
    make_condition_variable(&event->cv);
    event->signal = false;
}

void free_event(Event* event)
{
    free_lock(&event->lock);
    free_condition_variable(&event->cv);
}

void signal(Event* event)
{
    LockedScope(&event->lock);
    event->signal = true;
    signal(&event->cv);
}

void wait(Event* event)
{
    LockedScope(&event->lock);
    while (!event->signal)
        wait(&event->cv, &event->lock);
    event->signal = false;
}

bool wait(Event* event, double timeout_seconds)
{
    timespec time;
    set_timespec_absolute_timeout(&time, CLOCK_MONOTONIC, timeout_seconds);
    LockedScope(&event->lock);
    while (!event->signal)
        if (!wait(&event->cv, &event->lock, &time))
            return false;
    event->signal = false;
    return true;
}




thread_local String current_thread_name;

CompileTimeAssert(MemberSize(Thread, handle) == sizeof(pthread_t));

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
    info->name = allocate_string_on_heap(name);  // null-terminated, relevant below
    info->userdata = userdata;
    info->entry = entry;

    pthread_t thread;
    int error = pthread_create(&thread, NULL, [](void* info_ptr) -> void*
    {
        Info info = *(Info*) info_ptr;
        leakcheck_ignore_next_allocation();
        pthread_t i = pthread_self();
        current_thread_name = Format(NULL, "% (ID %)", info.name, i);
        pthread_setname_np(pthread_self(), (char*) info.name.data);  // null-terminated, it's ok
        free_heap_string(&info.name);
        free(info_ptr);

        info.entry(info.userdata);

        free_heap_string(&current_thread_name);
        lk_region_free(temp);
        return NULL;
    }, info);
    CheckFatalErrno(subsystem_thread, error, "FAILED TO CREATE A THREAD! pthread_create failed.");

    if (out_thread)
    {
        out_thread->handle = (void*) thread;
        out_thread->valid_handle = true;
    }
    else
    {
        error = pthread_detach(thread);
        CheckFatalErrno(subsystem_thread, error, "FAILED TO CREATE A THREAD! pthread_detach failed.");
    }
}

void wait(Thread* thread)
{
    if (!thread || !thread->valid_handle) return;
    int error = pthread_join((pthread_t) thread->handle, NULL);
    CheckFatalErrno(subsystem_thread, error, "FAILED TO WAIT FOR A THREAD! pthread_join failed.");
    thread->handle = NULL;
    thread->valid_handle = false;
}


umm get_hardware_parallelism()
{
    return (umm) sysconf(_SC_NPROCESSORS_ONLN);
}



////////////////////////////////////////////////////////////////////////////////
// Process utilities
////////////////////////////////////////////////////////////////////////////////


static String subsystem_process = "process"_s;

void get_cpu_and_memory_usage(double* out_cpu_usage, u64* out_physical_use, u64* out_physical_max)
{
    static QPC last_query_time;
    static double last_cpu_use;
    static u64 last_physical_use;
    static u64 last_physical_max;

    QPC now = current_qpc();
    if (now - last_query_time > qpc_from_seconds(1))
    {
        double load;
        if (getloadavg(&load, 1) == 1)
            last_cpu_use = load;

        struct sysinfo info;
        if (sysinfo(&info) == 0)
        {
            if (info.freeram > info.totalram) info.freeram = info.totalram;
            last_physical_use = info.totalram - info.freeram;
            last_physical_max = info.totalram;
        }
    }

    *out_cpu_usage    = last_cpu_use;
    *out_physical_use = last_physical_use;
    *out_physical_max = last_physical_max;
}

void get_network_performance(double* out_read_bps, double* out_write_bps)
{
    *out_read_bps  = 0;
    *out_write_bps = 0;
    // @Incomplete Unimplemented
}

void get_system_information(System_Information* info)
{
    ZeroStruct(info);
    // @Incomplete Unimplemented
}


u32 current_process_id()
{
    CompileTimeAssert(sizeof(pid_t) <= sizeof(u32));
    return getpid();
}

void terminate_current_process(u32 exit_code)
{
    (void) exit_code;  // ignored
    raise(SIGKILL);
    Unreachable;
}

String get_executable_path()
{
    char path[1024] = { 0 };
    if (readlink("/proc/self/exe", path, sizeof(path) - 1) == -1)
    {
        ReportLastErrno(subsystem_process, "While trying to get the executable path.");
        return {};
    }
    return make_string(path);
}

String get_current_working_directory()
{
    umm buffer_size = 1024;
    while (buffer_size < Kilobyte(10))
    {
        LK_Region_Cursor cursor = {};
        lk_region_cursor(temp, &cursor);

        char* buffer = alloc<char>(temp, buffer_size);
        if (getcwd(buffer, buffer_size))
            return wrap_string(buffer);

        if (errno != ERANGE) break; // report error immediately

        lk_region_rewind(temp, &cursor);
        buffer_size += 1024;
    }

    ReportLastErrno(subsystem_files, "While trying to get the current working directory.");
    return {};
}

Array<String> command_line_arguments()
{
    static Array<String> cached = {};
    OnlyOnce
    {
        String path = Format(temp, "/proc/%/cmdline", getpid());
        String cmdline = {};
        if (read_file(&cmdline,         // output string
                      path, 0,          // path and offset
                      UMM_MAX, UMM_MAX, // min and max size
                      NULL, NULL))      // buffer and allocator
        {
            umm argc = 0;
            for (umm i = 0; i < cmdline.length; i++)
                if (cmdline.data[i] == 0)
                    argc++;
            cached = allocate_array<String>(NULL, argc);
            for (umm i = 0; i < argc; i++)
                cached[i] = consume_until(&cmdline, 0);
        }
    }
    return cached;
}


CompileTimeAssert(MemberSize(Process, pid) == sizeof(pid_t));

bool run_process(Process* process)
{
    int fd_in = -1;
    if (process->file_stdin)
    {
        fd_in = open(make_c_style_string(process->file_stdin), O_RDONLY);
        if (fd_in == -1)
            return ReportLastErrno(subsystem_process, "open() failed in run_process(), path %", process->file_stdin);
    }

    int fd_out = -1;
    if (process->file_stdout)
    {
        fd_out = open(make_c_style_string(process->file_stdout), O_CREAT | O_TRUNC | O_WRONLY, 0777);
        if (fd_out == -1)
            return ReportLastErrno(subsystem_process, "open() failed in run_process(), path %", process->file_stdout);
    }

    int fd_err = -1;
    if (process->file_stderr)
    {
        fd_err = open(make_c_style_string(process->file_stderr), O_CREAT | O_TRUNC | O_WRONLY, 0777);
        if (fd_err == -1)
            return ReportLastErrno(subsystem_process, "open() failed in run_process(), path %", process->file_stderr);
    }

    char** argv = alloc<char*>(temp, process->arguments.count + 2);
    argv[0] = make_c_style_string(process->path);
    for (umm i = 0; i < process->arguments.count; i++)
        argv[i + 1] = make_c_style_string(process->arguments[i]);
    argv[process->arguments.count + 1] = NULL;

    char** envp = alloc<char*>(temp, 1);
    envp[0] = NULL;

    char* c_chdir = make_c_style_string(process->current_directory);

    bool detached = process->detached;


    pid_t pid = vfork();
    if (pid == -1)
        return ReportLastErrno(subsystem_process, "vfork() failed in run_process()");
    if (pid == 0)
    {
        // child process

        if (!detached)
            prctl(PR_SET_PDEATHSIG, SIGKILL);

        if (c_chdir)
        {
            int ignored = chdir(c_chdir);
            (void) ignored;
        }

        auto set_std_fd = [](int file_fd, FILE* std)
        {
            if (file_fd == -1) return;
            dup2(file_fd, fileno(std));
            ::close(file_fd);
        };
        set_std_fd(fd_in,  stdin);
        set_std_fd(fd_out, stdout);
        set_std_fd(fd_err, stderr);

        execve(argv[0], argv, envp);
        raise(SIGKILL);
        Unreachable;
    }

    if (fd_in  != -1) close_file_descriptor(fd_in);
    if (fd_out != -1) close_file_descriptor(fd_out);
    if (fd_err != -1) close_file_descriptor(fd_err);

    process->pid = pid;
    return true;
}

bool get_process_id(Process* process, u32* out_process_id)
{
    *out_process_id = process->pid;
    return true;
}

void terminate_process_without_waiting(Process* process)
{
    kill(process->pid, SIGKILL);
}


#if !defined(SYS_pidfd_open) && defined(__x86_64__) // just to be sure.
#define SYS_pidfd_open 434
#endif

bool wait_for_process(struct Process* process, double seconds, u32* out_exit_code)
{
    if (process->pid == 0)
    {
        *out_exit_code = -1;
        return false;
    }

    pid_t pid = process->pid;

    if (seconds == WAIT_FOR_PROCESS_FOREVER)
    {
        int status;
        if (waitpid(pid, &status, 0) > 0)
        {
            if (WIFEXITED(status))
                *out_exit_code = WEXITSTATUS(status);
            else
                *out_exit_code = -1;

            process->pid = 0;
            return true;
        }
    }
    else
    {
        int status;
        QPC qpc_timeout = qpc_from_seconds(seconds);
        QPC start_time  = current_qpc();
        umm attempts = 0;
        do
        {
            if (waitpid(pid, &status, WNOHANG) > 0)
            {
                if (WIFEXITED(status))
                    *out_exit_code = WEXITSTATUS(status);
                else
                    *out_exit_code = -1;

                process->pid = 0;
                return true;
            }

            attempts++;
            useconds_t wait_us = 1ull << (attempts < 15 ? attempts : 15);
            usleep(wait_us);
        }
        while (current_qpc() - start_time <= qpc_timeout);
    }

    *out_exit_code = -1;
    return false;
}

bool wait_for_process_by_id(u32 id)
{
    int pidfd = syscall(SYS_pidfd_open, id, 0);
    if (pidfd == -1) return false;
    Defer(close_file_descriptor(pidfd));

wait_again:
    struct pollfd pfd = {};
    pfd.fd = pidfd;
    pfd.events = POLLIN;
    switch (poll(&pfd, 1, 1000 /* 1 second */))
    {
    case 0:  goto wait_again;
    case 1:  return true;
    default: return false;
    }
}

bool terminate_and_wait_for_process(u32 id)
{
    if (kill(id, SIGKILL)) return false;
    return wait_for_process_by_id(id);
}


String get_os_name()
{
    struct utsname info;
    if (uname(&info)) return "Linux (unknown)"_s;
    return Format(temp, "% % (% %)", info.sysname, info.release, info.nodename, info.machine);
}


void error_box(String message, String title, void* window)
{
    (void) window;

    // @Incomplete
    if (!title) title = "Error"_s;
    LogError(title, "%", message);
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

    QPC qpc = current_qpc();
    AddEntropy(qpc);
    AddEntropy((umm) &qpc);
    AddEntropy((umm) &entropy);
    AddEntropy((umm) &entropy_source_callback);
    AddEntropy((umm) &errno);

    u8 buffer[128];
    int urandom = ::open("/dev/urandom", O_CLOEXEC | O_RDONLY);
    if (urandom >= 0)
    {
        int unused;
        unused = ::read(urandom, buffer, sizeof(buffer));
        (void) unused;  // just to get rid of a warning

        ::close(urandom);
    }
    AddEntropy(buffer);

#if defined(ARCHITECTURE_X64)
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) && ((ecx >> 30) & 1))  // RDRAND supported
    {
        unsigned long long cpu_random_numbers[4];
        __builtin_ia32_rdrand64_step(&cpu_random_numbers[0]);
        __builtin_ia32_rdrand64_step(&cpu_random_numbers[1]);
        __builtin_ia32_rdrand64_step(&cpu_random_numbers[2]);
        __builtin_ia32_rdrand64_step(&cpu_random_numbers[3]);
        AddEntropyV(cpu_random_numbers);
    }
#endif

    AddEntropy(getpid());

    AddEntropy(current_qpc());

    #undef AddEntropy
    #undef AddEntropyV
}



ExitApplicationNamespace
