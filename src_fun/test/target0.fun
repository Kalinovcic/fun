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

    // more members exist in the C++ codebase, but are opaque here
}

UNIT_IS_STRUCT    := cast(u32, 0x0001);
UNIT_IS_PLACED    := cast(u32, 0x0002);
UNIT_IS_COMPLETED := cast(u32, 0x0004);

Unit :: struct
{
    flags:              u32;

    initiator_from:     Token;
    initiator_to:       Token;
    initiator_block:   &Block;

    entry_block:       &Block;

    pointer_size:       umm;
    pointer_alignment:  umm;

    storage_size:       u64;
    storage_alignment:  u64;

    // more members exist in the C++ codebase, but are opaque here
}

EVENT_CONTEXT_FINISHED        := cast(u32, 1);
EVENT_UNIT_PARSED             := cast(u32, 2);
EVENT_UNIT_REQUIRES_PLACEMENT := cast(u32, 3);
EVENT_UNIT_PLACED             := cast(u32, 4);

Event :: struct {
    kind: u32;
    unit_ref: &Unit;
}

Context :: struct {}  // opaque

make_context :: (out_ctx: &&Context)                                      {} intrinsic "compiler_make_context";
add_file     :: (ctx: &Context, path: string)                             {} intrinsic "compiler_add_file";
wait_event   :: (ctx: &Context, event: &Event)                            {} intrinsic "compiler_wait_event";
place_unit   :: (ctx: &Context, placed: &Unit, size: u64, alignment: u64) {} intrinsic "compiler_place_unit";

syscall :: (rax: umm, rdi: umm, rsi: umm, rdx: umm, r10: umm, r8: umm, r9: umm) -> (rax: umm) {} intrinsic "syscall";

consume :: (str: &string, n: umm) -> (lhs: string) {
    lhs.length = n;
    lhs.base   = str.base;
    str.length = str.length - n;
    str.base   = str.base  &+ n;
}

puts :: (what: string) -> (amount_written: umm, error: umm) {
    SYS_WRITE := cast(umm, 1);
    fd := cast(umm, 1);  // stdout
    while what.length > cast(umm, 0) {
        amount := syscall(SYS_WRITE, fd, cast(umm, what.base), what.length, zero, zero, zero).rax;
        if amount <= what.length
         => amount_written = amount_written + amount;
        else {
            error = -amount;
            amount = what.length;
        }
         
        consume(&what, amount);
    }
}

puts("ello m8\n");


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
        debug event.unit_ref.storage_size;
    }
    else
    {
        debug "unrecognized event";
    }
}

debug "finished compilation!";



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



/*poof := debug_alloc u32;
debug poof;
*poof = cast(u32, 123);
debug *poof;
debug &poof;
debug_free poof;*/

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

foo2 :: () => debug M;

repeat :: ($N: u32, $a: block)
{
    foo2();
    n := cast(u8, N);
    while n
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
