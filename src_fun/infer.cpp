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

    Compiler* ctx = unit->env->ctx;
    ctx->count_inferred_blocks++;

    Pipeline_Task task = {};
    task.kind  = PIPELINE_TASK_INFER_BLOCK;
    task.unit  = unit;
    task.block = block;
    add_item(&unit->env->pipeline, &task);
    return block;
}



User_Type* get_user_type_data(Environment* env, Type type)
{
    assert(type >= TYPE_FIRST_USER_TYPE);
    return &env->user_types[type - TYPE_FIRST_USER_TYPE];
}

static Type create_user_type(Unit* unit)
{
    Environment* env = unit->env;
    Type type = (Type)(TYPE_FIRST_USER_TYPE + env->user_types.count);
    User_Type* data = reserve_item(&env->user_types);
    data->unit = unit;
    return type;
}

static bool is_user_type_sizeable(Environment* env, Type type)
{
    assert(is_user_defined_type(type));
    User_Type* data = get_user_type_data(env, type);
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
    User_Type* data = get_user_type_data(unit->env, type);
    if (data->has_alias)
        return get_identifier(unit->env->ctx, &data->alias);

    // @ErrorReporting more detail, what unit?
    return "unit"_s;
}



static Find_Result find_declaration_internal(
        Environment* env, Token const* name,
        Block* scope, Visibility visibility_limit,
        Block** out_decl_scope, Expression* out_decl_expr,
        Dynamic_Array<Resolved_Name::Use>* out_use_chain,
        bool allow_parent_traversal,
        bool allow_alias_using_traversal,
        Dynamic_Array<Block*>* visited_scopes)
{
    Find_Result status = FIND_FAILURE;
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
            // if the name is deleted, forget it
            if (expr->kind == EXPRESSION_DELETE &&
                (expr->visibility_limit <= visibility_limit || visibility_limit == NO_VISIBILITY) &&
                expr->deleted_name.atom == name->atom)
            {
                *out_decl_scope = scope;
                *out_decl_expr = id;
                status = FIND_DELETED;
                continue;
            }
            // if we found a name and didn't delete it, no need to keep looking
            else if (status != FIND_FAILURE && status != FIND_DELETED) continue;
            // if it's not a declaration of the name we want, don't care
            else if (expr->kind != EXPRESSION_DECLARATION ||
                     expr->declaration.name.atom != name->atom) continue;
            // if the declaration is not visible, don't care
            else if ((expr->flags & EXPRESSION_DECLARATION_IS_ORDERED) &&
                     (expr->visibility_limit >= visibility_limit ||
                            visibility_limit == NO_VISIBILITY)) continue;
            // we found the name, but keep looking since it might have been deleted
            *out_decl_scope = scope;
            *out_decl_expr = id;
            status = FIND_SUCCESS;
        }
        if (status != FIND_FAILURE && status != FIND_DELETED)
            return status;

        // 2: try to find declarations from a used scope
        if (out_use_chain)
        {
            for (Expression id = {}; id < scope->parsed_expressions.count; id = (Expression)(id + 1))
            {
                auto* expr = &scope->parsed_expressions[id];

                // if the name is deleted, forget it
                if (expr->kind == EXPRESSION_DELETE &&
                    (expr->visibility_limit <= visibility_limit || visibility_limit == NO_VISIBILITY) &&
                    expr->deleted_name.atom == name->atom)
                {
                    *out_decl_scope = scope;
                    *out_decl_expr = id;
                    status = FIND_DELETED;
                    continue;
                }
                // if we found a name and didn't delete it, no need to keep looking
                else if (status != FIND_FAILURE && status != FIND_DELETED) continue;
                // if it's not a using declaration, don't care
                if (expr->kind != EXPRESSION_DECLARATION ||
                    !(expr->flags & EXPRESSION_DECLARATION_IS_USING)) continue;

                auto* infer = &scope->inferred_expressions[id];
                if (infer->type == INVALID_TYPE)
                {
                    *out_decl_scope = scope;
                    *out_decl_expr  = id;
                    status = FIND_WAIT;
                    continue;
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
                        status = FIND_WAIT;
                        continue;
                    }
                    user_type = *constant_type;
                    using_visibility = NO_VISIBILITY;
                }

                if (!is_user_defined_type(user_type)) continue;
                User_Type* data = get_user_type_data(env, user_type);
                assert(data->unit);

                Find_Result result = find_declaration_internal(env, name, data->unit->entry_block, using_visibility,
                                                               out_decl_scope, out_decl_expr, out_use_chain,
                                                               /* allow_parent_traversal */ false,
                                                               /* allow_alias_using_traversal */ true, // @Reconsider when implementing private
                                                               visited_scopes);
                if (result == FIND_FAILURE) continue;
                if (result == FIND_DELETED) continue;
                if (result == FIND_WAIT)
                {
                    status = FIND_WAIT;
                    continue;
                }
                assert(result == FIND_SUCCESS);

                Resolved_Name::Use use = { scope, id };
                insert_item(out_use_chain, &use, 0);
                status = FIND_SUCCESS;
            }
        }

        // if deleted, found, or need to wait, don't recurse to the parent
        if (status != FIND_FAILURE)
            return status;

        // 3: try to recurse to parent scope
        if (!allow_parent_traversal)
            break;
        visibility_limit = scope->parent_scope_visibility_limit;
        scope            = scope->parent_scope;
    }
    return FIND_FAILURE;
}

Find_Result find_declaration(Environment* env, Token const* name,
                             Block* scope, Visibility visibility_limit,
                             Block** out_decl_scope, Expression* out_decl_expr,
                             Dynamic_Array<Resolved_Name::Use>* out_use_chain,
                             bool allow_parent_traversal)
{
    Dynamic_Array<Block*> visited_scopes = {};
    Defer(free_heap_array(&visited_scopes));
    return find_declaration_internal(env, name, scope, visibility_limit,
                                     out_decl_scope, out_decl_expr, out_use_chain,
                                     allow_parent_traversal, /* allow_alias_using_traversal */ true,
                                     &visited_scopes);
}


static umm edit_distance(String a, String b)
{
    Scope_Region_Cursor temp_cursor(temp);
    if (a.length < b.length) swap(&a, &b);
    Array<umm> prev = allocate_array<umm>(temp, b.length + 1);
    Array<umm> next = allocate_array<umm>(temp, b.length + 1);

    auto min3 = [](umm x, umm y, umm z) { return x < y ? (x < z ? x : z) : (y < z ? y : z); };
    for (umm j = 0; j <= b.length; j++) prev[j] = j;
    for (umm i = 1; i <= a.length; i++)
    {
        next[0] = i;
        for (umm j = 1; j <= b.length; j++)
            if (a[a.length - i] == b[b.length - j])
                next[j] = prev[j - 1];
            else
                next[j] = 1 + min3(next[j - 1], prev[j - 1], prev[j]);
        swap(&prev, &next);
    }
    return prev[b.length];
}

