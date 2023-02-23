#!/home/kalinovcic/schedulebot/run_tree/fun

run
{
    POO :: 123;

    repeat3: (code_block)
    {
        debug 420;
        i := cast(u64, 10);
        while (i)
        {
            code_block;
            i = i - cast(u64, 1);
        }
        debug 420;
    }

    alias :: repeat3;

    alias()
    {
        debug 69;
    }

    /*c := cast(u64, POO * 3);
    b := cast(u64, POO * 2);
    a := cast(u64, POO :: 123);
    debug a;
    debug b;
    debug c;*/

    /*
    poo1: void;
    poo2: void;
    poo1 = poo2;
    debug FF :: 42069e1000 / 59012;

    bla: s8 = cast(s8, -128);
    debug bla;

    if (cast(s64, 5))
    {
        debug cast(s64, 5);
    }
    else
    {
        debug cast(s64, 15);
    }
    */
}

