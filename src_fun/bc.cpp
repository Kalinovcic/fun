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
    return unit->next_storage_offset;
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

#define Op(opcode, ...)                     \
    {                                       \
        Bytecode_Operation op = (opcode);   \
        u64 r = 0, a = 0, b = 0, s = 0;     \
        { __VA_ARGS__; }                    \
        *reserve_item(&builder->bytecode) = { op, r, a, b, s }; \
    }

static u64 generate_bytecode_for_expression_rhs(Bytecode_Builder* builder, Expression id)
{
    Unit*  unit  = builder->unit;
    Block* block = builder->block;
    auto*  expr  = &block->parsed_expressions  [id];
    auto*  infer = &block->inferred_expressions[id];

    switch (expr->kind)
    {

    case EXPRESSION_ZERO:               Unreachable;
    case EXPRESSION_TRUE:               Unreachable;
    case EXPRESSION_FALSE:              Unreachable;
    case EXPRESSION_NUMERIC_LITERAL:    Unreachable;
    case EXPRESSION_STRING_LITERAL:     NotImplemented;
    case EXPRESSION_TYPE_LITERAL:       Unreachable;
    case EXPRESSION_BLOCK:              Unreachable;
    case EXPRESSION_UNIT:               Unreachable;

    case EXPRESSION_NAME:
    {
        Resolved_Name resolved = get(&block->resolved_names, &id);
        assert(resolved.scope);
        u64 offset;
        assert(get(&resolved.scope->declaration_placement, &resolved.declaration, &offset));
        return offset;
    } break;

    case EXPRESSION_MEMBER:             NotImplemented;
    case EXPRESSION_NEGATE:             NotImplemented;
    case EXPRESSION_ADDRESS:            NotImplemented;
    case EXPRESSION_DEREFERENCE:        NotImplemented;
    case EXPRESSION_SIZEOF:             Unreachable;
    case EXPRESSION_ALIGNOF:            Unreachable;
    case EXPRESSION_CODEOF:             NotImplemented;
    case EXPRESSION_DEBUG:              NotImplemented;
    case EXPRESSION_DEBUG_ALLOC:        NotImplemented;
    case EXPRESSION_DEBUG_FREE:         NotImplemented;

    case EXPRESSION_ASSIGNMENT:         NotImplemented;
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
    case EXPRESSION_GOTO_UNIT:          NotImplemented;
    case EXPRESSION_BRANCH:             NotImplemented;
    case EXPRESSION_CALL:               NotImplemented;
    case EXPRESSION_DECLARATION:        NotImplemented;

    }
}

static void generate_bytecode_for_block(Bytecode_Builder* builder, Block* block)
{
    Unit* unit = builder->unit;
    builder->block = block;
    block->first_instruction = builder->bytecode.count;

    // all non-entry blocks can return, so they need a return address
    if (block != unit->entry_block)
        block->return_address_offset = allocate_storage(unit, BLOCK_RETURN_ADDRESS_SIZE, sizeof(void*));

    For (block->imperative_order)
        if (!(block->inferred_expressions[*it].flags & INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME))
            generate_bytecode_for_expression_rhs(builder, *it);

    if (block == unit->entry_block)
        Op(OP_FINISH_UNIT)
    else
        Op(OP_GOTO_INDIRECT, r = block->return_address_offset)
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
        generate_bytecode_for_block(&builder, unit->entry_block);
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
