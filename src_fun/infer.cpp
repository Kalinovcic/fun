#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"
#include <stdio.h>


EnterApplicationNamespace


#define STRESS_TEST 1

#if STRESS_TEST
static Random rng = {};
GlobalBlock
{
    u64 seed64 = get_command_line_integer("seed"_s);
    if (!seed64)
    {
        seed(&rng);
        seed64 = next_u64(&rng);
    }

    fprintf(stderr, "using random seed %llu\n", (unsigned long long) seed64);
    seed(&rng, 6010837357958729528ull, 1);
};
#endif


static Block* materialize_block(Unit* unit, Block* materialize_from,
                                Block* parent_scope, Visibility parent_scope_visibility_limit)
{
    Block* block = PushValue(&unit->memory, Block);
    *block = *materialize_from;
    assert(!(materialize_from->flags & BLOCK_IS_MATERIALIZED));

    block->flags |= BLOCK_IS_MATERIALIZED;
    block->materialized_by_unit = unit;
    block->materialized_from    = materialize_from;

    block->inferred_expressions = allocate_array<Inferred_Expression>(&unit->memory, block->parsed_expressions.count);
    For (block->inferred_expressions)
    {
        it->constant = INVALID_CONSTANT;
        it->type     = INVALID_TYPE;
    }

    block->parent_scope                  = parent_scope;
    block->parent_scope_visibility_limit = parent_scope_visibility_limit;

    assert(!(unit->flags & UNIT_IS_PLACED));
    assert(unit->blocks_not_ready_for_placement != 0 || unit->materialized_block_count == 0);
    assert(unit->blocks_not_completed != 0           || unit->materialized_block_count == 0);
    unit->blocks_not_ready_for_placement++;
    unit->blocks_not_completed++;

    unit->materialized_block_count++;
    unit->most_recent_materialized_block = block;

    Pipeline_Task task = {};
    task.kind  = PIPELINE_TASK_INFER_BLOCK;
    task.unit  = unit;
    task.block = block;
    add_item(&unit->ctx->pipeline, &task);
    return block;
}



User_Type* get_user_type_data(Compiler* ctx, Type type)
{
    assert(type >= TYPE_FIRST_USER_TYPE);
    return &ctx->user_types[type - TYPE_FIRST_USER_TYPE];
}

static Type create_user_type(Compiler* ctx, Unit* unit)
{
    Type type = (Type)(TYPE_FIRST_USER_TYPE + ctx->user_types.count);
    User_Type* data = reserve_item(&ctx->user_types);
    data->unit                  = unit;
    return type;
}

static bool is_user_type_sizeable(Compiler* ctx, Type type)
{
    assert(is_user_defined_type(type));
    User_Type* data = get_user_type_data(ctx, type);
    assert(data->unit);
    return data->unit->flags & UNIT_IS_PLACED;
}


String vague_type_description(Unit* unit, Type type, bool point_out_soft_types)
{
    if (is_pointer_type(type))
        return "a pointer value"_s;

    if (point_out_soft_types)
    {
        switch (type)
        {
        case TYPE_SOFT_NUMBER: return "a compile-time number"_s;
        case TYPE_SOFT_BOOL:   return "a compile-time bool"_s;
        case TYPE_SOFT_TYPE:   return "a compile-time type"_s;
        }
    }

    switch (type)
    {
    // run-time values
    case TYPE_VOID:        return "a void value"_s;
    case TYPE_U8:  case TYPE_U16: case TYPE_U32: case TYPE_U64: case TYPE_UMM:
    case TYPE_S8:  case TYPE_S16: case TYPE_S32: case TYPE_S64: case TYPE_SMM:
    case TYPE_F16: case TYPE_F32: case TYPE_F64:
    case TYPE_SOFT_NUMBER: return "a numeric value"_s;
    case TYPE_BOOL:
    case TYPE_SOFT_BOOL:   return "a bool value"_s;
    case TYPE_TYPE:
    case TYPE_SOFT_TYPE:   return "a type value"_s;
    case TYPE_SOFT_ZERO:   return "a zero"_s;
    case TYPE_SOFT_BLOCK:  return "a block"_s;
    }

    assert(is_user_defined_type(type));
    return "a unit"_s;
}

String vague_type_description_in_compile_time_context(Unit* unit, Type type)
{
    return vague_type_description(unit, type, /* point_out_soft_types */ true);
}

String exact_type_description(Unit* unit, Type type)
{
    if (is_pointer_type(type))
    {
        umm indirection = get_indirection(type);
        String stars = {};
        while (indirection--)
            FormatAppend(&stars, temp, "&");
        return concatenate(temp, stars, exact_type_description(unit, get_base_type(type)));
    }

    switch (type)
    {
    // run-time values
    case TYPE_VOID:        return "void"_s;
    case TYPE_U8:          return "u8"_s;
    case TYPE_U16:         return "u16"_s;
    case TYPE_U32:         return "u32"_s;
    case TYPE_U64:         return "u64"_s;
    case TYPE_UMM:         return "umm"_s;
    case TYPE_S8:          return "s8"_s;
    case TYPE_S16:         return "s16"_s;
    case TYPE_S32:         return "s32"_s;
    case TYPE_S64:         return "s64"_s;
    case TYPE_SMM:         return "smm"_s;
    case TYPE_F16:         return "f16"_s;
    case TYPE_F32:         return "f32"_s;
    case TYPE_F64:         return "f64"_s;
    case TYPE_SOFT_NUMBER: return "compile-time number"_s;
    case TYPE_BOOL:        return "bool"_s;
    case TYPE_SOFT_BOOL:   return "compile-time bool"_s;
    case TYPE_TYPE:        return "type"_s;
    case TYPE_SOFT_TYPE:   return "compile-time type"_s;
    case TYPE_SOFT_ZERO:   return "zero"_s;
    case TYPE_SOFT_BLOCK:  return "block"_s;
    }

    assert(is_user_defined_type(type));
    User_Type* data = get_user_type_data(unit->ctx, type);
    if (data->has_alias)
        return get_identifier(unit->ctx, &data->alias);

    // @ErrorReporting more detail, what unit?
    return "unit"_s;
}



static Find_Result find_declaration_internal(
        Compiler* ctx, Token const* name,
        Block* scope, Visibility visibility_limit,
        Block** out_decl_scope, Expression* out_decl_expr,
        Dynamic_Array<Resolved_Name::Use>* out_use_chain,
        bool allow_parent_traversal,
        bool allow_alias_using_traversal,
        Dynamic_Array<Block*>* visited_scopes)
{
    while (scope)
    {
        For (*visited_scopes)
            if (*it == scope)
                return FIND_FAILURE;
        add_item(visited_scopes, &scope);

        // 1: try to find normal declarations
        for (Expression id = {}; id < scope->parsed_expressions.count; id = (Expression)(id + 1))
        {
            auto* expr = &scope->parsed_expressions[id];
            if (expr->kind != EXPRESSION_DECLARATION) continue;

            if (expr->declaration.name.atom != name->atom) continue;
            if ((expr->flags & EXPRESSION_DECLARATION_IS_ORDERED) &&
                (expr->visibility_limit >= visibility_limit || visibility_limit == NO_VISIBILITY))
                continue;

            *out_decl_scope = scope;
            *out_decl_expr = id;
            return FIND_SUCCESS;
        }

        // 2: try to find declarations from a used scope
        if (out_use_chain)
        {
            for (Expression id = {}; id < scope->parsed_expressions.count; id = (Expression)(id + 1))
            {
                auto* expr = &scope->parsed_expressions[id];
                if (expr->kind != EXPRESSION_DECLARATION) continue;
                if (!(expr->flags & EXPRESSION_DECLARATION_IS_USING)) continue;

                auto* infer = &scope->inferred_expressions[id];
                if (infer->type == INVALID_TYPE)
                {
                    *out_decl_scope = scope;
                    *out_decl_expr  = id;
                    return FIND_WAIT;
                }

                Type user_type = infer->type;
                Visibility using_visibility = ALL_VISIBILITY;
                if (user_type == TYPE_SOFT_TYPE && allow_alias_using_traversal)
                {
                    Type const* constant_type = get_constant_type(scope, id);
                    if (!constant_type)
                    {
                        *out_decl_scope = scope;
                        *out_decl_expr  = id;
                        return FIND_WAIT;
                    }
                    user_type = *constant_type;
                    using_visibility = NO_VISIBILITY;
                }

                if (!is_user_defined_type(user_type)) continue;
                User_Type* data = get_user_type_data(ctx, user_type);
                assert(data->unit);

                Find_Result result = find_declaration_internal(ctx, name, data->unit->entry_block, using_visibility,
                                                               out_decl_scope, out_decl_expr, out_use_chain,
                                                               /* allow_parent_traversal */ false,
                                                               /* allow_alias_using_traversal */ false,
                                                               visited_scopes);
                if (result == FIND_FAILURE) continue;
                if (result == FIND_WAIT) return FIND_WAIT;
                assert(result == FIND_SUCCESS);

                Resolved_Name::Use use = { scope, id };
                insert_item(out_use_chain, &use, 0);
                return FIND_SUCCESS;
            }
        }

        // 3: try to recurse to parent scope
        if (!allow_parent_traversal)
            break;
        visibility_limit = scope->parent_scope_visibility_limit;
        scope            = scope->parent_scope;
    }
    return FIND_FAILURE;
}

