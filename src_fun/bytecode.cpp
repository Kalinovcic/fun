#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "api.h"
#include <stdio.h>


EnterApplicationNamespace



union Runtime_Value
{
    u64 integer;
};

static Runtime_Value* run_expression(Unit* unit, Block* block, Expression id)
{
    auto*          expr  = &block->parsed_expressions[id];
    Runtime_Value* value = &block->values[id];

    switch (expr->kind)
    {

    case EXPRESSION_ZERO: NotImplemented;
    case EXPRESSION_TRUE: NotImplemented;
    case EXPRESSION_FALSE: NotImplemented;

    case EXPRESSION_INTEGER_LITERAL:
        value->integer = ((Token_Info_Integer*) get_token_info(unit->ctx, &expr->literal))->value;
        break;

    case EXPRESSION_FLOATING_POINT_LITERAL: NotImplemented;

    case EXPRESSION_NAME:
    {
        Block*     decl_scope;
        Expression decl_id;
        bool found = find_declaration(unit, &expr->name, block, expr->visibility_limit, &decl_scope, &decl_id);
        assert(found);
        value = &decl_scope->values[decl_id];
    } break;

    case EXPRESSION_NEGATE: NotImplemented;

    case EXPRESSION_CAST:
    {
        // @Incomplete - do casts
        *value = *run_expression(unit, block, expr->unary_operand);
    } break;

    case EXPRESSION_DECLARATION:
    {
        *value = {};
    } break;

    case EXPRESSION_ASSIGNMENT:
    {
        Runtime_Value* lhs = run_expression(unit, block, expr->binary_lhs);
        Runtime_Value* rhs = run_expression(unit, block, expr->binary_rhs);
        *lhs = *rhs;
        *value = *lhs;
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

    return value;
}

static void run_block(Unit* unit, Block* block)
{
    if (!block->values.count)
        block->values = allocate_array<Runtime_Value>(&unit->memory, block->parsed_expressions.count);

    For (block->parsed_statements)
    {
        switch (it->kind)
        {

        case STATEMENT_EXPRESSION:
        {
            run_expression(unit, block, it->expression);
        } break;

        case STATEMENT_IF:
        {
            Runtime_Value* value = run_expression(unit, block, it->expression);
            if (value->integer)
                run_block(unit, block->children_blocks[it->true_block].child);
            else if (it->false_block != NO_BLOCK)
                run_block(unit, block->children_blocks[it->false_block].child);
        } break;

        case STATEMENT_WHILE:
        {
            while (run_expression(unit, block, it->expression)->integer)
                run_block(unit, block->children_blocks[it->true_block].child);
        } break;

        case STATEMENT_DEBUG_OUTPUT:
        {
            Runtime_Value* value = run_expression(unit, block, it->expression);
            printf("%llu\n", (unsigned long long) value->integer);
        } break;

        IllegalDefaultCase;
        }
    }
}

void run_unit(Unit* unit)
{
    run_block(unit, unit->entry_block);
}



ExitApplicationNamespace
