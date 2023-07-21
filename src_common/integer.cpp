#include "common.h"
#include "integer.h"

EnterApplicationNamespace




void int_free(Integer* v)
{
    free(v->digit);
    ZeroStruct(v);
}


void int_ensure_capacity(Integer* v, umm capacity)
{
    if (capacity > v->capacity)
    {
        if (!v->capacity) v->capacity = 32;
        do v->capacity *= 2;
        while (capacity > v->capacity);
        v->digit = (u32*) realloc(v->digit, v->capacity * sizeof(u32));
    }
}

void int_set_size_uninitialized(Integer* v, umm size)
{
    v->size = size;
    int_ensure_capacity(v, size);
}

void int_set_size(Integer* v, umm size)
{
    if (v->size >= size)
        v->size = size;
    else
        int_grow(v, size - v->size);
}

void int_grow(Integer* v, umm delta_size)
{
    int_set_size_uninitialized(v, v->size + delta_size);
    memset(v->digit + v->size - delta_size, 0, delta_size * sizeof(u32));
}

u32* int_digit(Integer* v, umm index)
{
    if (index >= v->size)
        int_grow(v, index - v->size + 1);
    return &v->digit[index];
}

u32 int_maybe_digit(Integer const* v, umm index)
{
    return (index < v->size) ? v->digit[index] : 0;
}

void int_normalize(Integer* v)
{
    while (v->size && v->digit[v->size - 1] == 0)
        v->size--;
    if (!v->size)
        v->negative = false;
}


void int_set_zero(Integer* v)
{
    v->size = 0;
    v->negative = false;
}

void int_set16(Integer* v, s16 value)
{
    if (!value)
    {
        int_set_zero(v);
        return;
    }

    v->negative = (value < 0);
    if (v->negative)
        *int_digit(v, 0) = -(s32) value;
    else
        *int_digit(v, 0) = value;
    v->size = 1;
}

void int_set32(Integer* v, s32 value)
{
    if (!value)
    {
        int_set_zero(v);
        return;
    }

    u64 abs = (u64) value;
    v->negative = (value < 0);
    if (v->negative)
        abs = (u64) -(s64)(value);

    *int_digit(v, 0) = abs & DIGIT_MASK;
    abs >>= DIGIT_BITS;
    if (!abs) v->size = 1;
    else
    {
        *int_digit(v, 1) = abs;
        v->size = 2;
    }
}

void int_setu64(Integer* v, u64 value)
{
    if (!value)
    {
        int_set_zero(v);
        return;
    }

    u32 size = 0;
    while (value)
    {
        *int_digit(v, size++) = value & DIGIT_MASK;
        value >>= DIGIT_BITS;
    }
    v->size = size;
}

void int_set(Integer* v, Integer const* a)
{
    int_free(v);
    int_grow(v, a->size);
    memcpy(v->digit, a->digit, v->size * sizeof(u32));
    v->negative = a->negative;
}

void int_set(Integer* v, String binary, bool big_endian)
{
    int_set_zero(v);
    u64 bits  = 0;
    u64 count = 0;
    umm digit = 0;
    for (umm i = 0; i < binary.length; i++)
    {
        bits |= (u64) binary[big_endian ? (binary.length - i - 1) : i] << count;
        count += 8;
        if (count >= DIGIT_BITS)
        {
            *int_digit(v, digit++) = bits & DIGIT_MASK;
            bits >>= DIGIT_BITS;
            count -= DIGIT_BITS;
        }
    }
    if (bits) *int_digit(v, digit++) = bits;
    int_normalize(v);
}

String int_binary_abs(Integer* v, Region* memory, bool big_endian)
{
    umm bytes = int_bytes(v);
    String binary = allocate_zero_string(memory, bytes);

    u64 bits  = 0;
    u64 count = 0;
    umm byte  = 0;
    for (umm i = 0; i < v->size; i++)
    {
        bits |= (u64) v->digit[i] << count;
        count += DIGIT_BITS;
        while (count >= 8 && byte < bytes)
        {
            binary[big_endian ? (binary.length - byte - 1) : byte] = bits;
            bits >>= 8;
            count -= 8;
            byte++;
        }
    }

    if (bits)
        binary[big_endian ? (binary.length - byte - 1) : byte] = bits;
    return binary;
}

