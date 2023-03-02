#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"
#include <stdio.h>


EnterApplicationNamespace



static u64 allocate_storage(Unit* unit, u64 size, u64 alignment)
{
    if (unit->storage_alignment < alignment)
        unit->storage_alignment = alignment;
    while (unit->next_storage_offset % alignment)
        unit->next_storage_offset++;
    u64 offset = unit->next_storage_offset;
    unit->next_storage_offset += size;
    return offset;
}

static u64 allocate_storage(Unit* unit, Type type)
{
    return allocate_storage(unit, get_type_size(unit, type), get_type_alignment(unit, type));
}

static void place_variables(Unit* unit, Block* block)
{
    for (umm i = 0; i < block->inferred_expressions.count; i++)
    {
        auto* expr  = &block->parsed_expressions  [i];
        auto* infer = &block->inferred_expressions[i];
        if (expr->kind != EXPRESSION_DECLARATION) continue;
        assert(infer->type != INVALID_TYPE);
        if (is_soft_type(infer->type)) continue;

        Expression id = (Expression) i;
        u64 offset = allocate_storage(unit, infer->type);
        set(&block->declaration_placement, &id, &offset);
    }

    For (block->inferred_expressions)
        if (it->called_block)
            place_variables(unit, it->called_block);
}

static constexpr umm UNIT_RETURN_ADDRESS_SIZE  = 3 * sizeof(void*);
static constexpr umm BLOCK_RETURN_ADDRESS_SIZE = 1 * sizeof(void*);

struct Bytecode_Builder
{
    Unit*                  unit;
    Block*                 block;
    Concatenator<Bytecode> bytecode;
};

#define Op(opcode, ...)                                               \
    {                                                                 \
        u64 r = 0, a = 0, b = 0, s = 0; { __VA_ARGS__; }              \
        *reserve_item(&builder->bytecode) = { (opcode), r, a, b, s }; \
    }

struct Location
{
    Type type;
    bool indirect;
    u64  offset;

    inline Location(u64 offset, Type type, bool indirect): type(type), indirect(indirect), offset(offset) {}
};

static Location void_location(Type type_assertion)
{
    assert(type_assertion == TYPE_VOID);
    return Location(0, TYPE_VOID, false);
}

static Location allocate_location(Bytecode_Builder* builder, Type type)
{
    u64 offset = allocate_storage(builder->unit, type);
    return Location(offset, type, false);
}

static void copy(Bytecode_Builder* builder, Location to, Location from)
{
    Unit* unit = builder->unit;
    assert(to.type == from.type);

    Bytecode_Operation op;
         if (to.indirect && from.indirect) op = OP_COPY_BETWEEN_INDIRECT;
    else if (to.indirect)                  op = OP_COPY_TO_INDIRECT;
    else if (from.indirect)                op = OP_COPY_FROM_INDIRECT;
    else                                   op = OP_COPY;
    Op(op, r = to.offset, a = from.offset, s = get_type_size(unit, to.type));
}

static Location direct(Bytecode_Builder* builder, Location location)
{
    if (!location.indirect)
        return location;
    Location result = allocate_location(builder, location.type);
    copy(builder, result, location);
    return result;
}

