#!/home/kalinovcic/schedulebot/run_tree/fun

set_to_69 :: (what: &u64)
{
    debug what;
    *what = cast(u64, 69);
}

set_to_69(&thing: u64);
debug thing;