void int_move(Integer* v, Integer* a)
{
    int_free(v);
    *v = *a;
    *a = {};
}

Integer int_clone(Integer const* v)
{
    Integer result = {};
    int_set(&result, v);
    return result;
}


bool int_is_zero(Integer const* v)
{
    return v->size == 0;
}

bool int_is_odd(Integer const* v)
{
    if (!v->size) return false;
    return v->digit[0] & 1;
}

bool int_is_even(Integer const* v)
{
    if (!v->size) return true;
    return !(v->digit[0] & 1);
}

void int_negate(Integer* v)
{
    if (v->size)
        v->negative = !v->negative;
}

void int_abs(Integer* v)
{
    v->negative = false;
}

void int_abs(Integer* v, Integer const* a)
{
    assert(v != a);
    int_set(v, a);
    int_abs(v);
}

umm int_log2_abs(Integer const* v)
{
    if (!v->size) return 0;
    umm result = (v->size - 1) * DIGIT_BITS;
    u32 most_significant = v->digit[v->size - 1];
    while (most_significant >>= 1)
        result++;
    return result;
}

umm int_ctz_abs(Integer const* v)
{
    if (!v->size) return 0;
    umm result = 0;
    umm i = 0;
    while (!v->digit[i]) result += DIGIT_BITS, i++;
    u32 digit = v->digit[i];
    while (!(digit & 1)) result++, digit >>= 1;
    return result;
}

umm int_bytes(Integer const* v)
{
    return (int_log2_abs(v) / 8) + 1;
}

smm int_sign(Integer const* a)
{
    if (!a->size) return 0;
    return a->negative ? -1 : 1;
}

smm int_compare_abs(Integer const* a, Integer const* b)
{
    if (a->size > b->size) return  1;
    if (a->size < b->size) return -1;
    for (umm i = a->size; i; i--)
    {
        if (a->digit[i - 1] > b->digit[i - 1]) return  1;
        if (a->digit[i - 1] < b->digit[i - 1]) return -1;
    }
    return 0;
}

smm int_compare(Integer const* a, Integer const* b)
{
    if (!a->size     && !b->size)     return  0;
    if ( a->negative && !b->negative) return -1;
    if (!a->negative &&  b->negative) return  1;
    if (a->negative)
        return -int_compare_abs(a, b);
    return int_compare_abs(a, b);
}

smm int_compare16(Integer const* a, s16 b)
{
    // @Optimization
    Integer temp = {};
    int_set16(&temp, b);
    smm result = int_compare(a, &temp);
    int_free(&temp);
    return result;
}

bool int_test_bit(Integer const* v, umm index)
{
    u32 bit   = index % DIGIT_BITS;
    umm digit = index / DIGIT_BITS;
    return int_maybe_digit(v, digit) & (1ul << bit);
}

void int_set_bit(Integer* v, umm index)
{
    u32 bit   = index % DIGIT_BITS;
    umm digit = index / DIGIT_BITS;
    *int_digit(v, digit) |= 1ul << bit;
}

void int_clear_bit(Integer* v, umm index)
{
    u32 bit   = index % DIGIT_BITS;
    umm digit = index / DIGIT_BITS;
    if (digit >= v->size) return;
    *int_digit(v, digit) &= ~(1ul << bit);
    int_normalize(v);
}

void int_shift_right(Integer* v, umm amount)
{
    if (!amount || int_is_zero(v)) return;

    u32 bits   = amount % DIGIT_BITS;
    umm digits = amount / DIGIT_BITS;
    if (digits >= v->size)
    {
        int_set_zero(v);
        return;
    }

    u32 mask  = (1ul << bits) - 1;
    u32 shift = DIGIT_BITS - bits;

    for (umm i = 0; i < v->size - digits; i++)
        v->digit[i] = (v->digit[i + digits] >> bits)
                    | ((int_maybe_digit(v, i + digits + 1) & mask) << shift);
    v->size -= digits;
    int_normalize(v);
}

