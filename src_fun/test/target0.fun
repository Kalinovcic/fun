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

    print :: (b: block)
     => debug 123;

    if (cast(u64, 1))
     => print() => debug 420;
    else
     => debug 69;


/*
    N :: 10;
    M :: 420e100;

    foo :: () => debug M;

    repeat :: (block)
    {
        foo();
        n := cast(u64, N);
        while (n)
        {
            block;
            n = n - cast(u64, 1);
        }
        foo();
    }

    repeat()
     => debug 69;

    a := cast(u64, POO :: 123); debug a;
    b := cast(u64, POO * 2);    debug b;
    c := cast(u64, POO * 3);    debug c;
*/
}

