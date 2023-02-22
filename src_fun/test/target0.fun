#!/home/kalinovcic/schedulebot/run_tree/fun

run
{
    debug 123;
    debug 12;
    debug 1;

    bla: u64;
    bla = cast(u64, 69);
    debug bla;

    if 5
    {
        debug 5;
    }
    else
    {
        debug 15;
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