void int_shift_left(Integer* v, umm amount)
{
    if (!amount || int_is_zero(v)) return;

    u32 bits   = amount % DIGIT_BITS;
    umm digits = amount / DIGIT_BITS;
    u32 shift  = DIGIT_BITS - bits;

    int_grow(v, digits + 1);
    for (umm i = v->size; i > digits; i--)
        v->digit[i - 1] = ((v->digit[i - digits - 1] << bits) & DIGIT_MASK)
                        | (int_maybe_digit(v, i - digits - 2) >> shift);
    for (umm i = digits; i; i--)
        v->digit[i - 1] = 0;
    int_normalize(v);
}

void int_add_ignore_sign(Integer* v, Integer const* a)  // |v| += |a|
{
    int_ensure_capacity(v, a->size);
    u32 carry = 0;
    for (umm i = 0; i < a->size || carry; i++)
    {
        u32* digit = int_digit(v, i);
        u64 sum = (u64) *digit + int_maybe_digit(a, i) + carry;
        *digit = sum & DIGIT_MASK;
        carry = sum >> DIGIT_BITS;
    }
}

void int_sub_ignore_sign(Integer* v, Integer const* a)  // |v| -= |a| assert(v >= a)
{
    u32 carry = 0;
    for (umm i = 0; i < a->size || carry; i++)
    {
        assert(i < v->size);
        u64 sum = (u64) v->digit[i] - int_maybe_digit(a, i) - carry;
        v->digit[i] = sum & DIGIT_MASK;
        carry = (sum >> DIGIT_BITS) & 1;
    }

    int_normalize(v);
}

void int_add(Integer* v, Integer const* a)
{
    if (v->negative == a->negative)
        int_add_ignore_sign(v, a);  // + plus +, - plus -
    else if (int_compare_abs(v, a) >= 0)
        int_sub_ignore_sign(v, a);  // + plus smaller -, - plus smaller +
    else
    {
        // + plus larger -, - plus larger +
        Integer result = {};
        int_set(&result, a);
        int_sub_ignore_sign(&result, v);
        int_move(v, &result);
    }
}

void int_add(Integer* v, Integer const* a, Integer const* b)
{
    assert(v != a && v != b);
    int_set(v, a);
    int_add(v, b);
}

void int_sub(Integer* v, Integer const* a)
{
    if (v->negative != a->negative)
        int_add_ignore_sign(v, a);  // + minus -, - minus +
    else if (int_compare_abs(v, a) >= 0)
        int_sub_ignore_sign(v, a);  // + minus smaller +, - minus smaller -
    else
    {
        // + minus larger +, - minus larger -
        Integer result = {};
        int_set(&result, a);
        int_sub_ignore_sign(&result, v);
        int_negate(&result);
        int_move(v, &result);
    }
}

void int_sub(Integer* v, Integer const* a, Integer const* b)
{
    assert(v != a && v != b);
    int_set(v, a);
    int_sub(v, b);
}

static constexpr umm COMBA_LIMIT = 1 << (64 - 2 * DIGIT_BITS);
static constexpr umm KARATSUBA_THRESHOLD = 80;

void int_mul(Integer* v, Integer const* a, Integer const* b, umm size_limit)
{
    assert(v != a && v != b);
    int_set_zero(v);
    if (int_is_zero(a) || int_is_zero(b))
        return;

    if (a->size < COMBA_LIMIT && b->size < COMBA_LIMIT)
    {
        int_comba_mul(v, a, b, size_limit);
        return;
    }

    if (a->size > KARATSUBA_THRESHOLD && b->size > KARATSUBA_THRESHOLD && size_limit == UMM_MAX)
    {
        int_karatsuba_mul(v, a, b);
        return;
    }

    for (umm i = 0; i < b->size; i++)
    {
        u32 carry = 0;
        for (umm j = 0; j < a->size || carry; j++)
        {
            umm k = i + j;
            if (k >= size_limit) break;
            u32* digit = int_digit(v, k);
            u64 result = *digit + (u64) int_maybe_digit(a, j) * (u64) b->digit[i] + carry;
            *digit = result & DIGIT_MASK;
            carry = result >> DIGIT_BITS;
        }
    }

    v->negative = a->negative ^ b->negative;
    int_normalize(v);
}

