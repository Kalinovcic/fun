#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"
#include <stdio.h>


EnterApplicationNamespace



static String int_base10(Integer const* integer, Region* memory)
{
    Integer i = int_clone(integer);
    Defer(int_free(&i));
    if (i.negative)
        int_negate(&i);

    Integer ten = {};
    int_set16(&ten, 10);
    Defer(int_free(&ten));

    String_Concatenator cat = {};
    if (int_is_zero(&i))
        add(&cat, "0"_s);
    else while (!int_is_zero(&i))
    {
        Integer mod = {};
        Defer(int_free(&mod));
        int_div(&i, &ten, &mod);

        u32 number = mod.size ? mod.digit[0] : 0;
        assert(number < 10);
        char c = '0' + number;
        add(&cat, &c, 1);
    }

    if (integer->negative)
        add(&cat, "-"_s);

    String str = resolve_to_string_and_free(&cat, memory);
    for (umm i = 0; i < str.length - i - 1; i++)
    {
        u8 temp = str[i];
        str[i] = str[str.length - i - 1];
        str[str.length - i - 1] = temp;
    }
    return str;
}




union Memory
{
    byte base[0];
    u8   as_u8;
    u16  as_u16;
    u32  as_u32;
    u64  as_u64;
    s8   as_s8;
    s16  as_s16;
    s32  as_s32;
    s64  as_s64;
    f32  as_f32;
    f64  as_f64;
};

static void run_block(Unit* unit, byte* storage, Block* block);