Find_Result find_declaration(Compiler* ctx, Token const* name,
                             Block* scope, Visibility visibility_limit,
                             Block** out_decl_scope, Expression* out_decl_expr,
                             Dynamic_Array<Resolved_Name::Use>* out_use_chain,
                             bool allow_parent_traversal)
{
    Dynamic_Array<Block*> visited_scopes = {};
    Defer(free_heap_array(&visited_scopes));
    return find_declaration_internal(ctx, name, scope, visibility_limit,
                                     out_decl_scope, out_decl_expr, out_use_chain,
                                     allow_parent_traversal, /* allow_alias_using_traversal */ true,
                                     &visited_scopes);
}


static umm edit_distance(String a, String b)
{
    if (!a) return b.length;
    if (!b) return a.length;

    if (a[0] == b[0])
        return edit_distance(consume(a, 1), consume(b, 1));

    umm option1 = edit_distance(consume(a, 1), b);
    umm option2 = edit_distance(a, consume(b, 1));
    umm option3 = edit_distance(consume(a, 1), consume(b, 1));

    umm best = option1;
    if (best > option2) best = option2;
    if (best > option3) best = option3;
    return 1 + best;
}

static void helpful_error_for_missing_name(Compiler* ctx, String base_error, Token const* name,
                                           Block* scope, Visibility visibility_limit,
                                           bool allow_parent_traversal)
{

    Token const* best_alternative = NULL;
    umm          best_distance    = UMM_MAX;

    String identifier = get_identifier(ctx, name);
    while (scope)
    {
        for (Expression id = {}; id < scope->parsed_expressions.count; id = (Expression)(id + 1))
        {
            auto* expr = &scope->parsed_expressions[id];
            if (expr->kind != EXPRESSION_DECLARATION) continue;

            Token const* decl_name = &expr->declaration.name;
            if (decl_name->atom == name->atom)
            {
                String hint = "Maybe you are referring to this, but you don't have visibility because of imperative order."_s;
                if (visibility_limit == NO_VISIBILITY)
                    hint = "Maybe you are referring to this, but you don't have visibility because it's in an outer scope."_s;
                Report(ctx).part(name, base_error)
                           .part(name, Format(temp, "Can't find name '%'.", identifier)).part(decl_name, hint)
                           .done();
                return;
            }

            String other_identifier = get_identifier(ctx, decl_name);
            umm distance = edit_distance(identifier, other_identifier);
            if (best_distance > distance && distance < identifier.length / 3)
            {
                best_distance = distance;
                best_alternative = decl_name;
            }
        }

        if (!allow_parent_traversal)
            break;

        visibility_limit = scope->parent_scope_visibility_limit;
        scope            = scope->parent_scope;
    }

    if (best_alternative)
    {
        Report(ctx).part(name, base_error)
                   .part(best_alternative, Format(temp, "Maybe you meant '%'?", get_identifier(ctx, best_alternative)))
                   .done();
        return;
    }

    Report(ctx).part(name, base_error).done();
    return;
}




u64 get_type_size(Unit* unit, Type type)
{
    if (get_indirection(type))
        return unit->pointer_size;

    if (is_user_defined_type(type))
    {
        User_Type* data = get_user_type_data(unit->ctx, type);
        assert(data->unit);
        assert(data->unit->flags & UNIT_IS_PLACED);
        return data->unit->storage_size;
    }

    switch (type)
    {
    case TYPE_VOID:        return 0;
    case TYPE_SOFT_ZERO:   Unreachable;
    case TYPE_U8:          return 1;
    case TYPE_U16:         return 2;
    case TYPE_U32:         return 4;
    case TYPE_U64:         return 8;
    case TYPE_UMM:         return unit->pointer_size;
    case TYPE_S8:          return 1;
    case TYPE_S16:         return 2;
    case TYPE_S32:         return 4;
    case TYPE_S64:         return 8;
    case TYPE_SMM:         return unit->pointer_size;
    case TYPE_F16:         return 2;
    case TYPE_F32:         return 4;
    case TYPE_F64:         return 8;
    case TYPE_SOFT_NUMBER: Unreachable;
    case TYPE_BOOL:        return 1;
    case TYPE_SOFT_BOOL:   Unreachable;
    case TYPE_TYPE:        return 4;
    case TYPE_SOFT_TYPE:   Unreachable;
    IllegalDefaultCase;
    }
}

u64 get_type_alignment(Unit* unit, Type type)
{
    if (get_indirection(type))
        return unit->pointer_alignment;

    if (is_user_defined_type(type))
    {
        User_Type* data = get_user_type_data(unit->ctx, type);
        assert(data->unit);
        assert(data->unit->flags & UNIT_IS_PLACED);
        return data->unit->storage_alignment;
    }

    // For primitives, the alignment is the same as their size.
    u64 size = get_type_size(unit, type);
    if (size == 0)
        size = 1;
    return size;
}


static void complete_expression(Block* block, Expression id)
{
    auto* infer = &block->inferred_expressions[id];
    assert(infer->type != INVALID_TYPE);
    if (is_soft_type(infer->type) && infer->type != TYPE_SOFT_ZERO)
        assert(infer->constant != INVALID_CONSTANT);
    infer->flags |= INFERRED_EXPRESSION_COMPLETED_INFERENCE;
    remove(&block->waiting_expressions, &id);
}

static void set_inferred_type(Block* block, Expression id, Type type)
{
    auto* expr  = &block->parsed_expressions[id];
    auto* infer = &block->inferred_expressions[id];

    assert(!(infer->flags & INFERRED_EXPRESSION_COMPLETED_INFERENCE));
    assert(type != INVALID_TYPE);
    assert(infer->type == INVALID_TYPE || infer->type == type);

    if (type == TYPE_SOFT_ZERO)
        assert(expr->kind == EXPRESSION_ZERO);
    if (is_soft_type(type))
    {
        infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
        infer->flags |= INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE;
    }
    infer->type = type;
}



Constant* get_constant(Block* block, Expression expr, Type type_assertion)
{
    auto* infer = &block->inferred_expressions[expr];
    assert(infer->type == type_assertion);
    if (infer->constant == INVALID_CONSTANT)
        return NULL;
    return &block->constants[infer->constant];
}

void set_constant(Block* block, Expression expr, Type type_assertion, Constant* value)
{
    auto* infer = &block->inferred_expressions[expr];
    assert(!(infer->flags & INFERRED_EXPRESSION_COMPLETED_INFERENCE));
    assert(infer->type == type_assertion);
    assert(infer->constant == INVALID_CONSTANT);
    infer->constant = block->constants.count;
    add_item(&block->constants, value);
}


static void place_unit(Unit* unit)
{
    assert(!(unit->flags & UNIT_IS_PLACED));

    generate_bytecode_for_unit_placement(unit);

    if (!unit->storage_alignment)
        unit->storage_alignment = 1;
    unit->storage_size = unit->next_storage_offset;
    while (unit->storage_size % unit->storage_alignment)
        unit->storage_size++;
    unit->flags |= UNIT_IS_PLACED;
}



static bool copy_constant(Compiler* ctx, Block* to_block, Expression to_id, Block* from_block, Expression from_id, Type type_assertion, Token const* alias = NULL)
{
    Constant* constant_ptr = get_constant(from_block, from_id, type_assertion);
    if (!constant_ptr) return false;
    Constant constant = *constant_ptr;

    switch (type_assertion)
    {

    case TYPE_SOFT_NUMBER:
        constant.number = fract_clone(&constant.number);
        break;

    case TYPE_SOFT_BOOL:
        break;

    case TYPE_SOFT_TYPE:
        if (alias && is_user_defined_type(constant.type))
        {
            User_Type* data = get_user_type_data(ctx, constant.type);
            if (!data->has_alias)
            {
                data->has_alias = true;
                data->alias = *alias;
            }
        }
        break;

    case TYPE_SOFT_BLOCK:
        if (alias && !constant.block.has_alias)
        {
            constant.block.has_alias = true;
            constant.block.alias = *alias;
        }
        break;

    IllegalDefaultCase;
    }

    set_constant(to_block, to_id, type_assertion, &constant);
    return true;
}

static bool check_constant_fits_in_runtime_type(Unit* unit, Parsed_Expression const* expr, Fraction const* fraction, Type type)
{
    assert(!is_soft_type(type));
    assert(is_numeric_type(type));
    String name = exact_type_description(unit, type);

    if (!is_integer_type(type))
        NotImplemented;

#define Error(...) return (report_error(unit->ctx, expr, Format(temp, ##__VA_ARGS__)), false)
    if (!fract_is_integer(fraction))
        Error("The number is fractional, it can't fit in %.\n"
              "    value: %",
              exact_type_description(unit, type), fract_display(fraction));

    u64 target_size = get_type_size(unit, type);

    Integer const* integer = &fraction->num;
    if (integer->negative)
    {
        if (is_unsigned_integer_type(type))
            Error("The number doesn't fit in %.\n"
                  "    value: %\n"
                  "  minimum: 0 (lowest number that fits in %)",
                  name, fract_display(fraction), name);

        Integer min = {};
        int_set_zero(&min);
        int_set_bit(&min, target_size * 8 - 1);
        int_negate(&min);
        if (int_compare(integer, &min) < 0)
            Error("The number doesn't fit in %.\n"
                  "    value: %\n"
                  "  minimum: % (lowest number that fits in %)",
                  name, fract_display(fraction), int_base10(&min), name);
    }
    else
    {
        u64 max_log2 = (target_size * 8) - (is_signed_integer_type(type) ? 1 : 0);
        u64 max_value = (((1ull << (max_log2 - 1)) - 1) << 1) | 1;  // clown behavior because << 64 doesn't work
        if (int_log2_abs(integer) >= max_log2)
            Error("The number doesn't fit in %.\n"
                  "    value: %\n"
                  "  maximum: % (greatest number that fits in %)",
                  name, fract_display(fraction), max_value, name);
    }
#undef Error

    return true;
}