void int_mul(Integer* v, Integer const* a, umm size_limit)
{
    Integer result = {};
    int_mul(&result, v, a, size_limit);
    int_move(v, &result);
}

void int_comba_mul(Integer* v, Integer const* a, Integer const* b, umm size_limit)
{
    umm count = a->size + b->size;
    if (count > size_limit)
        count = size_limit;
    int_set_size_uninitialized(v, count + 1);

    u64 carry = 0;
    for (umm i = 0; i < count; i++)
    {
        umm off_b = i;
        if (off_b >= b->size)
            off_b = b->size - 1;

        umm off_a = i - off_b;

        umm n = a->size - off_a;
        if (n > off_b + 1)
            n = off_b + 1;

        u32* dig_a = a->digit + off_a;
        u32* dig_b = b->digit + off_b;
        for (umm j = 0; j < n; j++)
            carry += (u64)(*(dig_a++)) * (u64)(*(dig_b--));

        v->digit[i] = carry & DIGIT_MASK;
        carry >>= DIGIT_BITS;
    }
    v->digit[count] = carry & DIGIT_MASK;

    v->negative = a->negative ^ b->negative;
    int_normalize(v);
}

void int_karatsuba_mul(Integer* v, Integer const* a, Integer const* b)
{
    umm B = ((a->size < b->size) ? a->size : b->size) >> 1;

    // a = a1 * B + a0
    // b = b1 * B + b0
    Integer a0 = *a;
    Integer b0 = *b;
    a0.size = B;
    b0.size = B;

    Integer a1 = *a;
    Integer b1 = *b;
    a1.digit += B;
    a1.size  -= B;
    b1.digit += B;
    b1.size  -= B;

    // a * b = a1b1 * B^2 + ((a1 + a0)(b1 + b0) - (a0b0 + a1b1)) * B + a0b0

    Integer a0b0 = {};
    Integer a1b1 = {};
    int_mul(&a0b0, &a0, &b0);
    int_mul(&a1b1, &a1, &b1);

    Integer a0_plus_a1 = {};
    Integer b0_plus_b1 = {};
    int_add(&a0_plus_a1, &a0, &a1);
    int_add(&b0_plus_b1, &b0, &b1);

    // v = ((a1 + a0)(b1 + b0) - (a0b0 + a1b1)) * B
    int_mul(v, &a0_plus_a1, &b0_plus_b1);
    int_sub(v, &a0b0);
    int_sub(v, &a1b1);
    int_shift_left(v, B * DIGIT_BITS);

    // v += a0b0 + a1b1 * B^2
    int_shift_left(&a1b1, 2 * B * DIGIT_BITS);
    int_add(v, &a0b0);
    int_add(v, &a1b1);

    int_free(&a0b0);
    int_free(&a1b1);
    int_free(&a0_plus_a1);
    int_free(&b0_plus_b1);
}

bool int_div(Integer* v, Integer const* a, Integer* mod_opt)
{
    assert(v != a && v != mod_opt);
    if (int_is_zero(a)) return false;
    if (int_is_zero(v))
    {
        if (mod_opt) int_set_zero(mod_opt);
        return true;
    }

    umm v_log2 = int_log2_abs(v);
    umm a_log2 = int_log2_abs(a);
    if (a_log2 > v_log2)
    {
        if (mod_opt) int_move(mod_opt, v);
        int_free(v);
        return true;
    }

    Integer result = {};
    result.negative = v->negative ^ a->negative;

    umm steps = v_log2 - a_log2;
    Integer a_shifted = int_clone(a);
    int_shift_left(&a_shifted, steps);
    for (u32 i = 0; i <= steps; i++)
    {
        if (i) int_shift_right(&a_shifted, 1);
        if (int_compare_abs(v, &a_shifted) < 0) continue;
        int_set_bit(&result, steps - i);
        int_sub_ignore_sign(v, &a_shifted);
    }
    int_free(&a_shifted);

    int_normalize(&result);
    if (mod_opt) int_move(mod_opt, v);
    int_move(v, &result);
    return true;
}

bool int_div(Integer* v, Integer const* a, Integer const* b, Integer* mod_opt)
{
    int_set(v, a);
    return int_div(v, b, mod_opt);
}

