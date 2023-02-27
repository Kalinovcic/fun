#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"
#include <stdio.h>


EnterApplicationNamespace




#define TRACE 0


template <typename Base>
struct Boolean
{
    Base value;
    inline operator bool() { return value; }
    inline void operator=(bool v) { this->value = v ? 1 : 0; }
};

union Memory
{
    byte  base[0];
    byte* as_address;
    u8    as_u8;
    u16   as_u16;
    u32   as_u32;
    u64   as_u64;
    s8    as_s8;
    s16   as_s16;
    s32   as_s32;
    s64   as_s64;
    f32   as_f32;
    f64   as_f64;
    Boolean<u8>  as_bool8;
    Boolean<u16> as_bool16;
    Boolean<u32> as_bool32;
    Boolean<u64> as_bool64;
};

static void run_block(Unit* unit, byte* storage, Block* block);

static Memory* run_expression(Unit* unit, byte* storage, Block* block, Expression id)
{
    auto*   expr    = &block->parsed_expressions[id];
    auto*   infer   = &block->inferred_expressions[id];
    Memory* address = (Memory*)(storage + infer->offset);
    assert(!(infer->flags & INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME));

#if TRACE
    {
        String location = Report(unit->ctx).snippet(expr, false, 5, 5).return_without_reporting();
        int newlines = 1;
        for (umm i = 0; i < location.length; i++)
            if (location[i] == '\n')
                newlines++;
        for (int i = 0; i < 20; i++)
            printf("\x1b[0K\n");
        printf("\x1b[20A");
        printf("\n%.*s\n", StringArgs(location));
        printf("\x1b[%dA", newlines + 1);
        printf("\x1b[0K");
        rough_sleep(0.2);
    }
#endif

    // report_error(unit->ctx, expr, Format(temp, "block: %, kind: %", block, expr->kind));

    auto specialize = [](Type type, Memory* memory, auto&& lambda)
    {
        switch (type)
        {
        case TYPE_U8:     lambda(&memory->as_u8);     break;
        case TYPE_U16:    lambda(&memory->as_u16);    break;
        case TYPE_U32:    lambda(&memory->as_u32);    break;
        case TYPE_U64:    lambda(&memory->as_u64);    break;
        case TYPE_S8:     lambda(&memory->as_s8);     break;
        case TYPE_S16:    lambda(&memory->as_s16);    break;
        case TYPE_S32:    lambda(&memory->as_s32);    break;
        case TYPE_S64:    lambda(&memory->as_s64);    break;
        case TYPE_F32:    lambda(&memory->as_f32);    break;
        case TYPE_F64:    lambda(&memory->as_f64);    break;
        case TYPE_BOOL8:  lambda(&memory->as_bool8);  break;
        case TYPE_BOOL16: lambda(&memory->as_bool16); break;
        case TYPE_BOOL32: lambda(&memory->as_bool32); break;
        case TYPE_BOOL64: lambda(&memory->as_bool64); break;
        IllegalDefaultCase;
        }
    };

    auto specialize_numeric_binary = [&](auto&& lambda)
    {
        assert(block->inferred_expressions[expr->binary.lhs].type == infer->type);
        assert(block->inferred_expressions[expr->binary.rhs].type == infer->type);
        Memory* lhs = run_expression(unit, storage, block, expr->binary.lhs);
        Memory* rhs = run_expression(unit, storage, block, expr->binary.rhs);
        specialize(infer->type, address, [&](auto* v) { lambda(v, (decltype(v)) lhs, (decltype(v)) rhs); });
    };

    auto specialize_logic_binary = [&](auto&& lambda)
    {
        assert(infer->type == TYPE_BOOL8);
        assert(block->inferred_expressions[expr->binary.lhs].type == block->inferred_expressions[expr->binary.rhs].type);
        Memory* lhs = run_expression(unit, storage, block, expr->binary.lhs);
        Memory* rhs = run_expression(unit, storage, block, expr->binary.rhs);
        specialize(infer->type, lhs, [&](auto* lhs) { lambda(&address->as_bool8, lhs, (decltype(lhs)) rhs); });
    };

    switch (expr->kind)
    {

    case EXPRESSION_ZERO:                   Unreachable;
    case EXPRESSION_TRUE:                   Unreachable;
    case EXPRESSION_FALSE:                  Unreachable;
    case EXPRESSION_NUMERIC_LITERAL:        Unreachable;
    case EXPRESSION_BLOCK:                  Unreachable;

    case EXPRESSION_NAME: break;

    case EXPRESSION_NEGATE:
    {
        assert(block->inferred_expressions[expr->unary_operand].type == infer->type);
        Memory* op = run_expression(unit, storage, block, expr->unary_operand);
        specialize(infer->type, address, [&](auto* v) { *v = -*(decltype(v)) op; });
    } break;

    case EXPRESSION_ADDRESS:
    {
        assert(infer->size == sizeof(void*));
        address->as_address = (byte*) run_expression(unit, storage, block, expr->unary_operand);
    } break;

    case EXPRESSION_DEREFERENCE:
    {
        Memory* op = run_expression(unit, storage, block, expr->unary_operand);
        address = (Memory*) op->as_address;
    } break;

    case EXPRESSION_ASSIGNMENT:
    {
        u64 size = infer->size;
        assert(size == block->inferred_expressions[expr->binary.lhs].size);
        assert(size == block->inferred_expressions[expr->binary.rhs].size);

        Memory* rhs = run_expression(unit, storage, block, expr->binary.rhs);
        Memory* lhs = run_expression(unit, storage, block, expr->binary.lhs);
        memcpy(lhs, rhs, size);
        memcpy(address, lhs, size);
    } break;

    case EXPRESSION_ADD:                specialize_numeric_binary([](auto* v, auto* l, auto* r) { *v = *l + *r; }); break;
    case EXPRESSION_SUBTRACT:           specialize_numeric_binary([](auto* v, auto* l, auto* r) { *v = *l - *r; }); break;
    case EXPRESSION_MULTIPLY:           specialize_numeric_binary([](auto* v, auto* l, auto* r) { *v = *l * *r; }); break;

    // @Reconsider
    case EXPRESSION_DIVIDE_WHOLE:       specialize_numeric_binary([](auto* v, auto* l, auto* r) { *v = *l / *r; }); break;
    case EXPRESSION_DIVIDE_FRACTIONAL:  specialize_numeric_binary([](auto* v, auto* l, auto* r) { *v = *l / *r; }); break;

    case EXPRESSION_EQUAL:              specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l == *r; }); break;
    case EXPRESSION_NOT_EQUAL:          specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l != *r; }); break;
    case EXPRESSION_GREATER_THAN:       specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l >  *r; }); break;
    case EXPRESSION_GREATER_OR_EQUAL:   specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l >= *r; }); break;
    case EXPRESSION_LESS_THAN:          specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l <  *r; }); break;
    case EXPRESSION_LESS_OR_EQUAL:      specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l <= *r; }); break;

    case EXPRESSION_CAST:
    {
        if (infer->constant_index != INVALID_CONSTANT_INDEX)
        {
            if (is_integer_type(infer->type))
            {
                Fraction* fract = &block->constants[infer->constant_index];
                assert(fract_is_integer(fract));

                u64 abs = 0;
                bool ok = int_get_abs_u64(&abs, &fract->num);
                assert(ok);
                if (fract->num.negative)
                    abs = -abs;
                memcpy(address->base, &abs, infer->size);
            }
            else NotImplemented;
        }
        else
        {
            Memory* op = run_expression(unit, storage, block, expr->binary.rhs);
            specialize(infer->type, address, [&](auto* result)
            {
                specialize(block->inferred_expressions[expr->binary.rhs].type, op, [&](auto* value)
                {
                    *result = *value;
                });
            });
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
            if (!condition || !(expr->flags & EXPRESSION_BRANCH_IS_LOOP))
                break;
        }
    } break;

    case EXPRESSION_CALL:
    {
        Block* callee = infer->called_block;
        assert(callee);

        Expression_List const* args = expr->call.arguments;
        assert(args);

        umm parameter_index = 0;
        For (callee->imperative_order)
        {
            auto* param_expr  = &callee->parsed_expressions  [*it];
            auto* param_infer = &callee->inferred_expressions[*it];
            if (!(param_expr->flags & EXPRESSION_DECLARATION_IS_PARAMETER)) continue;
            Defer(parameter_index++);

            if (param_expr->flags & EXPRESSION_DECLARATION_IS_ALIAS) continue;

            assert(parameter_index < args->count);
            Expression arg_expression = args->expressions[parameter_index];
            umm size = block->inferred_expressions[arg_expression].size;
            assert(size == param_infer->size);
            assert(param_infer->offset != INVALID_STORAGE_OFFSET);

            Memory* arg = run_expression(unit, storage, block, arg_expression);
            Memory* param = (Memory*)(storage + param_infer->offset);
            memcpy(param, arg, size);
        }

        run_block(unit, storage, callee);
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

    case EXPRESSION_DEBUG:
    {
        auto* op_infer = &block->inferred_expressions[expr->unary_operand];
        switch (op_infer->type)
        {
        case TYPE_VOID:                 printf("void\n"); break;
        case TYPE_SOFT_ZERO:            printf("zero\n"); break;
        case TYPE_SOFT_NUMBER:
        {
            String str = fract_display(&block->constants[op_infer->constant_index]);
            printf("%.*s\n", StringArgs(str));
        } break;
        case TYPE_SOFT_BOOL:            printf("%s\n", op_infer->constant_bool ? "true" : "false"); break;
        case TYPE_SOFT_TYPE:
        {
            String type = exact_type_description(unit, op_infer->constant_type);
            printf("%.*s\n", StringArgs(type));
        } break;
        case TYPE_SOFT_BLOCK:
        {
            Token_Info info;
            {
                Token_Info* from_info = get_token_info(unit->ctx, &op_infer->constant_block.parsed_child->from);
                Token_Info* to_info   = get_token_info(unit->ctx, &op_infer->constant_block.parsed_child->to);
                info = *from_info;
                info.length = to_info->offset + to_info->length - from_info->offset;
            }
            String location = Report(unit->ctx).snippet(info, false, 0, 0).return_without_reporting();
            printf("<soft block>\n%.*s\n", StringArgs(location));
        } break;
        default:
            Memory* value = run_expression(unit, storage, block, expr->unary_operand);
            if (is_pointer_type(op_infer->type))
                printf("%p\n", value->as_address);
            else
                specialize(op_infer->type, value, [](auto* v)
                {
                    String text = Format(temp, "%", *v);
                    printf("%.*s\n", StringArgs(text));
                });
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
    //     else
    //         report_error(unit->ctx, &block->parsed_expressions[*it], Format(temp, "SKIP block: %, kind: %", block, block->parsed_expressions[*it].kind));
    // report_error(unit->ctx, &block->to, Format(temp, "left block: %", block));
}


static void allocate_remaining_unit_storage(Unit* unit, Block* block)
{
    for (umm i = 0; i < block->inferred_expressions.count; i++)
    {
        auto* expr  = &block->parsed_expressions[i];
        auto* infer = &block->inferred_expressions[i];
        if (infer->called_block)
            allocate_remaining_unit_storage(unit, infer->called_block);

        if (infer->flags & INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME) continue;
        if (infer->offset != INVALID_STORAGE_OFFSET) continue;
        assert(infer->type != INVALID_TYPE);

        if (infer->flags & INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE)
        {
            infer->size = get_type_size(unit, infer->type);
            continue;
        }
        allocate_unit_storage(unit, infer->type, &infer->size, &infer->offset);
    }

    block->flags |= BLOCK_RUNTIME_ALLOCATED;
}

void run_unit(Unit* unit)
{
    allocate_remaining_unit_storage(unit, unit->entry_block);
    byte* storage = alloc<byte>(NULL, unit->next_storage_offset);
    Defer(free(storage));

    run_block(unit, storage, unit->entry_block);

#if TRACE
    for (int i = 0; i < 20; i++)
        printf("\x1b[0K\n");
    printf("\x1b[20A");
#endif
}



ExitApplicationNamespace