static bool pattern_matching_inference(Unit* unit, Block* block, Expression id, Type type,
                                       Parsed_Expression const* inferred_from, Type full_inferred_type)
{
    assert(type != INVALID_TYPE);
    assert(!is_soft_type(type));

    auto* expr  = &block->parsed_expressions[id];
    auto* infer = &block->inferred_expressions[id];
    assert(expr->flags & EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED);
    assert(infer->type == INVALID_TYPE);

    switch (expr->kind)
    {

    case EXPRESSION_DECLARATION:
    {
        if (expr->flags & EXPRESSION_DECLARATION_IS_INFERRED_ALIAS)
        {
            assert(expr->declaration.type  == NO_EXPRESSION);
            assert(expr->declaration.value == NO_EXPRESSION);
            set_inferred_type(block, id, TYPE_SOFT_TYPE);
            set_constant_type(block, id, type);
            complete_expression(block, id);
        }
        else if (expr->flags & EXPRESSION_DECLARATION_IS_ALIAS)
        {
            assert(expr->declaration.type  == NO_EXPRESSION);
            assert(expr->declaration.value != NO_EXPRESSION);
            if (!pattern_matching_inference(unit, block, expr->declaration.value, type, inferred_from, full_inferred_type))
                return false;
            set_inferred_type(block, id, TYPE_SOFT_TYPE);
            set_constant_type(block, id, type);
            complete_expression(block, id);
        }
        else Unreachable;
    } break;

    case EXPRESSION_ADDRESS:
    {
        if (!is_pointer_type(type))
        {
            String full_type = exact_type_description(unit, full_inferred_type);
            Report(unit->ctx).part(expr, "Can't match the type to the expected pattern."_s)
                             .part(inferred_from, Format(temp, "The type is %, inferred from here:", full_type))
                             .done();
            return false;
        }

        Type reduced = set_indirection(type, get_indirection(type) - 1);
        if (!pattern_matching_inference(unit, block, expr->unary_operand, reduced, inferred_from, full_inferred_type))
            return false;
        set_inferred_type(block, id, TYPE_SOFT_TYPE);
        set_constant_type(block, id, type);
        complete_expression(block, id);
    } break;

    case EXPRESSION_DEREFERENCE:
    {
        u32 indirection = get_indirection(type) + 1;
        if (indirection > TYPE_MAX_INDIRECTION)
        {
            String full_type = exact_type_description(unit, full_inferred_type);
            Report(unit->ctx).part(expr, Format(temp, "Can't match the type to the expected pattern, because it would exceed the maximum indirection %.", TYPE_MAX_INDIRECTION))
                             .part(inferred_from, Format(temp, "The type is %, inferred from here:", full_type))
                             .done();
        }

        Type indirected = set_indirection(type, indirection);
        if (!pattern_matching_inference(unit, block, expr->unary_operand, indirected, inferred_from, full_inferred_type))
            return false;
        set_inferred_type(block, id, TYPE_SOFT_TYPE);
        set_constant_type(block, id, type);
        complete_expression(block, id);
    } break;

    IllegalDefaultCase;

    }

    return true;
}


enum Yield_Result
{
    YIELD_COMPLETED,
    YIELD_MADE_PROGRESS,
    YIELD_NO_PROGRESS,
    YIELD_ERROR,
};