bool int_mod(Integer* v, Integer const* a)
{
    Integer mod = {};
    if (!int_div(v, a, &mod))
        return false;
    int_free(v);
    *v = mod;
    return true;
}

bool int_mod(Integer* v, Integer const* a, Integer const* b)
{
    Integer mod = {};
    if (!int_div(v, a, b, &mod))
        return false;
    int_free(v);
    *v = mod;
    return true;
}

void int_barrett_setup(Integer* m, Integer const* n)
{
    umm k = 2 * n->size * DIGIT_BITS;
    int_set_zero(m);
    int_set_bit(m, k);
    int_div(m, n, NULL);
}

void int_barrett_reduce(Integer* a, Integer const* n, Integer const* m)
{
    umm k = 2 * n->size;

    Integer q = {};
    int_mul(&q, a, m);

    Integer q_shifted = q;
    q_shifted.size  -= k;
    q_shifted.digit += k;

    Integer qn = {};
    int_mul(&qn, &q_shifted, n);
    int_sub(a, &qn);
    int_free(&q);
    int_free(&qn);

    if (int_compare(a, n) >= 0)
        int_sub(a, n);
}

void int_square(Integer* v, umm size_limit)
{
    Integer result = {};
    int_mul(&result, v, v, size_limit);
    int_move(v, &result);
}

void int_pow(Integer* v, Integer const* a, umm size_limit)
{
    if (int_is_zero(v)) return;
    if (int_is_zero(a))
    {
        int_set16(v, 1);
        return;
    }

    Integer result = {};
    int_set16(&result, 1);

    umm a_log2 = int_log2_abs(a);
    for (umm i = 0; i <= a_log2; i++)
    {
        if (i) int_square(v, size_limit);
        if (int_test_bit(a, i))
            int_mul(&result, v, size_limit);
    }

    int_move(v, &result);
}

void int_pow(Integer* v, Integer const* a, Integer const* b, umm size_limit)
{
    int_set(v, a);
    int_pow(v, b, size_limit);
}

void int_pow_mod(Integer* v, Integer const* a, Integer const* m)
{
    if (int_is_zero(v)) return;
    if (int_is_zero(a))
    {
        int_set16(v, 1);
        return;
    }

    if (int_is_odd(m))
    {
        int_montgomery_pow_mod(v, a, m);
        return;
    }

    if (int_compare(v, m) >= 0)
        int_mod(v, m);
    Integer barrett = {};

    int_barrett_setup(&barrett, m);

    Integer result = {};
    int_set16(&result, 1);

    umm a_log2 = int_log2_abs(a);
    for (umm i = 0; i <= a_log2; i++)
    {
        if (i)
        {
            int_square(v);
            int_barrett_reduce(v, m, &barrett);
        }
        if (int_test_bit(a, i))
        {
            int_mul(&result, v);
            int_barrett_reduce(&result, m, &barrett);
        }
    }

    int_free(&barrett);
    int_move(v, &result);
}

void int_pow_mod(Integer* v, Integer const* a, Integer const* b, Integer const* m)
{
    int_set(v, a);
    int_pow_mod(v, b, m);
}

bool int_montgomery_setup(Integer const* n, u32* rho)
{
    if (int_is_even(n))
        return false;

    u32 b = n->digit[0];
    u32 x = (((b + 2) & 4) << 1) + b;
    x *= 2 - (b * x);
    x *= 2 - (b * x);
    x *= 2 - (b * x);

    *rho = ((1ul << DIGIT_BITS) - x) & DIGIT_MASK;
    return true;
}

void int_montgomery_reduce(Integer* x, Integer const* n, u32 rho)
{
    int_set_size(x, (n->size * 2) + 1);

    for (umm i = 0; i < n->size; i++)
    {
        u64 mu = (x->digit[i] * rho) & DIGIT_MASK;

        u32* cursor_n = n->digit;
        u32* cursor_x = x->digit + i;

        u64 carry = 0;
        for (umm j = 0; j < n->size; j++)
        {
            carry += *(cursor_n++) * mu + *cursor_x;
            *(cursor_x++) = (u32) carry & DIGIT_MASK;
            carry >>= DIGIT_BITS;
        }

        while (carry)
        {
            carry += *cursor_x;
            *(cursor_x++) = carry & DIGIT_MASK;
            carry >>= DIGIT_BITS;
        }
    }

    int_normalize(x);
    int_shift_right(x, n->size * DIGIT_BITS);  // @Optimization

    if (int_compare(x, n) >= 0)
        int_sub(x, n);
}

