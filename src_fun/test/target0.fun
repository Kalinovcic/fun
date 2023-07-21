#!/home/kalinovcic/fun/run_tree/fun
// =($a: s64, $b: s64)
// debug a + b;

run unit {
    using System   :: import "system.fun";
    using Compiler :: import "compiler.fun";

    // debug cast(f64, 4e308);
    // debug cast(f64, 1e-1000);
    // debug cast(f32, 1 %/ 3);
    debug cast(f32, 0.25);

    x: u64 = 1;
    while true {
        if x > 100 {
            debug x;
            yield;
        }
        x = x + 1;
    }

/*
    puts(FD_STDOUT, "ello m8\n");


    Cool_String :: struct {
        using str: string;
        cool_factor: u64;
    }

    cs: Cool_String;
    cs.str = "asdf";
    cs.cool_factor = cast(u64, 123);
    debug cs.base;
    debug cs.length;
    debug cs.cool_factor;




    /*asdf: string = "asdf";
    consume(&lhs: string, &asdf, cast(umm, 1));
    debug lhs;
    debug asdf;*/

    foo :: unit
    {
        x: string = _;
        y: string = _;
        debug x;
        debug y;
    }

    poo := "asdf";
    debug poo;

    ptr1 := &poo;
    ptr2 := &ptr1;

    **ptr2 = "no longer asdf";
    debug poo;

    debug cast(u64, 12345678910);
    debug cast(u8, cast(u64, 12345678910));


    f: foo;
    (*&f).x = "foo";
    (*&f).y = "foofoo";
    goto(codeof f, &f);
    debug "back";


    blok :: ()
    {
        debug "eyo";
    }

    blok();

    debug "beyond the blok";



    junit :: unit
    {
        debug "ello";
    }

    j: junit;
    goto(codeof j, &j);

    asdf: string = "asdf";
    debug asdf;

    asdf.length = cast(umm, 2);
    debug asdf;

    $if false
    {
        this;
        doesnt;
        compile;
    }
    else
    {
        debug "yay";
    }



    U :: unit
    {
        x: u64 = cast(u64, 420);
        y: u64 = cast(u64, 69);
        z := x * cast(u64, 100) + y;
        debug z;
    }

    storage: U;
    goto(codeof U, &storage);

    debug storage.x;
    debug storage.y;


    runtime_type := cast(type, u32);
    debug runtime_type;



    adder_coroutine :: unit
    {
        a: u64 = _;
        b: u64 = _;
        result := a + b;
    }

    co: adder_coroutine;
    co.a = cast(u64, 123);
    co.b = cast(u64, 123);
    goto(codeof co, &co);
    debug co.result;


    max :: (a: $T, b: T)
    {
        if a > b
         => debug a;
        else
         => debug b;
    }

    max(cast(u32, 123), cast(u32, 321));



    Point :: struct
    {
        SIZE :: sizeof(Point);
        x: u64;
        y: u64;
    }

    p1: Point;
    p1.x = cast(u64, 12);
    p1.y = cast(u64, 34);

    p2 := p1;
    p1.x = cast(u64, 56);
    p1.y = cast(u64, 78);

    debug sizeof Point;
    debug alignof Point;
    debug p1.x;
    debug p1.y;
    debug p2.x;
    debug p2.y;
    debug p1.SIZE;



    /*poof := debug_alloc u32;
    debug poof;
    *poof = cast(u32, 123);
    debug *poof;
    debug &poof;
    debug_free poof;*/

    if cast(u64, 1) != 0
    {
        x: u32 = cast(u32, 123);
        y: &u32 = &x;
        z: (G :: &$T) = y;
        w: *$Z = z;
        debug T;
        debug G;
        debug Z;

        foo :: ($a: u8)
         => debug a;
        foo(123);
        debug 69;
    }

    set_to_69 :: (what: &u64)
    {
        *what = cast(u64, 69);
    }

    set_to_69(&thing: u64);
    debug thing;




    test :: ($T: type, v: u64)
    {
        x: T = cast(T, v);
        debug x;
    }

    test(u32, cast(u64, 123));


    print :: ($b: u32, $c: bool)
     => debug b;

    if cast(u64, 1) != 0
     => print(123, true);
    else
     => debug 420;



    N :: 10;
    M :: 420e100;

    foo2 :: () => debug M;

    repeat :: ($N: u32, $a: block)
    {
        foo2();
        n := cast(u8, N);
        while n != 0
        {
            a();
            n = n - cast(u8, 1);
        }
        foo2();
    }

    repeat(N)
     => debug "ello! ^_^";


    two_blocks :: ($a: block, $b: block)
    {
        a();
        b();
        a();
    }

    two_blocks(() => debug 123, () => debug 456);

    two_blocks(() => debug 123)
     => debug 456;


    a := cast(s64, POO :: 123); debug -a;
    b := cast(s64, POO * 2);    debug -b;
    c := cast(s64, POO * 3);    debug -c;


    var := cast(u32, 1337);
    ptr: &u32 = &var;
    debug ptr;

    T :: &u32;
    var2: *T = *ptr;
    debug var2;

    *ptr = *ptr + cast(u32, 1);
    debug var;


    debug true;
    debug !true;
    š := true;
    debug !š;
    debug !!š;


    debug "and test";
    debug (š     & true);
    debug (š     & false);
    debug (true  & š);
    debug (false & š);
    debug (false & !š);

    debug "or test";
    debug (š     | true);
    debug (š     | false);
    debug (true  | š);
    debug (false | š);
    debug (false | !š);


    /*proc1  :: (x: u32) { debug x; }
    proc2  :: (x: u32) { proc1 (x * cast(u32, 2) + cast(u32, 0)); proc1 (x * cast(u32, 2) + cast(u32, 1)); }
    proc3  :: (x: u32) { proc2 (x * cast(u32, 2) + cast(u32, 0)); proc2 (x * cast(u32, 2) + cast(u32, 1)); }
    proc4  :: (x: u32) { proc3 (x * cast(u32, 2) + cast(u32, 0)); proc3 (x * cast(u32, 2) + cast(u32, 1)); }
    proc5  :: (x: u32) { proc4 (x * cast(u32, 2) + cast(u32, 0)); proc4 (x * cast(u32, 2) + cast(u32, 1)); }
    proc6  :: (x: u32) { proc5 (x * cast(u32, 2) + cast(u32, 0)); proc5 (x * cast(u32, 2) + cast(u32, 1)); }
    proc7  :: (x: u32) { proc6 (x * cast(u32, 2) + cast(u32, 0)); proc6 (x * cast(u32, 2) + cast(u32, 1)); }
    proc8  :: (x: u32) { proc7 (x * cast(u32, 2) + cast(u32, 0)); proc7 (x * cast(u32, 2) + cast(u32, 1)); }
    proc9  :: (x: u32) { proc8 (x * cast(u32, 2) + cast(u32, 0)); proc8 (x * cast(u32, 2) + cast(u32, 1)); }
    proc10 :: (x: u32) { proc9 (x * cast(u32, 2) + cast(u32, 0)); proc9 (x * cast(u32, 2) + cast(u32, 1)); }
    proc11 :: (x: u32) { proc10(x * cast(u32, 2) + cast(u32, 0)); proc10(x * cast(u32, 2) + cast(u32, 1)); }
    proc12 :: (x: u32) { proc11(x * cast(u32, 2) + cast(u32, 0)); proc11(x * cast(u32, 2) + cast(u32, 1)); }
    proc13 :: (x: u32) { proc12(x * cast(u32, 2) + cast(u32, 0)); proc12(x * cast(u32, 2) + cast(u32, 1)); }
    proc14 :: (x: u32) { proc13(x * cast(u32, 2) + cast(u32, 0)); proc13(x * cast(u32, 2) + cast(u32, 1)); }
    proc15 :: (x: u32) { proc14(x * cast(u32, 2) + cast(u32, 0)); proc14(x * cast(u32, 2) + cast(u32, 1)); }
    proc16 :: (x: u32) { proc15(x * cast(u32, 2) + cast(u32, 0)); proc15(x * cast(u32, 2) + cast(u32, 1)); }
    proc16(cast(u32, 0));*/

    /*
    proc_a :: ()
    {
        proc_a();
    }

    proc_b :: ()
    {
        proc_a();
    }

    proc_b();
    */
*/
}
