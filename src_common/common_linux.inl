#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sched.h>
#include <pthread.h>
#include <string.h>
#include <fcntl.h>

EnterApplicationNamespace


#define CheckFatalErrno(subsystem, error_code, string, ...)                 \
    if ((error_code))                                                       \
    {                                                                       \
        ReportErrno((subsystem), (error_code), (string), ##__VA_ARGS__);    \
        assert(false && "fatal error");                                     \
    }

#define CheckFatalLastErrno(subsystem, string, ...)                         \
    if (errno)                                                              \
    {                                                                       \
        ReportLastErrno((subsystem), (string), ##__VA_ARGS__);              \
        assert(false && "fatal error");                                     \
    }

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
// Logging.
////////////////////////////////////////////////////////////////////////////////


void report_errno(String subsystem, int error_code, String while_doing_what)
{
    char message_buffer[512];
    message_buffer[sizeof(message_buffer) - 1] = 0;
    int result = strerror_r(errno, message_buffer, sizeof(message_buffer) - 1);
    if (result == 0)
        LogError(subsystem, "%\n .. Error code 0x%\n .. %", message_buffer, hex_format(error_code, 8), while_doing_what);
    else
        LogError(subsystem, "Error code 0x%\n .. %", hex_format(error_code, 8), while_doing_what);
}

void report_last_errno(String subsystem, String while_doing_what)
{
    report_errno(subsystem, errno, while_doing_what);
}


////////////////////////////////////////////////////////////////////////////////
// File system
////////////////////////////////////////////////////////////////////////////////


int close_file_descriptor(int fd)
{
    int result;
    while ((result = ::close(fd)) == EINTR)
        continue;
    return result ? errno : 0;
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
    u64 unix_100ns = (u64) time.tv_sec * 10000000ull + (u64)(time.tv_nsec + 50) / 100;
    return unix_100ns + 116444736000000000ull;
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

bool wait(Condition_Variable* variable, Lock* lock, double timeout_seconds)
{
    timespec delta_time;
    set_timespec_from_seconds(&delta_time, timeout_seconds);

    timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    time.tv_nsec += delta_time.tv_nsec;
    time.tv_sec  += delta_time.tv_sec + (time.tv_nsec / 1000000000);
    time.tv_nsec %= 1000000000;

    int error = pthread_cond_timedwait((pthread_cond_t*) variable, (pthread_mutex_t*) lock, &time);
    if (error == ETIMEDOUT)
        return false;

    CheckFatalErrno(subsystem_thread, error, "pthread_cond_timedwait() failed in wait()");
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
    int urandom = ::open("/dev/urandom", O_RDONLY);
    if (urandom >= 0)
    {
        ::read(urandom, buffer, sizeof(buffer));
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
