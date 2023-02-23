#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"
#include <stdio.h>


EnterApplicationNamespace



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

static Memory* run_expression(Unit* unit, byte* storage, Block* block, Expression id)
{
    auto*   expr    = &block->parsed_expressions[id];
    auto*   infer   = &block->inferred_expressions[id];
    Memory* address = (Memory*)(storage + infer->offset);

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
            Memory* op = run_expression(unit, storage, block, expr->cast.value);
            memcpy(address->base, op->base, infer->size);
        }
    } break;

    case EXPRESSION_DECLARATION:
    {
        // @Incomplete - sizes
        address->as_u64 = 0;
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
    case EXPRESSION_SUBTRACT: NotImplemented;
    case EXPRESSION_MULTIPLY: NotImplemented;
    case EXPRESSION_DIVIDE: NotImplemented;
    case EXPRESSION_EQUAL: NotImplemented;
    case EXPRESSION_NOT_EQUAL: NotImplemented;
    case EXPRESSION_GREATER_THAN: NotImplemented;
    case EXPRESSION_GREATER_OR_EQUAL: NotImplemented;
    case EXPRESSION_LESS_THAN: NotImplemented;
    case EXPRESSION_LESS_OR_EQUAL: NotImplemented;
    case EXPRESSION_CALL: NotImplemented;

    IllegalDefaultCase;
    }

    return address;
}

static void run_block(Unit* unit, byte* storage, Block* block)
{
    For (block->parsed_statements)
    {
        switch (it->kind)
        {

        case STATEMENT_EXPRESSION:
        {
            run_expression(unit, storage, block, it->expression);
        } break;

        case STATEMENT_IF:
        {
            Memory* value = run_expression(unit, storage, block, it->expression);
            assert(block->inferred_expressions[it->expression].size == 8);
            if (value->as_u64)
                run_block(unit, storage, block->children_blocks[it->true_block].child);
            else if (it->false_block != NO_BLOCK)
                run_block(unit, storage, block->children_blocks[it->false_block].child);
        } break;

        case STATEMENT_WHILE:
        {
            assert(block->inferred_expressions[it->expression].size == 8);
            while (run_expression(unit, storage, block, it->expression)->as_u64)
                run_block(unit, storage, block->children_blocks[it->true_block].child);
        } break;

        case STATEMENT_DEBUG_OUTPUT:
        {
            Memory* value = run_expression(unit, storage, block, it->expression);
            switch (block->inferred_expressions[it->expression].type)
            {
            case TYPE_U8:                   printf("%llu\n", (unsigned long long) value->as_u8);  break;
            case TYPE_U16:                  printf("%llu\n", (unsigned long long) value->as_u16); break;
            case TYPE_U32:                  printf("%llu\n", (unsigned long long) value->as_u32); break;
            case TYPE_U64:                  printf("%llu\n", (unsigned long long) value->as_u64); break;
            case TYPE_S8:                   printf("%lld\n", (long long) value->as_s8);  break;
            case TYPE_S16:                  printf("%lld\n", (long long) value->as_s16); break;
            case TYPE_S32:                  printf("%lld\n", (long long) value->as_s32); break;
            case TYPE_S64:                  printf("%lld\n", (long long) value->as_s64); break;
            case TYPE_SOFT_INTEGER:         printf("<soft integer>\n"); break;
            case TYPE_F16:                  printf("<f16>\n"); break;
            case TYPE_F32:                  printf("%f\n", (double) value->as_f32); break;
            case TYPE_F64:                  printf("%f\n", (double) value->as_f64); break;
            case TYPE_SOFT_FLOATING_POINT:  printf("<soft floating point>\n"); break;
            case TYPE_BOOL8:                printf("%s\n", value->as_u8  ? "true" : "false"); break;
            case TYPE_BOOL16:               printf("%s\n", value->as_u16 ? "true" : "false"); break;
            case TYPE_BOOL32:               printf("%s\n", value->as_u32 ? "true" : "false"); break;
            case TYPE_BOOL64:               printf("%s\n", value->as_u64 ? "true" : "false"); break;
            case TYPE_SOFT_BOOL:            printf("<soft bool>\n"); break;
            IllegalDefaultCase;
            }
        } break;

        IllegalDefaultCase;
        }
    }
}


static void allocate_remaining_unit_storage(Unit* unit, Block* block)
{
    for (umm i = 0; i < block->inferred_expressions.count; i++)
    {
        Inferred_Expression* infer = &block->inferred_expressions[i];
        if (infer->offset != INVALID_STORAGE_OFFSET) continue;
        assert(infer->type != INVALID_TYPE);
        allocate_unit_storage(unit, infer->type, &infer->size, &infer->offset);
    }

    For (block->children_blocks)
        allocate_remaining_unit_storage(unit, it->child);
}

void run_unit(Unit* unit)
{
    allocate_remaining_unit_storage(unit, unit->entry_block);

    byte* storage = alloc<byte>(NULL, unit->next_storage_offset);
    Defer(free(storage));
    run_block(unit, storage, unit->entry_block);
}



ExitApplicationNamespace
