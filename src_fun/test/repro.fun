#!/home/kalinovcic/schedulebot/run_tree/fun

foo :: (a: $T, b: T)
{
    debug a;
    debug b;
}

foo(cast(u32, 123), cast(u32, 321));
