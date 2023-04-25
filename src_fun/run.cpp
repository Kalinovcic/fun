#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"
#include <stdio.h>


EnterApplicationNamespace



static bool run_intrinsic(User* user, Unit* unit, byte* storage, Block* block, String intrinsic,
                          Bytecode_Continuation continuation)
{
    Environment* env = unit->env;
    Compiler*    ctx = env->ctx;

    assert(env->pointer_size      == sizeof (void*));
    assert(env->pointer_alignment == alignof(void*));

    auto get_runtime_parameter = [&](String name, auto** out_address, Type assertion = INVALID_TYPE, Type* out_type = NULL)
    {
        for (Expression id = {}; id < block->inferred_expressions.count; id = (Expression)(id + 1))
        {
            auto* expr  = &block->parsed_expressions  [id];
            auto* infer = &block->inferred_expressions[id];
            if (!(expr->flags & EXPRESSION_DECLARATION_IS_PARAMETER)) continue;
            if (get_identifier(ctx, &expr->declaration.name) != name) continue;
            if (is_soft_type(infer->type)) break;
            if (out_type) *out_type = infer->type;
            if (assertion != INVALID_TYPE && assertion != infer->type)
            {
                String type_desc = exact_type_description(unit, assertion);
                fprintf(stderr, "Runtime parameter '%.*s' to intrinsic '%.*s' must be of type '%.*s'\nAborting...\n",
                    StringArgs(name), StringArgs(intrinsic), StringArgs(type_desc));
                exit(1);
            }

            u64 offset;
            assert(get(&block->declaration_placement, &id, &offset));
            *(byte**) out_address = storage + offset;
            return;
        }

        fprintf(stderr, "Missing runtime parameter '%.*s' to intrinsic '%.*s'\nAborting...\n", StringArgs(name), StringArgs(intrinsic));
        exit(1);
    };

    auto get_runtime_return = [&](String name, auto** out_address, Type assertion = INVALID_TYPE, Type* out_type = NULL)
    {
        Block* return_struct = NULL;
        for (Expression id = {}; id < block->inferred_expressions.count; id = (Expression)(id + 1))
        {
            auto* expr  = &block->parsed_expressions  [id];
            auto* infer = &block->inferred_expressions[id];
            if (!(expr->flags & EXPRESSION_DECLARATION_IS_RETURN)) continue;
            if (!is_user_defined_type(infer->type)) break;
            Block* structure = get_user_type_data(env, infer->type)->unit->entry_block;

            u64 structure_offset;
            assert(get(&block->declaration_placement, &id, &structure_offset));

            enum {} block;
            for (Expression id = {}; id < structure->inferred_expressions.count; id = (Expression)(id + 1))
            {
                auto* expr  = &structure->parsed_expressions  [id];
                auto* infer = &structure->inferred_expressions[id];
                if (expr->kind != EXPRESSION_DECLARATION) continue;
                if (get_identifier(ctx, &expr->declaration.name) != name) continue;
                if (is_soft_type(infer->type)) break;
                if (out_type) *out_type = infer->type;
                if (assertion != INVALID_TYPE && assertion != infer->type)
                {
                    String type_desc = exact_type_description(unit, assertion);
                    fprintf(stderr, "Runtime return '%.*s' to intrinsic '%.*s' must be of type '%.*s'\nAborting...\n",
                        StringArgs(name), StringArgs(intrinsic), StringArgs(type_desc));
                    exit(1);
                }

                u64 offset;
                assert(get(&structure->declaration_placement, &id, &offset));
                *(byte**) out_address = storage + structure_offset + offset;
                return;
            }
        }

        fprintf(stderr, "Missing runtime return '%.*s' to intrinsic '%.*s'\nAborting...\n", StringArgs(name), StringArgs(intrinsic));
        exit(1);
    };

    auto confirm_response_to_actionable_event = [](Environment* chlid_env)
    {
        chlid_env->puppeteer_event_is_actionable = false;
        chlid_env->puppeteer_event = { INVALID_PIPELINE_TASK };
    };

    if (intrinsic == "syscall"_s)
    {
        Scope_Region_Cursor temp_scope(temp);

        umm *out, *rax, *rdi, *rsi, *rdx, *r10, *r8, *r9;
        get_runtime_parameter("sys"_s, &rax, TYPE_UMM);
        get_runtime_parameter("rdi"_s, &rdi, TYPE_UMM);
        get_runtime_parameter("rsi"_s, &rsi, TYPE_UMM);
        get_runtime_parameter("rdx"_s, &rdx, TYPE_UMM);
        get_runtime_parameter("r10"_s, &r10, TYPE_UMM);
        get_runtime_parameter("r8"_s,  &r8,  TYPE_UMM);
        get_runtime_parameter("r9"_s,  &r9,  TYPE_UMM);
        get_runtime_return   ("rax"_s, &out, TYPE_UMM);
        *out = syscall(*rax, *rdi, *rsi, *rdx, *r10, *r8, *r9);
    }
    else if (intrinsic == "compiler_make_environment"_s)
    {
        struct Environment_Settings
        {
            bool silence_errors;
            bool custom_backend;
            u64  pointer_size;
            u64  pointer_alignment;
        };

        Environment_Settings* settings; get_runtime_parameter("settings"_s, &settings);
        Environment***        out_env;  get_runtime_parameter("out_env"_s,  &out_env);

        Environment* child_env = make_environment(ctx, env);
        child_env->silence_errors               = settings->silence_errors;
        child_env->puppeteer_has_custom_backend = settings->custom_backend;
        if (settings->custom_backend)
        {
            child_env->pointer_size      = settings->pointer_size;
            child_env->pointer_alignment = settings->pointer_alignment;
            if (!child_env->pointer_alignment)
                child_env->pointer_alignment = 1;
        }
        **out_env = child_env;
    }
    else if (intrinsic == "compiler_yield"_s)
    {
        Environment** child_env_ptr;
        get_runtime_parameter("env"_s, &child_env_ptr);
        Environment* child_env = *child_env_ptr;

        assert(env == child_env->puppeteer);
        if (child_env->puppeteer_event.kind != INVALID_PIPELINE_TASK)
        {
            if (child_env->puppeteer_event_is_actionable)
                fprintf(stderr, "warning: user did not resolve an actionable environment event\n");
            return false;
        }

        child_env->puppeteer_is_waiting = true;
        child_env->puppeteer_continuation = continuation;
        return true;
    }
    else if (intrinsic == "compiler_add_file"_s)
    {
        Environment** child_env_ptr;
        get_runtime_parameter("env"_s, &child_env_ptr);
        Environment* child_env = *child_env_ptr;

        String* path;
        get_runtime_parameter("path"_s, &path);

        Block* tlb = parse_top_level_from_file(child_env->ctx, *path);
        if (tlb)
            materialize_unit(child_env, tlb);
    }
    else if (intrinsic == "compiler_get_event"_s)
    {
        enum: u32
        {
            EVENT_FINISHED                = 1,
            EVENT_UNIT_WAS_PLACED         = 2,
            EVENT_UNIT_WAS_PATCHED        = 3,
            EVENT_UNIT_WAS_RUN            = 4,
            EVENT_ERROR                   = 5,

            EVENT_ACTIONABLE_BASE         = 1000,
            EVENT_UNIT_REQUIRES_PLACEMENT = EVENT_ACTIONABLE_BASE + EVENT_UNIT_WAS_PLACED,
            EVENT_UNIT_REQUIRES_PATCHING  = EVENT_ACTIONABLE_BASE + EVENT_UNIT_WAS_PATCHED,
            EVENT_UNIT_REQUIRES_RUNNING   = EVENT_ACTIONABLE_BASE + EVENT_UNIT_WAS_RUN,
        };

        struct Compiler_Event
        {
            u32    kind;
            bool   actionable;
            Unit*  unit;
            String error;
        };

        Environment** child_env_ptr;
        get_runtime_parameter("env"_s, &child_env_ptr);
        Environment* child_env = *child_env_ptr;

        Compiler_Event** event_ptr;
        get_runtime_parameter("event"_s, &event_ptr);
        Compiler_Event* event = *event_ptr;

        event->actionable = child_env->puppeteer_event_is_actionable;
        if (child_env->puppeteer_event.kind == INVALID_PIPELINE_TASK)
        {
            assert(!event->actionable);
            event->kind = EVENT_FINISHED;
            event->unit = NULL;
        }
        else if (child_env->puppeteer_event.kind == PIPELINE_TASK_PLACE)
        {
            event->kind = event->actionable ? EVENT_UNIT_REQUIRES_PLACEMENT : EVENT_UNIT_WAS_PLACED;
            event->unit = child_env->puppeteer_event.unit;
        }
        else if (child_env->puppeteer_event.kind == PIPELINE_TASK_PATCH)
        {
            event->kind = event->actionable ? EVENT_UNIT_REQUIRES_PATCHING : EVENT_UNIT_WAS_PATCHED;
            event->unit = child_env->puppeteer_event.unit;
        }
        else if (child_env->puppeteer_event.kind == PIPELINE_TASK_RUN)
        {
            event->kind = event->actionable ? EVENT_UNIT_REQUIRES_RUNNING : EVENT_UNIT_WAS_RUN;
            event->unit = child_env->puppeteer_event.unit;
        }
        else Unreachable;

        if (!child_env->puppeteer_event_is_actionable)
            child_env->puppeteer_event = {};
    }
    else if (intrinsic == "compiler_confirm_place_unit"_s)
    {
        Environment** child_env_ptr; get_runtime_parameter("env"_s,       &child_env_ptr);
        Unit**        unit_ptr;      get_runtime_parameter("placed"_s,    &unit_ptr);
        u64*          size;          get_runtime_parameter("size"_s,      &size);
        u64*          alignment;     get_runtime_parameter("alignment"_s, &alignment);

        Environment* child_env = *child_env_ptr;
        Unit* unit = *unit_ptr;

        assert(child_env->puppeteer_event_is_actionable);
        assert(child_env->puppeteer_event.kind == PIPELINE_TASK_PLACE);
        assert(child_env->puppeteer_event.unit == unit);
        confirm_unit_placed(unit, *size, *alignment);

        confirm_response_to_actionable_event(child_env);
        child_env->puppeteer_event_is_actionable = false;
        child_env->puppeteer_event = { PIPELINE_TASK_PLACE, unit };
    }
    else if (intrinsic == "compiler_confirm_patch_unit"_s)
    {
        Environment** child_env_ptr; get_runtime_parameter("env"_s,     &child_env_ptr);
        Unit**        unit_ptr;      get_runtime_parameter("patched"_s, &unit_ptr);

        Environment* child_env = *child_env_ptr;
        Unit* unit = *unit_ptr;

        assert(child_env->puppeteer_event_is_actionable);
        assert(child_env->puppeteer_event.kind == PIPELINE_TASK_PATCH);
        assert(child_env->puppeteer_event.unit == unit);
        confirm_unit_patched(unit);

        confirm_response_to_actionable_event(child_env);
        child_env->puppeteer_event_is_actionable = false;
        child_env->puppeteer_event = { PIPELINE_TASK_PATCH, unit };
    }
    else if (intrinsic == "compiler_confirm_run_unit"_s)
    {
        Environment** child_env_ptr; get_runtime_parameter("env"_s, &child_env_ptr);
        Unit**        unit_ptr;      get_runtime_parameter("ran"_s, &unit_ptr);

        Environment* child_env = *child_env_ptr;
        Unit* unit = *unit_ptr;

        assert(child_env->puppeteer_event_is_actionable);
        assert(child_env->puppeteer_event.kind == PIPELINE_TASK_RUN);
        assert(child_env->puppeteer_event.unit == unit);

        confirm_response_to_actionable_event(child_env);
        child_env->puppeteer_event_is_actionable = false;
        child_env->puppeteer_event = { PIPELINE_TASK_RUN, unit };
    }
    else
    {
        fprintf(stderr, "User is attempting to run an unknown intrinsic '%.*s'\nAborting...\n", StringArgs(intrinsic));
        exit(1);
    }

    return false;
}