static void helpful_error_for_missing_name(Compiler* ctx, String base_error,
                                           Token const* name, Type lhs_type,
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
                {
                    hint = "Maybe you are referring to this, but you don't have visibility because it's in an outer scope."_s;
                    if (lhs_type == TYPE_SOFT_TYPE)
                    {
                        assert(!(expr->flags & EXPRESSION_DECLARATION_IS_ALIAS));
                        hint = "Maybe you are referring to this, but you can only refer to type members which are aliases,\n"
                               "while this declares a runtime variable."_s;
                    }
                }

                Report(ctx).part(name, base_error)
                           .part(decl_name, hint)
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
        return unit->env->pointer_size;

    if (is_user_defined_type(type))
    {
        User_Type* data = get_user_type_data(unit->env, type);
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
    case TYPE_UMM:         return unit->env->pointer_size;
    case TYPE_S8:          return 1;
    case TYPE_S16:         return 2;
    case TYPE_S32:         return 4;
    case TYPE_S64:         return 8;
    case TYPE_SMM:         return unit->env->pointer_size;
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
        return unit->env->pointer_alignment;

    if (is_user_defined_type(type))
    {
        User_Type* data = get_user_type_data(unit->env, type);
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

bool get_numeric_description(Unit* unit, Numeric_Description* desc, Type type)
{
    ZeroStruct(desc);
    if (!is_numeric_type(type))
        return false;

    desc->is_signed         = !is_unsigned_integer_type(type);
    desc->is_integer        =  is_integer_type(type);
    desc->is_floating_point =  is_floating_point_type(type);

    desc->bits = get_type_size(unit, type) * 8;
    desc->radix = 2;

    if (desc->is_floating_point)
    {

        smm exp_min;
        smm exp_max;
        umm exp_bits;
        umm sig_bits;
        switch (type)
        {
        case TYPE_F16: exp_min =   -14; exp_max =   15; exp_bits =  5; sig_bits = 10; break;
        case TYPE_F32: exp_min =  -126; exp_max =  127; exp_bits =  8; sig_bits = 23; break;
        case TYPE_F64: exp_min = -1022; exp_max = 1023; exp_bits = 11; sig_bits = 52; break;
        IllegalDefaultCase;
        }

        desc->supports_subnormal     = true;
        desc->supports_infinity      = true;
        desc->supports_nan           = true;
        desc->mantissa_bits          = sig_bits + 1;
        desc->significand_bits       = sig_bits;
        desc->exponent_bits          = exp_bits;
        desc->exponent_bias          = exp_max;
        desc->min_exponent           = exp_min;
        desc->min_exponent_subnormal = exp_min - sig_bits;
        desc->max_exponent           = exp_max;
    }

    return true;
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

static bool set_inferred_type(Block* block, Expression id, Type type)
{
    auto* expr  = &block->parsed_expressions[id];
    auto* infer = &block->inferred_expressions[id];

    assert(!(infer->flags & INFERRED_EXPRESSION_COMPLETED_INFERENCE));
    assert(type != INVALID_TYPE);
    bool first_time = (infer->type == INVALID_TYPE);
    assert(first_time || infer->type == type);

    if (type == TYPE_SOFT_ZERO)
        assert(expr->kind == EXPRESSION_ZERO);
    if (is_soft_type(type))
    {
        infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
        infer->flags |= INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE;
    }
    infer->type = type;

    return first_time;
}



Constant* get_constant(Block* block, Expression expr, Type type_assertion)
{
    auto* infer = &block->inferred_expressions[expr];
    assert(infer->type == type_assertion);
    if (infer->constant == INVALID_CONSTANT)
        return NULL;
    return &block->constants[infer->constant];
}

void set_constant(Compiler* ctx, Block* block, Expression expr, Type type_assertion, Constant* value)
{
    auto* infer = &block->inferred_expressions[expr];
    assert(!(infer->flags & INFERRED_EXPRESSION_COMPLETED_INFERENCE));
    assert(infer->type == type_assertion);
    assert(infer->constant == INVALID_CONSTANT);
    infer->constant = block->constants.count;
    add_item(&block->constants, value);
    ctx->count_inferred_constants++;
}



static bool copy_constant(Environment* env, Block* to_block, Expression to_id, Block* from_block, Expression from_id, Type type_assertion, Token const* alias = NULL)
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
            User_Type* data = get_user_type_data(env, constant.type);
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

    set_constant(env->ctx, to_block, to_id, type_assertion, &constant);
    return true;
}

static bool check_constant_fits_in_runtime_type(Unit* unit, Parsed_Expression const* expr, Fraction const* fraction, Type type)
{
#define Error(...) return (report_error(ctx, expr, Format(temp, ##__VA_ARGS__)), false)
    if (fract_is_zero(fraction))
        return true;

    Environment* env = unit->env;
    Compiler*    ctx = env->ctx;
    assert(!is_soft_type(type));
    assert(is_numeric_type(type));
    String name = exact_type_description(unit, type);

    if (is_floating_point_type(type))
    {
        Numeric_Description numeric;
        bool numeric_ok = get_numeric_description(unit, &numeric, type);
        assert(numeric_ok);

        Integer mantissa = {};
        smm exponent;
        umm mantissa_size, msb;
        umm count_decimals = -numeric.min_exponent_subnormal;
        bool exact = fract_scientific_abs(fraction, count_decimals, &mantissa, &exponent, &mantissa_size, &msb);
        Defer(int_free(&mantissa));


        bool fits = true;
        bool next_is_infinity = false;
        Fraction f_prev = {};
        Fraction f_next = {};
        Defer(fract_free(&f_prev));
        Defer(fract_free(&f_next));

        // Check if the number is too large.
        if (exponent > numeric.max_exponent)
        {
            assert(numeric.max_exponent >= numeric.significand_bits);

            Integer prev = {};
            Defer(int_free(&prev));
            for (umm i = 0; i <= numeric.significand_bits; i++)
                int_set_bit(&prev, numeric.max_exponent - i);

            Integer zero = {};
            Integer one = {};
            int_set16(&one, 1);
            f_prev = fract_make(&prev, &one);
            f_next = fract_make(&zero, &one);
            int_free(&one);

            next_is_infinity = true;
            fits = false;
        }
        // Check if the number is too small.
        else if (int_is_zero(&mantissa) || exponent < numeric.min_exponent_subnormal)
        {
            int_set16(&f_prev.num, 0);
            int_set16(&f_prev.den, 1);
            int_set16(&f_next.num, 1);
            int_set_bit(&f_next.den, count_decimals);
            fits = false;
        }
        // Check if the mantissa is inexact.
        else if (!exact || mantissa_size > numeric.mantissa_bits)
        {
            // To get the closest smaller value, we copy the first
            // significand_bits+1 bits after the most significant bit.
            Integer prev = {};
            Defer(int_free(&prev));

            for (smm bit = msb;
                     bit >= 0 && bit >= msb - numeric.significand_bits;
                     bit--)
                if (int_test_bit(&mantissa, bit))
                    int_set_bit(&prev, bit);

            // To get the next largest value, we add one at the least significant
            // available position.
            Integer next = {};
            Defer(int_free(&next));
            {
                smm bit = msb - numeric.significand_bits;
                if (bit < 0) bit = 0;

                Integer to_add = {};
                int_set_bit(&to_add, bit);
                int_add(&next, &prev, &to_add);
                int_free(&to_add);
            }

            if (exponent == numeric.max_exponent && int_log2_abs(&next) > msb)
                next_is_infinity = true;

            smm exponent_fixup = exponent - (smm)(msb);
            if (exponent_fixup <= 0)
            {
                Integer den = {};
                int_set_bit(&den, -exponent_fixup);
                f_prev = fract_make(&prev, &den);
                f_next = fract_make(&next, &den);
                int_free(&den);
            }
            else
            {
                Integer den = {};
                int_set16(&den, 1);
                int_shift_left(&prev, exponent_fixup);
                int_shift_left(&next, exponent_fixup);
                f_prev = fract_make(&prev, &den);
                f_next = fract_make(&next, &den);
                int_free(&den);
            }
            fits = false;
        }

        if (fits)
            return true;

        bool negative = fract_is_negative(fraction);
        if (negative)
        {
            if (!fract_is_zero(&f_prev)) f_prev.num.negative = true;
            if (!fract_is_zero(&f_next)) f_next.num.negative = true;
        }

        fract_reduce(&f_prev);
        fract_reduce(&f_next);

        String s_prev = fract_display(&f_prev);
        String s_next = fract_display(&f_next);
        String s_frac = fract_display(fraction);
        String s_prev_hex = Format(temp, " (%)", fract_display_hex(&f_prev));
        String s_next_hex = Format(temp, " (%)", fract_display_hex(&f_next));

        // Pad the three numbers so '.' or ends align
        auto numeric_pad = [](String* a, String* b, String* c)
        {
            auto pad = [](String* s, umm to_add)
            {
                if (!to_add) return;
                String new_s = allocate_uninitialized_string(temp, s->length + to_add);
                memset(new_s.data, ' ', to_add);
                memcpy(new_s.data + to_add, s->data, s->length);
                *s = new_s;
            };

            auto whole_length = [](String s)
            {
                umm l = find_first_occurance(s, '.');
                return (l == NOT_FOUND) ? s.length : l;
            };

            umm a_len = whole_length(*a);
            umm b_len = whole_length(*b);
            umm c_len = whole_length(*c);

            umm length = a_len;
            if (length < b_len) length = b_len;
            if (length < c_len) length = c_len;

            pad(a, length - a_len);
            pad(b, length - b_len);
            pad(c, length - c_len);
        };

        auto highlight_first_difference = [](String* str, String cmp)
        {
            if (!supports_colored_output()) return;
            umm length = str->length < cmp.length ? str->length : cmp.length;
            umm i = 0;
            for (; i < length; i++)
            {
                if (str->data[i] == cmp.data[i]) continue;
            found:
                *str = concatenate(temp,
                    substring(*str, 0, i),
                    "\x1b[41;1m"_s,
                    substring(*str, i, 1),
                    "\x1b[m"_s,
                    substring(*str, i + 1, str->length - i - 1));
                return;
            }
            if (i < str->length)
            {
                while (str->data[i] == '.' || str->data[i] == '0') i++;
                if (i < str->length) goto found;
            }
            if (str->length < cmp.length)
                *str = concatenate(temp, *str, "\x1b[41;1m \x1b[m"_s);
        };

        numeric_pad(&s_prev, &s_frac, &s_next);
        highlight_first_difference(&s_prev, s_frac);
        highlight_first_difference(&s_next, s_frac);

        if (next_is_infinity)
        {
            s_next = negative ? "-infinity"_s : "+infinity"_s;
            s_next_hex = {};
        }

        if (negative)
        {
            swap(&s_prev,     &s_next);
            swap(&s_prev_hex, &s_next_hex);
        }

        Error("The number can't be represented exactly as a %.\n"
              "     less: %~%\n"
              "    value: % (%)\n"
              "  greater: %~%\n",
              name, s_prev, s_prev_hex, s_frac, fract_display_hex(fraction), s_next, s_next_hex);
        return false;
    }

    if (!is_integer_type(type))
        NotImplemented;

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

    return true;
#undef Error
}

static void harden(Unit* unit, Block* block, Expression expr, Type type)
{
    auto* infer = &block->inferred_expressions[expr];
    assert(infer->flags & INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME);
    assert(is_soft_type(infer->type));
    assert(!is_soft_type(type));

    if (infer->hardened_type == INVALID_TYPE)
    {
        assert(!(infer->flags & INFERRED_EXPRESSION_IS_HARDENED_CONSTANT));
        infer->flags |= INFERRED_EXPRESSION_IS_HARDENED_CONSTANT;
        infer->hardened_type = type;
    }
    else
    {
        assert(infer->flags & INFERRED_EXPRESSION_IS_HARDENED_CONSTANT);
        assert(infer->hardened_type == type);
    }
}

static bool pattern_matching_inference(Unit* unit, Block* block, Expression id, Type type,
                                       Parsed_Expression const* inferred_from, Type full_inferred_type)
{
    Environment* env = unit->env;
    Compiler*    ctx = env->ctx;
    assert(type != INVALID_TYPE);
    assert(!is_soft_type(type));

    auto* expr = &block->parsed_expressions[id];
    assert(expr->flags & EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED);

    switch (expr->kind)
    {

    case EXPRESSION_DECLARATION:
    {
        if (expr->flags & EXPRESSION_DECLARATION_IS_INFERRED_ALIAS)
        {
            assert(expr->declaration.type  == NO_EXPRESSION);
            assert(expr->declaration.value == NO_EXPRESSION);
            set_inferred_type(block, id, TYPE_SOFT_TYPE);
            set_constant_type(ctx, block, id, type);
            complete_expression(block, id);
        }
        else if (expr->flags & EXPRESSION_DECLARATION_IS_ALIAS)
        {
            assert(expr->declaration.type  == NO_EXPRESSION);
            assert(expr->declaration.value != NO_EXPRESSION);
            if (!pattern_matching_inference(unit, block, expr->declaration.value, type, inferred_from, full_inferred_type))
                return false;
            set_inferred_type(block, id, TYPE_SOFT_TYPE);
            set_constant_type(ctx, block, id, type);
            complete_expression(block, id);
        }
        else Unreachable;
    } break;

    case EXPRESSION_ADDRESS:
    {
        if (!is_pointer_type(type))
        {
            String full_type = exact_type_description(unit, full_inferred_type);
            Report(ctx).part(expr, "Can't match the type to the expected pattern."_s)
                       .part(inferred_from, Format(temp, "The type is %, inferred from here:", full_type))
                       .done();
            return false;
        }

        Type reduced = set_indirection(type, get_indirection(type) - 1);
        if (!pattern_matching_inference(unit, block, expr->unary_operand, reduced, inferred_from, full_inferred_type))
            return false;

        set_inferred_type(block, id, TYPE_SOFT_TYPE);
        set_constant_type(ctx, block, id, type);
        complete_expression(block, id);
    } break;

    case EXPRESSION_DEREFERENCE:
    {
        u32 indirection = get_indirection(type) + 1;
        if (indirection > TYPE_MAX_INDIRECTION)
        {
            String full_type = exact_type_description(unit, full_inferred_type);
            Report(ctx).part(expr, Format(temp, "Can't match the type to the expected pattern, because it would exceed the maximum indirection %.", TYPE_MAX_INDIRECTION))
                       .part(inferred_from, Format(temp, "The type is %, inferred from here:", full_type))
                       .done();
        }

        Type indirected = set_indirection(type, indirection);
        if (!pattern_matching_inference(unit, block, expr->unary_operand, indirected, inferred_from, full_inferred_type))
            return false;

        set_inferred_type(block, id, TYPE_SOFT_TYPE);
        set_constant_type(ctx, block, id, type);
        complete_expression(block, id);
    } break;

    IllegalDefaultCase;

    }

    return true;
}


enum Hardening_Result
{
    HARDENING_NONE,         // both types aren't soft and match
    HARDENING_SOFT,         // both types are soft and match
    HARDENING_HARDENED,     // one of the types was soft and hardened to match
    HARDENING_MISMATCH,     // types can't be made to match, caller should report
    HARDENING_ERROR,        // error in hardening, already reported
    HARDENING_WAIT,         // have to wait to harden
};

static Hardening_Result harden_binary(Unit* unit, Block* block, Expression id, Type* out_type)
{
    *out_type = INVALID_TYPE;

    Environment* env = unit->env;
    Compiler*    ctx = env->ctx;

    auto* expr  = &block->parsed_expressions  [id];
    auto* infer = &block->inferred_expressions[id];

    Expression lhs = expr->binary.lhs;
    Expression rhs = expr->binary.rhs;

    Type lhs_type = block->inferred_expressions[lhs].type;
    Type rhs_type = block->inferred_expressions[rhs].type;

    bool lhs_soft = is_soft_type(lhs_type);
    bool rhs_soft = is_soft_type(rhs_type);
    if (lhs_soft && rhs_soft)
    {
        if (lhs_soft != rhs_soft)
            return HARDENING_MISMATCH;
        *out_type = lhs_type;
        return HARDENING_SOFT;
    }

    if (lhs_soft || rhs_soft);
    {
        assert(!lhs_soft || !rhs_soft);
        Expression soft      = lhs_soft ? lhs : rhs;
        Expression hard      = lhs_soft ? rhs : lhs;
        Type       soft_type = block->inferred_expressions[soft].type;
        Type       hard_type = block->inferred_expressions[hard].type;

        *out_type = hard_type;

        if (is_bool_type(hard_type) && soft_type == TYPE_SOFT_BOOL)
        {
            harden(unit, block, soft, hard_type);
            return HARDENING_HARDENED;
        }

        if (is_type_type(hard_type) && soft_type == TYPE_SOFT_TYPE)
        {
            harden(unit, block, soft, hard_type);
            return HARDENING_HARDENED;
        }

        if (is_numeric_type(hard_type) && soft_type == TYPE_SOFT_NUMBER)
        {
            Fraction const* value = get_constant_number(block, soft);
            if (!value)
            {
                Wait_Info info = { WAITING_ON_OPERAND, soft, block };
                set(&block->waiting_expressions, &id, &info);
                return HARDENING_WAIT;
            }

            if (!check_constant_fits_in_runtime_type(unit, expr, value, hard_type))
                return HARDENING_ERROR;

            harden(unit, block, soft, hard_type);
            return HARDENING_HARDENED;
        }
    }

    if (lhs_type != rhs_type)
        return HARDENING_MISMATCH;
    *out_type = lhs_type;
    return HARDENING_NONE;
}


enum Yield_Result
{
    YIELD_COMPLETED,
    YIELD_MADE_PROGRESS,
    YIELD_NO_PROGRESS,
    YIELD_ERROR,
};

static Yield_Result harden_assignment(Unit* unit, Parsed_Expression const* report_expr,
                                      Block* block, Type lhs_type, Expression rhs,
                                      String report_header)
{
    Type rhs_type = block->inferred_expressions[rhs].type;

    Environment* env = unit->env;
    Compiler*    ctx = env->ctx;
#define Error(...) return (report_error(ctx, report_expr, Format(temp, ##__VA_ARGS__)), YIELD_ERROR)

    if (is_soft_type(lhs_type))
        Error("Can't assign to a constant expression.");

    if (is_bool_type(lhs_type) && rhs_type == TYPE_SOFT_BOOL)
    {
        harden(unit, block, rhs, lhs_type);
        return YIELD_COMPLETED;
    }

    if (is_type_type(lhs_type) && rhs_type == TYPE_SOFT_TYPE)
    {
        harden(unit, block, rhs, lhs_type);
        return YIELD_COMPLETED;
    }

    if (is_numeric_type(lhs_type) && rhs_type == TYPE_SOFT_NUMBER)
    {
        Fraction const* value = get_constant_number(block, rhs);
        if (!value) return YIELD_NO_PROGRESS;

        if (!check_constant_fits_in_runtime_type(unit, report_expr, value, lhs_type))
            return YIELD_ERROR;

        harden(unit, block, rhs, lhs_type);
        return YIELD_COMPLETED;
    }

    if (lhs_type != rhs_type && rhs_type != TYPE_SOFT_ZERO) types_dont_match:
        Error("%\n"
              "    lhs: %\n"
              "    rhs: %",
              report_header,
              exact_type_description(unit, lhs_type),
              exact_type_description(unit, rhs_type));

#undef Error
    return YIELD_COMPLETED;
}

static Yield_Result infer_expression(Pipeline_Task* task, Expression id)
{
    Unit*        unit  = task->unit;
    Block*       block = task->block;
    Environment* env   = unit->env;
    Compiler*    ctx   = env->ctx;

    auto* expr  = &block->parsed_expressions[id];
    auto* infer = &block->inferred_expressions[id];
    assert(!(infer->flags & INFERRED_EXPRESSION_COMPLETED_INFERENCE));


    bool override_made_progress = false;
    #define WaitReturn() return override_made_progress ? YIELD_MADE_PROGRESS : YIELD_NO_PROGRESS;

    #define Wait(why, on_expression, on_block)                                      \
    {                                                                               \
        Wait_Info info = { why, on_expression, on_block };                          \
        set(&block->waiting_expressions, &id, &info);                               \
        WaitReturn();                                                               \
    }

    #define WaitOperand(on_expression) Wait(WAITING_ON_OPERAND, on_expression, block)

    #define Error(...) return (report_error(ctx, expr, Format(temp, ##__VA_ARGS__)), YIELD_ERROR)

    #define InferType(type) { if (set_inferred_type(block, id, type)) override_made_progress = true; }
    #define InferenceComplete() \
        return (complete_expression(block, id), \
                ctx->count_inferred_expressions++, \
                ctx->count_inferred_expressions_by_kind[expr->kind]++, \
                YIELD_COMPLETED)


    if (expr->flags & EXPRESSION_HAS_CONDITIONAL_INFERENCE)
    {
        if (infer->flags & INFERRED_EXPRESSION_CONDITION_DISABLED)
        {
            infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
            infer->flags |= INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE;
            InferType(TYPE_VOID);

            if (expr->kind == EXPRESSION_BRANCH)
            {
                // spread the disabledness
                Expression on_success = expr->branch.on_success;
                Expression on_failure = expr->branch.on_failure;
                if (on_success != NO_EXPRESSION) block->inferred_expressions[on_success].flags |= INFERRED_EXPRESSION_CONDITION_DISABLED;
                if (on_failure != NO_EXPRESSION) block->inferred_expressions[on_failure].flags |= INFERRED_EXPRESSION_CONDITION_DISABLED;
            }
            else if (expr->kind == EXPRESSION_CALL)
            {
                // nothing to be done
            }
            else Unreachable;

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

    case EXPRESSION_COMMENT:
        infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
        infer->flags |= INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE;
        InferType(TYPE_VOID);
        InferenceComplete();

    case EXPRESSION_ZERO:    InferType(TYPE_SOFT_ZERO); InferenceComplete();
    case EXPRESSION_TRUE:    InferType(TYPE_SOFT_BOOL); set_constant_bool(ctx, block, id, true);  InferenceComplete();
    case EXPRESSION_FALSE:   InferType(TYPE_SOFT_BOOL); set_constant_bool(ctx, block, id, false); InferenceComplete();
    case EXPRESSION_NUMERIC_LITERAL:
    {
        InferType(TYPE_SOFT_NUMBER);
        Token_Info_Number* token_info = (Token_Info_Number*) get_token_info(ctx, &expr->literal);
        set_constant_number(ctx, block, id, fract_clone(&token_info->value));
        InferenceComplete();
    } break;

    case EXPRESSION_STRING_LITERAL:
    {
        assert(get_identifier(ctx, &get_user_type_data(env, TYPE_STRING)->alias) == "string"_s);
        InferType(TYPE_STRING);
        InferenceComplete();
    } break;

    case EXPRESSION_TYPE_LITERAL:
    {
        InferType(TYPE_SOFT_TYPE);
        assert(is_primitive_type(expr->parsed_type) || expr->parsed_type == TYPE_STRING);
        set_constant_type(ctx, block, id, expr->parsed_type);
        InferenceComplete();
    } break;

    case EXPRESSION_BLOCK:
    {
        InferType(TYPE_SOFT_BLOCK);
        Soft_Block soft = {};
        soft.materialized_parent = block;
        soft.parsed_child = expr->parsed_block;
        set_constant_block(ctx, block, id, soft);
        InferenceComplete();
    } break;

    case EXPRESSION_UNIT:
    {
        InferType(TYPE_SOFT_TYPE);

        Block* parent = (expr->flags & EXPRESSION_UNIT_IS_IMPORT) ? NULL : block;
        Unit* new_unit = materialize_unit(env, expr->parsed_block, parent);
        if (expr->parsed_block->flags & BLOCK_HAS_STRUCTURE_PLACEMENT)
            new_unit->flags |= UNIT_IS_STRUCT;
        set_constant_type(ctx, block, id, new_unit->type_id);
        InferenceComplete();
    } break;

    case EXPRESSION_NAME:
    {
        Token const* name = &expr->name.token;

        Resolved_Name resolved = get(&block->resolved_names, &id);
        if (!resolved.scope)
        {
            Find_Result result = find_declaration(env, name, block, expr->visibility_limit, &resolved.scope, &resolved.declaration, &resolved.use_chain);
            if (result == FIND_WAIT)
                Wait(WAITING_ON_USING_TYPE, resolved.declaration, resolved.scope);
            if (result == FIND_DELETED)
            {
                Report(ctx)
                    .intro(SEVERITY_ERROR, expr)
                    .message(Format(temp, "Can't find '%'. The name was deleted here.", get_identifier(ctx, name)))
                    .snippet(&resolved.scope->parsed_expressions[resolved.declaration])
                    .done();
                return YIELD_ERROR;
            }
            if (result == FIND_FAILURE)
            {
                String error = Format(temp, "Can't find '%'.", get_identifier(ctx, name));
                helpful_error_for_missing_name(ctx, error, &expr->declaration.name, INVALID_TYPE,
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
            if (!copy_constant(env, block, id, resolved.scope, resolved.declaration, decl_infer->type))
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

        User_Type* data = get_user_type_data(env, user_type);
        assert(data->unit);
        Block* member_block = data->unit->entry_block;
        assert(member_block);

        Resolved_Name resolved = get(&block->resolved_names, &id);
        if (!resolved.scope)
        {
            Find_Result result = find_declaration(env, &expr->member.name, member_block, member_visibility, &resolved.scope, &resolved.declaration, &resolved.use_chain, /* allow_parent_traversal */ false);
            if (result == FIND_WAIT)
                Wait(WAITING_ON_USING_TYPE, resolved.declaration, resolved.scope);
            if (result == FIND_FAILURE)
            {
                String compile_time = (lhs_type == TYPE_SOFT_TYPE) ? "compile-time "_s : ""_s;
                String identifier = get_identifier(ctx, &expr->member.name);
                String error = Format(temp, "Can't find %member '%' in %.", compile_time, identifier, exact_type_description(unit, user_type));
                helpful_error_for_missing_name(ctx, error, &expr->member.name, lhs_type,
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
            if (!copy_constant(env, block, id, resolved.scope, resolved.declaration, decl_infer->type))
                Wait(WAITING_ON_DECLARATION, resolved.declaration, resolved.scope);

        InferenceComplete();
    } break;

    case EXPRESSION_NOT:
    {
        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE)
            WaitOperand(expr->unary_operand);

        if (!is_bool_type(op_infer->type))
            Error("Unary operator '!' expects a bool argument, but got %.", vague_type_description(unit, op_infer->type));
        InferType(op_infer->type);

        if (op_infer->type == TYPE_SOFT_BOOL)
        {
            bool const* value = get_constant_bool(block, expr->unary_operand);
            if (!value) WaitOperand(expr->unary_operand);
            set_constant_bool(ctx, block, id, !value);
        }

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
            set_constant_number(ctx, block, id, fract_neg(value));
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
            set_constant_type(ctx, block, id, set_indirection(*type, indirection));
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
            set_constant_type(ctx, block, id, set_indirection(*type, indirection - 1));
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

        if (is_user_defined_type(*type) && !is_user_type_sizeable(env, *type))
            WaitOperand(expr->unary_operand);
        set_constant_number(ctx, block, id, fract_make_u64(get_type_size(unit, *type)));

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

        if (is_user_defined_type(*type) && !is_user_type_sizeable(env, *type))
            WaitOperand(expr->unary_operand);
        set_constant_number(ctx, block, id, fract_make_u64(get_type_alignment(unit, *type)));

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

            User_Type* data = get_user_type_data(env, *type);
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
                String identifier = get_identifier(ctx, &block->parsed_expressions[expr->binary.lhs].member.name);
                Report(ctx)
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
        InferType(lhs_type);

        switch (harden_assignment(unit, expr, block, lhs_type, expr->binary.rhs, "Types don't match."_s))
        {
        IllegalDefaultCase;
        case YIELD_COMPLETED:   break;
        case YIELD_ERROR:       return YIELD_ERROR;
        case YIELD_NO_PROGRESS: WaitOperand(expr->binary.rhs);
        }

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

        String op_token = "???"_s;
             if (expr->kind == EXPRESSION_ADD)               op_token = "+"_s;
        else if (expr->kind == EXPRESSION_SUBTRACT)          op_token = "-"_s;
        else if (expr->kind == EXPRESSION_MULTIPLY)          op_token = "*"_s;
        else if (expr->kind == EXPRESSION_DIVIDE_WHOLE)      op_token = "!/"_s;
        else if (expr->kind == EXPRESSION_DIVIDE_FRACTIONAL) op_token = "%/"_s;
        else Unreachable;


        Type common_type = INVALID_TYPE;
        Hardening_Result hardening_result = harden_binary(unit, block, id, &common_type);
        if (common_type != INVALID_TYPE)
            InferType(common_type);
        switch (hardening_result)
        {
        case HARDENING_NONE:
        case HARDENING_SOFT:
        case HARDENING_HARDENED:
        {
            if (!is_numeric_type(common_type))
                Error("Operands to '%' must be numeric, but they are %.",
                      op_token, vague_type_description(unit, common_type));
        } break;
        case HARDENING_MISMATCH:
        {
            Error("Operands to '%' must match and be numeric, but they are:\n"
                  "    lhs: %\n"
                  "    rhs: %",
                  op_token,
                  exact_type_description(unit, lhs_type),
                  exact_type_description(unit, rhs_type));
        } break;
        case HARDENING_ERROR: return YIELD_ERROR;
        case HARDENING_WAIT:  WaitReturn();
        }

        if (hardening_result == HARDENING_SOFT)
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
            set_constant_number(ctx, block, id, result);
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

        bool soft_result = is_soft_type(lhs_type) && is_soft_type(rhs_type);
        InferType(soft_result ? TYPE_SOFT_BOOL : TYPE_BOOL);

        String op_token = "???"_s;
             if (expr->kind == EXPRESSION_EQUAL)            op_token = "=="_s;
        else if (expr->kind == EXPRESSION_NOT_EQUAL)        op_token = "!="_s;
        else if (expr->kind == EXPRESSION_GREATER_THAN)     op_token = ">"_s;
        else if (expr->kind == EXPRESSION_GREATER_OR_EQUAL) op_token = ">="_s;
        else if (expr->kind == EXPRESSION_LESS_THAN)        op_token = "<"_s;
        else if (expr->kind == EXPRESSION_LESS_OR_EQUAL)    op_token = "<="_s;
        else Unreachable;

        Type common_type;
        switch (harden_binary(unit, block, id, &common_type))
        {
        case HARDENING_NONE:
        case HARDENING_SOFT:
        case HARDENING_HARDENED:
        {
            if (!is_numeric_type(common_type) && !is_bool_type(common_type) && !is_pointer_type(common_type))
                Error("Operands to '%' must be numeric, bool, or pointers, but they are %.",
                      op_token, vague_type_description(unit, common_type));
        } break;
        case HARDENING_MISMATCH:
        {
            Error("Operands to '%' must match and be numeric, bool, or pointers, but they are:\n"
                  "    lhs: %\n"
                  "    rhs: %",
                  op_token,
                  exact_type_description(unit, lhs_type),
                  exact_type_description(unit, rhs_type));
        } break;
        case HARDENING_ERROR: return YIELD_ERROR;
        case HARDENING_WAIT:  WaitReturn();
        }

        if (soft_result)
        {
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
            set_constant_bool(ctx, block, id, result);
        }

        InferenceComplete();
    } break;

    case EXPRESSION_AND:
    case EXPRESSION_OR:
    {
        Type lhs_type = block->inferred_expressions[expr->binary.lhs].type;
        Type rhs_type = block->inferred_expressions[expr->binary.rhs].type;
        if (lhs_type == INVALID_TYPE) WaitOperand(expr->binary.lhs);
        if (rhs_type == INVALID_TYPE) WaitOperand(expr->binary.rhs);

        String op_token = "???"_s;
             if (expr->kind == EXPRESSION_AND) op_token = "&"_s;
        else if (expr->kind == EXPRESSION_OR)  op_token = "|"_s;
        else Unreachable;


        Type common_type = INVALID_TYPE;
        Hardening_Result hardening_result = harden_binary(unit, block, id, &common_type);
        if (common_type != INVALID_TYPE)
            InferType(common_type);
        switch (hardening_result)
        {
        case HARDENING_NONE:
        case HARDENING_SOFT:
        case HARDENING_HARDENED:
        {
            if (!is_bool_type(common_type))
                Error("Operands to '%' must be bools, but they are %.",
                      op_token, vague_type_description(unit, common_type));
        } break;
        case HARDENING_MISMATCH:
        {
            Error("Operands to '%' must be bools, but they are:\n"
                  "    lhs: %\n"
                  "    rhs: %",
                  op_token,
                  exact_type_description(unit, lhs_type),
                  exact_type_description(unit, rhs_type));
        } break;
        case HARDENING_ERROR: return YIELD_ERROR;
        case HARDENING_WAIT:  WaitReturn();
        }

        if (hardening_result == HARDENING_SOFT)
        {
            bool const* lhs_value = get_constant_bool(block, expr->binary.lhs);
            bool const* rhs_value = get_constant_bool(block, expr->binary.rhs);
            if (!lhs_value) WaitOperand(expr->binary.lhs);
            if (!rhs_value) WaitOperand(expr->binary.rhs);

            bool result;
                 if (expr->kind == EXPRESSION_AND) result = *lhs_value && *rhs_value;
            else if (expr->kind == EXPRESSION_OR)  result = *lhs_value || *rhs_value;
            else Unreachable;

            assert(infer->type == TYPE_SOFT_BOOL);
            set_constant_bool(ctx, block, id, result);
        }

        InferenceComplete();
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
            if (is_pointer_type(rhs_infer->type))
            {
                if (!is_pointer_integer_type(cast_type))
                    Error("Can't cast a pointer to type '%', only to pointer-sized integers.", exact_type_description(unit, cast_type));
            }
            else if (!is_numeric_type(rhs_infer->type))  // @Incomplete
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
        else if (is_pointer_type(cast_type))
        {
            if (!is_pointer_type(rhs_infer->type) && !is_pointer_integer_type(rhs_infer->type))
                Error("Expected a pointer or pointer-sized integer as the second operand to 'cast', but got %.", vague_type_description(unit, rhs_infer->type));
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

                auto set_flag = [&](Expression expression, flags32 to_set)
                {
                    if (expression == NO_EXPRESSION) return;
                    flags32* flags = &block->inferred_expressions[expression].flags;
                    if ((*flags & to_set) != to_set)
                        override_made_progress = true;  // we made progress by enabling/disabling expressions
                    *flags |= to_set;
                };

                set_flag(on_success, baked_condition ? INFERRED_EXPRESSION_CONDITION_ENABLED  : INFERRED_EXPRESSION_CONDITION_DISABLED);
                set_flag(on_failure, baked_condition ? INFERRED_EXPRESSION_CONDITION_DISABLED : INFERRED_EXPRESSION_CONDITION_ENABLED);
            }
            else
            {
                if (!is_bool_type(type))
                    Error("Expected a boolean value as the condition, but got %.", vague_type_description(unit, type));
                if (is_soft_type(type))
                {
                    assert(type == TYPE_SOFT_BOOL);
                    harden(unit, block, condition, TYPE_BOOL);
                }
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
        // Wait for the callee to infer.
        auto* lhs_infer = &block->inferred_expressions[expr->call.lhs];
        if (lhs_infer->type == INVALID_TYPE) WaitOperand(expr->call.lhs);
        if (lhs_infer->type != TYPE_SOFT_BLOCK)
            Error("Expected a block on the left-hand side of the call expression, but got %.", vague_type_description(unit, lhs_infer->type));

        Soft_Block const* soft_callee = get_constant_block(block, expr->call.lhs);
        if (!soft_callee) WaitOperand(expr->call.lhs);

        if (soft_callee->parsed_child->flags & BLOCK_IS_UNIT)
            Error("The block on the left-hand side of the call expression is a unit. Units can't be called.");

        Block* lhs_parent = soft_callee->materialized_parent;
        Block* lhs_block  = soft_callee->parsed_child;
        assert(lhs_block);

        String callee_name = "anonymous callee"_s;
        if (soft_callee->has_alias)
            callee_name = get_identifier(ctx, &soft_callee->alias);

        Expression_List const* args = expr->call.arguments;
        umm count_ordered_arguments = args->count;
        umm count_named_arguments   = 0;  // @Incomplete

        struct Found_Argument
        {
            umm                      index;  // 1-indexed printable index
            Expression               expr_id;
            Parsed_Expression const* expr;
            Inferred_Expression*     infer;
            bool                     is_named;
        };

        auto get_argument_index_for_parameter_index = [&](umm index, Found_Argument* out)
        {
            // @Incomplete - when we merge in named and default parameters,
            // this will be incomplete
            if (index >= count_ordered_arguments)
                return false;

            out->index     = index + 1;
            out->expr_id   = args->expressions[index];
            out->expr      = &block->parsed_expressions[out->expr_id];
            out->infer     = &block->inferred_expressions[out->expr_id];
            out->is_named  = false;  // @Incomplete
            return true;
        };

        // If we haven't found the materialized callee yet, here we go...
        if (!infer->called_block)
        {
            Dynamic_Array<Argument_Key> argument_keys = {};
            Defer(free_heap_array(&argument_keys));  // :ClearArgumentKeysOnMaterialization

            // Go over all parameters that the callee expects:
            //  - check that they are passed correctly
            //  - wait for argument types or constants we need to infer
            //  - collect argument keys which identify equivalent calls
            umm param_index = 0;
            For (lhs_block->imperative_order)
            {
                Expression param_id = *it;
                auto* param_expr = &lhs_block->parsed_expressions[param_id];
                if (!(param_expr->flags & EXPRESSION_DECLARATION_IS_PARAMETER)) continue;
                Defer(param_index++);

                String param_name = get_identifier(ctx, &param_expr->declaration.name);

                bool has_default_value = false;  // @Incomplete
                bool must_be_named     = false;  // @Incomplete

                auto* param_type_expr = &lhs_block->parsed_expressions[param_expr->declaration.type];
                bool param_type_is_incomplete =
                    (param_type_expr->flags & EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED);
                bool param_value_is_incomplete =
                    (param_expr->flags & EXPRESSION_DECLARATION_IS_ALIAS);
                assert(!(param_type_is_incomplete && param_value_is_incomplete));

                Argument_Key* equivalence_key = reserve_item(&argument_keys);

                Found_Argument arg;
                if (!get_argument_index_for_parameter_index(param_index, &arg))
                {
                    if (has_default_value)
                    {
                        NotImplemented;
                    }

                    Error("Missing % required parameter (%) in this call to %.",
                        english_ordinal(param_index + 1), param_name, callee_name);
                }
                else if (must_be_named && !arg.is_named)
                {
                    NotImplemented;
                }
                else
                {
                    if (param_type_is_incomplete)
                    {
                        // If the parameter type is incomplete, the type will be equal
                        // to the argument type, and call equivalence depends on it.
                        Type arg_type = arg.infer->type;
                        if (arg_type == INVALID_TYPE)
                            WaitOperand(arg.expr_id);

                        assert(!param_value_is_incomplete);
                        if (is_soft_type(arg_type))
                        {
                            // @ErrorReporting - print the incomplete type (&$T for instance)
                            Report(ctx)
                                .part(arg.expr, Format(temp,
                                    "% parameter (%) in call to % has an incomplete type which can't be resolved from %.",
                                    english_ordinal(param_index + 1), param_name, callee_name, vague_type_description_in_compile_time_context(unit, arg_type)))
                               .part(param_type_expr, "You are required to pass a runtime value for this parameter because of it's type."_s)
                               .done();
                            return YIELD_ERROR;
                        }

                        equivalence_key->type = arg_type;
                    }

                    if (param_value_is_incomplete)
                    {
                        // If the parameter is an alias, the constant will be equal
                        // to the argument constant, and call equivalence depends on it.

                        Type arg_type = arg.infer->type;
                        if (arg_type == INVALID_TYPE)
                            WaitOperand(arg.expr_id);

                        assert(!param_type_is_incomplete);
                        if (!is_soft_type(arg_type))
                        {
                            Report(ctx)
                                .part(arg.expr, Format(temp,
                                    "% parameter (%) in call to % is an incomplete alias which can't be resolved from %.",
                                    english_ordinal(param_index + 1), param_name, callee_name, vague_type_description_in_compile_time_context(unit, arg_type)))
                               .part(param_type_expr, "You are required to pass a compile-time value for this parameter."_s)
                               .done();
                            return YIELD_ERROR;
                        }

                        Constant* constant = get_constant(block, arg.expr_id, arg_type);
                        if (!constant)
                            WaitOperand(arg.expr_id);

                        equivalence_key->type     =  arg_type;
                        equivalence_key->constant = *constant;
                    }
                }
            }

            // Check if an equivalent call is already being inferred.
            Call_Key call_key            = {};
            call_key.unit                = unit;
            call_key.materialized_parent = lhs_parent;
            call_key.arguments           = argument_keys;
            call_key.recompute_hash();

            Visibility visibility = (expr->flags & EXPRESSION_ALLOW_PARENT_SCOPE_VISIBILITY)
                                  ? expr->visibility_limit
                                  : NO_VISIBILITY;

            if (Call_Value call_value = get(&lhs_block->calls, &call_key))
            {
                // We found an equivalent call, so just steal its materialized callee.
                Block* caller = call_value.caller_block;
                Expression call = call_value.call_expression;

                Block* called_block = caller->inferred_expressions[call].called_block;
                assert(called_block);
                infer->called_block = called_block;

#if 0
                printf("found an existing call of %.*s! %p %d\n", StringArgs(callee_name), block, id);
                printf(" unit = %p\n", call_key.unit);
                printf(" args = [\n");
                For (call_key.arguments)
                {
                    printf("  type = %d", it->type);
                    if (it->type == TYPE_SOFT_NUMBER)
                        printf("  number = %.*s", StringArgs(fract_display(&it->constant.number)));
                    printf("\n");
                }
                printf(" ]\n");
#endif
            }
            else
            {
                // We didn't find an equivalent call, so we're the first here.

                For (argument_keys)
                    if (it->type == TYPE_SOFT_NUMBER)
                        it->constant.number = fract_clone(&it->constant.number);
                argument_keys = {};  // :ClearArgumentKeysOnMaterialization

                call_value.caller_block    = block;
                call_value.call_expression = id;
                set(&lhs_block->calls, &call_key, &call_value);

                // Materialize the callee
                Block* callee = materialize_block(unit, lhs_block, lhs_parent, visibility);
                infer->called_block = callee;

#if 0
                printf("found a new unique call of %.*s! %p %d\n", StringArgs(callee_name), block, id);
                printf(" unit = %p\n", call_key.unit);
                printf(" args = [\n");
                For (call_key.arguments)
                {
                    printf("  type = %d", it->type);
                    if (it->type == TYPE_SOFT_NUMBER)
                        printf("  number = %.*s", StringArgs(fract_display(&it->constant.number)));
                    printf("\n");
                }
                printf(" ]\n");
#endif
            }

            assert(infer->called_block);
            assert(infer->called_block->parent_scope_visibility_limit == visibility);
            override_made_progress = true;  // We made progress by finding the callee.
        }

        Expression waiting_on_return      = NO_EXPRESSION;
        Expression waiting_on_a_parameter = NO_EXPRESSION;
        Expression waiting_on_an_argument = NO_EXPRESSION;

        // Checking the parameter types and inferring the return type
        Block* callee = infer->called_block;
        umm param_index = 0;
        For (callee->imperative_order)
        {
            Expression param_id = *it;
            auto* param_expr  = &callee->parsed_expressions  [param_id];
            auto* param_infer = &callee->inferred_expressions[param_id];

            if (param_expr->flags & EXPRESSION_DECLARATION_IS_RETURN)
            {
                if (param_infer->type == INVALID_TYPE)
                    waiting_on_return = param_id;
                else
                    InferType(param_infer->type);
                continue;
            }

            if (!(param_expr->flags & EXPRESSION_DECLARATION_IS_PARAMETER)) continue;
            Defer(param_index++);

            String param_name = get_identifier(ctx, &param_expr->declaration.name);

            bool has_default_value = false;  // @Incomplete
            bool must_be_named     = false;  // @Incomplete

            auto  param_type_id    = param_expr->declaration.type;
            auto* param_type_expr  = &callee->parsed_expressions  [param_type_id];
            auto* param_type_infer = &callee->inferred_expressions[param_type_id];
            bool param_type_is_incomplete =
                (param_type_expr->flags & EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED);
            bool param_value_is_incomplete =
                (param_expr->flags & EXPRESSION_DECLARATION_IS_ALIAS);
            assert(!(param_type_is_incomplete && param_value_is_incomplete));

            Found_Argument arg;
            bool arg_found = get_argument_index_for_parameter_index(param_index, &arg);
            assert(arg_found || has_default_value);
            assert(!arg_found || !(must_be_named && !arg.is_named));

            if (!arg_found) NotImplemented;
            Type arg_type = arg.infer->type;

            if (param_type_is_incomplete && param_infer->type == INVALID_TYPE)
            {
                // If the parameter type is incomplete, the type will be equal
                // to the argument type.
                assert(arg_type != INVALID_TYPE);  // waited for this in callee inference
                assert(!is_soft_type(arg_type));   // checked in call inference
                assert(!param_value_is_incomplete);

                if (!pattern_matching_inference(unit, callee, param_type_id, arg_type, arg.expr, arg_type))
                    return YIELD_ERROR;

                param_infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
                if (set_inferred_type(callee, param_id, arg_type))
                    override_made_progress = true;  // we made progress by inferring the callee's param type.
                complete_expression(callee, param_id);
            }

            // We need to wait for the callee to infer its parameter types,
            // but we need to allow them to infer out of order, so we wait at the
            // end of the loop.
            Type const* param_type_constant = NULL;
            if (param_type_infer->type == INVALID_TYPE ||
                param_type_infer->type != TYPE_SOFT_TYPE ||
                !(param_type_constant = get_constant_type(callee, param_type_id)))
            {
                waiting_on_a_parameter = param_type_id;
                continue;
            }
            Type param_type = *param_type_constant;

            if (param_value_is_incomplete)
            {
                if (param_infer->type != INVALID_TYPE)
                    continue;  // already inferred
                assert(arg_type != INVALID_TYPE);  // waited for this in callee inference
                assert(is_soft_type(arg_type));    // checked in callee inference

                if (is_numeric_type(param_type))
                {
                    if (arg_type != TYPE_SOFT_NUMBER)
                    {
                        Report(ctx)
                        .part(arg.expr, Format(temp,
                            "% argument in this call to % is expected to be a compile-time number, but is %.",
                            english_ordinal(arg.index), callee_name, vague_type_description_in_compile_time_context(unit, arg_type)))
                        .part(param_type_expr, Format(temp,
                            "This is because it matches the % parameter '%' which is an incomplete % alias.",
                            english_ordinal(param_index + 1), param_name, exact_type_description(unit, param_type)))
                        .done();
                        return YIELD_ERROR;
                    }

                    Fraction const* fraction = get_constant_number(block, arg.expr_id);
                    assert(fraction);  // waited for this in callee inference
                    if (!check_constant_fits_in_runtime_type(unit, arg.expr, fraction, param_type))
                        return YIELD_ERROR;

                    if (set_inferred_type(callee, param_id, TYPE_SOFT_NUMBER))
                        override_made_progress = true;  // we made progress by inferring the callee's param type.
                    set_constant_number(ctx, callee, param_id, fract_clone(fraction));
                    complete_expression(callee, param_id);
                }
                else if (is_bool_type(param_type))
                {
                    if (arg_type != TYPE_SOFT_BOOL)
                    {
                        Report(ctx)
                        .part(arg.expr, Format(temp,
                            "% argument in this call to % is expected to be a compile-time boolean, but is %.",
                            english_ordinal(arg.index), callee_name, vague_type_description_in_compile_time_context(unit, arg_type)))
                        .part(param_type_expr, Format(temp,
                            "This is because it matches the % parameter '%' which is an incomplete % alias.",
                            english_ordinal(param_index + 1), param_name, exact_type_description(unit, param_type)))
                        .done();
                        return YIELD_ERROR;
                    }
                    bool const* constant = get_constant_bool(block, arg.expr_id);
                    assert(constant);  // waited for this in callee inference
                    if (set_inferred_type(callee, param_id, TYPE_SOFT_BOOL))
                        override_made_progress = true;  // we made progress by inferring the callee's param type.
                    set_constant_bool(ctx, callee, param_id, *constant);
                    complete_expression(callee, param_id);
                }
                else if (param_type == TYPE_TYPE)
                {
                    if (arg_type != TYPE_SOFT_TYPE)
                    {
                        Report(ctx)
                        .part(arg.expr, Format(temp,
                            "% argument in this call to % is expected to be a compile-time type, but is %.",
                            english_ordinal(arg.index), callee_name, vague_type_description_in_compile_time_context(unit, arg_type)))
                        .part(param_type_expr, Format(temp,
                            "This is because it matches the % parameter '%' which is an incomplete % alias.",
                            english_ordinal(param_index + 1), param_name, exact_type_description(unit, param_type)))
                        .done();
                        return YIELD_ERROR;
                    }
                    Type const* constant = get_constant_type(block, arg.expr_id);
                    assert(constant);  // waited for this in callee inference
                    if (set_inferred_type(callee, param_id, TYPE_SOFT_TYPE))
                        override_made_progress = true;  // we made progress by inferring the callee's param type.
                    set_constant_type(ctx, callee, param_id, *constant);
                    complete_expression(callee, param_id);
                }
                else if (param_type == TYPE_SOFT_BLOCK)
                {
                    if (arg_type != TYPE_SOFT_BLOCK)
                    {
                        Report(ctx)
                        .part(arg.expr, Format(temp,
                            "% argument in this call to % is expected to be a block, but is %.",
                            english_ordinal(arg.index), callee_name, vague_type_description_in_compile_time_context(unit, arg_type)))
                        .part(param_type_expr, Format(temp,
                            "This is because it matches the % parameter '%' which is an incomplete % alias.",
                            english_ordinal(param_index + 1), param_name, exact_type_description(unit, param_type)))
                        .done();
                        return YIELD_ERROR;
                    }

                    Soft_Block const* soft = get_constant_block(block, arg.expr_id);
                    assert(soft);  // waited for this in callee inference
                    if (set_inferred_type(callee, param_id, TYPE_SOFT_BLOCK))
                        override_made_progress = true;  // we made progress by inferring the callee's param type.
                    set_constant_block(ctx, callee, param_id, *soft);
                    complete_expression(callee, param_id);
                }
                else Unreachable;
                continue;
            }

            // We may need to wait for the argument type still.
            if (arg_type == INVALID_TYPE)
            {
                assert(!param_type_is_incomplete);   // ... but not for these cases, they
                assert(!param_value_is_incomplete);  // are checked in callee inference
                waiting_on_an_argument = arg.expr_id;
                continue;
            }

            // Check the assignment is valid.
            switch (harden_assignment(unit, expr, block, param_type, arg.expr_id, Format(temp,
                    "% argument in this call to % doesn't match the % parameter (%) type %.",
                    english_ordinal(arg.index), callee_name,
                    english_ordinal(param_index + 1), param_name,
                    exact_type_description(unit, param_type))))
            {
            IllegalDefaultCase;
            case YIELD_COMPLETED:   break;
            case YIELD_ERROR:       return YIELD_ERROR;
            case YIELD_NO_PROGRESS: waiting_on_an_argument = arg.expr_id; break;
            }
        }

        if (waiting_on_an_argument != NO_EXPRESSION)
            WaitOperand(waiting_on_an_argument);
        if (waiting_on_a_parameter != NO_EXPRESSION)
            Wait(WAITING_ON_PARAMETER_INFERENCE, waiting_on_a_parameter, callee);
        if (waiting_on_return != NO_EXPRESSION)
            Wait(WAITING_ON_RETURN_TYPE_INFERENCE, waiting_on_return, callee);

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

        auto infer_default_type = [&](Type type) -> Type
        {
            if (!is_soft_type(type))
                return type;

            if (type == TYPE_SOFT_BOOL)
                return TYPE_BOOL;

            if (type == TYPE_SOFT_TYPE)
                return TYPE_TYPE;

            assert(expr->declaration.value != NO_EXPRESSION);
            auto* value_expr = &block->parsed_expressions[expr->declaration.value];
            Report(ctx).part(value_expr, Format(temp, "A runtime value is required here, but this is %.\n",
                                                vague_type_description_in_compile_time_context(unit, type)))
                       .part(expr, "This is because the declaration type is inferred from the value."_s)
                       .done();
            return INVALID_TYPE;
        };

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
                value_type = infer_default_type(value_type);
                if (value_type == INVALID_TYPE)
                    return YIELD_ERROR;
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
                if (!copy_constant(env, block, id, block, expr->declaration.value, value_infer->type, &expr->declaration.name))
                    WaitOperand(expr->declaration.value);
            }
            else
            {
                Report(ctx).part(value_expr, Format(temp, "A compile-time value is required here, but this is %.",
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
                value_type = infer_default_type(value_type);
                type = value_type;
            }
            assert(type != INVALID_TYPE);
            InferType(type);

            if (expr->declaration.value != NO_EXPRESSION)
            {
                switch (harden_assignment(unit, expr, block, type, expr->declaration.value, "Types don't match."_s))
                {
                IllegalDefaultCase;
                case YIELD_COMPLETED:   break;
                case YIELD_ERROR:       return YIELD_ERROR;
                case YIELD_NO_PROGRESS: WaitOperand(expr->declaration.value);
                }
            }
        }

        InferenceComplete();
    } break;

    case EXPRESSION_RUN:
    {
        infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
        InferType(TYPE_VOID);

        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE) WaitOperand(expr->unary_operand);

        auto get_user_type_data_if_unit = [env](Type type) -> User_Type*
        {
            if (!is_user_defined_type(type)) return NULL;
            User_Type* data = get_user_type_data(env, type);
            if (!data) return NULL;
            assert(data->unit);
            if (data->unit->flags & UNIT_IS_STRUCT) return NULL;
            return data;
        };

        if (op_infer->type != TYPE_SOFT_TYPE)
        {
            // This is an error, but try give a good error when the user passes a runtime value with a unit type, instead of its type.
            User_Type* data = get_user_type_data_if_unit(op_infer->type);
            if (data && data->has_alias)
            {
                Report report(ctx);
                report.intro(SEVERITY_ERROR)
                      .part(expr, "Expected a unit type as an operand to 'run', but got a runtime unit value."_s)
                      .continuation()
                      .message("Try replacing it with the runtime value's type instead."_s)
                      .suggestion_replace(&block->parsed_expressions[expr->unary_operand], &data->alias);

                Resolved_Name resolved = get(&block->resolved_names, &expr->unary_operand);
                if (resolved.scope)
                {
                    report.continuation();
                    report.message(Format(temp, "Note: the object is declared to be of type '%' here:", exact_type_description(data->unit, op_infer->type)));
                    report.snippet(&resolved.scope->parsed_expressions[resolved.declaration], /* skinny */ true);
                }

                report.done();
                return YIELD_ERROR;
            }
            else Error("Expected a unit type as an operand to 'run', but got %.", vague_type_description(unit, op_infer->type));
        }

        Type const* type = get_constant_type(block, expr->unary_operand);
        if (!type) WaitOperand(expr->unary_operand);

        User_Type* data = get_user_type_data(env, *type);
        if (!data)
            Error("Expected a unit type as an operand to 'run', but got %.", exact_type_description(unit, *type));

        assert(data->unit->entry_block);
        assert(data->unit->entry_block->flags & BLOCK_IS_MATERIALIZED);

        Pipeline_Task* await_task = reserve_item(&env->pipeline);
        await_task->kind = PIPELINE_TASK_AWAIT_RUN;
        await_task->unit = data->unit;

        InferenceComplete();
    } break;

    case EXPRESSION_DELETE:
    {
        infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
        InferType(TYPE_VOID);
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
    Unit*        unit  = task->unit;
    Block*       block = task->block;
    Environment* env   = unit->env;
    Compiler*    ctx   = env->ctx;


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
                report_error(ctx, expr, "Blocks with structured placement may not contain any expressions evaluated at runtime."_s);
                return YIELD_ERROR;
            }

            if ((infer->flags & INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE) && is_soft_type(infer->type))
                continue;

            if (is_user_defined_type(infer->type))
                if (!is_user_type_sizeable(env, infer->type))
                    goto skip_placement;
        }

        block->flags |= BLOCK_READY_FOR_PLACEMENT;
        made_progress = true;

        // maybe complete unit placement
        assert(unit->blocks_not_ready_for_placement > 0);
        if (--unit->blocks_not_ready_for_placement == 0)
        {
            Pipeline_Task* run_task = reserve_item(&env->pipeline);
            run_task->kind = PIPELINE_TASK_PLACE;
            run_task->unit = unit;
        }

        if (false) skip_placement:
            waiting = true;
    }

    if (waiting)
        return made_progress ? YIELD_MADE_PROGRESS : YIELD_NO_PROGRESS;

    assert(unit->blocks_not_completed > 0);
    if (--unit->blocks_not_completed == 0)
    {
        Pipeline_Task* run_task = reserve_item(&env->pipeline);
        run_task->kind = PIPELINE_TASK_PATCH;
        run_task->unit = unit;
    }

    return YIELD_COMPLETED;
}


Unit* materialize_unit(Environment* env, Block* initiator, Block* materialized_parent)
{
    if (initiator->flags & BLOCK_IS_TOP_LEVEL)
    {
        assert(materialized_parent == NULL);

        u64 key = (u64) initiator;
        Unit* unit = get(&env->top_level_units, &key);
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
        set(&env->top_level_units, &key, &unit);
    }

    unit->env            = env;
    unit->initiator_from = initiator->from;
    unit->initiator_to   = initiator->to;

    unit->type_id = create_user_type(unit);

    unit->entry_block = materialize_block(unit, initiator, materialized_parent, NO_VISIBILITY);

    env->materialized_unit_count++;
    env->most_recent_materialized_unit = unit;

    env->ctx->count_inferred_units++;
    return unit;
}


void confirm_unit_placed(Unit* unit, u64 size, u64 alignment)
{
    if (!alignment) alignment = 1;
    while (size % alignment) size++;

    unit->storage_alignment = alignment;
    unit->storage_size = size;
    unit->flags |= UNIT_IS_PLACED;
}

void confirm_unit_patched(Unit* unit)
{
    unit->flags |= UNIT_IS_PATCHED;
}



Environment* make_environment(Compiler* ctx, Environment* puppeteer)
{
    Environment* env = alloc<Environment>(&ctx->pipeline_memory);

    env->ctx = ctx;
    env->user = create_user();

    env->silence_errors    = false;
    env->pointer_size      = sizeof(void*);
    env->pointer_alignment = alignof(void*);

    env->puppeteer = puppeteer;

    add_item(&ctx->environments, &env);

    String imports_relative_to_directory = "."_s; // not important, preload doesn't import
    materialize_unit(env, parse_top_level_from_memory(
        ctx, imports_relative_to_directory, "<preload>"_s,
        R"XXX(
            `string`@ :: struct
            {
                length: umm;
                base: &u8;
            }
        )XXX"_s));
    return env;
}

static void wake_puppeteer(Environment* env, Pipeline_Task for_what, bool actionable)
{
    // printf("waking puppeteer\n");
    assert(env->puppeteer);
    assert(env->puppeteer_is_waiting);
    env->puppeteer_is_waiting          = false;
    env->puppeteer_event               = for_what;
    env->puppeteer_event_is_actionable = actionable;

    Pipeline_Task* run_task = reserve_item(&env->puppeteer->pipeline);
    run_task->kind = PIPELINE_TASK_RUN;
    run_task->run_environment = env->puppeteer;
    run_task->run_from = env->puppeteer_continuation;
}

Yield_Result pump_environment(Environment* env)
{
    Compiler* ctx = env->ctx;
    bool made_progress = false;

continue_pipeline:
    if (env->puppeteer_event.kind != INVALID_PIPELINE_TASK)
    {
        assert(!env->puppeteer_is_waiting);
        return made_progress ? YIELD_MADE_PROGRESS : YIELD_NO_PROGRESS;
    }

    // printf("pumping env %p\n", env);

    if (env->pipeline.count == 0)
        return YIELD_COMPLETED;

#if STRESS_TEST
    shuffle_array(&rng, env->pipeline);
#endif

    bool had_inference_tasks_to_do = false;
    bool had_placing_to_do = false;
    for (umm it_index = 0; it_index < env->pipeline.count; it_index++)
    {
        bool task_completed = false;

        Pipeline_Task task = env->pipeline[it_index];
        if (task.kind == PIPELINE_TASK_PLACE)
        {
            env->pipeline[it_index] = env->pipeline[env->pipeline.count - 1];
            env->pipeline.count--;
            it_index--;
            had_placing_to_do = true;
            made_progress = true;

            Unit* unit = task.unit;
            assert(!(unit->flags & UNIT_IS_PLACED));

            if (env->puppeteer && env->puppeteer_has_custom_backend)
            {
                wake_puppeteer(env, task, /* actionable */ true);
                goto continue_pipeline;
            }

            generate_bytecode_for_unit_placement(unit);
            confirm_unit_placed(unit, unit->next_storage_offset, unit->storage_alignment);

            if (env->puppeteer)
            {
                wake_puppeteer(env, task, /* actionable */ false);
                goto continue_pipeline;
            }
        }
        else if (task.kind == PIPELINE_TASK_INFER_BLOCK)
        {
            had_inference_tasks_to_do = true;

            Unit* unit = task.unit;
            switch (infer_block(&task))
            {
            case YIELD_COMPLETED:       task_completed = true; // fallthrough
            case YIELD_MADE_PROGRESS:   made_progress = true;  // fallthrough
            case YIELD_NO_PROGRESS:     break;
            case YIELD_ERROR:           return YIELD_ERROR;
            IllegalDefaultCase;
            }

            if (env->materialized_unit_count >= MAX_UNITS_PER_ENVIRONMENT)
            {
                Report(ctx).part(&unit->initiator_from, Format(temp, "Too many units instantiated in this environment. Maximum is %.", MAX_UNITS_PER_ENVIRONMENT))
                           .part(&unit->entry_block->from, "The most recent instantiated unit is here. It may or may not be part of the problem."_s)
                           .done();
                return YIELD_ERROR;
            }

            if (unit->materialized_block_count >= MAX_BLOCKS_PER_UNIT)
            {
                Report(ctx).part(&unit->initiator_from, Format(temp, "Too many blocks instantiated in this unit. Maximum is %.", MAX_BLOCKS_PER_UNIT))
                           .part(&unit->most_recent_materialized_block->from, "The most recent instantiated block is here. It may or may not be part of the problem."_s)
                           .done();
                return YIELD_ERROR;
            }
        }

        if (task_completed)
        {
            env->pipeline[it_index] = env->pipeline[env->pipeline.count - 1];
            env->pipeline.count--;
            it_index--;
        }
    }

    if (had_inference_tasks_to_do || had_placing_to_do)
    {
        if (made_progress)
            goto continue_pipeline;

        Report report(ctx);
        report.intro(SEVERITY_ERROR);
        report.message("Can't make inference progress."_s);
        For (env->pipeline)
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
                else
                {
                    report.continuation();
                    report.message("Idk what this is"_s);
                    report.snippet(expr, /* skinny */ true);
                }
            }
        }
        report.done();
        return YIELD_ERROR;
    }


    bool had_patching_to_do = false;
    for (umm it_index = 0; it_index < env->pipeline.count; it_index++)
    {
        Pipeline_Task task = env->pipeline[it_index];
        if (task.kind != PIPELINE_TASK_PATCH) continue;

        env->pipeline[it_index] = env->pipeline[env->pipeline.count - 1];
        env->pipeline.count--;
        it_index--;
        had_patching_to_do = true;
        made_progress = true;

        Unit* unit = task.unit;
        assert(!(unit->flags & UNIT_IS_PATCHED));
        assert(unit->flags & UNIT_IS_PLACED);

        if (env->puppeteer && env->puppeteer_has_custom_backend)
        {
            wake_puppeteer(env, task, /* actionable */ true);
            goto continue_pipeline;
        }

        generate_bytecode_for_unit_completion(unit);
        confirm_unit_patched(unit);

        if (env->puppeteer)
        {
            wake_puppeteer(env, task, /* actionable */ false);
            goto continue_pipeline;
        }
    }

    if (had_patching_to_do)
        goto continue_pipeline;

    Pipeline_Task task = env->pipeline[0];

    if (task.kind == PIPELINE_TASK_AWAIT_RUN)
    {
        Unit* unit = task.unit;
        if (!(unit->flags & UNIT_IS_PATCHED))
            goto continue_pipeline;

        Environment* env = unit->env;
        byte* storage = user_alloc(env->user, unit->storage_size, unit->storage_alignment);
        memset(storage, 0, 3 * sizeof(void*));

        task.kind = PIPELINE_TASK_RUN;
        if (env->puppeteer && env->puppeteer_has_custom_backend)
        {
            task.unit = unit;
        }
        else
        {
            assert(unit->compiled_bytecode);
            task.run_environment = env;
            task.run_from = { unit, unit->entry_block->first_instruction, storage };
        }
    }

    env->pipeline[0] = env->pipeline[env->pipeline.count - 1];
    env->pipeline.count--;
    made_progress = true;

    assert(task.kind == PIPELINE_TASK_RUN);

    if (env->puppeteer && env->puppeteer_has_custom_backend)
    {
        wake_puppeteer(env, task, /* actionable */ true);
        goto continue_pipeline;
    }

    enter_lockdown(task.run_environment->user);
    run_bytecode(task.run_environment->user, task.run_from);
    exit_lockdown(task.run_environment->user);

    if (env->puppeteer)
    {
        wake_puppeteer(env, task, /* actionable */ false);
        goto continue_pipeline;
    }

    goto continue_pipeline;
}

bool pump_pipeline(Compiler* ctx)
{
    while (true)
    {
#if STRESS_TEST
        shuffle_array(&rng, ctx->environments);
#endif

        bool had_work      = false;
        bool made_progress = false;
        for (umm it_index = 0; it_index < ctx->environments.count; it_index++)
        {
            Environment* it = ctx->environments[it_index];
            if (it->pipeline.count == 0)
            {
                if (it->puppeteer_is_waiting)
                {
                    wake_puppeteer(it, { INVALID_PIPELINE_TASK }, /* actionable */ false);
                    made_progress = true;
                    had_work = true;
                }
                continue;
            }

            had_work = true;
            switch (pump_environment(it))
            {
            case YIELD_COMPLETED:
            case YIELD_MADE_PROGRESS: made_progress = true;  // fallthrough
            case YIELD_NO_PROGRESS:   break;
            case YIELD_ERROR:         return false;
            IllegalDefaultCase
            }
        }

        if (!had_work)
            return true;
        if (!made_progress)
        {
            Report(ctx).intro(SEVERITY_ERROR).message("Environments are stuck!"_s).done();
            return false;
        }
    }
}



ExitApplicationNamespace
