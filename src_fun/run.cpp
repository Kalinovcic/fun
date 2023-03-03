#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"
#include <stdio.h>


EnterApplicationNamespace



void run_bytecode(User* user, Unit* unit, umm instruction, byte* storage)
{
run:
    if (!unit) return;

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
    case OP_LITERAL:               opname = "OP_LITERAL"_s;               break;
    case OP_COPY:                  opname = "OP_COPY"_s;                  break;
    case OP_COPY_FROM_INDIRECT:    opname = "OP_COPY_FROM_INDIRECT"_s;    break;
    case OP_COPY_TO_INDIRECT:      opname = "OP_COPY_TO_INDIRECT"_s;      break;
    case OP_COPY_BETWEEN_INDIRECT: opname = "OP_COPY_BETWEEN_INDIRECT"_s; break;
    case OP_ADDRESS:               opname = "OP_ADDRESS"_s;               break;
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
    case OP_LITERAL:                memcpy(storage + r, &a, s);             break;
    case OP_COPY:                   memcpy(storage + r, storage + a, s);    break;
    case OP_COPY_FROM_INDIRECT:     memcpy(storage + r, M(void*, a), s);    break;
    case OP_COPY_TO_INDIRECT:       memcpy(M(void*, r), storage + a, s);    break;
    case OP_COPY_BETWEEN_INDIRECT:  memcpy(M(void*, r), M(void*, a), s);    break;
    case OP_ADDRESS:                M(byte*, r) = storage + a;              break;
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
        case TYPE_U8:   M(u8,  r) = M(u8,  a) * M(u8,  b); break;
        case TYPE_U16:  M(u16, r) = M(u16, a) * M(u16, b); break;
        case TYPE_U32:  M(u32, r) = M(u32, a) * M(u32, b); break;
        case TYPE_U64:  M(u64, r) = M(u64, a) * M(u64, b); break;
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
    case OP_DEBUG_ALLOC:            M(void*, r) = malloc(M(umm, a)); break;
    case OP_DEBUG_FREE:             free(M(void*, r));               break;
    }

    instruction++;
    goto run;
#undef M
}

void run_unit(Unit* unit)
{
    assert(unit->compiled_bytecode);
    assert(!(unit->flags & UNIT_IS_STRUCT));

    User* user = create_user();
    Defer(delete_user(user));
    enter_lockdown(user);
    Defer(exit_lockdown(user));

    byte* storage = user_alloc(user, unit->storage_size, unit->storage_alignment);
    Defer(user_free(user, storage));
    memset(storage, 0, 3 * sizeof(void*));

    run_bytecode(user, unit, unit->entry_block->first_instruction, storage);
}


ExitApplicationNamespace