void run_bytecode(User* user, Bytecode_Continuation continue_from)
{
    Unit* unit        = continue_from.unit;
    umm   instruction = continue_from.instruction;
    byte* storage     = continue_from.storage;

run:
    if (!unit) return;

    assert(unit->compiled_bytecode);
    assert(!(unit->flags & UNIT_IS_STRUCT));

    Bytecode const* bc = &unit->bytecode[instruction];
    flags32 flags = bc->flags;
    u64 r = bc->r;
    u64 a = bc->a;
    u64 b = bc->b;
    u64 s = bc->s;

    set_most_recent_execution_location(user, unit, bc);

#if 0
    String opname = {};
    switch (bc->op)
    {
    case OP_ZERO:                  opname = "OP_ZERO"_s;                  break;
    case OP_ZERO_INDIRECT:         opname = "OP_ZERO_INDIRECT"_s;         break;
    case OP_LITERAL:               opname = "OP_LITERAL"_s;               break;
    case OP_COPY:                  opname = "OP_COPY"_s;                  break;
    case OP_COPY_FROM_INDIRECT:    opname = "OP_COPY_FROM_INDIRECT"_s;    break;
    case OP_COPY_TO_INDIRECT:      opname = "OP_COPY_TO_INDIRECT"_s;      break;
    case OP_COPY_BETWEEN_INDIRECT: opname = "OP_COPY_BETWEEN_INDIRECT"_s; break;
    case OP_ADDRESS:               opname = "OP_ADDRESS"_s;               break;
    case OP_NOT:                   opname = "OP_NOT"_s;                   break;
    case OP_NEGATE:                opname = "OP_NEGATE"_s;                break;
    case OP_ADD:                   opname = "OP_ADD"_s;                   break;
    case OP_SUBTRACT:              opname = "OP_SUBTRACT"_s;              break;
    case OP_MULTIPLY:              opname = "OP_MULTIPLY"_s;              break;
    case OP_DIVIDE_WHOLE:          opname = "OP_DIVIDE_WHOLE"_s;          break;
    case OP_DIVIDE_FRACTIONAL:     opname = "OP_DIVIDE_FRACTIONAL"_s;     break;
    case OP_COMPARE:               opname = "OP_COMPARE"_s;               break;
    case OP_MOVE_POINTER_CONSTANT: opname = "OP_MOVE_POINTER_CONSTANT"_s; break;
    case OP_MOVE_POINTER_FORWARD:  opname = "OP_MOVE_POINTER_FORWARD"_s;  break;
    case OP_MOVE_POINTER_BACKWARD: opname = "OP_MOVE_POINTER_BACKWARD"_s; break;
    case OP_POINTER_DISTANCE:      opname = "OP_POINTER_DISTANCE"_s;      break;
    case OP_CAST:                  opname = "OP_CAST"_s;                  break;
    case OP_GOTO:                  opname = "OP_GOTO"_s;                  break;
    case OP_GOTO_IF_FALSE:         opname = "OP_GOTO_IF_FALSE"_s;         break;
    case OP_GOTO_INDIRECT:         opname = "OP_GOTO_INDIRECT"_s;         break;
    case OP_CALL:                  opname = "OP_CALL"_s;                  break;
    case OP_SWITCH_UNIT:           opname = "OP_SWITCH_UNIT"_s;           break;
    case OP_FINISH_UNIT:           opname = "OP_FINISH_UNIT"_s;           break;
    case OP_DEBUG_PRINT:           opname = "OP_DEBUG_PRINT"_s;           break;
    case OP_DEBUG_ALLOC:           opname = "OP_DEBUG_ALLOC"_s;           break;
    case OP_DEBUG_FREE:            opname = "OP_DEBUG_FREE"_s;            break;
    default:                       opname = Format(temp, "%", bc->op);    break;
    }
    exit_lockdown(user);
    printf("%-25.*s %16llx %16llx %16llx %16llx", StringArgs(opname),
        (unsigned long long) r, (unsigned long long) a, (unsigned long long) b, (unsigned long long) s);
    if (bc->flags & OP_COMPARE_EQUAL)   printf(" OP_COMPARE_EQUAL");
    if (bc->flags & OP_COMPARE_GREATER) printf(" OP_COMPARE_GREATER");
    if (bc->flags & OP_COMPARE_LESS)    printf(" OP_COMPARE_LESS");
    printf("\n");
    enter_lockdown(user);
#endif

#define M(type, offset) (*(type*)(storage + (offset)))

    switch (bc->op)
    {
    IllegalDefaultCase;

    case OP_ZERO:                   memset(storage + r, 0, s);              break;
    case OP_ZERO_INDIRECT:          memset(M(void*, r), 0, s);              break;
    case OP_LITERAL:                memcpy(storage + r, &a, s);             break;
    case OP_COPY:                   memcpy(storage + r, storage + a, s);    break;
    case OP_COPY_FROM_INDIRECT:     memcpy(storage + r, M(void*, a), s);    break;
    case OP_COPY_TO_INDIRECT:       memcpy(M(void*, r), storage + a, s);    break;
    case OP_COPY_BETWEEN_INDIRECT:  memcpy(M(void*, r), M(void*, a), s);    break;
    case OP_ADDRESS:                M(byte*, r) = storage + a;              break;
    case OP_NOT:                    M(u8, r) = M(u8, a) ? 0 : 1;            break;
    case OP_NEGATE:
    {
        switch (s)
        {
        IllegalDefaultCase;
        case TYPE_F16:  NotImplemented;
        case TYPE_S8:   M(s8,  r) = -M(s8,  a); break;
        case TYPE_S16:  M(s16, r) = -M(s16, a); break;
        case TYPE_S32:  M(s32, r) = -M(s32, a); break;
        case TYPE_S64:  M(s64, r) = -M(s64, a); break;
        case TYPE_F32:  M(f32, r) = -M(f32, a); break;
        case TYPE_F64:  M(f64, r) = -M(f64, a); break;
        }
    } break;
    case OP_ADD:
    {
        switch (s)
        {
        IllegalDefaultCase;
        case TYPE_F16:  NotImplemented;
        case TYPE_U8:   M(u8,  r) = M(u8,  a) + M(u8,  b); break;
        case TYPE_U16:  M(u16, r) = M(u16, a) + M(u16, b); break;
        case TYPE_U32:  M(u32, r) = M(u32, a) + M(u32, b); break;
        case TYPE_U64:  M(u64, r) = M(u64, a) + M(u64, b); break;
        case TYPE_F32:  M(f32, r) = M(f32, a) + M(f32, b); break;
        case TYPE_F64:  M(f64, r) = M(f64, a) + M(f64, b); break;
        }
    } break;
    case OP_SUBTRACT:
    {
        switch (s)
        {
        IllegalDefaultCase;
        case TYPE_F16:  NotImplemented;
        case TYPE_U8:   M(u8,  r) = M(u8,  a) - M(u8,  b); break;
        case TYPE_U16:  M(u16, r) = M(u16, a) - M(u16, b); break;
        case TYPE_U32:  M(u32, r) = M(u32, a) - M(u32, b); break;
        case TYPE_U64:  M(u64, r) = M(u64, a) - M(u64, b); break;
        case TYPE_F32:  M(f32, r) = M(f32, a) - M(f32, b); break;
        case TYPE_F64:  M(f64, r) = M(f64, a) - M(f64, b); break;
        }
    } break;
    case OP_MULTIPLY:
    {
        switch (s)
        {
        IllegalDefaultCase;
        case TYPE_F16:  NotImplemented;
        case TYPE_U8:   M(u8,  r) = M(u8,  a) * M(u8,  b); break;
        case TYPE_U16:  M(u16, r) = M(u16, a) * M(u16, b); break;
        case TYPE_U32:  M(u32, r) = M(u32, a) * M(u32, b); break;
        case TYPE_U64:  M(u64, r) = M(u64, a) * M(u64, b); break;
        case TYPE_F32:  M(f32, r) = M(f32, a) * M(f32, b); break;
        case TYPE_F64:  M(f64, r) = M(f64, a) * M(f64, b); break;
        }
    } break;
    case OP_DIVIDE_WHOLE:
    {
        switch (s)
        {
        IllegalDefaultCase;
        case TYPE_U8:   M(u8,  r) = M(u8,  a) / M(u8,  b); break;
        case TYPE_U16:  M(u16, r) = M(u16, a) / M(u16, b); break;
        case TYPE_U32:  M(u32, r) = M(u32, a) / M(u32, b); break;
        case TYPE_U64:  M(u64, r) = M(u64, a) / M(u64, b); break;
        case TYPE_S8:   M(s8,  r) = M(s8,  a) / M(s8,  b); break;
        case TYPE_S16:  M(s16, r) = M(s16, a) / M(s16, b); break;
        case TYPE_S32:  M(s32, r) = M(s32, a) / M(s32, b); break;
        case TYPE_S64:  M(s64, r) = M(s64, a) / M(s64, b); break;
        }
    } break;
    case OP_DIVIDE_FRACTIONAL:
    {
        switch (s)
        {
        IllegalDefaultCase;
        case TYPE_F16:  NotImplemented;
        case TYPE_F32:  M(f32, r) = M(f32, a) / M(f32, b); break;
        case TYPE_F64:  M(f64, r) = M(f64, a) / M(f64, b); break;
        }
    } break;
    case OP_COMPARE:
    {
        auto compare = []<typename T>(flags32 flags, T lhs, T rhs) -> bool
        {
            if (lhs == rhs) return flags & OP_COMPARE_EQUAL;
            if (lhs <  rhs) return flags & OP_COMPARE_LESS;
                            return flags & OP_COMPARE_GREATER;
        };

        CompileTimeAssert(sizeof(bool) == 1);
        switch ((Type) s)
        {
        IllegalDefaultCase;
        case TYPE_F16:  NotImplemented;
        case TYPE_U8:   M(bool, r) = compare(flags, M(u8,   a), M(u8,   b)); break;
        case TYPE_U16:  M(bool, r) = compare(flags, M(u16,  a), M(u16,  b)); break;
        case TYPE_U32:  M(bool, r) = compare(flags, M(u32,  a), M(u32,  b)); break;
        case TYPE_U64:  M(bool, r) = compare(flags, M(u64,  a), M(u64,  b)); break;
        case TYPE_S8:   M(bool, r) = compare(flags, M(s8,   a), M(s8,   b)); break;
        case TYPE_S16:  M(bool, r) = compare(flags, M(s16,  a), M(s16,  b)); break;
        case TYPE_S32:  M(bool, r) = compare(flags, M(s32,  a), M(s32,  b)); break;
        case TYPE_S64:  M(bool, r) = compare(flags, M(s64,  a), M(s64,  b)); break;
        case TYPE_F32:  M(bool, r) = compare(flags, M(f32,  a), M(f32,  b)); break;
        case TYPE_F64:  M(bool, r) = compare(flags, M(f64,  a), M(f64,  b)); break;
        case TYPE_BOOL: M(bool, r) = compare(flags, M(bool, a), M(bool, b)); break;
        }
    } break;
    case OP_MOVE_POINTER_CONSTANT:  M(byte*, r) = M(byte*, a) + s;                 break;
    case OP_MOVE_POINTER_FORWARD:   M(byte*, r) = M(byte*, a) + M(umm, b) * s;     break;
    case OP_MOVE_POINTER_BACKWARD:  M(byte*, r) = M(byte*, a) - M(umm, b) * s;     break;
    case OP_POINTER_DISTANCE:       M(umm,   r) = (M(byte*, b) - M(byte*, a)) / s; break;
    case OP_CAST:
    {
        auto cast = [&]<typename T>(T* result)
        {
            switch (b)
            {
            IllegalDefaultCase;
            case TYPE_F16:  NotImplemented;
            case TYPE_U8:   *result = (T) M(u8,   a); break;
            case TYPE_U16:  *result = (T) M(u16,  a); break;
            case TYPE_U32:  *result = (T) M(u32,  a); break;
            case TYPE_U64:  *result = (T) M(u64,  a); break;
            case TYPE_S8:   *result = (T) M(s8,   a); break;
            case TYPE_S16:  *result = (T) M(s16,  a); break;
            case TYPE_S32:  *result = (T) M(s32,  a); break;
            case TYPE_S64:  *result = (T) M(s64,  a); break;
            case TYPE_F32:  *result = (T) M(f32,  a); break;
            case TYPE_F64:  *result = (T) M(f64,  a); break;
            case TYPE_BOOL: *result = (T) M(bool, a); break; CompileTimeAssert(sizeof(bool) == 1);
            }
        };

        switch (s)
        {
        IllegalDefaultCase;
        case TYPE_F16:  NotImplemented;
        case TYPE_U8:   cast(&M(u8,   r)); break;
        case TYPE_U16:  cast(&M(u16,  r)); break;
        case TYPE_U32:  cast(&M(u32,  r)); break;
        case TYPE_U64:  cast(&M(u64,  r)); break;
        case TYPE_S8:   cast(&M(s8,   r)); break;
        case TYPE_S16:  cast(&M(s16,  r)); break;
        case TYPE_S32:  cast(&M(s32,  r)); break;
        case TYPE_S64:  cast(&M(s64,  r)); break;
        case TYPE_F32:  cast(&M(f32,  r)); break;
        case TYPE_F64:  cast(&M(f64,  r)); break;
        case TYPE_BOOL: cast(&M(bool, r)); break; CompileTimeAssert(sizeof(bool) == 1);
        }
    } break;
    case OP_GOTO:                   instruction = r;                              goto run;
    case OP_GOTO_IF_FALSE:          instruction = M(u8, a) ? instruction + 1 : r; goto run;
    case OP_GOTO_INDIRECT:          instruction = M(umm, r);                      goto run;
    case OP_CALL:                   M(umm, a) = instruction + 1; instruction = r; goto run;
    case OP_INTRINSIC:
    {
        exit_lockdown(user);
        Block* block     = (Block*) a;
        String intrinsic = { (umm) s, (u8*) b };
        assert(block->flags & BLOCK_IS_PARAMETER_BLOCK);

        Bytecode_Continuation continuation = { unit, instruction + 1, storage };
        bool exit_here = run_intrinsic(user, unit, storage, block, intrinsic, continuation);

        enter_lockdown(user);
        if (exit_here)
            return;
    } break;
    case OP_SWITCH_UNIT:
    {
        void** destination = M(void**, a);
        destination[0] = (void*)(unit);
        destination[1] = (void*)(umm)(instruction + 1);
        destination[2] = (void*)(storage);

        unit    = M(Unit*, r);
        storage = M(byte*, a);
        instruction = unit->entry_block->first_instruction;
        goto run;
    } break;
    case OP_FINISH_UNIT:
    {
        unit        = *(Unit**)(storage + 0 * sizeof(void*));
        instruction = *(umm  *)(storage + 1 * sizeof(void*));
        storage     = *(byte**)(storage + 2 * sizeof(void*));
        goto run;
    } break;
    case OP_DEBUG_PRINT:
    {
        exit_lockdown(user);
        String text = {};
        switch (s)
        {
        case TYPE_F16:    NotImplemented;
        case TYPE_VOID:   text = "void"_s;                             break;
        case TYPE_U8:     text = Format(temp, "%",      M(u8,     r)); break;
        case TYPE_U16:    text = Format(temp, "%",      M(u16,    r)); break;
        case TYPE_U32:    text = Format(temp, "%",      M(u32,    r)); break;
        case TYPE_U64:    text = Format(temp, "%",      M(u64,    r)); break;
        case TYPE_UMM:    text = Format(temp, "%",      M(umm,    r)); break;
        case TYPE_S8:     text = Format(temp, "%",      M(s8,     r)); break;
        case TYPE_S16:    text = Format(temp, "%",      M(s16,    r)); break;
        case TYPE_S32:    text = Format(temp, "%",      M(s32,    r)); break;
        case TYPE_S64:    text = Format(temp, "%",      M(s64,    r)); break;
        case TYPE_SMM:    text = Format(temp, "%",      M(smm,    r)); break;
        case TYPE_F32:    text = Format(temp, "%",      M(f32,    r)); break;
        case TYPE_F64:    text = Format(temp, "%",      M(f64,    r)); break;
        case TYPE_BOOL:   text = Format(temp, "%",      M(bool,   r)); break;
        case TYPE_TYPE:   text = Format(temp, "type %", M(Type,   r)); break;
        case TYPE_STRING: text =                        M(String, r);  break;
        default:
            if (is_pointer_type((Type) s))
            {
                text = Format(temp, "%", M(void*, r));
            }
            else
            {
                assert(is_user_defined_type((Type) s));
                text = "user defined type"_s;
            }
            break;
        }
        printf("%.*s\n", StringArgs(text));
        enter_lockdown(user);
    } break;
    case OP_DEBUG_ALLOC:            M(void*, r) = user_alloc(user, M(umm, a), 16); break;
    case OP_DEBUG_FREE:             user_free(user, M(void*, r));                  break;
    }

    instruction++;
    goto run;
#undef M
}


ExitApplicationNamespace
