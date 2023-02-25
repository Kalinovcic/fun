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


    test :: ($T: type, v: u64)
    {
        x: T = cast(T, v);
        debug x;
    }

    test(u32, cast(u64, 123));


    print :: ($b: u32, $c: bool8)
     => debug b;

    if (cast(u64, 1))
     => print(123, true);
    else
     => debug 420;


    N :: 10;
    M :: 420e100;

    foo :: () => debug M;

    repeat :: ($N: u32, $a: block)
    {
        foo();
        n := cast(u8, N);
        while (n)
        {
            a();
            n = n - cast(u8, 1);
        }
        foo();
    }

    repeat(N)
     => debug 69;

    a := cast(s64, POO :: 123); debug -a;
    b := cast(s64, POO * 2);    debug -b;
    c := cast(s64, POO * 3);    debug -c;


    var := cast(u32, 1337);
    ptr: &u32 = &var;
    debug ptr;

    T :: &u32;
    var2: *T = *ptr;
    debug var2;

    // *ptr = *ptr + 1;
    // debug var;
}

