#!/home/kalinovcic/schedulebot/run_tree/fun

using Self     :: import "c_backend.fun";
using System   :: import "system.fun";
using Compiler :: import "compiler.fun";

run {
    mem: Region;
    mem.page_size = 123;
    debug push(&mem, u32).base;

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
