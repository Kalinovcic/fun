#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "api.h"
#include <stdio.h>


EnterApplicationNamespace



static Block* materialize_block(Unit* unit, Block* materialize_from,
                                Block* parent_scope, Statement parent_scope_visibility_limit)
{
    Block* block = PushValue(&unit->memory, Block);

    block->materialized_from  = materialize_from;
    block->parsed_expressions = materialize_from->parsed_expressions;
    block->parsed_statements  = materialize_from->parsed_statements;

    block->inferred_types = allocate_array<Type>(&unit->memory, block->parsed_expressions.count);
    For (block->inferred_types)
        *it = TYPE_INVALID;

    block->parent_scope                  = parent_scope;
    block->parent_scope_visibility_limit = parent_scope_visibility_limit;

    Pipeline_Task task = {};
    task.block = block;
    add_item(&unit->pipeline, &task);
    return block;
}



bool find_declaration(Unit* unit, Token const* name,
                      Block* scope, Statement visibility_limit,
                      Block** out_decl_scope, Expression* out_decl_expr)
{
    while (scope)
    {
        for (Expression id = (Expression) 0; id < scope->parsed_expressions.count; id = (Expression)(id + 1))
        {
            auto* expr = &scope->parsed_expressions[id];
            if (expr->kind != EXPRESSION_DECLARATION) continue;
            if (expr->name.atom != name->atom) continue;
            if (expr->visibility_limit >= visibility_limit) break;

            *out_decl_scope = scope;
            *out_decl_expr = id;
            return true;
        }

        visibility_limit = scope->parent_scope_visibility_limit;
        scope            = scope->parent_scope;
    }
    return false;
}


enum Yield_Result
{
    YIELD_COMPLETED,
    YIELD_MADE_PROGRESS,
    YIELD_NO_PROGRESS,
    YIELD_ERROR,
};

