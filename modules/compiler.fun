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

    storage_size:       u64;
    storage_alignment:  u64;

    // more members exist in the C++ codebase, but are opaque here
}



Environment :: struct {}  // opaque

Environment_Settings :: struct {
    silence_errors:     bool;
    custom_backend:     bool;
    pointer_size:       u64;
    pointer_alignment:  u64;
}

make_environment :: (out_env: &&Environment, settings: Environment_Settings) {} intrinsic "compiler_make_environment";
add_file         :: (env: &Environment, path: string)                        {} intrinsic "compiler_add_file";


// Non-actionable events
EVENT_FINISHED                :: 1;
EVENT_UNIT_WAS_PLACED         :: 2;
EVENT_UNIT_WAS_PATCHED        :: 3;
EVENT_UNIT_WAS_RUN            :: 4;
EVENT_ERROR                   :: 5;
// Actionable events (you will keep getting them forever until you act)
EVENT_ACTIONABLE_BASE         :: 1000;
EVENT_UNIT_REQUIRES_PLACEMENT :: EVENT_ACTIONABLE_BASE + EVENT_UNIT_WAS_PLACED;
EVENT_UNIT_REQUIRES_PATCHING  :: EVENT_ACTIONABLE_BASE + EVENT_UNIT_WAS_PATCHED;
EVENT_UNIT_REQUIRES_RUNNING   :: EVENT_ACTIONABLE_BASE + EVENT_UNIT_WAS_RUN;

Event :: struct {
    kind:       u32;
    actionable: bool;
    unit_ref:  &Unit;
    error:      string;
}

wait_event :: (env: &Environment, event: &Event) {
    compiler_yield :: (env: &Environment)                {} intrinsic "compiler_yield";
    get_event      :: (env: &Environment, event: &Event) {} intrinsic "compiler_get_event";

    compiler_yield(env);    // give control back to the compiler so it can think about our actions
    get_event(env, event);  // get what's asked of us
}


confirm_place_unit :: (env: &Environment, placed:  &Unit, size: u64, alignment: u64) {} intrinsic "compiler_confirm_place_unit";
confirm_patch_unit :: (env: &Environment, patched: &Unit)                            {} intrinsic "compiler_confirm_patch_unit";
confirm_run___unit :: (env: &Environment, ran:     &Unit)                            {} intrinsic "compiler_confirm_run_unit";

