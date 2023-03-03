#!/home/kalinovcic/schedulebot/run_tree/fun
// =($a: s64, $b: s64)
// debug a + b;



Atom :: u32;

Token :: struct
{
    atom:       Atom;
    info_index: u32;
}

Expression_Kind :: u16;
Expression      :: u32;
Visibility      :: u32;

Parsed_Expression :: struct
{
    kind:               Expression_Kind;
    flags:              u16;
    visibility_limit:   Visibility;

    from:               Token;
    to:                 Token;
}

Block :: struct
{
    flags:              u32;
    from:               Token;
    to:                 Token;
}

Unit :: struct
{
    flags:              u32;

    initiator_from:     Token;
    initiator_to:       Token;
    initiator_block:   &Block;

    entry_block:       &Block;

    pointer_size:       umm;
    pointer_alignment:  umm;

    storage_alignment:  u64;
    storage_size:       u64;

    // more members exist in the C++ codebase, but are opaque here
}

debug sizeof Unit;


Context :: struct {}  // opaque

EVENT_CONTEXT_FINISHED:        u32 = cast(u32, 1);
EVENT_UNIT_PARSED:             u32 = cast(u32, 2);
EVENT_UNIT_REQUIRES_PLACEMENT: u32 = cast(u32, 3);
EVENT_UNIT_PLACED:             u32 = cast(u32, 4);

Event :: struct
{
    kind: u32;
    placed_unit: &Unit;
}

make_context :: (out_ctx: &&Context)           {} intrinsic "compiler_make_context";
add_file     :: (ctx: &Context, path: string)  {} intrinsic "compiler_add_file";
wait_event   :: (ctx: &Context, event: &Event) {} intrinsic "compiler_wait_event";


make_context(&ctx: &Context);
add_file(ctx, "target1.fun");

more_events := cast(bool, true);
while more_events
{
    wait_event(ctx, &event: Event);
    if event.kind == EVENT_CONTEXT_FINISHED
    {
        more_events = cast(bool, false);
    }
    else => if event.kind == EVENT_UNIT_PARSED
    {
        debug "parsed a unit";
    }
    else => if event.kind == EVENT_UNIT_REQUIRES_PLACEMENT
    {
        debug "could do custom unit placement right now, but won't";
    }
    else => if event.kind == EVENT_UNIT_PLACED
    {
        debug "unit placed, size of unit is:";
        debug event.placed_unit.storage_size;
    }
    else
    {
        debug "unrecognized event";
    }
}

debug "finished compilation!";


/*
print :: (value: $T) {} intrinsic "print";
heap_allocate :: (out_base: &&$T, size: umm) {} intrinsic "heap_allocate";
heap_free     :: (base: &$T) {} intrinsic "heap_free";

heap_allocate(&heap_pointer: &u32, cast(umm, sizeof u32));
print(heap_pointer);
print(*heap_pointer);
*heap_pointer = cast(u32, 123);
print(*heap_pointer);
heap_free(heap_pointer);


/*consume :: (res: &string, str: &string, n: umm)
{
    res.length = n;
    res.base   = str.base;
    str.length = str.length - n;
    str.base   = str.base  &+ n;
}

asdf: string = "asdf";
consume(&lhs: string, &asdf, cast(umm, 1));
debug lhs;
debug asdf;*/

foo :: unit
{
    x: string = _;
    y: string = _;
    print(x);
    print(y);
}

poo := "asdf";
print(poo);

ptr1 := &poo;
ptr2 := &ptr1;

**ptr2 = "no longer asdf";
print(poo);

print(cast(u64, 12345678910));
print(cast(u8, cast(u64, 12345678910)));


f: foo;
(*&f).x = "foo";
(*&f).y = "foofoo";
goto(codeof f, &f);
print("back");


blok :: ()
{
    print("eyo");
}

blok();

print("beyond the blok");
*/


/*junit :: unit
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
}*/


/*
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
    /*if a > b
     => debug a;
    else
     => debug b;*/
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


print :: ($b: u32, $c: bool)
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
*/

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