static Yield_Result infer_expression(Pipeline_Task* task, Expression id)
{
    Unit*  unit  = task->unit;
    Block* block = task->block;

    auto* expr  = &block->parsed_expressions[id];
    auto* infer = &block->inferred_expressions[id];
    assert(!(infer->flags & INFERRED_EXPRESSION_COMPLETED_INFERENCE));


    bool override_made_progress = false;
    #define Wait(why, on_expression, on_block)                                      \
    {                                                                               \
        Wait_Info info = { why, on_expression, on_block };                          \
        set(&block->waiting_expressions, &id, &info);                               \
        return override_made_progress ? YIELD_MADE_PROGRESS : YIELD_NO_PROGRESS;    \
    }

    #define WaitOperand(on_expression) Wait(WAITING_ON_OPERAND, on_expression, block)

    #define Error(...) return (report_error(unit->ctx, expr, Format(temp, ##__VA_ARGS__)), YIELD_ERROR)

    #define InferType(type) set_inferred_type(block, id, type)
    #define InferenceComplete() return (complete_expression(block, id), YIELD_COMPLETED)


    if (expr->flags & EXPRESSION_HAS_CONDITIONAL_INFERENCE)
    {
        if (infer->flags & INFERRED_EXPRESSION_CONDITION_DISABLED)
        {
            infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
            infer->flags |= INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE;
            InferType(TYPE_VOID);
            InferenceComplete();
        }
        if (!(infer->flags & INFERRED_EXPRESSION_CONDITION_ENABLED))
        {
            Wait(WAITING_ON_CONDITION_INFERENCE, NO_EXPRESSION, NULL);
        }
    }

    if (expr->flags & EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED)
        Wait(WAITING_ON_EXTERNAL_INFERENCE, NO_EXPRESSION, NULL);

    switch (expr->kind)
    {

    case EXPRESSION_ZERO:   InferType(TYPE_SOFT_ZERO); InferenceComplete();
    case EXPRESSION_TRUE:   InferType(TYPE_SOFT_BOOL); set_constant_bool(block, id, true);  InferenceComplete();
    case EXPRESSION_FALSE:  InferType(TYPE_SOFT_BOOL); set_constant_bool(block, id, false); InferenceComplete();
    case EXPRESSION_NUMERIC_LITERAL:
    {
        InferType(TYPE_SOFT_NUMBER);
        Token_Info_Number* token_info = (Token_Info_Number*) get_token_info(unit->ctx, &expr->literal);
        set_constant_number(block, id, fract_clone(&token_info->value));
        InferenceComplete();
    } break;

    case EXPRESSION_STRING_LITERAL:
    {
        InferType(TYPE_STRING);
        InferenceComplete();
    } break;

    case EXPRESSION_TYPE_LITERAL:
    {
        InferType(TYPE_SOFT_TYPE);
        assert(is_primitive_type(expr->parsed_type) || expr->parsed_type == TYPE_STRING);
        set_constant_type(block, id, expr->parsed_type);
        InferenceComplete();
    } break;

    case EXPRESSION_BLOCK:
    {
        InferType(TYPE_SOFT_BLOCK);
        Soft_Block soft = {};
        soft.materialized_parent = block;
        soft.parsed_child = expr->parsed_block;
        set_constant_block(block, id, soft);
        InferenceComplete();
    } break;

    case EXPRESSION_UNIT:
    {
        InferType(TYPE_SOFT_TYPE);

        Block* parent = (expr->flags & EXPRESSION_UNIT_IS_IMPORT) ? NULL : block;
        Unit* new_unit = materialize_unit(unit->ctx, expr->parsed_block, parent);
        if (expr->parsed_block->flags & BLOCK_HAS_STRUCTURE_PLACEMENT)
            new_unit->flags |= UNIT_IS_STRUCT;
        set_constant_type(block, id, new_unit->type_id);
        InferenceComplete();
    } break;

    case EXPRESSION_NAME:
    {
        Token const* name = &expr->name.token;

        Resolved_Name resolved = get(&block->resolved_names, &id);
        if (!resolved.scope)
        {
            Find_Result result = find_declaration(unit->ctx, name, block, expr->visibility_limit, &resolved.scope, &resolved.declaration, &resolved.use_chain);
            if (result == FIND_WAIT)
                Wait(WAITING_ON_USING_TYPE, resolved.declaration, resolved.scope);
            if (result == FIND_FAILURE)
            {
                String error = Format(temp, "Can't find '%'.", get_identifier(unit->ctx, name));
                helpful_error_for_missing_name(unit->ctx, error, &expr->declaration.name,
                                               block, expr->visibility_limit, /* allow_parent_traversal */ true);
                return YIELD_ERROR;
            }
            assert(result == FIND_SUCCESS);
            assert(!(unit->flags & UNIT_IS_PLACED));
            set(&block->resolved_names, &id, &resolved);
        }

        Inferred_Expression* decl_infer = &resolved.scope->inferred_expressions[resolved.declaration];
        if (decl_infer->type == INVALID_TYPE)
            Wait(WAITING_ON_DECLARATION, resolved.declaration, resolved.scope);
        infer->flags |= INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE;
        InferType(decl_infer->type);

        if (is_soft_type(decl_infer->type))
            if (!copy_constant(unit->ctx, block, id, resolved.scope, resolved.declaration, decl_infer->type))
                Wait(WAITING_ON_DECLARATION, resolved.declaration, resolved.scope);

        InferenceComplete();
    } break;

    case EXPRESSION_MEMBER:
    {
        Type lhs_type = block->inferred_expressions[expr->member.lhs].type;
        if (lhs_type == INVALID_TYPE)
            WaitOperand(expr->member.lhs);

        Visibility member_visibility = ALL_VISIBILITY;
        Type user_type = lhs_type;
        if (user_type == TYPE_SOFT_TYPE)
        {
            member_visibility = NO_VISIBILITY;
            Type const* constant_type = get_constant_type(block, expr->member.lhs);
            if (!constant_type) WaitOperand(expr->member.lhs);
            user_type = *constant_type;
        }
        if (is_pointer_type(user_type))
            user_type = get_element_type(user_type);
        if (!is_user_defined_type(user_type))
            Error("Expected a unit type on the left side of the '.' operator, but got %.", vague_type_description(unit, lhs_type));

        User_Type* data = get_user_type_data(unit->ctx, user_type);
        assert(data->unit);
        Block* member_block = data->unit->entry_block;
        assert(member_block);

        Resolved_Name resolved = get(&block->resolved_names, &id);
        if (!resolved.scope)
        {
            Find_Result result = find_declaration(unit->ctx, &expr->member.name, member_block, member_visibility, &resolved.scope, &resolved.declaration, &resolved.use_chain, /* allow_parent_traversal */ false);
            if (result == FIND_WAIT)
                Wait(WAITING_ON_USING_TYPE, resolved.declaration, resolved.scope);
            if (result == FIND_FAILURE)
            {
                String identifier = get_identifier(unit->ctx, &expr->member.name);
                String error = Format(temp, "Can't find member '%' in %.", identifier, exact_type_description(unit, user_type));
                helpful_error_for_missing_name(unit->ctx, error, &expr->member.name,
                                               member_block, member_visibility, /* allow_parent_traversal */ false);
                return YIELD_ERROR;
            }
            assert(result == FIND_SUCCESS);
            assert(!(unit->flags & UNIT_IS_PLACED));
            set(&block->resolved_names, &id, &resolved);
        }

        Inferred_Expression* decl_infer = &resolved.scope->inferred_expressions[resolved.declaration];
        if (decl_infer->type == INVALID_TYPE)
            Wait(WAITING_ON_DECLARATION, resolved.declaration, resolved.scope);
        infer->flags |= INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE;
        InferType(decl_infer->type);

        if (is_soft_type(decl_infer->type))
            if (!copy_constant(unit->ctx, block, id, resolved.scope, resolved.declaration, decl_infer->type))
                Wait(WAITING_ON_DECLARATION, resolved.declaration, resolved.scope);

        InferenceComplete();
    } break;

    case EXPRESSION_NEGATE:
    {
        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE)
            WaitOperand(expr->unary_operand);

        if (!is_numeric_type(op_infer->type))
            Error("Unary operator '-' expects a numeric argument, but got %.", vague_type_description(unit, op_infer->type));
        InferType(op_infer->type);

        if (op_infer->type == TYPE_SOFT_NUMBER)
        {
            Fraction const* value = get_constant_number(block, expr->unary_operand);
            if (!value) WaitOperand(expr->unary_operand);
            set_constant_number(block, id, fract_neg(value));
        }

        InferenceComplete();
    } break;

    case EXPRESSION_ADDRESS:
    {
        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE)
            WaitOperand(expr->unary_operand);

        if (is_type_type(op_infer->type))
        {
            if (op_infer->type != TYPE_SOFT_TYPE)
                Error("The operand to '&' is a type not known at compile-time.");
            InferType(TYPE_SOFT_TYPE);

            Type const* type = get_constant_type(block, expr->unary_operand);
            if (!type) WaitOperand(expr->unary_operand);
            u32 indirection = get_indirection(*type) + 1;
            if (indirection > TYPE_MAX_INDIRECTION)
                Error("The operand to '&' is already at maximum indirection %!", TYPE_MAX_INDIRECTION);
            set_constant_type(block, id, set_indirection(*type, indirection));
        }
        else if (is_soft_type(op_infer->type))
        {
            Error("Can't take an address of %.", vague_type_description_in_compile_time_context(unit, op_infer->type));
        }
        else
        {
            Type type = op_infer->type;
            u32 indirection = get_indirection(type) + 1;
            if (indirection > TYPE_MAX_INDIRECTION)
                Error("The operand to '&' is already at maximum indirection %!", TYPE_MAX_INDIRECTION);
            InferType(set_indirection(type, indirection));
        }

        InferenceComplete();
    } break;

    case EXPRESSION_DEREFERENCE:
    {
        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE)
            WaitOperand(expr->unary_operand);
        infer->flags |= INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE;

        if (is_type_type(op_infer->type))
        {
            if (op_infer->type != TYPE_SOFT_TYPE)
                Error("The operand to '*' is a type not known at compile-time.");
            InferType(TYPE_SOFT_TYPE);

            Type const* type = get_constant_type(block, expr->unary_operand);
            if (!type) WaitOperand(expr->unary_operand);
            u32 indirection = get_indirection(*type);
            if (!indirection)
                Error("Expected a pointer as operand to '*', but got %.", exact_type_description(unit, *type));
            set_constant_type(block, id, set_indirection(*type, indirection - 1));
        }
        else if (is_soft_type(op_infer->type))
        {
            Error("Can't dereference %.", vague_type_description_in_compile_time_context(unit, op_infer->type));
        }
        else
        {
            Type type = op_infer->type;
            u32 indirection = get_indirection(type);
            if (!indirection)
                Error("Expected a pointer as operand to '*', but got %.", exact_type_description(unit, type));
            InferType(set_indirection(type, indirection - 1));
        }

        InferenceComplete();
    } break;

    case EXPRESSION_SIZEOF:
    {
        InferType(TYPE_SOFT_NUMBER);

        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE) WaitOperand(expr->unary_operand);

        if (!is_type_type(op_infer->type))
            Error("Expected a type as operand to 'sizeof', but got %.", vague_type_description(unit, op_infer->type));
        if (op_infer->type != TYPE_SOFT_TYPE)
            Error("The operand to 'sizeof' is a type not known at compile-time.");

        Type const* type = get_constant_type(block, expr->unary_operand);
        if (!type) WaitOperand(expr->unary_operand);

        if (is_user_defined_type(*type) && !is_user_type_sizeable(unit->ctx, *type))
            WaitOperand(expr->unary_operand);
        set_constant_number(block, id, fract_make_u64(get_type_size(unit, *type)));

        InferenceComplete();
    } break;

    case EXPRESSION_ALIGNOF:
    {
        InferType(TYPE_SOFT_NUMBER);

        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE) WaitOperand(expr->unary_operand);

        if (!is_type_type(op_infer->type))
            Error("Expected a type as operand to 'sizeof', but got %.", vague_type_description(unit, op_infer->type));
        if (op_infer->type != TYPE_SOFT_TYPE)
            Error("The operand to 'sizeof' is a type not known at compile-time.");

        Type const* type = get_constant_type(block, expr->unary_operand);
        if (!type) WaitOperand(expr->unary_operand);

        if (is_user_defined_type(*type) && !is_user_type_sizeable(unit->ctx, *type))
            WaitOperand(expr->unary_operand);
        set_constant_number(block, id, fract_make_u64(get_type_alignment(unit, *type)));

        InferenceComplete();
    } break;

    case EXPRESSION_CODEOF:
    {
        InferType(set_indirection(TYPE_VOID, 1));

        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE) WaitOperand(expr->unary_operand);

        if (!is_user_defined_type(op_infer->type))
        {
            if (op_infer->type != TYPE_SOFT_TYPE)
                Error("Expected a unit as operand to 'codeof', but got %.", vague_type_description(unit, op_infer->type));

            Type const* type = get_constant_type(block, expr->unary_operand);
            if (!type) WaitOperand(expr->unary_operand);
            if (!is_user_defined_type(*type))
                Error("Expected a unit as operand to 'codeof', but got %.", exact_type_description(unit, *type));

            User_Type* data = get_user_type_data(unit->ctx, *type);
            assert(data->unit);
            if (data->unit->flags & UNIT_IS_STRUCT)
                Error("'%' is a structure, and those don't have associated code.", exact_type_description(unit, *type));
        }

        InferenceComplete();
    } break;

    case EXPRESSION_DEBUG:
    {
        InferType(TYPE_VOID);
        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE) WaitOperand(expr->unary_operand);
        InferenceComplete();
    } break;

    case EXPRESSION_DEBUG_ALLOC:
    {
        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE) WaitOperand(expr->unary_operand);

        if (!is_type_type(op_infer->type))
            Error("Expected a type as operand to 'debug_alloc', but got %.", vague_type_description(unit, op_infer->type));
        if (op_infer->type != TYPE_SOFT_TYPE)
            Error("The operand to 'debug_alloc' is a type not known at compile-time.");

        Type const* type = get_constant_type(block, expr->unary_operand);
        if (!type) WaitOperand(expr->unary_operand);
        u32 indirection = get_indirection(*type) + 1;
        if (indirection > TYPE_MAX_INDIRECTION)
            Error("The operand to 'debug_alloc' is already at maximum indirection %! Can't yield a pointer to it.", TYPE_MAX_INDIRECTION);
        InferType(set_indirection(*type, indirection));
        InferenceComplete();
    } break;

    case EXPRESSION_DEBUG_FREE:
    {
        InferType(TYPE_VOID);
        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE) WaitOperand(expr->unary_operand);
        if (!is_pointer_type(op_infer->type))
            Error("Expected a pointer as operand to 'debug_free', but got %.", vague_type_description(unit, op_infer->type));
        InferenceComplete();
    } break;

    case EXPRESSION_ASSIGNMENT:
    {
        if (block->parsed_expressions[expr->binary.lhs].kind != EXPRESSION_NAME &&
            block->parsed_expressions[expr->binary.lhs].kind != EXPRESSION_MEMBER &&
            block->parsed_expressions[expr->binary.lhs].kind != EXPRESSION_DEREFERENCE)
            Error("Expected a variable name, member, or dereference expression on the left side of the assignment.");

        if (block->parsed_expressions[expr->binary.lhs].kind == EXPRESSION_MEMBER)
        {
            Resolved_Name resolved = get(&block->resolved_names, &expr->binary.lhs);
            if (resolved.scope &&
                !(resolved.scope->flags & BLOCK_HAS_STRUCTURE_PLACEMENT) &&
                !(resolved.scope->parsed_expressions[resolved.declaration].flags & EXPRESSION_DECLARATION_IS_UNINITIALIZED))
            {
                String identifier = get_identifier(unit->ctx, &block->parsed_expressions[expr->binary.lhs].member.name);
                Report(unit->ctx)
                    .intro(SEVERITY_WARNING, expr)
                    .message(Format(temp, "Assignment to '%' will be overwritten by the declaration once the unit executes.\n"
                                          "If you're trying to pass data to the unit, the declaration should be uninitialized.",
                                          identifier))
                    .snippet(expr)
                    .done();
            }
        }

        Type lhs_type = block->inferred_expressions[expr->binary.lhs].type;
        Type rhs_type = block->inferred_expressions[expr->binary.rhs].type;
        if (lhs_type == INVALID_TYPE) WaitOperand(expr->binary.lhs);
        if (rhs_type == INVALID_TYPE) WaitOperand(expr->binary.rhs);
        if (is_soft_type(lhs_type))
            Error("Can't assign to a constant expression.");
        if (lhs_type != rhs_type && rhs_type != TYPE_SOFT_ZERO)
            Error("Types don't match.\n"
                  "    lhs: %\n"
                  "    rhs: %",
                  exact_type_description(unit, lhs_type),
                  exact_type_description(unit, rhs_type));
        InferType(lhs_type);
        InferenceComplete();
    } break;

    case EXPRESSION_ADD:
    case EXPRESSION_SUBTRACT:
    case EXPRESSION_MULTIPLY:
    case EXPRESSION_DIVIDE_WHOLE:
    case EXPRESSION_DIVIDE_FRACTIONAL:
    {
        Type lhs_type = block->inferred_expressions[expr->binary.lhs].type;
        Type rhs_type = block->inferred_expressions[expr->binary.rhs].type;
        if (lhs_type == INVALID_TYPE) WaitOperand(expr->binary.lhs);
        if (rhs_type == INVALID_TYPE) WaitOperand(expr->binary.rhs);
        if (lhs_type != rhs_type)
            Error("Types don't match.\n"
                  "    lhs: %\n"
                  "    rhs: %",
                  exact_type_description(unit, lhs_type),
                  exact_type_description(unit, rhs_type));
        if (!is_numeric_type(lhs_type))
            Error("Expected a numeric operand, but got %.", vague_type_description(unit, lhs_type));

        InferType(lhs_type);

        if (lhs_type == TYPE_SOFT_NUMBER)
        {
            Fraction const* lhs_fract = get_constant_number(block, expr->binary.lhs);
            Fraction const* rhs_fract = get_constant_number(block, expr->binary.rhs);
            if (!lhs_fract) WaitOperand(expr->binary.lhs);
            if (!rhs_fract) WaitOperand(expr->binary.rhs);

            Fraction result = {};
                 if (expr->kind == EXPRESSION_ADD)      result = fract_add(lhs_fract, rhs_fract);
            else if (expr->kind == EXPRESSION_SUBTRACT) result = fract_sub(lhs_fract, rhs_fract);
            else if (expr->kind == EXPRESSION_MULTIPLY) result = fract_mul(lhs_fract, rhs_fract);
            else if (expr->kind == EXPRESSION_DIVIDE_WHOLE)
            {
                if (!fract_div_whole(&result, lhs_fract, rhs_fract))
                    Error("Division by zero!");
            }
            else if (expr->kind == EXPRESSION_DIVIDE_FRACTIONAL)
            {
                if (!fract_div_fract(&result, lhs_fract, rhs_fract))
                    Error("Division by zero!");
            }

            assert(infer->type == TYPE_SOFT_NUMBER);
            set_constant_number(block, id, result);
        }

        InferenceComplete();
    } break;

    case EXPRESSION_POINTER_ADD:
    {
        Type lhs_type = block->inferred_expressions[expr->binary.lhs].type;
        Type rhs_type = block->inferred_expressions[expr->binary.rhs].type;
        if (lhs_type == INVALID_TYPE) WaitOperand(expr->binary.lhs);
        if (rhs_type == INVALID_TYPE) WaitOperand(expr->binary.rhs);

        Type pointer_type, integer_type;
        if (is_pointer_type(lhs_type))
            pointer_type = lhs_type, integer_type = rhs_type;
        else
            pointer_type = rhs_type, integer_type = lhs_type;

        if (!is_pointer_type(pointer_type) || !is_pointer_integer_type(integer_type))
            Error("Operands to '&+' must be a pointer and a pointer-sized integer."
                  "    lhs: %\n"
                  "    rhs: %",
                  exact_type_description(unit, lhs_type),
                  exact_type_description(unit, rhs_type));

        InferType(pointer_type);
        InferenceComplete();
    } break;

    case EXPRESSION_POINTER_SUBTRACT:
    {
        NotImplemented;
    } break;

    case EXPRESSION_EQUAL:
    case EXPRESSION_NOT_EQUAL:
    case EXPRESSION_GREATER_THAN:
    case EXPRESSION_GREATER_OR_EQUAL:
    case EXPRESSION_LESS_THAN:
    case EXPRESSION_LESS_OR_EQUAL:
    {
        Type lhs_type = block->inferred_expressions[expr->binary.lhs].type;
        Type rhs_type = block->inferred_expressions[expr->binary.rhs].type;
        if (lhs_type == INVALID_TYPE) WaitOperand(expr->binary.lhs);
        if (rhs_type == INVALID_TYPE) WaitOperand(expr->binary.rhs);
        if (lhs_type != rhs_type)
            Error("Types don't match.\n"
                  "    lhs: %\n"
                  "    rhs: %",
                  exact_type_description(unit, lhs_type),
                  exact_type_description(unit, rhs_type));

        String op_token = "???"_s;
             if (expr->kind == EXPRESSION_EQUAL)            op_token = "=="_s;
        else if (expr->kind == EXPRESSION_NOT_EQUAL)        op_token = "!="_s;
        else if (expr->kind == EXPRESSION_GREATER_THAN)     op_token = ">"_s;
        else if (expr->kind == EXPRESSION_GREATER_OR_EQUAL) op_token = ">="_s;
        else if (expr->kind == EXPRESSION_LESS_THAN)        op_token = "<"_s;
        else if (expr->kind == EXPRESSION_LESS_OR_EQUAL)    op_token = "<="_s;
        else Unreachable;

        if (!is_numeric_type(lhs_type) && !is_bool_type(lhs_type) && !is_pointer_type(lhs_type))
            Error("Operands to '%' must be numeric, bool, or pointers, but they are %.\n",
                  op_token,
                  vague_type_description(unit, lhs_type));

        if (is_soft_type(lhs_type))
        {
            InferType(TYPE_SOFT_BOOL);

            bool zero     = false;
            bool negative = false;
            if (lhs_type == TYPE_SOFT_NUMBER)
            {
                Fraction const* lhs_fract = get_constant_number(block, expr->binary.lhs);
                Fraction const* rhs_fract = get_constant_number(block, expr->binary.rhs);
                if (!lhs_fract) WaitOperand(expr->binary.lhs);
                if (!rhs_fract) WaitOperand(expr->binary.rhs);
                Fraction d = fract_sub(lhs_fract, rhs_fract);
                zero     = fract_is_zero    (&d);
                negative = fract_is_negative(&d);
                fract_free(&d);
            }
            else if (lhs_type == TYPE_SOFT_BOOL)
            {
                bool const* lhs_fract = get_constant_bool(block, expr->binary.lhs);
                bool const* rhs_fract = get_constant_bool(block, expr->binary.rhs);
                if (!lhs_fract) WaitOperand(expr->binary.lhs);
                if (!rhs_fract) WaitOperand(expr->binary.rhs);
                int d = (*lhs_fract ? 1 : 0) - (*rhs_fract ? 1 : 0);
                zero     = (d == 0);
                negative = (d <  0);
            }
            else Unreachable;

            bool result = false;
                 if (expr->kind == EXPRESSION_EQUAL)            result =  zero;
            else if (expr->kind == EXPRESSION_NOT_EQUAL)        result = !zero;
            else if (expr->kind == EXPRESSION_GREATER_THAN)     result = !zero && !negative;
            else if (expr->kind == EXPRESSION_GREATER_OR_EQUAL) result = !negative;
            else if (expr->kind == EXPRESSION_LESS_THAN)        result =  negative;
            else if (expr->kind == EXPRESSION_LESS_OR_EQUAL)    result =  zero || negative;
            else Unreachable;
            set_constant_bool(block, id, result);
            InferenceComplete();
        }
        else
        {
            InferType(TYPE_BOOL);
            InferenceComplete();
        }
    } break;

    case EXPRESSION_CAST:
    {
        auto* lhs_infer = &block->inferred_expressions[expr->binary.lhs];
        auto* rhs_infer = &block->inferred_expressions[expr->binary.rhs];
        if (lhs_infer->type == INVALID_TYPE) WaitOperand(expr->binary.lhs);
        if (rhs_infer->type == INVALID_TYPE) WaitOperand(expr->binary.rhs);

        if (!is_type_type(lhs_infer->type))
            Error("Expected a type as the first operand to 'cast', but got %.", vague_type_description(unit, lhs_infer->type));
        if (lhs_infer->type != TYPE_SOFT_TYPE)
            Error("The first operand to 'cast' is not known at compile-time.");

        Type const* cast_type_ptr = get_constant_type(block, expr->binary.lhs);
        if (!cast_type_ptr) WaitOperand(expr->binary.lhs);
        Type cast_type = *cast_type_ptr;

        assert(!is_soft_type(cast_type));
        InferType(cast_type);

        if (cast_type == TYPE_TYPE)
        {
            if (!is_type_type(rhs_infer->type))  // @Incomplete
                Error("Expected a type value as the second operand to 'cast', but got %.", vague_type_description(unit, rhs_infer->type));
        }
        else if (is_numeric_type(cast_type))
        {
            if (!is_numeric_type(rhs_infer->type) && !is_pointer_type(rhs_infer->type))  // @Incomplete
                Error("Expected a numeric value as the second operand to 'cast', but got %.", vague_type_description(unit, rhs_infer->type));

            if (rhs_infer->type == TYPE_SOFT_NUMBER)
            {
                Fraction const* fraction = get_constant_number(block, expr->binary.rhs);
                if (!fraction) WaitOperand(expr->binary.rhs);
                if (!check_constant_fits_in_runtime_type(unit, &block->parsed_expressions[expr->binary.rhs], fraction, cast_type))
                    return YIELD_ERROR;
            }
        }
        else if (is_bool_type(cast_type))
        {
            if (!is_bool_type(rhs_infer->type))  // @Incomplete
                Error("Expected a bool value as the second operand to 'cast', but got %.", vague_type_description(unit, rhs_infer->type));
        }
        else
        {
            Error("Type '%' can't be cast to.", exact_type_description(unit, cast_type));
        }

        InferenceComplete();
    } break;

    case EXPRESSION_GOTO_UNIT:
    {
        InferType(TYPE_VOID);

        auto* lhs_infer = &block->inferred_expressions[expr->binary.lhs];
        auto* rhs_infer = &block->inferred_expressions[expr->binary.rhs];
        if (lhs_infer->type == INVALID_TYPE) WaitOperand(expr->binary.lhs);
        if (rhs_infer->type == INVALID_TYPE) WaitOperand(expr->binary.rhs);
        if (!is_pointer_type(lhs_infer->type))
            Error("Expected a pointer type as the first operand to 'goto', but got %.", vague_type_description(unit, lhs_infer->type));
        if (!is_pointer_type(rhs_infer->type))
            Error("Expected a pointer type as the second operand to 'goto', but got %.", vague_type_description(unit, rhs_infer->type));

        InferenceComplete();
    } break;

    case EXPRESSION_BRANCH:
    {
        Expression condition  = expr->branch.condition;
        Expression on_success = expr->branch.on_success;
        Expression on_failure = expr->branch.on_failure;
        if (condition != NO_EXPRESSION)
        {
            Type type = block->inferred_expressions[condition].type;
            if (type == INVALID_TYPE) WaitOperand(condition);

            if (expr->flags & EXPRESSION_BRANCH_IS_BAKED)
            {
                bool baked_condition;
                if (type == TYPE_SOFT_BOOL)
                {
                    bool const* value = get_constant_bool(block, condition);
                    if (!value) WaitOperand(condition);
                    baked_condition = *value;
                }
                else
                    Error("Expected a compile-time boolean value as the condition, but got %.", vague_type_description_in_compile_time_context(unit, type));

                if (baked_condition)
                {
                    if (on_success != NO_EXPRESSION) block->inferred_expressions[on_success].flags |= INFERRED_EXPRESSION_CONDITION_ENABLED;
                    if (on_failure != NO_EXPRESSION) block->inferred_expressions[on_failure].flags |= INFERRED_EXPRESSION_CONDITION_DISABLED;
                }
                else
                {
                    if (on_success != NO_EXPRESSION) block->inferred_expressions[on_success].flags |= INFERRED_EXPRESSION_CONDITION_DISABLED;
                    if (on_failure != NO_EXPRESSION) block->inferred_expressions[on_failure].flags |= INFERRED_EXPRESSION_CONDITION_ENABLED;
                }
                override_made_progress = true;  // we made progress by enabling/disabling expressions
            }
            else
            {
                if (!is_integer_type(type) && !is_bool_type(type))
                    Error("Expected a boolean value as the condition, but got %.", vague_type_description(unit, type));
                if (is_soft_type(type))
                    Error("@Incomplete - condition may not be a compile-time value");
            }
        }
        else
        {
            assert(!(expr->flags & EXPRESSION_BRANCH_IS_BAKED));
        }

        if (on_success != NO_EXPRESSION && block->inferred_expressions[on_success].flags & INFERRED_EXPRESSION_CONDITION_DISABLED) on_success = NO_EXPRESSION;
        if (on_failure != NO_EXPRESSION && block->inferred_expressions[on_failure].flags & INFERRED_EXPRESSION_CONDITION_DISABLED) on_failure = NO_EXPRESSION;

        if (on_success == NO_EXPRESSION && on_failure == NO_EXPRESSION)
        {
            InferType(TYPE_VOID);
            InferenceComplete();
        }

        Type success_type = INVALID_TYPE;
        Type failure_type = INVALID_TYPE;
        if (on_success != NO_EXPRESSION)
        {
            success_type = block->inferred_expressions[on_success].type;
            if (success_type == INVALID_TYPE) WaitOperand(on_success);
        }
        if (on_failure != NO_EXPRESSION)
        {
            failure_type = block->inferred_expressions[on_failure].type;
            if (failure_type == INVALID_TYPE) WaitOperand(on_failure);
        }

        if (on_success != NO_EXPRESSION && on_failure != NO_EXPRESSION)
            if (success_type != failure_type)
                Error("The expression yields a different type depending on the condition.\n"
                      "On success, it yields %.\n"
                      "On failure, it yields %.",
                      exact_type_description(unit, success_type),
                      exact_type_description(unit, failure_type));

        InferType(on_success != NO_EXPRESSION ? success_type : failure_type);
        InferenceComplete();
    } break;

    case EXPRESSION_CALL:
    {
        // Make sure everything on our side is inferred.
        auto* lhs_infer = &block->inferred_expressions[expr->call.lhs];
        if (lhs_infer->type == INVALID_TYPE) WaitOperand(expr->call.lhs);
        if (lhs_infer->type != TYPE_SOFT_BLOCK)
            Error("Expected a block on the left-hand side of the call expression, but got %.", vague_type_description(unit, lhs_infer->type));

        Soft_Block const* soft_callee = get_constant_block(block, expr->call.lhs);
        if (!soft_callee) WaitOperand(expr->call.lhs);

        if (soft_callee->parsed_child->flags & BLOCK_IS_UNIT)
            Error("The block on the left-hand side of the call expression is a unit. Units can't be called.");

        Expression_List const* args = expr->call.arguments;
        for (umm arg = 0; arg < args->count; arg++)
            if (block->inferred_expressions[args->expressions[arg]].type == INVALID_TYPE)
                WaitOperand(args->expressions[arg]);

        String callee_name = "Callee"_s;
        if (soft_callee->has_alias)
            callee_name = get_identifier(unit->ctx, &soft_callee->alias);

        // First stage: materializing the callee
        if (!infer->called_block)
        {
            // First check against the parsed block that the argument count is correct.
            Block* lhs_parent = soft_callee->materialized_parent;
            Block* lhs_block  = soft_callee->parsed_child;
            assert(lhs_block);

            umm parameter_count = 0;
            For (lhs_block->imperative_order)
                if (lhs_block->parsed_expressions[*it].flags & EXPRESSION_DECLARATION_IS_PARAMETER)
                    parameter_count++;
            if (parameter_count != args->count)
                Error("% expects % %, but you provided %.",
                    callee_name, parameter_count, plural(parameter_count, "argument"_s, "arguments"_s),
                    args->count);

            // Now materialize the callee
            Visibility visibility = (expr->flags & EXPRESSION_ALLOW_PARENT_SCOPE_VISIBILITY)
                                  ? expr->visibility_limit
                                  : NO_VISIBILITY;
            Block* callee = materialize_block(unit, lhs_block, lhs_parent, visibility);
            infer->called_block = callee;

            override_made_progress = true;  // We made progress by materializing the callee.
        }

        Expression waiting_on_return      = NO_EXPRESSION;
        Expression waiting_on_a_parameter = NO_EXPRESSION;
        Expression waiting_on_an_argument = NO_EXPRESSION;

        // Second stage: checking the parameter types and inferring the return type
        Block* callee = infer->called_block;
        umm parameter_index = 0;
        For (callee->imperative_order)
        {
            Expression param_id = *it;
            auto* param_expr  = &callee->parsed_expressions  [param_id];
            auto* param_infer = &callee->inferred_expressions[param_id];
            if (param_expr->kind != EXPRESSION_DECLARATION) continue;

            if (param_expr->flags & EXPRESSION_DECLARATION_IS_RETURN)
            {
                if (param_infer->type == INVALID_TYPE)
                    waiting_on_return = param_id;
                else
                    InferType(param_infer->type);
                continue;
            }

            if (!(param_expr->flags & EXPRESSION_DECLARATION_IS_PARAMETER)) continue;
            Defer(parameter_index++);
            assert(param_expr->declaration.type != NO_EXPRESSION);
            assert(param_expr->declaration.value == NO_EXPRESSION);

            String param_name = get_identifier(unit->ctx, &param_expr->declaration.name);

            auto* param_type_expr = &callee->parsed_expressions[param_expr->declaration.type];
            if ((param_type_expr->flags & EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED) &&
                param_infer->type == INVALID_TYPE)
            {
                assert(!(param_expr->flags & EXPRESSION_DECLARATION_IS_ALIAS));
                assert(parameter_index < args->count);

                auto* arg_expr = &block->parsed_expressions  [args->expressions[parameter_index]];
                Type  arg_type =  block->inferred_expressions[args->expressions[parameter_index]].type;
                assert(arg_type != INVALID_TYPE);

                if (is_soft_type(arg_type))
                {
                    Report(unit->ctx).part(arg_expr, Format(temp, "A runtime value is required for parameter #% ('%'), but the argument is %.",
                                                            parameter_index + 1, param_name,
                                                            vague_type_description_in_compile_time_context(unit, arg_type)))
                                     .part(param_type_expr, "This is required because the parameter infers its type from the caller."_s)
                                     .done();
                    return YIELD_ERROR;
                }

                if (!pattern_matching_inference(unit, callee, param_expr->declaration.type, arg_type, arg_expr, arg_type))
                    return YIELD_ERROR;

                param_infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
                set_inferred_type(callee, param_id, arg_type);
                complete_expression(callee, param_id);
                override_made_progress = true;  // We made progress by inferring the callee's param type.
            }

            auto* type_infer = &callee->inferred_expressions[param_expr->declaration.type];
            Type const* param_type_ptr = NULL;
            if (type_infer->type == INVALID_TYPE || type_infer->type != TYPE_SOFT_TYPE ||
                !(param_type_ptr = get_constant_type(callee, param_expr->declaration.type)))
            {
                // We don't immediately Wait(), to enable out of order parameter inference.
                waiting_on_a_parameter = param_expr->declaration.type;
                continue;
            }

            assert(param_type_ptr);
            Type param_type = *param_type_ptr;

            assert(args->count > parameter_index);
            auto  arg_id    = args->expressions[parameter_index];
            auto* arg_expr  = &block->parsed_expressions  [arg_id];
            auto* arg_infer = &block->inferred_expressions[arg_id];
            Type  arg_type  = arg_infer->type;
            if (param_expr->flags & EXPRESSION_DECLARATION_IS_ALIAS)
            {
                if (param_infer->type != INVALID_TYPE) continue;  // already inferred

                if (is_numeric_type(param_type))
                {
                    if (is_floating_point_type(param_type))
                        NotImplemented;

                    if (arg_type != TYPE_SOFT_NUMBER)
                        Error("Argument #% ('%') is expected to be a compile-time integer, but is %.",
                            parameter_index + 1, param_name,
                            vague_type_description_in_compile_time_context(unit, arg_type));

                    Fraction const* fraction = get_constant_number(block, arg_id);
                    if (!fraction)
                    {
                        waiting_on_an_argument = arg_id;
                        continue;
                    }
                    if (!fract_is_integer(fraction))
                        Error("Argument #% ('%') is expected to be a compile-time integer, but is fractional.\n"
                              "Value is %.", parameter_index + 1, param_name, fract_display(fraction));

                    if (!check_constant_fits_in_runtime_type(unit, arg_expr, fraction, param_type))
                        return YIELD_ERROR;

                    set_inferred_type(callee, param_id, TYPE_SOFT_NUMBER);
                    set_constant_number(callee, param_id, fract_clone(fraction));
                    complete_expression(callee, param_id);
                    override_made_progress = true;  // We made progress by inferring the callee's param type.
                }
                else if (is_bool_type(param_type))
                {
                    if (arg_type != TYPE_SOFT_BOOL)
                        Error("Argument #% ('%') is expected to be a compile-time boolean, but is %.",
                            parameter_index + 1, param_name,
                            vague_type_description_in_compile_time_context(unit, arg_type));
                    bool const* constant = get_constant_bool(block, arg_id);
                    if (!constant)
                    {
                        waiting_on_an_argument = arg_id;
                        continue;
                    }
                    set_inferred_type(callee, param_id, TYPE_SOFT_BOOL);
                    set_constant_bool(callee, param_id, *constant);
                    complete_expression(callee, param_id);
                    override_made_progress = true;  // We made progress by inferring the callee's param type.
                }
                else if (param_type == TYPE_TYPE)
                {
                    if (arg_type != TYPE_SOFT_TYPE)
                        Error("Argument #% ('%') is expected to be a compile-time type, but is %.",
                            parameter_index + 1, param_name,
                            vague_type_description_in_compile_time_context(unit, arg_type));
                    Type const* constant = get_constant_type(block, arg_id);
                    if (!constant)
                    {
                        waiting_on_an_argument = arg_id;
                        continue;
                    }
                    set_inferred_type(callee, param_id, TYPE_SOFT_TYPE);
                    set_constant_type(callee, param_id, *constant);
                    complete_expression(callee, param_id);
                    override_made_progress = true;  // We made progress by inferring the callee's param type.
                }
                else if (param_type == TYPE_SOFT_BLOCK)
                {
                    if (arg_type != TYPE_SOFT_BLOCK)
                        Error("Argument #% ('%') is expected to be a block, but is %.",
                            parameter_index + 1, param_name,
                            vague_type_description_in_compile_time_context(unit, arg_type));

                    Soft_Block const* soft = get_constant_block(block, arg_id);
                    if (!soft)
                    {
                        waiting_on_an_argument = arg_id;
                        continue;
                    }

                    set_inferred_type(callee, param_id, TYPE_SOFT_BLOCK);
                    set_constant_block(callee, param_id, *soft);
                    complete_expression(callee, param_id);
                    override_made_progress = true;  // We made progress by inferring the callee's param type.
                }
                else Unreachable;
            }
            else
            {
                if (param_type != arg_type && arg_type != TYPE_SOFT_ZERO)
                    Error("Argument #% ('%') doesn't match the parameter type.\n"
                          "    expected: %\n"
                          "    received: %",
                          parameter_index + 1, param_name,
                          exact_type_description(unit, param_type),
                          exact_type_description(unit, arg_type));
            }
        }

        if (waiting_on_return != NO_EXPRESSION)
            Wait(WAITING_ON_RETURN_TYPE_INFERENCE, waiting_on_return, callee);
        if (waiting_on_a_parameter != NO_EXPRESSION)
            Wait(WAITING_ON_PARAMETER_INFERENCE, waiting_on_a_parameter, callee);
        if (waiting_on_an_argument != NO_EXPRESSION)
            WaitOperand(waiting_on_an_argument);

        if (infer->type == INVALID_TYPE)
            InferType(TYPE_VOID);

        InferenceComplete();
    } break;

    case EXPRESSION_INTRINSIC:
    {
        InferType(TYPE_VOID);
        InferenceComplete();
    } break;

    case EXPRESSION_YIELD:
    {
        InferType(TYPE_VOID);
        // @Reconsider - check that the assignment names resolved to something inside the return and warn if not?
        InferenceComplete();
    } break;

    case EXPRESSION_DECLARATION:
    {
        if (expr->flags & (EXPRESSION_DECLARATION_IS_PARAMETER | EXPRESSION_DECLARATION_IS_UNINITIALIZED))
            infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;

        Type type = INVALID_TYPE;
        if (expr->declaration.type != NO_EXPRESSION)
        {
            auto* type_expr = &block->parsed_expressions[expr->declaration.type];
            if (type_expr->flags & EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED)
            {
                assert(expr->declaration.value != NO_EXPRESSION);
                auto* value_expr = &block->parsed_expressions[expr->declaration.value];
                Type value_type = block->inferred_expressions[expr->declaration.value].type;
                if (value_type == INVALID_TYPE) WaitOperand(expr->declaration.value);
                if (is_soft_type(value_type))
                {
                    Report(unit->ctx).part(value_expr, Format(temp, "A runtime value is required here, but this is %.\n",
                                                              vague_type_description_in_compile_time_context(unit, value_type)))
                                     .part(type_expr, "This is because the declaration type is inferred from the value."_s)
                                     .done();
                    return YIELD_ERROR;
                }
                if (!pattern_matching_inference(unit, block, expr->declaration.type, value_type, value_expr, value_type))
                    return YIELD_ERROR;
            }

            auto* type_infer = &block->inferred_expressions[expr->declaration.type];
            if (type_infer->type == INVALID_TYPE) WaitOperand(expr->declaration.type);

            if (!is_type_type(type_infer->type))
                Error("Expected a type after ':' in declaration, but got %.", vague_type_description_in_compile_time_context(unit, type_infer->type));
            if (type_infer->type != TYPE_SOFT_TYPE)
                Error("The type in declaration is not known at compile-time.");
            Type const* constant = get_constant_type(block, expr->declaration.type);
            if (!constant) WaitOperand(expr->declaration.type);
            type = *constant;
        }

        Type value_type = INVALID_TYPE;
        if (expr->declaration.value != NO_EXPRESSION)
        {
            value_type = block->inferred_expressions[expr->declaration.value].type;
            if (value_type == INVALID_TYPE) WaitOperand(expr->declaration.value);
        }

        assert(!(expr->flags & EXPRESSION_DECLARATION_IS_INFERRED_ALIAS));
        if (expr->flags & EXPRESSION_DECLARATION_IS_ALIAS)
        {
            assert(expr->declaration.value != NO_EXPRESSION);
            auto* value_expr  = &block->parsed_expressions  [expr->declaration.value];
            auto* value_infer = &block->inferred_expressions[expr->declaration.value];
            if (value_infer->type == INVALID_TYPE) WaitOperand(expr->declaration.value);
            InferType(value_infer->type);

            if (is_soft_type(value_infer->type))
            {
                if (!copy_constant(unit->ctx, block, id, block, expr->declaration.value, value_infer->type, &expr->declaration.name))
                    WaitOperand(expr->declaration.value);
            }
            else
            {
                Report(unit->ctx).part(value_expr, Format(temp, "A compile-time value is required here, but this is %.",
                                                          vague_type_description_in_compile_time_context(unit, value_infer->type)))
                                 .done();
                return YIELD_ERROR;
            }
        }
        else
        {
            assert(type != INVALID_TYPE || value_type != INVALID_TYPE);
            if (value_type == TYPE_SOFT_BLOCK)
                Error("Blocks can't be assigned to runtime values.");
            if (expr->declaration.type == NO_EXPRESSION)
            {
                if (is_soft_type(value_type))
                {
                    assert(expr->declaration.value != NO_EXPRESSION);
                    auto* value_expr = &block->parsed_expressions[expr->declaration.value];
                    Report(unit->ctx).part(value_expr, Format(temp, "A runtime value is required here, but this is %.\n",
                                                              vague_type_description_in_compile_time_context(unit, value_type)))
                                     .done();
                    return YIELD_ERROR;
                }
                type = value_type;
            }
            assert(type != INVALID_TYPE);

            if (expr->declaration.value != NO_EXPRESSION && type != value_type)
                Error("Types don't match.\n"
                      "    lhs: %\n"
                      "    rhs: %",
                      exact_type_description(unit, type),
                      exact_type_description(unit, value_type));

            InferType(type);
        }

        InferenceComplete();
    } break;

    case EXPRESSION_RUN:
    {
        infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
        InferType(TYPE_VOID);

        Unit* new_unit = materialize_unit(unit->ctx, expr->parsed_block, block);
        new_unit->flags |= UNIT_IS_RUN;
        InferenceComplete();
    } break;

    IllegalDefaultCase;
    }

