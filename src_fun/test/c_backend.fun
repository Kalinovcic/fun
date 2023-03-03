#!/home/kalinovcic/schedulebot/run_tree/fun

using Self     :: import "c_backend.fun";
using System   :: import "system.fun";
using Compiler :: import "compiler.fun";

run
{
    puts("ello m8\n");

    make_context(&ctx: &Context);
    add_file(ctx, "target1.fun");

    more_events := cast(bool, true);
    while more_events
    {
        wait_event(ctx, &event: Event);
        if event.kind == cast(u32, EVENT_CONTEXT_FINISHED)
        {
            more_events = cast(bool, false);
        }
        else => if event.kind == cast(u32, EVENT_UNIT_PARSED)
        {
            debug "parsed a unit";
        }
        else => if event.kind == cast(u32, EVENT_UNIT_REQUIRES_PLACEMENT)
        {
            debug "could do custom unit placement right now, but won't";
        }
        else => if event.kind == cast(u32, EVENT_UNIT_PLACED)
        {
            debug "unit placed, size of unit is:";
            debug event.unit_ref.storage_size;
        }
        else
        {
            debug "unrecognized event";
        }
    }

    puts("done compiling\n");
}
