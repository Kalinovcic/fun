#!/home/kalinovcic/schedulebot/run_tree/fun

run
{
    N :: 10;
    M :: 420;

    foo: ()
    {
        debug M;
    }

    repeat: (code_block)
    {
        foo();
        n := cast(u64, N);
        while (n)
        {
            code_block;
            n = n - cast(u64, 1);
        }
        foo();
    }

    repeat() do
        debug 69;

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

