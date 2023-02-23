#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
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

    block->inferred_expressions = allocate_array<Inferred_Expression>(&unit->memory, block->parsed_expressions.count);
    For (block->inferred_expressions)
    {
        it->constant_index = INVALID_CONSTANT_INDEX;
        it->type           = INVALID_TYPE;
        it->size           = INVALID_STORAGE_SIZE;
        it->offset         = INVALID_STORAGE_OFFSET;
    }

    block->parent_scope                  = parent_scope;
    block->parent_scope_visibility_limit = parent_scope_visibility_limit;

    Pipeline_Task task = {};
    task.unit  = unit;
    task.block = block;
    add_item(&unit->ctx->pipeline, &task);
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
            if (expr->declaration.name.atom != name->atom) continue;
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



static u64 get_type_size(Unit* unit, Type type)
{
    if (get_indirection(type))
        NotImplemented;  // @Reconsider - how do we handle pointer sizes?

    if (is_user_defined_type(type))
        NotImplemented;

    switch (type)
    {
    case TYPE_VOID:                return 0;
    case TYPE_SOFT_ZERO:           Unreachable;
    case TYPE_U8:                  return 1;
    case TYPE_U16:                 return 2;
    case TYPE_U32:                 return 4;
    case TYPE_U64:                 return 8;
    case TYPE_S8:                  return 1;
    case TYPE_S16:                 return 2;
    case TYPE_S32:                 return 4;
    case TYPE_S64:                 return 8;
    case TYPE_SOFT_INTEGER:        Unreachable;
    case TYPE_F16:                 return 2;
    case TYPE_F32:                 return 4;
    case TYPE_F64:                 return 8;
    case TYPE_SOFT_FLOATING_POINT: Unreachable;
    case TYPE_BOOL8:               return 1;
    case TYPE_BOOL16:              return 2;
    case TYPE_BOOL32:              return 4;
    case TYPE_BOOL64:              return 8;
    case TYPE_SOFT_BOOL:           Unreachable;
    IllegalDefaultCase;
    }
}

static u64 get_type_alignment(Unit* unit, Type type)
{
    if (get_indirection(type))
        NotImplemented;  // @Reconsider - how do we handle pointer sizes?

    if (is_user_defined_type(type))
        NotImplemented;

    // For primitives, the alignment is the same as their size.
    u64 size = get_type_size(unit, type);
    if (size == 0)
        size = 1;
    return size;
}

void allocate_unit_storage(Unit* unit, Type type, u64* out_size, u64* out_offset)
{
    u64 size      = get_type_size     (unit, type);
    u64 alignment = get_type_alignment(unit, type);

    while (unit->next_storage_offset % alignment)
        unit->next_storage_offset++;

    *out_size   = size;
    *out_offset = unit->next_storage_offset;
    unit->next_storage_offset += size;
}



enum Yield_Result
{
    YIELD_COMPLETED,
    YIELD_MADE_PROGRESS,
    YIELD_NO_PROGRESS,
    YIELD_ERROR,
};