static Memory* run_expression(Unit* unit, byte* storage, Block* block, Expression id)
{
    auto*   expr    = &block->parsed_expressions[id];
    auto*   infer   = &block->inferred_expressions[id];
    Memory* address = (Memory*)(storage + infer->offset);
    assert(!(infer->flags & INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME));

    // report_error(unit->ctx, expr, Format(temp, "block: %, kind: %", block, expr->kind));

    switch (expr->kind)
    {

    case EXPRESSION_ZERO:                   Unreachable;
    case EXPRESSION_TRUE:                   Unreachable;
    case EXPRESSION_FALSE:                  Unreachable;
    case EXPRESSION_INTEGER_LITERAL:        Unreachable;
    case EXPRESSION_FLOATING_POINT_LITERAL: Unreachable;

    case EXPRESSION_NAME: break;

    case EXPRESSION_NEGATE:
    {
        Memory* op = run_expression(unit, storage, block, expr->unary_operand);
        switch (infer->type)
        {
        case TYPE_U8:  address->as_u8  = -op->as_u8;
        case TYPE_U16: address->as_u16 = -op->as_u16;
        case TYPE_U32: address->as_u32 = -op->as_u32;
        case TYPE_U64: address->as_u64 = -op->as_u64;
        case TYPE_S8:  address->as_s8  = -op->as_s8;
        case TYPE_S16: address->as_s16 = -op->as_s16;
        case TYPE_S32: address->as_s32 = -op->as_s32;
        case TYPE_S64: address->as_s64 = -op->as_s64;
        case TYPE_F32: address->as_f32 = -op->as_f32;
        case TYPE_F64: address->as_f64 = -op->as_f64;
        IllegalDefaultCase;
        }
    } break;

    case EXPRESSION_DECLARATION:
    {
        u64 size = infer->size;
        if (expr->declaration.value == NO_EXPRESSION)
        {
            memset(address, 0, size);
        }
        else
        {
            assert(size == block->inferred_expressions[expr->declaration.value].size);
            Memory* value = run_expression(unit, storage, block, expr->declaration.value);
            memcpy(address, value, size);
        }
    } break;

    case EXPRESSION_ASSIGNMENT:
    {
        u64 size = infer->size;
        assert(size == block->inferred_expressions[expr->binary.lhs].size);
        assert(size == block->inferred_expressions[expr->binary.rhs].size);

        Memory* lhs = run_expression(unit, storage, block, expr->binary.lhs);
        Memory* rhs = run_expression(unit, storage, block, expr->binary.rhs);
        memcpy(lhs, rhs, size);
        memcpy(address, lhs, size);
    } break;

    case EXPRESSION_ADD: NotImplemented;
    case EXPRESSION_SUBTRACT:
    {
        Memory* lhs = run_expression(unit, storage, block, expr->binary.lhs);
        Memory* rhs = run_expression(unit, storage, block, expr->binary.rhs);

        switch (infer->type)
        {
        case TYPE_U8:  address->as_u8  = lhs->as_u8  - rhs->as_u8;  break;
        case TYPE_U16: address->as_u16 = lhs->as_u16 - rhs->as_u16; break;
        case TYPE_U32: address->as_u32 = lhs->as_u32 - rhs->as_u32; break;
        case TYPE_U64: address->as_u64 = lhs->as_u64 - rhs->as_u64; break;
        case TYPE_S8:  address->as_s8  = lhs->as_s8  - rhs->as_s8;  break;
        case TYPE_S16: address->as_s16 = lhs->as_s16 - rhs->as_s16; break;
        case TYPE_S32: address->as_s32 = lhs->as_s32 - rhs->as_s32; break;
        case TYPE_S64: address->as_s64 = lhs->as_s64 - rhs->as_s64; break;
        case TYPE_F32: address->as_f32 = lhs->as_f32 - rhs->as_f32; break;
        case TYPE_F64: address->as_f64 = lhs->as_f64 - rhs->as_f64; break;
        IllegalDefaultCase;
        }
    } break;

    case EXPRESSION_MULTIPLY: NotImplemented;
    case EXPRESSION_DIVIDE: NotImplemented;
    case EXPRESSION_EQUAL: NotImplemented;
    case EXPRESSION_NOT_EQUAL: NotImplemented;
    case EXPRESSION_GREATER_THAN: NotImplemented;
    case EXPRESSION_GREATER_OR_EQUAL: NotImplemented;
    case EXPRESSION_LESS_THAN: NotImplemented;
    case EXPRESSION_LESS_OR_EQUAL: NotImplemented;

    case EXPRESSION_CAST:
    {
        if (infer->constant_index != INVALID_CONSTANT_INDEX)
        {
            Integer const* integer = &block->constants[infer->constant_index];
            u64 abs = 0;
            for (umm i = 0; i < integer->size; i++)
                abs |= ((u64) integer->digit[i]) << (i * DIGIT_BITS);
            if (integer->negative)
                abs = -abs;
            memcpy(address->base, &abs, infer->size);
        }
        else
        {
            // @Incomplete - do the actual cast
            Memory* op = run_expression(unit, storage, block, expr->binary.rhs);
            memcpy(address->base, op->base, infer->size);
        }
    } break;

    case EXPRESSION_BRANCH:
    {
        if (expr->branch.condition == NO_EXPRESSION)
        {
            assert(expr->branch.on_success != NO_EXPRESSION);
            run_expression(unit, storage, block, expr->branch.on_success);
        }
        else while (true)
        {
            Memory* value = run_expression(unit, storage, block, expr->branch.condition);

            bool condition = false;
            switch (block->inferred_expressions[expr->branch.condition].size)
            {
            case 1: condition = value->as_u8;  break;
            case 2: condition = value->as_u16; break;
            case 4: condition = value->as_u32; break;
            case 8: condition = value->as_u64; break;
            IllegalDefaultCase;
            }

            Expression to_execute = condition ? expr->branch.on_success : expr->branch.on_failure;
            if (to_execute != NO_EXPRESSION)
                run_expression(unit, storage, block, to_execute);
            if (!(expr->flags & EXPRESSION_BRANCH_IS_LOOP))
                break;
        }
    } break;

    case EXPRESSION_CALL:
    {
        if (expr->call.arguments->count)
            NotImplemented;
        assert(infer->called_block);
        run_block(unit, storage, infer->called_block);
    } break;

    case EXPRESSION_DEBUG:
    {
        auto* op_infer = &block->inferred_expressions[expr->unary_operand];
        switch (op_infer->type)
        {
        case TYPE_SOFT_INTEGER:
        {
            String str = int_base10(&block->constants[op_infer->constant_index], temp);
            printf("%.*s\n", StringArgs(str));
        } break;
        case TYPE_SOFT_FLOATING_POINT:  printf("<soft floating point>\n"); break;
        case TYPE_SOFT_BOOL:            printf("%s\n", op_infer->constant_bool ? "true" : "false"); break;
        case TYPE_SOFT_TYPE:            printf("<soft type>\n"); break;
        case TYPE_SOFT_BLOCK:
        {
            Token_Info info;
            {
                Token_Info* from_info = get_token_info(unit->ctx, &op_infer->constant_block.parsed_child->from);
                Token_Info* to_info   = get_token_info(unit->ctx, &op_infer->constant_block.parsed_child->to);
                info = *from_info;
                info.length = to_info->offset + to_info->length - from_info->offset;
            }
            String location = location_report_part(unit->ctx, &info, 1);
            printf("<soft block>\n%.*s\n", StringArgs(location));
        } break;
        default:
            Memory* value = run_expression(unit, storage, block, expr->unary_operand);
            switch (op_infer->type)
            {
            case TYPE_U8:                   printf("%llu\n", (unsigned long long) value->as_u8);  break;
            case TYPE_U16:                  printf("%llu\n", (unsigned long long) value->as_u16); break;
            case TYPE_U32:                  printf("%llu\n", (unsigned long long) value->as_u32); break;
            case TYPE_U64:                  printf("%llu\n", (unsigned long long) value->as_u64); break;
            case TYPE_S8:                   printf("%lld\n", (long long) value->as_s8);  break;
            case TYPE_S16:                  printf("%lld\n", (long long) value->as_s16); break;
            case TYPE_S32:                  printf("%lld\n", (long long) value->as_s32); break;
            case TYPE_S64:                  printf("%lld\n", (long long) value->as_s64); break;
            case TYPE_F16:                  printf("<f16>\n"); break;
            case TYPE_F32:                  printf("%f\n", (double) value->as_f32); break;
            case TYPE_F64:                  printf("%f\n", (double) value->as_f64); break;
            case TYPE_BOOL8:                printf("%s\n", value->as_u8  ? "true" : "false"); break;
            case TYPE_BOOL16:               printf("%s\n", value->as_u16 ? "true" : "false"); break;
            case TYPE_BOOL32:               printf("%s\n", value->as_u32 ? "true" : "false"); break;
            case TYPE_BOOL64:               printf("%s\n", value->as_u64 ? "true" : "false"); break;
            IllegalDefaultCase;
            }
        }
    } break;

    IllegalDefaultCase;
    }

    return address;
}

