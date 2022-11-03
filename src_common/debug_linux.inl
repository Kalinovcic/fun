#include <unistd.h>

EnterApplicationNamespace


void(*after_crash_dump)();


static thread_local bool already_crashing;

void assertion_failure(const char* test, const char* file, int line)
{
    if (already_crashing)
    {
        kill(getpid(), SIGKILL);  // crashed again during crashing
        return;
    }
    already_crashing = true;

    printf("Assertion %s failed!\n%s:%d\n", test, file, line);
    LogError("debug system"_s, "Assertion % failed!\n%:%\n", test, file, line);
    if (after_crash_dump)
        after_crash_dump();
    kill(getpid(), SIGKILL);
}


ExitApplicationNamespace
