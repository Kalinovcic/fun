#!/home/kalinovcic/schedulebot/run_tree/fun

using Self     :: import "c_backend.fun";
using System   :: import "system.fun";
using Compiler :: import "compiler.fun";

run
{
    puts("hello from userland\n");

    settings: Environment_Settings;
    settings.custom_backend = cast(bool, false);

    make_environment(&env: &Environment, settings);
    add_file(env, "src_fun/test/target1.fun");

    more_events := cast(bool, true);
    while more_events
    {
        wait_event(env, &event: Event);
        if event.kind == cast(u32, EVENT_FINISHED)
        {
            more_events = cast(bool, false);
        }
        else => if event.kind == cast(u32, EVENT_UNIT_REQUIRES_PLACEMENT)
        {
            debug "need to place something";
            confirm_place_unit(env, event.unit_ref, zero, zero);
        }
        else => if event.kind == cast(u32, EVENT_UNIT_WAS_PLACED)
        {
            debug "a unit was placed, cool";
        }
        else => if event.kind == cast(u32, EVENT_UNIT_REQUIRES_PATCHING)
        {
            debug "need to patch something";
            confirm_patch_unit(env, event.unit_ref);
        }
        else => if event.kind == cast(u32, EVENT_UNIT_WAS_PATCHED)
        {
            debug "a unit was patched, cool";
        }
        else => if event.kind == cast(u32, EVENT_UNIT_REQUIRES_RUNNING)
        {
            debug "need to run something";
            confirm_run_unit(env, event.unit_ref);
        }
        else => if event.kind == cast(u32, EVENT_UNIT_WAS_RUN)
        {
            debug "a unit was run, cool";
        }
        else
        {
            debug "unrecognized event";
        }
    }

    puts("done compiling\n");
}