static Location generate_expression(Bytecode_Builder* builder, Expression id)
{
    Unit*  unit  = builder->unit;
    Block* block = builder->block;
    auto*  expr  = &block->parsed_expressions  [id];
    auto*  infer = &block->inferred_expressions[id];

    assert(!(infer->flags & INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME));

    switch (expr->kind)
    {
    IllegalDefaultCase;
    case EXPRESSION_ZERO:               Unreachable;  // locationless
    case EXPRESSION_TRUE:               Unreachable;  // locationless
    case EXPRESSION_FALSE:              Unreachable;  // locationless
    case EXPRESSION_NUMERIC_LITERAL:    Unreachable;  // locationless
    case EXPRESSION_TYPE_LITERAL:       Unreachable;  // locationless
    case EXPRESSION_BLOCK:              Unreachable;  // locationless
    case EXPRESSION_UNIT:               Unreachable;  // locationless
    case EXPRESSION_SIZEOF:             Unreachable;  // locationless
    case EXPRESSION_ALIGNOF:            Unreachable;  // locationless

    case EXPRESSION_STRING_LITERAL:
    {
        assert(expr->literal.atom == ATOM_STRING_LITERAL);
        Token_Info_String* token = (Token_Info_String*) get_token_info(unit->ctx, &expr->literal);

        assert(get_type_size(unit, TYPE_STRING) == sizeof(String));
        Location location = allocate_location(builder, TYPE_STRING);
        Op(OP_LITERAL_INDIRECT, r = location.offset, a = (umm) &token->value, s = sizeof(String));
        return location;
    } break;

    case EXPRESSION_NAME:
    {
        Resolved_Name resolved = get(&block->resolved_names, &id);
        assert(resolved.scope);
        u64 offset;
        assert(get(&resolved.scope->declaration_placement, &resolved.declaration, &offset));
        return Location(offset, infer->type, false);
    } break;

    case EXPRESSION_MEMBER:             NotImplemented;
    case EXPRESSION_NEGATE:             NotImplemented;

    case EXPRESSION_ADDRESS:
    {
        Location operand = generate_expression(builder, expr->unary_operand);
        if (operand.indirect)
            return Location(operand.offset, infer->type, false);
        Location location = allocate_location(builder, infer->type);
        Op(OP_ADDRESS, r = location.offset, a = operand.offset);
        return location;
    } break;

    case EXPRESSION_DEREFERENCE:
    {
        Location operand = direct(builder, generate_expression(builder, expr->unary_operand));
        return Location(operand.offset, infer->type, true);
    } break;

    case EXPRESSION_CODEOF:
    {
        auto* op_infer = &block->inferred_expressions[expr->unary_operand];
        Type unit_type = is_user_defined_type(op_infer->type)
                       ? op_infer->type
                       : *get_constant_type(block, expr->unary_operand);
        Unit* literal = get_user_type_data(unit->ctx, unit_type)->unit;

        assert(get_type_size(unit, infer->type) == sizeof(Unit*));
        Location location = allocate_location(builder, infer->type);
        Op(OP_LITERAL, r = location.offset, a = (umm) literal, s = sizeof(Unit*));
        return location;
    } break;

    case EXPRESSION_DEBUG:
    {
        auto* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (is_soft_type(op_infer->type))
        {
            NotImplemented;
        }
        else
        {
            Location value = direct(builder, generate_expression(builder, expr->unary_operand));
            Op(OP_DEBUG_PRINT, r = value.offset, s = value.type);
        }

        return void_location(infer->type);
    } break;

    case EXPRESSION_DEBUG_ALLOC:        NotImplemented;
    case EXPRESSION_DEBUG_FREE:         NotImplemented;

    case EXPRESSION_ASSIGNMENT:
    {
        Location lhs = generate_expression(builder, expr->binary.lhs);
        Location rhs = generate_expression(builder, expr->binary.rhs);
        copy(builder, lhs, rhs);
        return rhs;
    } break;

    case EXPRESSION_ADD:                NotImplemented;
    case EXPRESSION_SUBTRACT:           NotImplemented;
    case EXPRESSION_MULTIPLY:           NotImplemented;
    case EXPRESSION_DIVIDE_WHOLE:       NotImplemented;
    case EXPRESSION_DIVIDE_FRACTIONAL:  NotImplemented;
    case EXPRESSION_POINTER_ADD:        NotImplemented;
    case EXPRESSION_POINTER_SUBTRACT:   NotImplemented;
    case EXPRESSION_EQUAL:              NotImplemented;
    case EXPRESSION_NOT_EQUAL:          NotImplemented;
    case EXPRESSION_GREATER_THAN:       NotImplemented;
    case EXPRESSION_GREATER_OR_EQUAL:   NotImplemented;
    case EXPRESSION_LESS_THAN:          NotImplemented;
    case EXPRESSION_LESS_OR_EQUAL:      NotImplemented;
    case EXPRESSION_CAST:               NotImplemented;

    case EXPRESSION_GOTO_UNIT:
    {
        Location lhs = direct(builder, generate_expression(builder, expr->unary_operand));
        Location rhs = direct(builder, generate_expression(builder, expr->unary_operand));
        Op(OP_SWITCH_UNIT, r = lhs.offset, a = rhs.offset);
        return void_location(infer->type);
    } break;

    case EXPRESSION_BRANCH:             NotImplemented;

    case EXPRESSION_CALL:
    {
        Block* callee = infer->called_block;
        Expression_List const* args = expr->call.arguments;

        umm parameter_index = 0;
        For (callee->imperative_order)
        {
            auto* param_expr = &callee->parsed_expressions[*it];
            if (!(param_expr->flags & EXPRESSION_DECLARATION_IS_PARAMETER)) continue;
            Defer(parameter_index++);
            if (param_expr->flags & EXPRESSION_DECLARATION_IS_ALIAS) continue;

            u64 param_offset;
            assert(get(&callee->declaration_placement, it, &param_offset));
            Location parameter(param_offset, callee->inferred_expressions[*it].type, false);

            assert(parameter_index < args->count);
            Location argument = generate_expression(builder, args->expressions[parameter_index]);
            copy(builder, parameter, argument);
        }

        Op(OP_CALL, r = (umm) callee);  // :CallPlaceholder
        return void_location(infer->type);
    } break;

    case EXPRESSION_DECLARATION:
    {
        u64 offset;
        assert(get(&block->declaration_placement, &id, &offset));

        Location location(offset, infer->type, false);
        if (expr->declaration.value == NO_EXPRESSION)
        {
            Op(OP_ZERO, r = offset, s = get_type_size(unit, infer->type));
        }
        else
        {
            Location value = generate_expression(builder, expr->declaration.value);
            copy(builder, location, value);
        }
        return location;
    } break;

    }

    Unreachable;
}