static Yield_Result check_expression(Pipeline_Task* task, Expression id)
{
    Unit*  unit  = task->unit;
    Block* block = task->block;

    auto add_constant_integer = [&](Integer* integer) -> u32
    {
        u32 index = task->constants.count;
        add_item(&task->constants, integer);
        return index;
    };

    auto* expr  = &block->parsed_expressions[id];
    auto* infer = &block->inferred_expressions[id];
    assert(infer->type == INVALID_TYPE);


    #define Infer(t)                                                  \
    {                                                                 \
        assert(t != INVALID_TYPE);                                    \
        if (t == TYPE_SOFT_ZERO)                                      \
            assert(expr->kind == EXPRESSION_ZERO);                    \
        else if (is_soft_type(t))                                     \
            assert(infer->constant_index != INVALID_CONSTANT_INDEX);  \
        infer->type = t;                                              \
        return YIELD_COMPLETED;                                       \
    }

    #define Wait() return YIELD_NO_PROGRESS;

    #define Error(...) return (report_error(unit->ctx, &(expr)->from, Format(temp, ##__VA_ARGS__)), YIELD_ERROR)

    switch (expr->kind)
    {

    case EXPRESSION_ZERO:                   Infer(TYPE_SOFT_ZERO);
    case EXPRESSION_TRUE:                   Infer(TYPE_SOFT_BOOL);
    case EXPRESSION_FALSE:                  Infer(TYPE_SOFT_BOOL);
    case EXPRESSION_INTEGER_LITERAL:
    {
        Token_Info_Integer* token_info = (Token_Info_Integer*) get_token_info(unit->ctx, &expr->literal);

        Integer integer = int_clone(&token_info->value);
        infer->constant_index = add_constant_integer(&integer);
        Infer(TYPE_SOFT_INTEGER);
    } break;

    case EXPRESSION_FLOATING_POINT_LITERAL:
        NotImplemented;

    case EXPRESSION_NAME:
    {
        Block*     decl_scope;
        Expression decl_id;
        if (!find_declaration(unit, &expr->name.token, block, expr->visibility_limit, &decl_scope, &decl_id))
            Error("Can't find name '%'.", get_identifier(unit->ctx, &expr->name.token));

        Inferred_Expression* decl_infer = &decl_scope->inferred_expressions[decl_id];
        if (decl_infer->type == INVALID_TYPE) Wait();

        assert(decl_infer->size   != INVALID_STORAGE_SIZE);
        assert(decl_infer->offset != INVALID_STORAGE_OFFSET);
        infer->size   = decl_infer->size;
        infer->offset = decl_infer->offset;
        Infer(decl_infer->type);
    } break;

    case EXPRESSION_NEGATE:
    {
        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE) Wait();

        if (!is_numeric_type(op_infer->type))
            Error("Unary operator '-' expects a numeric argument.");

        if (op_infer->type == TYPE_SOFT_INTEGER)
        {
            Integer integer = int_clone(&task->constants[op_infer->constant_index]);
            int_negate(&integer);
            infer->constant_index = add_constant_integer(&integer);
        }
        else if (op_infer->type == TYPE_SOFT_FLOATING_POINT)
        {
            NotImplemented;
        }
        else assert(!is_soft_type(op_infer->type));

        Infer(op_infer->type);
    } break;

    case EXPRESSION_CAST:
    {
        if (!is_primitive_type(expr->cast.parsed_type)) NotImplemented;
        Type cast_type = expr->cast.parsed_type;
        auto* op_infer = &block->inferred_expressions[expr->cast.value];
        if (op_infer->type == INVALID_TYPE) Wait();

        if (op_infer->type == TYPE_SOFT_INTEGER)
        {
            if (!is_integer_type(cast_type))
                NotImplemented;

            u64 target_size = get_type_size(unit, cast_type);

            Integer const* integer = &task->constants[op_infer->constant_index];
            if (integer->negative)
            {
                if (is_unsigned_integer_type(cast_type))
                    Error("The cast value operand is a negative constant, it can't be cast to an unsigned type.");

                Integer min = {};
                int_set_zero(&min);
                int_set_bit(&min, target_size * 8 - 1);
                if (int_compare_abs(integer, &min) > 0)
                    Error("The cast value operand doesn't fit in the specified type.");
            }
            else
            {
                u64 max_log2 = (target_size * 8) - (is_signed_integer_type(cast_type) ? 1 : 0);
                if (int_log2_abs(integer) >= max_log2)
                    Error("The cast value operand doesn't fit in the specified type.");
            }

            Integer copy = int_clone(integer);
            infer->constant_index = add_constant_integer(&copy);
        }
        else if (op_infer->type == TYPE_SOFT_FLOATING_POINT)
        {
            NotImplemented;
        }
        else if (op_infer->type == TYPE_SOFT_BOOL)
        {
            NotImplemented;
        }
        else
        {
            assert(!is_soft_type(op_infer->type));
            // @Incomplete - check if cast is possible
        }

        Infer(cast_type);
    } break;

    case EXPRESSION_DECLARATION:
    {
        Type type = expr->declaration.parsed_type;
        if (!is_primitive_type(type)) NotImplemented;
        allocate_unit_storage(unit, type, &infer->size, &infer->offset);
        Infer(type);
    } break;

    case EXPRESSION_ASSIGNMENT:
    {
        if (block->parsed_expressions[expr->binary.lhs].kind != EXPRESSION_NAME)
            Error("Expected a variable name on the left side of the assignment.");

        Type lhs_type = block->inferred_expressions[expr->binary.lhs].type;
        Type rhs_type = block->inferred_expressions[expr->binary.rhs].type;
        if (lhs_type == INVALID_TYPE || rhs_type == INVALID_TYPE) Wait();
        if (lhs_type != rhs_type)
            Error("LHS and RHS types don't match.");
        Infer(lhs_type);
    } break;

    case EXPRESSION_ADD:
    case EXPRESSION_SUBTRACT:
    case EXPRESSION_MULTIPLY:
    case EXPRESSION_DIVIDE:
    {
        Type lhs_type = block->inferred_expressions[expr->binary.lhs].type;
        Type rhs_type = block->inferred_expressions[expr->binary.rhs].type;
        if (lhs_type == INVALID_TYPE || rhs_type == INVALID_TYPE) Wait();
        if (lhs_type != rhs_type)
            Error("LHS and RHS types don't match.");
        if (!is_numeric_type(lhs_type))
            Error("Expected a numeric operand.");

        if (lhs_type == TYPE_SOFT_INTEGER)
        {
            Integer const* lhs_int = &task->constants[block->inferred_expressions[expr->binary.lhs].constant_index];
            Integer const* rhs_int = &task->constants[block->inferred_expressions[expr->binary.rhs].constant_index];
            Integer result = int_clone(lhs_int);
                 if (expr->kind == EXPRESSION_ADD)      int_add(&result, rhs_int);
            else if (expr->kind == EXPRESSION_SUBTRACT) int_sub(&result, rhs_int);
            else if (expr->kind == EXPRESSION_MULTIPLY) int_mul(&result, rhs_int);
            else if (expr->kind == EXPRESSION_DIVIDE)
            {
                Integer mod = {};
                Defer(int_free(&mod));
                if (!int_div(&result, rhs_int, &mod))
                    Error("Division by zero!");
            }
            infer->constant_index = add_constant_integer(&result);
        }
        else if (lhs_type == TYPE_SOFT_FLOATING_POINT)
        {
            NotImplemented;
        }
        else assert(!is_soft_type(lhs_type));

        Infer(lhs_type);
    } break;

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

static Yield_Result check_block(Pipeline_Task* task)
{
    Unit*  unit  = task->unit;
    Block* block = task->block;

    bool made_progress = false;
    bool waiting       = false;
    for (Expression id = (Expression) 0; id < block->parsed_expressions.count; id = (Expression)(id + 1)) Loop(expression_loop)
    {
        if (block->inferred_expressions[id].type != INVALID_TYPE) continue;

        Yield_Result result = check_expression(task, id);
        if (result == YIELD_COMPLETED || result == YIELD_MADE_PROGRESS)
            made_progress = true;
        else if (result == YIELD_NO_PROGRESS)
            waiting = true;
        else if (result == YIELD_ERROR)
            return YIELD_ERROR;
        else Unreachable;
    }

    if (waiting)
        return made_progress ? YIELD_MADE_PROGRESS : YIELD_NO_PROGRESS;
    block->constants = allocate_array(&unit->memory, &task->constants);
    free_heap_array(&task->constants);

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
    return unit;
}


bool pump_pipeline(Compiler* ctx)
{
    while (true)
    {
        bool made_progress = false;
        bool tried_to_make_progress = false;
        for (umm it_index = 0; it_index < ctx->pipeline.count; it_index++)
        {
            if (!ctx->pipeline[it_index].block) continue;  // already completed
            tried_to_make_progress = true;

            Yield_Result result = check_block(&ctx->pipeline[it_index]);
            switch (result)
            {
            case YIELD_COMPLETED:       ctx->pipeline[it_index].block = NULL;  // fallthrough
            case YIELD_MADE_PROGRESS:   made_progress = true;                   // fallthrough
            case YIELD_NO_PROGRESS:     break;
            case YIELD_ERROR:           return false;
            IllegalDefaultCase;
            }
        }

        if (!tried_to_make_progress)
            break;

        if (!made_progress)
            return report_error_locationless(ctx, "Can't progress typechecking."_s);
    }

    return true;
}



ExitApplicationNamespace
