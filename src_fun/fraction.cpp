#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"
#include <stdio.h>

EnterApplicationNamespace


String int_base10(Integer const* integer, Region* memory, umm min_digits)
{
    Integer i = int_clone(integer);
    Defer(int_free(&i));
    if (i.negative)
        int_negate(&i);

    Integer ten = {};
    int_set16(&ten, 10);
    Defer(int_free(&ten));

    String_Concatenator cat = {};
    if (int_is_zero(&i))
        add(&cat, "0"_s);
    else while (!int_is_zero(&i))
    {
        Integer mod = {};
        Defer(int_free(&mod));
        int_div(&i, &ten, &mod);

        u32 number = mod.size ? mod.digit[0] : 0;
        assert(number < 10);
        char c = '0' + number;
        add(&cat, &c, 1);
    }
    while (cat.count < min_digits)
        add(&cat, "0"_s);

    if (integer->negative)
        add(&cat, "-"_s);

    String str = resolve_to_string_and_free(&cat, memory);
    for (umm i = 0; i < str.length - i - 1; i++)
    {
        u8 temp = str[i];
        str[i] = str[str.length - i - 1];
        str[str.length - i - 1] = temp;
    }
    return str;
}


static void quick_asserts(Fraction const* f)
{
    assert(!f->den.negative);
    assert(!int_is_zero(&f->den));
}

Fraction fract_make(Integer const* num, Integer const* den)
{
    assert(!int_is_zero(den));
    Fraction result = {};
    result.num = int_clone(num);
    result.den = int_clone(den);
    fract_reduce(&result);
    return result;
}

void fract_free(Fraction* f)
{
    int_free(&f->num);
    int_free(&f->den);
    ZeroStruct(f);
}

Fraction fract_clone(Fraction const* from)
{
    quick_asserts(from);
    Fraction result = {};
    result.num = int_clone(&from->num);
    result.den = int_clone(&from->den);
    return result;
}

void fract_reduce(Fraction* f)
{
    assert(!int_is_zero(&f->den));
    if (f->den.negative)
    {
        f->den.negative = false;
        int_negate(&f->num);
    }

    Integer gcd = {};
    Defer(int_free(&gcd));

    int_gcd(&gcd, &f->num, &f->den);
    bool ok1 = int_div(&f->num, &gcd, NULL);
    bool ok2 = int_div(&f->den, &gcd, NULL);
    assert(ok1 && ok2);
}

bool fract_is_zero(Fraction const* f)
{
    quick_asserts(f);
    return int_is_zero(&f->num);
}

bool fract_is_integer(Fraction const* f)
{
    quick_asserts(f);
    return f->den.size == 1 && f->den.digit[0] == 1;
}

Fraction fract_neg(Fraction const* a)
{
    quick_asserts(a);
    Fraction result = fract_clone(a);
    int_negate(&result.num);
    return result;
}

Fraction fract_add(Fraction const* a, Fraction const* b)
{
    quick_asserts(a);
    quick_asserts(b);

    Fraction result = {};
    int_mul(&result.num, &a->num, &b->den);
    int_mul(&result.den, &a->den, &b->den);

    Integer temp = {};
    int_mul(&temp, &b->num, &a->den);
    int_add(&result.num, &temp);
    int_free(&temp);

    fract_reduce(&result);
    return result;
}

Fraction fract_sub(Fraction const* a, Fraction const* b)
{
    quick_asserts(a);
    quick_asserts(b);

    Fraction result = {};
    int_mul(&result.num, &a->num, &b->den);
    int_mul(&result.den, &a->den, &b->den);

    Integer temp = {};
    int_mul(&temp, &b->num, &a->den);
    int_sub(&result.num, &temp);
    int_free(&temp);

    fract_reduce(&result);
    return result;
}

Fraction fract_mul(Fraction const* a, Fraction const* b)
{
    quick_asserts(a);
    quick_asserts(b);
    Fraction result = fract_clone(a);
    int_mul(&result.num, &b->num);
    int_mul(&result.den, &b->den);
    fract_reduce(&result);
    return result;
}

bool fract_div_fract(Fraction* out, Fraction const* a, Fraction const* b)
{
    ZeroStruct(out);
    quick_asserts(a);
    quick_asserts(b);
    if (fract_is_zero(b))
        return false;
    Fraction result = fract_clone(a);
    int_mul(&result.num, &b->den);
    int_mul(&result.den, &b->num);
    fract_reduce(&result);
    *out = result;
    return true;
}

bool fract_div_whole(Fraction* out, Fraction const* a, Fraction const* b)
{
    if (!fract_div_fract(out, a, b))
        return false;
    int_div(&out->num, &out->den, NULL);
    int_set16(&out->den, 1);
    return true;
}


static Integer int_pow(u64 a, u64 b)
{
    Integer a_int = {};
    int_setu64(&a_int, a);
    Integer b_int = {};
    int_setu64(&b_int, b);
    int_pow(&a_int, &b_int);
    int_free(&b_int);
    return a_int;
}

String fract_display(Fraction const* f, Region* memory)
{
    quick_asserts(f);
    if (fract_is_integer(f))
    {
        return int_base10(&f->num, memory);
    }
    else
    {
        Integer int_part = {};
        Integer remainder = {};
        Defer(int_free(&int_part));
        Defer(int_free(&remainder));
        bool ok = int_div(&int_part, &f->num, &f->den, &remainder);
        assert(ok);
        assert(!int_is_zero(&remainder));
        int_abs(&remainder);

        umm count_digits = 10;
        Integer digits = int_pow(10, count_digits);
        int_mul(&digits, &remainder);
        Defer(int_free(&digits));

        Integer mod = {};
        Defer(int_free(&mod));
        ok = int_div(&digits, &f->den, &mod);
        assert(ok);
        bool is_exact = int_is_zero(&mod);

        if (!is_exact)  // rounding
        {
            int_shift_left(&mod, 1);
            if (int_compare_abs(&mod, &f->den) >= 0)
            {
                Integer one = {};
                int_set16(&one, 1);
                int_add(&digits, &one);
                int_free(&one);
            }
        }

        String result = concatenate(memory, int_base10(&int_part), "."_s, int_base10(&digits, temp, count_digits), is_exact ? ""_s : "..."_s);
        while (result[result.length - 1] == '0')
            result.length--;
        return result;
    }
}



ExitApplicationNamespace