static void generate_block(Bytecode_Builder* builder, Block* block)
{
    Unit* unit = builder->unit;
    builder->block = block;
    block->first_instruction = builder->bytecode.count;

    // all non-entry blocks can return, so they need a return address
    if (block != unit->entry_block)
        block->return_address_offset = allocate_storage(unit, BLOCK_RETURN_ADDRESS_SIZE, sizeof(void*));

    For (block->imperative_order)
        if (!(block->inferred_expressions[*it].flags & INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME))
            generate_expression(builder, *it);

    if (block == unit->entry_block)
        Op(OP_FINISH_UNIT)
    else
        Op(OP_GOTO_INDIRECT, r = block->return_address_offset)

    For (block->inferred_expressions)
        if (it->called_block)
            generate_block(builder, it->called_block);
}

static void fix_placeholders(Unit* unit)
{
    For (unit->bytecode)
    {
        if (it->op == OP_CALL)
        {
            Block* callee = (Block*)(umm)(it->r);  // :CallPlaceholder
            assert(callee != unit->entry_block);
            it->r = callee->first_instruction;
            it->a = callee->return_address_offset;
        }
    }
}

void generate_bytecode_for_unit_placement(Unit* unit)
{
    assert(unit->next_storage_offset == 0);
    if (!(unit->flags & UNIT_IS_STRUCT))
    {
        // all non-struct units' storage begins with a return address
        unit->next_storage_offset += UNIT_RETURN_ADDRESS_SIZE;
    }

    place_variables(unit, unit->entry_block);

    if (!(unit->flags & UNIT_IS_STRUCT))
    {
        Bytecode_Builder builder = {};
        builder.unit = unit;
        generate_block(&builder, unit->entry_block);
        unit->bytecode = resolve_to_array_and_free(&builder.bytecode, &unit->memory);

        fix_placeholders(unit);
    }
}

void generate_bytecode_for_unit_completion(Unit* unit)
{
    if (!(unit->flags & UNIT_IS_STRUCT))
    {
        unit->compiled_bytecode = true;
    }
}



ExitApplicationNamespace
