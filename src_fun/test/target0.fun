#!/home/kalinovcic/schedulebot/run_tree/fun

run
{
/*
    wrap: ($T: type)
    {
        inner: ($a: T)
        {
            debug 123;
        }

        inner(zero);
    }

    wrap(u32);  // works fine

    wrap(block);  // this will make wrap() not compile, because the call to inner() is wrong
*/



    /*N :: 10;
    M :: 420e100;

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

    a := cast(u64, POO :: 123); debug a;
    b := cast(u64, POO * 2);    debug b;
    c := cast(u64, POO * 3);    debug c;*/
}

