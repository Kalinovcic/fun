#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"
#include <stdio.h>


EnterApplicationNamespace




#define TRACE 0

union Memory
{
    byte  base[0];
    byte* as_address;
    u8    as_u8;
    u16   as_u16;
    u32   as_u32;
    u64   as_u64;
    u64   as_umm;
    s8    as_s8;
    s16   as_s16;
    s32   as_s32;
    s64   as_s64;
    smm   as_smm;
    f32   as_f32;
    f64   as_f64;
    struct
    {
        u8 value;
        inline operator bool()        { return value; }
        inline void operator=(bool v) { value = v ? 1 : 0; }
    } as_bool;
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
        case TYPE_UMM:    lambda(&memory->as_umm);    break;
        case TYPE_S8:     lambda(&memory->as_s8);     break;
        case TYPE_S16:    lambda(&memory->as_s16);    break;
        case TYPE_S32:    lambda(&memory->as_s32);    break;
        case TYPE_S64:    lambda(&memory->as_s64);    break;
        case TYPE_SMM:    lambda(&memory->as_smm);    break;
        case TYPE_F32:    lambda(&memory->as_f32);    break;
        case TYPE_F64:    lambda(&memory->as_f64);    break;
        case TYPE_BOOL:   lambda(&memory->as_bool);   break;
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
        assert(infer->type == TYPE_BOOL);
        assert(block->inferred_expressions[expr->binary.lhs].type == block->inferred_expressions[expr->binary.rhs].type);
        Memory* lhs = run_expression(unit, storage, block, expr->binary.lhs);
        Memory* rhs = run_expression(unit, storage, block, expr->binary.rhs);
        specialize(infer->type, lhs, [&](auto* lhs) { lambda(&address->as_bool, lhs, (decltype(lhs)) rhs); });
    };

    switch (expr->kind)
    {

    case EXPRESSION_ZERO:                   Unreachable;
    case EXPRESSION_TRUE:                   Unreachable;
    case EXPRESSION_FALSE:                  Unreachable;
    case EXPRESSION_NUMERIC_LITERAL:        Unreachable;

    case EXPRESSION_STRING_LITERAL:
    {
        assert(expr->literal.atom == ATOM_STRING_LITERAL);
        Token_Info_String* token = (Token_Info_String*) get_token_info(unit->ctx, &expr->literal);
        assert(infer->size == sizeof(String));
        *(String*) address = token->value;
    } break;

    case EXPRESSION_BLOCK:                  Unreachable;

    case EXPRESSION_NAME: break;

    case EXPRESSION_MEMBER:
    {
        Memory* lhs = run_expression(unit, storage, block, expr->member.lhs);
        if (is_pointer_type(block->inferred_expressions[expr->member.lhs].type))
            lhs = (Memory*) lhs->as_address;
        address = (Memory*)((byte*) lhs + infer->offset);
    } break;

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

    case EXPRESSION_SIZEOF: Unreachable;
    case EXPRESSION_CODEOF:
    {
        auto* op_infer = &block->inferred_expressions[expr->unary_operand];
        Type unit_type;
        if (is_user_defined_type(op_infer->type))
            unit_type = op_infer->type;
        else
            unit_type = *get_constant_type(block, expr->unary_operand);
        Unit* result = get_user_type_data(unit->ctx, unit_type)->unit;
        assert(result);
        address->as_address = (byte*) result;
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
            String str = fract_display(get_constant_number(block, expr->unary_operand));
            printf("%.*s\n", StringArgs(str));
        } break;
        case TYPE_SOFT_BOOL:            printf("%s\n", *get_constant_bool(block, expr->unary_operand) ? "true" : "false"); break;
        case TYPE_SOFT_TYPE:
        {
            String type = exact_type_description(unit, *get_constant_type(block, expr->unary_operand));
            printf("%.*s\n", StringArgs(type));
        } break;
        case TYPE_SOFT_BLOCK:
        {
            Token_Info info;
            {
                Soft_Block constant_block = *get_constant_block(block, expr->unary_operand);
                Token_Info* from_info = get_token_info(unit->ctx, &constant_block.parsed_child->from);
                Token_Info* to_info   = get_token_info(unit->ctx, &constant_block.parsed_child->to);
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
            else if (op_infer->type == TYPE_TYPE)
                printf("type id %u\n", value->as_u32);
            else if (op_infer->type == TYPE_STRING)
                printf("%.*s\n", StringArgs(*(String*) value));
            else
                specialize(op_infer->type, value, [](auto* v)
                {
                    String text = Format(temp, "%", *v);
                    printf("%.*s\n", StringArgs(text));
                });
        }
    } break;

    case EXPRESSION_DEBUG_ALLOC:
    {
        Type type = infer->type;
        assert(is_pointer_type(type));
        Type to_alloc = set_indirection(type, get_indirection(type) - 1);
        umm size = get_type_size(unit, to_alloc);
        // @Reconsider - what about alignment?
        address->as_address = alloc<byte>(NULL, size);
    } break;

    case EXPRESSION_DEBUG_FREE:
    {
        assert(block->inferred_expressions[expr->unary_operand].size == sizeof(void*));
        Memory* op = run_expression(unit, storage, block, expr->unary_operand);
        free(op->as_address);
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

    case EXPRESSION_POINTER_ADD:
    {
        auto* lhs_infer = &block->inferred_expressions[expr->binary.lhs];
        auto* rhs_infer = &block->inferred_expressions[expr->binary.rhs];
        assert(infer    ->size == sizeof(umm));
        assert(lhs_infer->size == sizeof(umm));
        assert(rhs_infer->size == sizeof(umm));

        Memory* lhs = run_expression(unit, storage, block, expr->binary.lhs);
        Memory* rhs = run_expression(unit, storage, block, expr->binary.rhs);

        if (is_pointer_type(lhs_infer->type))
        {
            Type pointer_type = lhs_infer->type;
            Type element_type = set_indirection(pointer_type, get_indirection(pointer_type) - 1);
            umm  element_size = get_type_size(unit, element_type);
            address->as_umm = lhs->as_umm + rhs->as_umm * element_size;
        }
        else
        {
            Type pointer_type = rhs_infer->type;
            Type element_type = set_indirection(pointer_type, get_indirection(pointer_type) - 1);
            umm  element_size = get_type_size(unit, element_type);
            address->as_umm = rhs->as_umm + lhs->as_umm * element_size;
        }
    } break;

    case EXPRESSION_POINTER_SUBTRACT: NotImplemented;

    case EXPRESSION_EQUAL:              specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l == *r; }); break;
    case EXPRESSION_NOT_EQUAL:          specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l != *r; }); break;
    case EXPRESSION_GREATER_THAN:       specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l >  *r; }); break;
    case EXPRESSION_GREATER_OR_EQUAL:   specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l >= *r; }); break;
    case EXPRESSION_LESS_THAN:          specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l <  *r; }); break;
    case EXPRESSION_LESS_OR_EQUAL:      specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l <= *r; }); break;

    case EXPRESSION_CAST:
    {
        if (is_soft_type(block->inferred_expressions[expr->binary.rhs].type))
        {
            if (is_integer_type(infer->type))
            {
                Fraction const* fract = get_constant_number(block, expr->binary.rhs);
                assert(fract);
                assert(fract_is_integer(fract));

                u64 abs = 0;
                bool ok = int_get_abs_u64(&abs, &fract->num);
                assert(ok);
                if (fract->num.negative)
                    abs = -abs;
                memcpy(address->base, &abs, infer->size);
            }
            else if (infer->type == TYPE_TYPE)
            {
                assert(infer->size == 4);
                address->as_u32 = *get_constant_type(block, expr->binary.rhs);
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

    case EXPRESSION_GOTO_UNIT:
    {
        assert(block->inferred_expressions[expr->binary.lhs].size == sizeof(void*));
        assert(block->inferred_expressions[expr->binary.rhs].size == sizeof(void*));
        Memory* rhs = run_expression(unit, storage, block, expr->binary.rhs);
        Memory* lhs = run_expression(unit, storage, block, expr->binary.lhs);
        run_unit((Unit*) lhs->as_address, rhs->as_address);
    } break;

    case EXPRESSION_BRANCH:
    {
        if (expr->flags & EXPRESSION_BRANCH_IS_BAKED)
        {
            if (expr->branch.on_success != NO_EXPRESSION && block->inferred_expressions[expr->branch.on_success].flags & INFERRED_EXPRESSION_CONDITION_ENABLED)
                run_expression(unit, storage, block, expr->branch.on_success);
            else if (expr->branch.on_failure != NO_EXPRESSION && block->inferred_expressions[expr->branch.on_failure].flags & INFERRED_EXPRESSION_CONDITION_ENABLED)
                run_expression(unit, storage, block, expr->branch.on_failure);
        }
        else if (expr->branch.condition == NO_EXPRESSION)
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

    IllegalDefaultCase;
    }

    return address;
}

static void run_block(Unit* unit, byte* storage, Block* block)
{
    assert(block->flags & BLOCK_IS_MATERIALIZED);
    assert(block->flags & BLOCK_PLACEMENT_COMPLETED);
    // report_error(unit->ctx, &block->from, Format(temp, "entered block: %", block));
    For (block->imperative_order)
        if (!(block->inferred_expressions[*it].flags & INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME))
            run_expression(unit, storage, block, *it);
    //     else
    //         report_error(unit->ctx, &block->parsed_expressions[*it], Format(temp, "SKIP block: %, kind: %", block, block->parsed_expressions[*it].kind));
    // report_error(unit->ctx, &block->to, Format(temp, "left block: %", block));
}


void run_unit(Unit* unit, byte* storage)
{
    run_block(unit, storage, unit->entry_block);
}

void run_unit(Unit* unit)
{
    byte* storage = alloc<byte>(NULL, unit->next_storage_offset);
    Defer(free(storage));
    run_unit(unit, storage);

#if TRACE
    for (int i = 0; i < 20; i++)
        printf("\x1b[0K\n");
    printf("\x1b[20A");
#endif
}



ExitApplicationNamespace