static Yield_Result check_block(Unit* unit, Block* block)
{
    bool made_progress = false;
    bool waiting       = false;

#define InferredType(id) (block->inferred_types[id] != TYPE_INVALID)
    for (Expression id = (Expression) 0; id < block->parsed_expressions.count; id = (Expression)(id + 1)) Loop(expression_loop)
    {
        if (InferredType(id)) continue;
        auto* expr = &block->parsed_expressions[id];

#define Infer(type) { block->inferred_types[id] = type; made_progress = true; ContinueLoop(expression_loop); }
#define Wait()      {                                   waiting       = true; ContinueLoop(expression_loop); }
#define MaybeInfer(type) { auto _t = type; if (_t == TYPE_INVALID) Wait() else Infer(_t) }
#define Error(...) return (report_error(unit->ctx, &(expr)->from, Format(temp, ##__VA_ARGS__)), YIELD_ERROR)
        switch (expr->kind)
        {

        case EXPRESSION_ZERO:                   Infer(TYPE_SOFT_ZERO);
        case EXPRESSION_TRUE:                   Infer(TYPE_SOFT_BOOL);
        case EXPRESSION_FALSE:                  Infer(TYPE_SOFT_BOOL);
        case EXPRESSION_INTEGER_LITERAL:        Infer(TYPE_SOFT_INTEGER);
        case EXPRESSION_FLOATING_POINT_LITERAL: Infer(TYPE_SOFT_FLOATING_POINT);

        case EXPRESSION_NAME:
        {
            Block*     decl_scope;
            Expression decl_id;
            if (!find_declaration(unit, &expr->name, block, expr->visibility_limit, &decl_scope, &decl_id))
                Error("Can't find name '%'.", get_identifier(unit->ctx, &expr->name));
            MaybeInfer(decl_scope->inferred_types[decl_id]);
        } break;

        case EXPRESSION_NEGATE:
            MaybeInfer(block->inferred_types[expr->unary_operand]);

        case EXPRESSION_CAST:
        {
            if (!is_primitive_type(expr->parsed_type)) NotImplemented;
            if (!InferredType(expr->unary_operand)) Wait();
            // @Incomplete - check if cast is possible
            Infer(expr->parsed_type);
        } break;

        case EXPRESSION_DECLARATION:
        {
            if (!is_primitive_type(expr->parsed_type)) NotImplemented;
            Infer(expr->parsed_type);
        } break;

        case EXPRESSION_ASSIGNMENT:
        {
            if (block->parsed_expressions[expr->binary_lhs].kind != EXPRESSION_NAME)
                Error("Expected a variable name on the left side of the assignment.");

            Type lhs_type = block->inferred_types[expr->binary_lhs];
            Type rhs_type = block->inferred_types[expr->binary_rhs];
            if (lhs_type == TYPE_INVALID || rhs_type == TYPE_INVALID) Wait();

            // @Incomplete - implicit conversions
            if (lhs_type != rhs_type)
                Error("LHS and RHS types don't match.");

            Infer(block->inferred_types[expr->binary_lhs]);
        } break;

        case EXPRESSION_ADD:              NotImplemented;
        case EXPRESSION_SUBTRACT:         NotImplemented;
        case EXPRESSION_MULTIPLY:         NotImplemented;
        case EXPRESSION_DIVIDE:           NotImplemented;
        case EXPRESSION_EQUAL:            NotImplemented;
        case EXPRESSION_NOT_EQUAL:        NotImplemented;
        case EXPRESSION_GREATER_THAN:     NotImplemented;
        case EXPRESSION_GREATER_OR_EQUAL: NotImplemented;
        case EXPRESSION_LESS_THAN:        NotImplemented;
        case EXPRESSION_LESS_OR_EQUAL:    NotImplemented;

        case EXPRESSION_CALL: NotImplemented;

        IllegalDefaultCase;
        }
#undef Infer
#undef Wait
#undef Error

        Unreachable;
    }

    if (waiting)
        return made_progress ? YIELD_MADE_PROGRESS : YIELD_NO_PROGRESS;

    // typecheck statements
    // @Incomplete

    // materialize children blocks
    auto parsed_children = block->materialized_from->children_blocks;
    Array<Child_Block> children = allocate_array<Child_Block>(&unit->memory, parsed_children.count);
    for (umm i = 0; i < children.count; i++)
    {
        children[i]       = parsed_children[i];
        children[i].child = materialize_block(unit, children[i].child, block, children[i].visibility_limit);
    }
    block->children_blocks = const_array(children);

    return YIELD_COMPLETED;
}

static bool pump_pipeline(Unit* unit)
{
    while (true)
    {
        bool made_progress = false;
        bool tried_to_make_progress = false;
        for (umm it_index = 0; it_index < unit->pipeline.count; it_index++)
        {
            Block* block = unit->pipeline[it_index].block;
            if (!block) continue;  // already completed
            tried_to_make_progress = true;

            Yield_Result result = check_block(unit, block);
            switch (result)
            {
            case YIELD_COMPLETED:       unit->pipeline[it_index].block = NULL;  // fallthrough
            case YIELD_MADE_PROGRESS:   made_progress = true;                   // fallthrough
            case YIELD_NO_PROGRESS:     break;
            case YIELD_ERROR:           return false;
            IllegalDefaultCase;
            }
        }

        if (!tried_to_make_progress)
            break;

        if (!made_progress)
            return report_error(unit->ctx, &unit->initiator_from, "Can't progress for unit materialized here."_s);
    }

    return true;
}


Unit* materialize_unit(Compiler* ctx, Run* initiator)
{
    Unit* unit;
    {
        Region memory = {};
        unit = PushValue(&memory, Unit);
        unit->memory = memory;
    }

    unit->ctx            = ctx;
    unit->initiator_from = initiator->from;
    unit->initiator_to   = initiator->to;
    unit->initiator_run  = initiator;
    unit->entry_block    = materialize_block(unit, initiator->entry_block, NULL, (Statement) 0);

    if (pump_pipeline(unit))
        return unit;

    Region memory = unit->memory;
    lk_region_free(&memory);
    return NULL;
}


bool check_unit(Unit* unit)
{
    return true;
}



ExitApplicationNamespace
