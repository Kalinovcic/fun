
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

UNIT_IS_STRUCT    :: 0x0001;
UNIT_IS_RUN       :: 0x0002;
UNIT_IS_PLACED    :: 0x0004;
UNIT_IS_COMPLETED :: 0x0008;

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

EVENT_CONTEXT_FINISHED        :: 1;
EVENT_UNIT_PARSED             :: 2;
EVENT_UNIT_REQUIRES_PLACEMENT :: 3;
EVENT_UNIT_PLACED             :: 4;

Event :: struct {
    kind: u32;
    unit_ref: &Unit;
}

Context :: struct {}  // opaque

make_context :: (out_ctx: &&Context)                                      {} intrinsic "compiler_make_context";
add_file     :: (ctx: &Context, path: string)                             {} intrinsic "compiler_add_file";
wait_event   :: (ctx: &Context, event: &Event)                            {} intrinsic "compiler_wait_event";
place_unit   :: (ctx: &Context, placed: &Unit, size: u64, alignment: u64) {} intrinsic "compiler_place_unit";
