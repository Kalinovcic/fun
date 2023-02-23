#!/home/kalinovcic/schedulebot/run_tree/fun

run
{
    bla: s8;
    bla = cast(s8, -128);
    debug bla;

    if cast(s64, 5)
    {
        debug cast(s64, 5);
    }
    else
    {
        debug cast(s64, 15);
    }

    /*a: f32 = 123.45e+6;
    if a > 1e100 [[unlikely]]
    {
    }

    yield void;

    exit: u32 = 60;
    code: u32 = 69;
    syscall(exit, code);*/
}

