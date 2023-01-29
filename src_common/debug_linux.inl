#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <execinfo.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>

EnterApplicationNamespace


bool output_full_dumps;

void(*after_crash_dump)();


static void print_trace()
{
    char pid_buf[32];
    char name_buf[512];
    sprintf(pid_buf, "%d", getpid());
    name_buf[readlink("/proc/self/exe", name_buf, 511)] = 0;
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
    int child_pid = fork();
    if (!child_pid)
    {
        dup2(1, 2);
        execl("/usr/bin/gdb", "gdb", "--batch", "-n", "-ex", "thread", "-ex", "bt", name_buf, pid_buf, NULL);
        abort();
    }
    waitpid(child_pid,NULL,0);
}


static thread_local bool already_crashing;

void assertion_failure(const char* test, const char* file, int line)
{
    if (already_crashing)
    {
        raise(SIGKILL);  // crashed again during crashing
        return;
    }
    already_crashing = true;

    printf("Assertion %s failed!\n%s:%d\n", test, file, line);
    LogError("debug system"_s, "Assertion % failed!\n%:%\n", test, file, line);
    // print_trace();

    if (after_crash_dump)
        after_crash_dump();

    int* ptr = 0;
    *ptr = 123;
    raise(SIGKILL);
}

static void handler(int sig)
{
    printf("crash handler!\n");
    print_trace();
    raise(SIGKILL);
}

void install_unhandled_exception_handler()
{
    // ::signal(SIGSEGV, handler);
}


ExitApplicationNamespace
