#!/home/kalinovcic/schedulebot/run_tree/fun
// =($a: s64, $b: s64)
// debug a + b;


/*Vec :: ()
{
    a: f64;
    b: f64;
}

unit(Vec)*/



U :: unit
{
    debug 42069;
}
U();

// goto(&U, debug_alloc sizeof U);


/*
poo := debug_alloc u32;
debug poo;
*poo = cast(u32, 123);
debug *poo;
debug &poo;
debug_free poo;

if cast(u64, 1)
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



import "proc.fun"();

test :: ($T: type, v: u64)
{
    x: T = cast(T, v);
    debug x;
}

test(u32, cast(u64, 123));


print :: ($b: u32, $c: bool8)
 => debug b;

if cast(u64, 1)
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
    while n
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

*ptr = *ptr + cast(u32, 1);
debug var;
*/


/*
proc1  :: (x: u32) { debug x; }
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
proc16(cast(u32, 0));
*/

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
