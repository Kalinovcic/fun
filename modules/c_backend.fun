#!/home/kalinovcic/schedulebot/run_tree/fun

using Self     :: import "c_backend";
using System   :: import "system";
using Compiler :: import "compiler";

run unit {
    mem: Region;

    Big_Thing1 :: struct {
        a: u64; b: u64; c: u64; d: u64;
        e: u64; f: u64; g: u64; h: u64;
        i: u64; j: u64; k: u64; l: u64;
        m: u64; n: u64; o: u64; p: u64;
    }

    Big_Thing :: struct {
        a: Big_Thing1; b: Big_Thing1; c: Big_Thing1; d: Big_Thing1;
        e: Big_Thing1; f: Big_Thing1; g: Big_Thing1; h: Big_Thing1;
        i: Big_Thing1; j: Big_Thing1; k: Big_Thing1; l: Big_Thing1;
        m: Big_Thing1; n: Big_Thing1; o: Big_Thing1; p: Big_Thing1;
    }

    Big_Thing2 :: struct {
        a: Big_Thing; b: Big_Thing; c: Big_Thing; d: Big_Thing;
        e: Big_Thing; f: Big_Thing; g: Big_Thing; h: Big_Thing;
        i: Big_Thing; j: Big_Thing; k: Big_Thing; l: Big_Thing;
        m: Big_Thing; n: Big_Thing; o: Big_Thing; p: Big_Thing;
    }

    j: umm = 0;
    while j < 4 {
        i: umm = 0;
        while i < 9 {
            debug push(&mem, Big_Thing).base;
            i = i + 1;
        }
        debug push(&mem, Big_Thing2).base;
        j = j + 1;
    }

    cursor(&mem, &cur: Region.Cursor);

    j = 0;
    while j < 4 {
        i: umm = 0;
        while i < 9 {
            debug push(&mem, Big_Thing).base;
            i = i + 1;
        }
        debug push(&mem, Big_Thing2).base;
        j = j + 1;
    }

    rewind(&mem, &cur);

    drop(&mem);


    puts(FD_STDOUT, "hello from userland\n");

    settings: Environment_Settings;
    settings.custom_backend    = false;
    settings.pointer_size      = 8;
    settings.pointer_alignment = 8;

    make_environment(&env: &Environment, settings);
    add_file(env, "src_fun/test/target1.fun");

    more_events := true;
    while more_events {
        wait_event(env, &event: Event);

        if event.kind == EVENT_FINISHED
         => more_events = false;
        elif event.kind == EVENT_UNIT_REQUIRES_PLACEMENT {
            debug "need to place something";
            confirm_place_unit(env, event.unit_ref, zero, zero);
        }
        elif event.kind == EVENT_UNIT_WAS_PLACED
         => debug "a unit was placed, cool";
        elif event.kind == EVENT_UNIT_REQUIRES_PATCHING {
            debug "need to patch something";
            confirm_patch_unit(env, event.unit_ref);
        }
        elif event.kind == EVENT_UNIT_WAS_PATCHED
         => debug "a unit was patched, cool";
        elif event.kind == EVENT_UNIT_REQUIRES_RUNNING {
            debug "need to run something";
            confirm_run_unit(env, event.unit_ref);
        }
        elif event.kind == EVENT_UNIT_WAS_RUN
         => debug "a unit was run, cool";
        elif event.kind == EVENT_ERROR
         => debug "an error occured!";
        else
         => debug "unrecognized event";
    }

    puts(FD_STDOUT, "done compiling\n");
}