void int_to_montgomery(Integer* x, Integer const* n, u32 rho)
{
    int_mod(x, n);

    Integer temp = {};
    int_set_bit(&temp, 2 * n->size * DIGIT_BITS);
    int_mod(&temp, n);
    int_mul(x, &temp);
    int_free(&temp);

    int_montgomery_reduce(x, n, rho);
}

void int_from_montgomery(Integer* x, Integer const* n, u32 rho)
{
    int_montgomery_reduce(x, n, rho);
}

void int_montgomery_pow_mod(Integer* v, Integer const* a, Integer const* m)
{
    if (int_is_zero(v)) return;
    if (int_is_zero(a))
    {
        int_set16(v, 1);
        return;
    }

    if (int_compare(v, m) >= 0)
        int_mod(v, m);

    u32 rho;
    bool ok = int_montgomery_setup(m, &rho);
    assert(ok);

    Integer result = {};
    int_set16(&result, 1);
    int_to_montgomery(&result, m, rho);
    int_to_montgomery(v, m, rho);

    umm a_log2 = int_log2_abs(a);
    for (umm i = 0; i <= a_log2; i++)
    {
        if (i)
        {
            int_square(v);
            int_montgomery_reduce(v, m, rho);
        }
        if (int_test_bit(a, i))
        {
            int_mul(&result, v);
            int_montgomery_reduce(&result, m, rho);
        }
    }

    int_from_montgomery(&result, m, rho);
    int_move(v, &result);
}


void int_gcd(Integer* v, Integer const* a, Integer const* b)
{
    Integer t1 = {};
    Integer t2 = {};
    int_abs(&t1, a);
    int_abs(&t2, b);
    while (!int_is_zero(&t2))
    {
        int_mod(&t1, &t2);
        if (int_is_zero(&t1)) break;
        int_mod(&t2, &t1);
    }
    int_add(v, &t1, &t2);
    int_free(&t1);
    int_free(&t2);
}

void int_gcd_extended(Integer* v, Integer const* a, Integer const* b, Integer* x, Integer* y)
{
    // @Incomplete - turn into iterative function

    if (int_is_zero(a))
    {
        int_set_zero(x);
        int_set16(y, 1);
        int_set(v, b);
        return;
    }

    Integer b_div_a = {};
    Integer b_mod_a = {};
    int_div(&b_div_a, b, a, &b_mod_a);

    Integer x1 = {};
    Integer y1 = {};
    int_gcd_extended(v, &b_mod_a, a, &x1, &y1);

    int_mul(x, &b_div_a, &x1);
    int_sub(x, &y1);
    int_negate(x);
    int_set(y, &x1);

    int_free(&b_div_a);
    int_free(&b_mod_a);
    int_free(&x1);
    int_free(&y1);
}

bool int_mod_inverse(Integer* v, Integer const* a, Integer const* m)
{
    Integer gcd = {};
    Integer x = {};
    Integer y = {};
    int_gcd_extended(&gcd, a, m, &x, &y);
    bool exists = (int_compare16(&gcd, 1) == 0);
    if (exists)
    {
        int_mod(v, &x, m);
        int_add(v, m);
        int_mod(v, m);
    }
    int_free(&gcd);
    int_free(&x);
    int_free(&y);
    return exists;
}


String int_print(Integer const* v, Region* memory)
{
    String_Concatenator cat = {};
    if (v->negative)
        add(&cat, "-"_s);

    add(&cat, "0x"_s);
    if (!v->size)
    {
        add(&cat, "0"_s);
        return resolve_to_string_and_free(&cat, temp);
    }

    FormatAdd(&cat, "%", hex_format(v->digit[v->size - 1]));
    for (umm i = v->size - 1; i; i--)
        FormatAdd(&cat, "%", hex_format(v->digit[i - 1], 7));
    return resolve_to_string_and_free(&cat, temp);
}




ExitApplicationNamespace
