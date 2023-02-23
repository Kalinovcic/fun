EnterApplicationNamespace


static constexpr umm DIGIT_BITS = 28;
static constexpr u32 DIGIT_MASK = (1ul << DIGIT_BITS) - 1;

struct Integer
{
    umm  size;
    umm  capacity;
    bool negative;
    u32* digit;  // absolute value, base 2^28, little endian
};

void int_free(Integer* v);

void int_set_zero(Integer* v);                                      // v = 0
void int_set16(Integer* v, s16 value);                              // v = value
void int_set32(Integer* v, s32 value);                              // v = value
void int_setu64(Integer* v, u64 value);                             // v = value
void int_set(Integer* v, Integer const* a);                         // v = a
void int_set(Integer* v, String binary, bool big_endian);           // v = binary
String int_binary_abs(Integer* v, Region* memory, bool big_endian); // returns |v| as binary
void int_move(Integer* v, Integer* a);                              // free v, v <- a, a <- {}
Integer int_clone(Integer const* v);

bool int_is_zero(Integer const* v);                                 // true if v == 0
bool int_is_odd (Integer const* v);                                 // true if v is odd
bool int_is_even(Integer const* v);                                 // true if v is even
void int_negate(Integer* v);                                        // v = -v
void int_abs(Integer* v);                                           // v = |v|
void int_abs(Integer* v, Integer const* a);                         // v = |a|

umm  int_log2_abs(Integer const* v);                                // floor(log2(v)), 0 if v = 0
umm  int_bytes(Integer const* v);                                   // ceil(log256(v)), 1 if v = 0
smm  int_sign(Integer const* a);                                    // <0 =0 >0
smm  int_compare_abs(Integer const* a, Integer const* b);           // <0 =0 >0
smm  int_compare    (Integer const* a, Integer const* b);           // <0 =0 >0
smm  int_compare16  (Integer const* a, s16 b);                      // <0 =0 >0

bool int_test_bit (Integer const* v, umm index);                    // true if index'th bit is set
void int_set_bit  (Integer* v, umm index);                          // index'th bit in v set to 1
void int_clear_bit(Integer* v, umm index);                          // index'th bit in v set to 0
void int_shift_right(Integer* v, umm amount);                       // v >>= amount
void int_shift_left (Integer* v, umm amount);                       // v <<= amount

void int_add_ignore_sign(Integer* v, Integer const* a);             // |v| += |a|
void int_sub_ignore_sign(Integer* v, Integer const* a);             // |v| -= |a| (assert v >= a)
void int_add(Integer* v, Integer const* a);                         // v += a
void int_add(Integer* v, Integer const* a, Integer const* b);       // v  = a + b
void int_sub(Integer* v, Integer const* a);                         // v -= a
void int_sub(Integer* v, Integer const* a, Integer const* b);       // v  = a - b
void int_mul(Integer* v, Integer const* a, Integer const* b, umm size_limit = UMM_MAX);  // v = a * b
void int_mul(Integer* v, Integer const* a, umm size_limit = UMM_MAX);           // v *= a
bool int_div(Integer* v, Integer const* a, Integer* mod_opt);                   // v /= a,     mod = v % a
bool int_div(Integer* v, Integer const* a, Integer const* b, Integer* mod_opt); // v  = a / b, mod = a % b
bool int_mod(Integer* v, Integer const* a);                                     // v %= a
bool int_mod(Integer* v, Integer const* a, Integer const* b);                   // v = a % b
void int_barrett_setup(Integer* m, Integer const* n);                           // m = 2^k / n
void int_barrett_reduce(Integer* a, Integer const* n, Integer const* m);        // a %= n (using m from setup)
void int_square(Integer* v, umm size_limit = UMM_MAX);                          // v = v ^ 2
void int_pow(Integer* v, Integer const* a, umm size_limit = UMM_MAX);           // v = v ^ |a|
void int_pow(Integer* v, Integer const* a, Integer const* b, umm size_limit = UMM_MAX);  // v = a ^ |b|
void int_pow_mod(Integer* v, Integer const* a, Integer const* m);                    // v = v ^ |a| (mod m)
void int_pow_mod(Integer* v, Integer const* a, Integer const* b, Integer const* m);  // v = a ^ |b| (mod m)


// v = gcd(a, b)
void int_gcd(Integer* v, Integer const* a, Integer const* b);

// v = gcd(a, b)
// finds x and y such that:  a*x + b*y = gcd(a, b) = v
void int_gcd_extended(Integer* v, Integer const* a, Integer const* b, Integer* x, Integer* y);

// finds v such that:  a*v = 1 (mod m)
// returns true if such v exists
bool int_mod_inverse(Integer* v, Integer const* a, Integer const* m);

String int_print(Integer const* v, Region* memory);

// Internal, but you can use them at your own risk.
void int_ensure_capacity(Integer* v, umm capacity);
void int_set_size_uninitialized(Integer* v, umm size);
void int_set_size(Integer* v, umm size);
void int_grow(Integer* v, umm delta_size);
u32* int_digit(Integer* v, umm index);
u32  int_maybe_digit(Integer const* v, umm index);
void int_normalize(Integer* v);
void int_comba_mul(Integer* v, Integer const* a, Integer const* b, umm size_limit);
void int_karatsuba_mul(Integer* v, Integer const* a, Integer const* b);
bool int_montgomery_setup(Integer const* n, u32* rho);
void int_montgomery_reduce(Integer* x, Integer const* n, u32 rho);
void int_to_montgomery(Integer* x, Integer const* n, u32 rho);
void int_from_montgomery(Integer* x, Integer const* n, u32 rho);
void int_montgomery_pow_mod(Integer* v, Integer const* a, Integer const* m);


ExitApplicationNamespace