static void run_block(Unit* unit, byte* storage, Block* block)
{
    assert(block->flags & BLOCK_IS_MATERIALIZED);
    assert(block->flags & BLOCK_RUNTIME_ALLOCATED);
    // report_error(unit->ctx, &block->from, Format(temp, "entered block: %", block));
    For (block->imperative_order)
        if (!(block->inferred_expressions[*it].flags & INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME))
            run_expression(unit, storage, block, *it);
        // else
        //     report_error(unit->ctx, &block->parsed_expressions[*it], Format(temp, "SKIP block: %, kind: %", block, block->parsed_expressions[*it].kind));
    // report_error(unit->ctx, &block->to, Format(temp, "left block: %", block));
}


static void allocate_remaining_unit_storage(Unit* unit, Block* block)
{
    for (umm i = 0; i < block->inferred_expressions.count; i++)
    {
        Inferred_Expression* infer = &block->inferred_expressions[i];
        if (infer->called_block)
            allocate_remaining_unit_storage(unit, infer->called_block);

        if (infer->flags & INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME) continue;
        if (infer->offset != INVALID_STORAGE_OFFSET) continue;
        assert(infer->type != INVALID_TYPE);
        allocate_unit_storage(unit, infer->type, &infer->size, &infer->offset);
    }

    block->flags |= BLOCK_RUNTIME_ALLOCATED;
}

void run_unit(Unit* unit)
{
    byte* storage = alloc<byte>(NULL, unit->next_storage_offset);
    Defer(free(storage));
    allocate_remaining_unit_storage(unit, unit->entry_block);
    run_block(unit, storage, unit->entry_block);
}



ExitApplicationNamespace