#undef Infer
#undef Wait
#undef WaitOperand
#undef Error

    Unreachable;
}

static Yield_Result infer_block(Pipeline_Task* task)
{
    Unit*  unit  = task->unit;
    Block* block = task->block;


#if STRESS_TEST
    Array<Expression> visit_order = allocate_array<Expression>(NULL, block->parsed_expressions.count);
    for (umm i = 0; i < visit_order.count; i++)
        visit_order[i] = (Expression) i;
    shuffle_array(&rng, visit_order);
#endif

    bool made_progress = false;
    bool waiting       = false;

#if STRESS_TEST
    for (Expression* id_ptr : visit_order)
    {
        Expression id = *id_ptr;
#else
    for (Expression id = {}; id < block->parsed_expressions.count; id = (Expression)(id + 1))
    {
#endif
        auto* infer = &block->inferred_expressions[id];
        if (infer->flags & INFERRED_EXPRESSION_COMPLETED_INFERENCE) continue;

        Yield_Result result = infer_expression(task, id);
        if (result == YIELD_COMPLETED || result == YIELD_MADE_PROGRESS)
        {
            assert(result != YIELD_COMPLETED || (infer->flags & INFERRED_EXPRESSION_COMPLETED_INFERENCE));
            made_progress = true;
        }
        else if (result == YIELD_NO_PROGRESS)
            waiting = true;
        else if (result == YIELD_ERROR)
            return YIELD_ERROR;
        else Unreachable;

#if STRESS_TEST
        if (made_progress)
            return YIELD_MADE_PROGRESS;
#endif
    }

    if (!(block->flags & BLOCK_READY_FOR_PLACEMENT))
    {
        // check if we are ready to do placement
        for (umm i = 0; i < block->inferred_expressions.count; i++)
        {
            auto* expr  = &block->parsed_expressions  [i];
            auto* infer = &block->inferred_expressions[i];

            if (infer->type == INVALID_TYPE)
                goto skip_placement;

            if (!(infer->flags & INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME) &&
                block->flags & BLOCK_HAS_STRUCTURE_PLACEMENT &&
                expr->kind != EXPRESSION_DECLARATION)
            {
                report_error(unit->ctx, expr, "Blocks with structured placement may not contain any expressions evaluated at runtime."_s);
                return YIELD_ERROR;
            }

            if ((infer->flags & INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE) && is_soft_type(infer->type))
                continue;

            if (is_user_defined_type(infer->type))
                if (!is_user_type_sizeable(unit->ctx, infer->type))
                    goto skip_placement;
        }

        block->flags |= BLOCK_READY_FOR_PLACEMENT;
        made_progress = true;

        // maybe complete unit placement
        assert(unit->blocks_not_ready_for_placement > 0);
        if (--unit->blocks_not_ready_for_placement == 0)
        {
            place_unit(unit);
        }

        if (false) skip_placement:
            waiting = true;
    }

    if (waiting)
        return made_progress ? YIELD_MADE_PROGRESS : YIELD_NO_PROGRESS;

    assert(unit->blocks_not_completed > 0);
    if (--unit->blocks_not_completed == 0)
        add_item(&unit->ctx->units_to_patch, &unit);

    return YIELD_COMPLETED;
}


Unit* materialize_unit(Compiler* ctx, Block* initiator, Block* materialized_parent)
{
    if (initiator->flags & BLOCK_IS_TOP_LEVEL)
    {
        assert(materialized_parent == NULL);

        u64 key = (u64) initiator;
        Unit* unit = get(&ctx->top_level_units, &key);
        if (unit) return unit;
    }

    Unit* unit;
    {
        Region memory = {};
        unit = PushValue(&memory, Unit);
        unit->memory = memory;
    }

    if (initiator->flags & BLOCK_IS_TOP_LEVEL)
    {
        u64 key = (u64) initiator;
        set(&ctx->top_level_units, &key, &unit);
    }

    unit->ctx            = ctx;
    unit->initiator_from = initiator->from;
    unit->initiator_to   = initiator->to;

    unit->pointer_size      = sizeof (void*);
    unit->pointer_alignment = alignof(void*);

    unit->type_id = create_user_type(ctx, unit);

    unit->entry_block = materialize_block(unit, initiator, materialized_parent, NO_VISIBILITY);

    return unit;
}


bool pump_pipeline(Compiler* ctx)
{
    while (ctx->pipeline.count)
    {
#if STRESS_TEST
        shuffle_array(&rng, ctx->pipeline);
#endif

        bool made_progress = false;
        for (umm it_index = 0; it_index < ctx->pipeline.count; it_index++)
        {
            bool task_completed = false;
            Pipeline_Task* task = &ctx->pipeline[it_index];
            if (task->kind == PIPELINE_TASK_INFER_BLOCK)
            {
                Unit* unit = task->unit;
                switch (infer_block(task))
                {
                case YIELD_COMPLETED:       task_completed = true; // fallthrough
                case YIELD_MADE_PROGRESS:   made_progress = true;  // fallthrough
                case YIELD_NO_PROGRESS:     break;
                case YIELD_ERROR:           return false;
                IllegalDefaultCase;
                }

                if (unit->materialized_block_count >= MAX_BLOCKS_PER_UNIT)
                {
                    Report(ctx).part(&unit->initiator_from, Format(temp, "Too many blocks instantiated in this unit. Maximum is %.", MAX_BLOCKS_PER_UNIT))
                               .part(&unit->most_recent_materialized_block->from, "The most recent instantiated block is here. It may or may not be part of the problem."_s)
                               .done();
                    return false;
                }
            }
            else Unreachable;

            if (task_completed)
            {
                ctx->pipeline[it_index] = ctx->pipeline[ctx->pipeline.count - 1];
                ctx->pipeline.count--;
                it_index--;
            }
        }

        if (!made_progress)
        {
            Report report(ctx);
            report.intro(SEVERITY_ERROR);
            report.message("Can't make inference progress."_s);
            For (ctx->pipeline)
            {
                if (it->kind != PIPELINE_TASK_INFER_BLOCK) continue;
                Block* block = it->block;
                For (block->waiting_expressions)
                {
                    auto* expr = &block->parsed_expressions[it->key];
                    Parsed_Expression const* waiting_on = NULL;
                    if (it->value.on_expression != NO_EXPRESSION)
                        waiting_on = &it->value.on_block->parsed_expressions[it->value.on_expression];

                    if (it->value.why == WAITING_ON_OPERAND)
                        continue;
                    else if (it->value.why == WAITING_ON_DECLARATION)
                    {
                        report.continuation();
                        report.message("This name is waiting for a declaration..."_s);
                        report.snippet(expr, /* skinny */ true);
                        if (waiting_on)
                        {
                            report.message("and this is the declaration it's waiting for."_s);
                            report.snippet(waiting_on, /* skinny */ true);
                        }
                    }
                    else if (it->value.why == WAITING_ON_PARAMETER_INFERENCE)
                    {
                        report.continuation();
                        report.message("This call is waiting for a parameter type."_s);
                        report.snippet(expr, /* skinny */ true);
                        if (waiting_on)
                        {
                            report.message("and this is the parameter it's waiting for."_s);
                            report.snippet(waiting_on, /* skinny */ true);
                        }
                    }
                    else if (it->value.why == WAITING_ON_EXTERNAL_INFERENCE)
                    {
                        report.continuation();
                        report.message("This alias is waiting to be inferred."_s);
                        report.snippet(expr, /* skinny */ true);
                    }
                    else if (it->value.why == WAITING_ON_CONDITION_INFERENCE)
                    {
                        report.continuation();
                        report.message("This block is waiting for the baked condition to be inferred."_s);
                        report.snippet(expr, /* skinny */ true);
                    }
                    else Unreachable;
                }
            }
            report.done();
            return false;
        }
    }

    For (ctx->units_to_patch)
    {
        Unit* unit = *it;
        assert(unit->flags & UNIT_IS_PLACED);
        assert(!(unit->flags & UNIT_IS_PATCHED));

        generate_bytecode_for_unit_completion(unit);
        unit->flags |= UNIT_IS_PATCHED;
    }

    For (ctx->units_to_patch)
    {
        Unit* unit = *it;
        if (!(unit->flags & UNIT_IS_RUN)) continue;
        run_unit(unit);
    }

    free_heap_array(&ctx->units_to_patch);

    return true;
}



ExitApplicationNamespace
