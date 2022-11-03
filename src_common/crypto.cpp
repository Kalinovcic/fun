#include "common.h"
#include "integer.h"
#include "crypto.h"

EnterApplicationNamespace


////////////////////////////////////////////////////////////////////////////////
// Utilities
////////////////////////////////////////////////////////////////////////////////


bool8 crypto_compare(void const* a, void const* b, umm size)
{
    byte const* x = (byte const*) a;
    byte const* y = (byte const*) b;
    byte result = 0;
    for (umm i = 0; i < size; i++)
        result |= *(x++) ^ *(y++);

    // Constant-time "cast" to bool, which shouldn't get compiled into a branch on any architecture.
    // 1 if equal, 0 if not.
    result |= result >> 4;
    result |= result >> 2;
    result |= result >> 1;
    return (~result) & 1;
}


void crypto_wipe(void* secret, umm size)
{
    byte* cursor = (byte*) secret;
    for (umm i = 0; i < size; i++)
        *(cursor++) = 0;
}


////////////////////////////////////////////////////////////////////////////////
// Base64
////////////////////////////////////////////////////////////////////////////////


CacheAlign
static constexpr u8 BASE64_ENCODE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                      "abcdefghijklmnopqrstuvwxyz"
                                      "0123456789+/";

String encode_base64(String string, Region* memory, bool add_padding)
{
    String result;
    result.length = 4 * ((string.length + 2) / 3);
    result.data = alloc<u8, false>(memory, result.length);

    u8* read  = string.data;
    u8* write = result.data;

    u8* groups_of_three_end = string.data + string.length / 3 * 3;
    while (read < groups_of_three_end)
    {
        u32 bits = (read[0] << 16) | (read[1] << 8) | read[2];
        *(write++) = BASE64_ENCODE[(bits >> 18) & 63];
        *(write++) = BASE64_ENCODE[(bits >> 12) & 63];
        *(write++) = BASE64_ENCODE[(bits >>  6) & 63];
        *(write++) = BASE64_ENCODE[(bits      ) & 63];
        read += 3;
    }

    umm remaining = (string.data + string.length) - read;
    if (remaining == 1)
    {
        u32 bits = (read[0] << 16);
        *(write++) = BASE64_ENCODE[(bits >> 18) & 63];
        *(write++) = BASE64_ENCODE[(bits >> 12) & 63];
        if (add_padding)
        {
            *(write++) = '=';
            *(write++) = '=';
        }
        else
        {
            result.length -= 2;
        }
    }
    else if (remaining == 2)
    {
        u32 bits = (read[0] << 16) | (read[1] << 8);
        *(write++) = BASE64_ENCODE[(bits >> 18) & 63];
        *(write++) = BASE64_ENCODE[(bits >> 12) & 63];
        *(write++) = BASE64_ENCODE[(bits >>  6) & 63];
        if (add_padding)
        {
            *(write++) = '=';
        }
        else
        {
            result.length -= 1;
        }
    }

    assert(write == result.data + result.length);
    return result;
}

CacheAlign
static constexpr u8 BASE64_DECODE[256] = {
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63,
     52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,255,255,255,
    255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
     15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255,
    255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
     41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
};

bool decode_base64(String string, String* out_decoded, Region* memory)
{
    *out_decoded = {};

    // Remove padding characters at the end, if any.
    while (string.length && string.data[string.length - 1] == '=')
        string.length--;

    umm groups_of_four = string.length / 4;
    u8* groups_of_four_end = string.data + groups_of_four * 4;
    umm remaining = (string.data + string.length) - groups_of_four_end;
    if (remaining == 1)
        return false;

    String result;
    result.length = 3 * groups_of_four + (remaining ? remaining - 1 : 0);
    result.data = alloc<u8, false>(memory, result.length);
    *out_decoded = result;

    u8* read  = string.data;
    u8* write = result.data;
    while (read < groups_of_four_end)
    {
        u32 a = BASE64_DECODE[*(read++)];
        u32 b = BASE64_DECODE[*(read++)];
        u32 c = BASE64_DECODE[*(read++)];
        u32 d = BASE64_DECODE[*(read++)];
        if ((a | b | c | d) == 0xFF) return false;
        u32 bits = (a << 18) | (b << 12) | (c << 6) | d;
        *(write++) = (u8)(bits >> 16);
        *(write++) = (u8)(bits >>  8);
        *(write++) = (u8)(bits      );
    }

    if (remaining == 2)
    {
        u32 a = BASE64_DECODE[*(read++)];
        u32 b = BASE64_DECODE[*(read++)];
        if (a == 0xFF || b == 0xFF) return false;
        u32 bits = (a << 18) | (b << 12);
        *(write++) = (u8)(bits >> 16);
    }
    else if (remaining == 3)
    {
        u32 a = BASE64_DECODE[*(read++)];
        u32 b = BASE64_DECODE[*(read++)];
        u32 c = BASE64_DECODE[*(read++)];
        if (a == 0xFF || b == 0xFF || c == 0xFF) return false;
        u32 bits = (a << 18) | (b << 12) | (c << 6);
        *(write++) = (u8)(bits >> 16);
        *(write++) = (u8)(bits >>  8);
    }

    assert(write == result.data + result.length);
    return true;
}


////////////////////////////////////////////////////////////////////////////////
// SHA1
////////////////////////////////////////////////////////////////////////////////


SHA1 sha1(String string)
{
    SHA1_Context context;
    sha1_init(&context);
    sha1_data(&context, string);
    sha1_done(&context);
    return context.result;
}


void sha1_block(SHA1_Context* ctx, u8* block)
{
    u32 w[80];
    for (umm i = 0; i < 16; i++)
        w[i] = load_u32be(block + i * 4);
    for (umm i = 16; i < 80; i++)
        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    u32 a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3], e = ctx->h[4];
    for (umm i = 0; i < 80; i++)
    {
        u32 f, k;
             if (i < 20) { f = (b & c) | (~b & d);          k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }

        u32 temp = rotl32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl32(b, 30);
        b = a;
        a = temp;
    }
    ctx->h[0] += a, ctx->h[1] += b, ctx->h[2] += c, ctx->h[3] += d, ctx->h[4] += e;
}

void sha1_init(SHA1_Context* ctx)
{
    ctx->h[0] = 0x67452301, ctx->h[1] = 0xEFCDAB89, ctx->h[2] = 0x98BADCFE;
    ctx->h[3] = 0x10325476, ctx->h[4] = 0xC3D2E1F0;
    ctx->byte_count  = 0;
    ctx->block_count = 0;
}

void sha1_data(SHA1_Context* ctx, String data)
{
    ctx->byte_count += data.length;
    if (ctx->block_count)
    {
        u32 remaining = 64 - ctx->block_count;
        if (data.length < remaining)
        {
            memcpy(ctx->block + ctx->block_count, data.data, data.length);
            ctx->block_count += data.length;
            return;
        }

        memcpy(ctx->block + ctx->block_count, data.data, remaining);
        consume(&data, remaining);
        sha1_block(ctx, ctx->block);
        ctx->block_count = 0;
    }

    while (data.length >= 64)
        sha1_block(ctx, take(&data, 64).data);

    if (data.length)
    {
        memcpy(ctx->block, data.data, data.length);
        ctx->block_count = data.length;
    }
}

void sha1_done(SHA1_Context* ctx)
{
    ctx->block[ctx->block_count++] = 0x80;
    if (ctx->block_count > 56)
    {
        while (ctx->block_count < 64)
            ctx->block[ctx->block_count++] = 0;
        sha1_block(ctx, ctx->block);
        ctx->block_count = 0;
    }

    while (ctx->block_count < 56)
        ctx->block[ctx->block_count++] = 0;
    store_u64be(&ctx->block[56], ctx->byte_count * 8);
    sha1_block(ctx, ctx->block);

    for (int i = 0; i < 5; i++)
        ctx->h[i] = u32be(ctx->h[i]);
}


void hmac_sha1_init(HMAC_SHA1_Context* ctx, String secret)
{
    if (secret.length > sizeof(ctx->secret))
    {
        SHA1 secret_sha1 = sha1(secret);
        memcpy(ctx->secret, &secret_sha1, 20);
        memset(ctx->secret + 20, 0, 44);
    }
    else
    {
        memcpy(ctx->secret, secret.data, secret.length);
        memset(ctx->secret + secret.length, 0, sizeof(ctx->secret) - secret.length);
    }

    for (umm i = 0; i < sizeof(ctx->secret); i++)
        ctx->secret[i] ^= 0x36;

    sha1_init(&ctx->sha1);
    sha1_data(&ctx->sha1, static_array_as_string(ctx->secret));
}

void hmac_sha1_data(HMAC_SHA1_Context* ctx, String data)
{
    sha1_data(&ctx->sha1, data);
}

void hmac_sha1_done(HMAC_SHA1_Context* ctx)
{
    sha1_done(&ctx->sha1);
    SHA1 hash = ctx->sha1.result;

    for (umm i = 0; i < sizeof(ctx->secret); i++)
        ctx->secret[i] ^= 0x36 ^ 0x5C;

    sha1_init(&ctx->sha1);
    sha1_data(&ctx->sha1, static_array_as_string(ctx->secret));
    sha1_data(&ctx->sha1, memory_as_string(&hash));
    sha1_done(&ctx->sha1);
}


////////////////////////////////////////////////////////////////////////////////
// SHA256
////////////////////////////////////////////////////////////////////////////////


SHA256 sha256(String string)
{
    SHA256_Context ctx;
    sha256_init(&ctx);
    sha256_data(&ctx, string);
    sha256_done(&ctx);
    return ctx.result;
}


CacheAlign
static constexpr u32 SHA256_K[] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void sha256_block(SHA256_Context* ctx, u8* block)
{
    u32 w[64];
    for (umm i = 0; i < 16; i++)
        w[i] = load_u32be(block + i * 4);
    for (umm i = 16; i < 64; i++)
        w[i] = w[i - 16] + (rotr32(w[i - 15],  7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >>  3))
             + w[i -  7] + (rotr32(w[i -  2], 17) ^ rotr32(w[i -  2], 19) ^ (w[i -  2] >> 10));

    u32 a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3];
    u32 e = ctx->h[4], f = ctx->h[5], g = ctx->h[6], h = ctx->h[7];
    for (umm i = 0; i < 64; i++)
    {
        u32 s1    = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        u32 ch    = (e & f) ^ (~e & g);
        u32 temp1 = h + s1 + ch + SHA256_K[i] + w[i];
        u32 s0    = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        u32 maj   = (a & b) ^ (a & c) ^ (b & c);
        u32 temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    ctx->h[0] += a, ctx->h[1] += b, ctx->h[2] += c, ctx->h[3] += d;
    ctx->h[4] += e, ctx->h[5] += f, ctx->h[6] += g, ctx->h[7] += h;
}

void sha256_init(SHA256_Context* ctx)
{
    ctx->h[0] = 0x6a09e667, ctx->h[1] = 0xbb67ae85, ctx->h[2] = 0x3c6ef372, ctx->h[3] = 0xa54ff53a;
    ctx->h[4] = 0x510e527f, ctx->h[5] = 0x9b05688c, ctx->h[6] = 0x1f83d9ab, ctx->h[7] = 0x5be0cd19;
    ctx->byte_count  = 0;
    ctx->block_count = 0;
}

void sha256_data(SHA256_Context* ctx, String data)
{
    ctx->byte_count += data.length;
    if (ctx->block_count)
    {
        u32 remaining = 64 - ctx->block_count;
        if (data.length < remaining)
        {
            memcpy(ctx->block + ctx->block_count, data.data, data.length);
            ctx->block_count += data.length;
            return;
        }

        memcpy(ctx->block + ctx->block_count, data.data, remaining);
        consume(&data, remaining);
        sha256_block(ctx, ctx->block);
        ctx->block_count = 0;
    }

    while (data.length >= 64)
        sha256_block(ctx, take(&data, 64).data);

    if (data.length)
    {
        memcpy(ctx->block, data.data, data.length);
        ctx->block_count = data.length;
    }
}

void sha256_done(SHA256_Context* ctx)
{
    ctx->block[ctx->block_count++] = 0x80;
    if (ctx->block_count > 56)
    {
        while (ctx->block_count < 64)
            ctx->block[ctx->block_count++] = 0;
        sha256_block(ctx, ctx->block);
        ctx->block_count = 0;
    }

    while (ctx->block_count < 56)
        ctx->block[ctx->block_count++] = 0;
    store_u64be(&ctx->block[56], ctx->byte_count * 8);
    sha256_block(ctx, ctx->block);

    for (int i = 0; i < 8; i++)
        ctx->h[i] = u32be(ctx->h[i]);
}


void hmac_sha256_init(HMAC_SHA256_Context* ctx, String secret)
{
    if (secret.length > sizeof(ctx->secret))
    {
        SHA256 secret_sha256 = sha256(secret);
        memcpy(ctx->secret, &secret_sha256, 32);
        memset(ctx->secret + 32, 0, 32);
    }
    else
    {
        memcpy(ctx->secret, secret.data, secret.length);
        memset(ctx->secret + secret.length, 0, sizeof(ctx->secret) - secret.length);
    }

    for (umm i = 0; i < sizeof(ctx->secret); i++)
        ctx->secret[i] ^= 0x36;

    sha256_init(&ctx->sha256);
    sha256_data(&ctx->sha256, static_array_as_string(ctx->secret));
}

void hmac_sha256_data(HMAC_SHA256_Context* ctx, String data)
{
    sha256_data(&ctx->sha256, data);
}

void hmac_sha256_done(HMAC_SHA256_Context* ctx)
{
    sha256_done(&ctx->sha256);
    SHA256 hash = ctx->sha256.result;

    for (umm i = 0; i < sizeof(ctx->secret); i++)
        ctx->secret[i] ^= 0x36 ^ 0x5C;

    sha256_init(&ctx->sha256);
    sha256_data(&ctx->sha256, static_array_as_string(ctx->secret));
    sha256_data(&ctx->sha256, memory_as_string(&hash));
    sha256_done(&ctx->sha256);
}


void prf_sha256(String destination, String secret, String label, String seed)
{
    HMAC_SHA256_Context hmac;
    hmac_sha256_init(&hmac, secret);
    hmac_sha256_data(&hmac, label);
    hmac_sha256_data(&hmac, seed);
    hmac_sha256_done(&hmac);
    while (true)
    {
        SHA256 a = hmac.sha256.result;
        hmac_sha256_init(&hmac, secret);
        hmac_sha256_data(&hmac, memory_as_string(&a));
        hmac_sha256_data(&hmac, label);
        hmac_sha256_data(&hmac, seed);
        hmac_sha256_done(&hmac);

        SHA256* chunk = &hmac.sha256.result;
        if (destination.length < sizeof(*chunk))
        {
            memcpy(destination.data, chunk, destination.length);
            return;
        }

        memcpy(destination.data, chunk, sizeof(*chunk));
        consume(&destination, sizeof(*chunk));

        hmac_sha256_init(&hmac, secret);
        hmac_sha256_data(&hmac, memory_as_string(&a));
        hmac_sha256_done(&hmac);
    }
}

void hkdf_sha256_extract(SHA256* out_prk, String salt, String input_keying_material)
{
    SHA256 dummy_salt = {};
    if (!salt) salt = memory_as_string(&dummy_salt);

    HMAC_SHA256_Context hmac = {};
    hmac_sha256_init(&hmac, salt);
    hmac_sha256_data(&hmac, input_keying_material);
    hmac_sha256_done(&hmac);
    *out_prk = hmac.sha256.result;
}

void hkdf_sha256_expand(void* out_keying_material, String prk, String info, umm length)
{
    SHA256 previous_hash;
    u8     block_index = 1;
    byte*  cursor = (byte*) out_keying_material;
    while (length)
    {
        HMAC_SHA256_Context hmac = {};
        hmac_sha256_init(&hmac, prk);
        if (block_index > 1)
            hmac_sha256_data(&hmac, memory_as_string(&previous_hash));
        hmac_sha256_data(&hmac, info);
        hmac_sha256_data(&hmac, memory_as_string(&block_index));
        hmac_sha256_done(&hmac);
        previous_hash = hmac.sha256.result;
        block_index++;

        umm copy_size = length;
        if (copy_size > sizeof(previous_hash))
            copy_size = sizeof(previous_hash);
        memcpy(cursor, &previous_hash, copy_size);
        cursor += copy_size;
        length -= copy_size;
    }
}

String hkdf_label(HKDF_Label_Buffer* buffer, String label, String context, u16 length, String label_prefix)
{
    if (!label_prefix) label_prefix = "tls13 "_s;
    assert(label_prefix.length + label.length <= 255);
    assert(context.length <= 255);

    u8* cursor = buffer->data;
    store_u16be(cursor, length);
    cursor += 2;

    *(cursor++) = (u8)(label.length + label_prefix.length);
    memcpy(cursor, label_prefix.data, label_prefix.length);  cursor += label_prefix.length;
    memcpy(cursor, label.data,        label.length);         cursor += label.length;

    *(cursor++) = (u8) context.length;
    memcpy(cursor, context.data, context.length);  cursor += context.length;

    String result;
    result.data   = buffer->data;
    result.length = cursor - buffer->data;
    return result;
}

void hkdf_sha256_derive_secret(SHA256* out, SHA256* secret, String label, SHA256* transcript)
{
    HKDF_Label_Buffer buffer;
    String info = hkdf_label(&buffer, label, memory_as_string(transcript), sizeof(SHA256));
    hkdf_sha256_expand(out, memory_as_string(secret), info, sizeof(SHA256));
}


////////////////////////////////////////////////////////////////////////////////
// AES128
////////////////////////////////////////////////////////////////////////////////


// @Reconsider - we avoid anything to do with CPU extensions on Android, because
// we don't want to bother with different CPU types.
#if defined(ARCHITECTURE_X64) && !defined(OS_ANDROID)
#define AESNI_ARCHITECTURE
#endif


static constexpr u8 AES128_SBOX[256] = {
  0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
  0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
  0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
  0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
  0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
  0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
  0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
  0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
  0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
  0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
  0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
  0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
  0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
  0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
  0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
  0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

static constexpr u8 AES128_RSBOX[256] = {
  0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
  0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
  0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
  0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
  0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
  0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
  0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
  0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
  0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
  0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
  0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
  0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
  0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
  0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
  0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
  0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

static constexpr u8 AES128_RCON[11] = {
    0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};


#ifdef AESNI_ARCHITECTURE

static bool check_aesni()
{
#if defined(COMPILER_MSVC)
    int cpuinfo[4] = { -1 };
    __cpuid(cpuinfo, 1);
    return (cpuinfo[2] & (1 << 25))   // AES-NI
        && (cpuinfo[3] & (1 << 26));  // SSE2
#elif defined(COMPILER_GCC) || defined(COMPILER_CLANG)
    unsigned int eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    return (ecx & (1 << 25))   // AES-NI
        && (edx & (1 << 26));  // SSE2
#endif
}

static bool check_sse2()
{
#if defined(COMPILER_MSVC)
    int cpuinfo[4] = { -1 };
    __cpuid(cpuinfo, 1);
    return (cpuinfo[3] & (1 << 26));  // SSE2
#elif defined(COMPILER_GCC) || defined(COMPILER_CLANG)
    unsigned int eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    return (edx & (1 << 26));  // SSE2
#endif
}

static bool has_sse2  = check_sse2();
static bool has_aesni = check_aesni();

static __m128i aesni_encrypt(__m128i block, __m128i* round_keys)
{
    block = _mm_xor_si128       (block, _mm_load_si128(round_keys + 0));
    block = _mm_aesenc_si128    (block, _mm_load_si128(round_keys + 1));
    block = _mm_aesenc_si128    (block, _mm_load_si128(round_keys + 2));
    block = _mm_aesenc_si128    (block, _mm_load_si128(round_keys + 3));
    block = _mm_aesenc_si128    (block, _mm_load_si128(round_keys + 4));
    block = _mm_aesenc_si128    (block, _mm_load_si128(round_keys + 5));
    block = _mm_aesenc_si128    (block, _mm_load_si128(round_keys + 6));
    block = _mm_aesenc_si128    (block, _mm_load_si128(round_keys + 7));
    block = _mm_aesenc_si128    (block, _mm_load_si128(round_keys + 8));
    block = _mm_aesenc_si128    (block, _mm_load_si128(round_keys + 9));
    block = _mm_aesenclast_si128(block, _mm_load_si128(round_keys + 10));
    return block;
}

static __m128i aesni_decrypt(__m128i block, __m128i* round_keys)
{
    block = _mm_xor_si128       (block,                 (_mm_load_si128(round_keys + 10)));
    block = _mm_aesdec_si128    (block, _mm_aesimc_si128(_mm_load_si128(round_keys + 9)));
    block = _mm_aesdec_si128    (block, _mm_aesimc_si128(_mm_load_si128(round_keys + 8)));
    block = _mm_aesdec_si128    (block, _mm_aesimc_si128(_mm_load_si128(round_keys + 7)));
    block = _mm_aesdec_si128    (block, _mm_aesimc_si128(_mm_load_si128(round_keys + 6)));
    block = _mm_aesdec_si128    (block, _mm_aesimc_si128(_mm_load_si128(round_keys + 5)));
    block = _mm_aesdec_si128    (block, _mm_aesimc_si128(_mm_load_si128(round_keys + 4)));
    block = _mm_aesdec_si128    (block, _mm_aesimc_si128(_mm_load_si128(round_keys + 3)));
    block = _mm_aesdec_si128    (block, _mm_aesimc_si128(_mm_load_si128(round_keys + 2)));
    block = _mm_aesdec_si128    (block, _mm_aesimc_si128(_mm_load_si128(round_keys + 1)));
    block = _mm_aesdeclast_si128(block,                 (_mm_load_si128(round_keys + 0)));
    return block;
}
#endif


void aes128_init(AES128* aes, void const* key /* AES128_KEY_SIZE */)
{
    u32* round_key = aes->round_key;
    for (int i = 0; i < 4; i++)
        round_key[i] = load_u32((byte const*) key + i * 4);

    for (int i = 4; i < 44; i += 4)
    {
        union { u8 temp[4]; u32 temp32; };
        temp32 = u32le(rotr32(u32le(round_key[i - 1]), 8));
        temp[0] = AES128_SBOX[temp[0]] ^ AES128_RCON[i / 4];
        temp[1] = AES128_SBOX[temp[1]];
        temp[2] = AES128_SBOX[temp[2]];
        temp[3] = AES128_SBOX[temp[3]];

        round_key[i    ] = round_key[i - 4] ^ temp32;
        round_key[i + 1] = round_key[i - 3] ^ round_key[i    ];
        round_key[i + 2] = round_key[i - 2] ^ round_key[i + 1];
        round_key[i + 3] = round_key[i - 1] ^ round_key[i + 2];
    }
}

#define Xtime(x) ((u8)(((x) << 1) ^ ((((x) >> 7) & 1) * 0x1b)))
#define Multiply(x, y)                                 \
   ((((y     ) & 1) * x) ^                             \
    (((y >> 1) & 1) * Xtime(x)) ^                      \
    (((y >> 2) & 1) * Xtime(Xtime(x))) ^               \
    (((y >> 3) & 1) * Xtime(Xtime(Xtime(x)))) ^        \
    (((y >> 4) & 1) * Xtime(Xtime(Xtime(Xtime(x))))))
#define AddRoundKey(state, round_key, round)                            \
    for (umm i = 0; i < 4; i++)                                         \
    {                                                                   \
        u8* addr = (u8*) state + i * 4;                                 \
        store_u32(addr, load_u32(addr) ^ (round_key)[(round) * 4 + i]); \
    }

typedef u8 AES128_Block[4][4];

void aes128_encrypt(AES128* aes, void* block /* AES128_BLOCK_SIZE */)
{
#ifdef AESNI_ARCHITECTURE
    if (has_aesni)
    {
        __m128i b = _mm_loadu_si128((__m128i*) block);
        b = aesni_encrypt(b, (__m128i*) aes->round_key);
        _mm_storeu_si128((__m128i*) block, b);
        return;
    }
#endif

    AES128_Block& state = *(AES128_Block*) block;
    AddRoundKey(state, aes->round_key, 0);

    for (umm round = 1;; round++)
    {
        // sub bytes
        for (umm i = 0; i < 4; i++)
            for (umm j = 0; j < 4; j++)
                state[i][j] = AES128_SBOX[state[i][j]];

        // shift rows
        u8 temp     = state[0][1];
        state[0][1] = state[1][1];
        state[1][1] = state[2][1];
        state[2][1] = state[3][1];
        state[3][1] = temp;
        temp        = state[0][2];
        state[0][2] = state[2][2];
        state[2][2] = temp;
        temp        = state[1][2];
        state[1][2] = state[3][2];
        state[3][2] = temp;
        temp        = state[0][3];
        state[0][3] = state[3][3];
        state[3][3] = state[2][3];
        state[2][3] = state[1][3];
        state[1][3] = temp;

        if (round == 10)
            break;

        // mix columns
        for (umm i = 0; i < 4; i++)
        {
            u8 state0 = state[i][0];
            u8 temp   = state[i][0] ^ state[i][1] ^ state[i][2] ^ state[i][3];
            state[i][0] ^= Xtime(state[i][0] ^ state[i][1]) ^ temp;
            state[i][1] ^= Xtime(state[i][1] ^ state[i][2]) ^ temp;
            state[i][2] ^= Xtime(state[i][2] ^ state[i][3]) ^ temp;
            state[i][3] ^= Xtime(state[i][3] ^ state0     ) ^ temp;
        }

        AddRoundKey(state, aes->round_key, round);
    }
    AddRoundKey(state, aes->round_key, 10);
}

void aes128_decrypt(AES128* aes, void* block /* AES128_BLOCK_SIZE */)
{
#ifdef AESNI_ARCHITECTURE
    if (has_aesni)
    {
        __m128i b = _mm_loadu_si128((__m128i*) block);
        b = aesni_decrypt(b, (__m128i*) aes->round_key);
        _mm_storeu_si128((__m128i*) block, b);
        return;
    }
#endif

    AES128_Block& state = *(AES128_Block*) block;
    AddRoundKey(state, aes->round_key, 10);
    for (umm round = 9;; round--)
    {
        // inverse shift rows
        u8 temp     = state[3][1];
        state[3][1] = state[2][1];
        state[2][1] = state[1][1];
        state[1][1] = state[0][1];
        state[0][1] = temp;
        temp        = state[0][2];
        state[0][2] = state[2][2];
        state[2][2] = temp;
        temp        = state[1][2];
        state[1][2] = state[3][2];
        state[3][2] = temp;
        temp        = state[0][3];
        state[0][3] = state[1][3];
        state[1][3] = state[2][3];
        state[2][3] = state[3][3];
        state[3][3] = temp;

        // inverse sub bytes
        for (umm i = 0; i < 4; i++)
            for (umm j = 0; j < 4; j++)
                state[i][j] = AES128_RSBOX[state[i][j]];

        AddRoundKey(state, aes->round_key, round);
        if (round == 0)
            break;

        // inverse mix columns
        for (umm i = 0; i < 4; i++)
        {
            u8 a = state[i][0], b = state[i][1], c = state[i][2], d = state[i][3];
            state[i][0] = Multiply(a, 14) ^ Multiply(b, 11) ^ Multiply(c, 13) ^ Multiply(d,  9);
            state[i][1] = Multiply(a,  9) ^ Multiply(b, 14) ^ Multiply(c, 11) ^ Multiply(d, 13);
            state[i][2] = Multiply(a, 13) ^ Multiply(b,  9) ^ Multiply(c, 14) ^ Multiply(d, 11);
            state[i][3] = Multiply(a, 11) ^ Multiply(b, 13) ^ Multiply(c,  9) ^ Multiply(d, 14);
        }
    }
}

#undef Xtime
#undef Multiply
#undef AddRoundKey


void aes128_cbc_init(AES128_CBC* aes, void const* key, void const* iv)
{
    aes128_init(&aes->context, key);
    memcpy(aes->iv, iv, AES128_IV_SIZE);
}

bool aes128_cbc_encrypt(AES128_CBC* aes, String data)
{
    if (data.length % AES128_BLOCK_SIZE)
        return false;

#ifdef AESNI_ARCHITECTURE
    if (has_aesni)
    {
        __m128i iv = _mm_load_si128((__m128i*) aes->iv);
        __m128i* block      = (__m128i*) data.data;
        __m128i* round_keys = (__m128i*) aes->context.round_key;
        umm count = data.length >> 4;
        while (count--)
        {
            iv = _mm_xor_si128(iv, _mm_loadu_si128(block));
            iv = aesni_encrypt(iv, round_keys);
            _mm_storeu_si128(block++, iv);
        }
        return true;
    }
#endif

#if ARCHITECTURE_SUPPORTS_UNALIGNED_MEMORY_ACCESS
    typedef umm T;
#else
    typedef u8 T;
#endif

    CompileTimeAssert(AES128_BLOCK_SIZE % sizeof(T) == 0);
    constexpr umm BLOCK_SIZE_T = AES128_BLOCK_SIZE / sizeof(T);

    T* iv    = (T*) aes->iv;
    T* block = (T*) data.data;
    T* end   = (T*)(data.data + data.length);

    while (block < end)
    {
        for (umm i = 0; i < BLOCK_SIZE_T; i++)
            iv[i] ^= block[i];
        aes128_encrypt(&aes->context, iv);
        for (umm i = 0; i < BLOCK_SIZE_T; i++)
            block[i] = iv[i];
        block += BLOCK_SIZE_T;
    }

    return true;
}

bool aes128_cbc_decrypt(AES128_CBC* aes, String data)
{
    if (data.length % AES128_BLOCK_SIZE)
        return false;

#ifdef AESNI_ARCHITECTURE
    if (has_aesni)
    {
        __m128i iv = _mm_load_si128((__m128i*) aes->iv);
        __m128i* block      = (__m128i*) data.data;
        __m128i* round_keys = (__m128i*) aes->context.round_key;
        umm count = data.length >> 4;
        while (count--)
        {
            __m128i b = _mm_loadu_si128(block);
            _mm_storeu_si128(block++, _mm_xor_si128(iv, aesni_decrypt(b, round_keys)));
            iv = b;
        }
        return true;
    }
#endif

#if ARCHITECTURE_SUPPORTS_UNALIGNED_MEMORY_ACCESS
    typedef umm T;
#else
    typedef u8 T;
#endif

    CompileTimeAssert(AES128_BLOCK_SIZE % sizeof(T) == 0);
    constexpr umm BLOCK_SIZE_T = AES128_BLOCK_SIZE / sizeof(T);

    T* iv    = (T*) aes->iv;
    T* block = (T*) data.data;
    T* end   = (T*)(data.data + data.length);

    T decoded_block[BLOCK_SIZE_T];
    while (block < end)
    {
        for (umm i = 0; i < BLOCK_SIZE_T; i++)
            decoded_block[i] = block[i];
        aes128_decrypt(&aes->context, decoded_block);
        for (umm i = 0; i < BLOCK_SIZE_T; i++)
        {
            T temp = decoded_block[i] ^ iv[i];
            iv[i] = block[i];
            block[i] = temp;
        }
        block += BLOCK_SIZE_T;
    }

    return true;
}



static constexpr u8 AES128_GCM_SHIFT_TABLE[512] =
{
    0x00, 0x00, 0x01, 0xc2, 0x03, 0x84, 0x02, 0x46, 0x07, 0x08, 0x06, 0xca, 0x04, 0x8c, 0x05, 0x4e,
    0x0e, 0x10, 0x0f, 0xd2, 0x0d, 0x94, 0x0c, 0x56, 0x09, 0x18, 0x08, 0xda, 0x0a, 0x9c, 0x0b, 0x5e,
    0x1c, 0x20, 0x1d, 0xe2, 0x1f, 0xa4, 0x1e, 0x66, 0x1b, 0x28, 0x1a, 0xea, 0x18, 0xac, 0x19, 0x6e,
    0x12, 0x30, 0x13, 0xf2, 0x11, 0xb4, 0x10, 0x76, 0x15, 0x38, 0x14, 0xfa, 0x16, 0xbc, 0x17, 0x7e,
    0x38, 0x40, 0x39, 0x82, 0x3b, 0xc4, 0x3a, 0x06, 0x3f, 0x48, 0x3e, 0x8a, 0x3c, 0xcc, 0x3d, 0x0e,
    0x36, 0x50, 0x37, 0x92, 0x35, 0xd4, 0x34, 0x16, 0x31, 0x58, 0x30, 0x9a, 0x32, 0xdc, 0x33, 0x1e,
    0x24, 0x60, 0x25, 0xa2, 0x27, 0xe4, 0x26, 0x26, 0x23, 0x68, 0x22, 0xaa, 0x20, 0xec, 0x21, 0x2e,
    0x2a, 0x70, 0x2b, 0xb2, 0x29, 0xf4, 0x28, 0x36, 0x2d, 0x78, 0x2c, 0xba, 0x2e, 0xfc, 0x2f, 0x3e,
    0x70, 0x80, 0x71, 0x42, 0x73, 0x04, 0x72, 0xc6, 0x77, 0x88, 0x76, 0x4a, 0x74, 0x0c, 0x75, 0xce,
    0x7e, 0x90, 0x7f, 0x52, 0x7d, 0x14, 0x7c, 0xd6, 0x79, 0x98, 0x78, 0x5a, 0x7a, 0x1c, 0x7b, 0xde,
    0x6c, 0xa0, 0x6d, 0x62, 0x6f, 0x24, 0x6e, 0xe6, 0x6b, 0xa8, 0x6a, 0x6a, 0x68, 0x2c, 0x69, 0xee,
    0x62, 0xb0, 0x63, 0x72, 0x61, 0x34, 0x60, 0xf6, 0x65, 0xb8, 0x64, 0x7a, 0x66, 0x3c, 0x67, 0xfe,
    0x48, 0xc0, 0x49, 0x02, 0x4b, 0x44, 0x4a, 0x86, 0x4f, 0xc8, 0x4e, 0x0a, 0x4c, 0x4c, 0x4d, 0x8e,
    0x46, 0xd0, 0x47, 0x12, 0x45, 0x54, 0x44, 0x96, 0x41, 0xd8, 0x40, 0x1a, 0x42, 0x5c, 0x43, 0x9e,
    0x54, 0xe0, 0x55, 0x22, 0x57, 0x64, 0x56, 0xa6, 0x53, 0xe8, 0x52, 0x2a, 0x50, 0x6c, 0x51, 0xae,
    0x5a, 0xf0, 0x5b, 0x32, 0x59, 0x74, 0x58, 0xb6, 0x5d, 0xf8, 0x5c, 0x3a, 0x5e, 0x7c, 0x5f, 0xbe,
    0xe1, 0x00, 0xe0, 0xc2, 0xe2, 0x84, 0xe3, 0x46, 0xe6, 0x08, 0xe7, 0xca, 0xe5, 0x8c, 0xe4, 0x4e,
    0xef, 0x10, 0xee, 0xd2, 0xec, 0x94, 0xed, 0x56, 0xe8, 0x18, 0xe9, 0xda, 0xeb, 0x9c, 0xea, 0x5e,
    0xfd, 0x20, 0xfc, 0xe2, 0xfe, 0xa4, 0xff, 0x66, 0xfa, 0x28, 0xfb, 0xea, 0xf9, 0xac, 0xf8, 0x6e,
    0xf3, 0x30, 0xf2, 0xf2, 0xf0, 0xb4, 0xf1, 0x76, 0xf4, 0x38, 0xf5, 0xfa, 0xf7, 0xbc, 0xf6, 0x7e,
    0xd9, 0x40, 0xd8, 0x82, 0xda, 0xc4, 0xdb, 0x06, 0xde, 0x48, 0xdf, 0x8a, 0xdd, 0xcc, 0xdc, 0x0e,
    0xd7, 0x50, 0xd6, 0x92, 0xd4, 0xd4, 0xd5, 0x16, 0xd0, 0x58, 0xd1, 0x9a, 0xd3, 0xdc, 0xd2, 0x1e,
    0xc5, 0x60, 0xc4, 0xa2, 0xc6, 0xe4, 0xc7, 0x26, 0xc2, 0x68, 0xc3, 0xaa, 0xc1, 0xec, 0xc0, 0x2e,
    0xcb, 0x70, 0xca, 0xb2, 0xc8, 0xf4, 0xc9, 0x36, 0xcc, 0x78, 0xcd, 0xba, 0xcf, 0xfc, 0xce, 0x3e,
    0x91, 0x80, 0x90, 0x42, 0x92, 0x04, 0x93, 0xc6, 0x96, 0x88, 0x97, 0x4a, 0x95, 0x0c, 0x94, 0xce,
    0x9f, 0x90, 0x9e, 0x52, 0x9c, 0x14, 0x9d, 0xd6, 0x98, 0x98, 0x99, 0x5a, 0x9b, 0x1c, 0x9a, 0xde,
    0x8d, 0xa0, 0x8c, 0x62, 0x8e, 0x24, 0x8f, 0xe6, 0x8a, 0xa8, 0x8b, 0x6a, 0x89, 0x2c, 0x88, 0xee,
    0x83, 0xb0, 0x82, 0x72, 0x80, 0x34, 0x81, 0xf6, 0x84, 0xb8, 0x85, 0x7a, 0x87, 0x3c, 0x86, 0xfe,
    0xa9, 0xc0, 0xa8, 0x02, 0xaa, 0x44, 0xab, 0x86, 0xae, 0xc8, 0xaf, 0x0a, 0xad, 0x4c, 0xac, 0x8e,
    0xa7, 0xd0, 0xa6, 0x12, 0xa4, 0x54, 0xa5, 0x96, 0xa0, 0xd8, 0xa1, 0x1a, 0xa3, 0x5c, 0xa2, 0x9e,
    0xb5, 0xe0, 0xb4, 0x22, 0xb6, 0x64, 0xb7, 0xa6, 0xb2, 0xe8, 0xb3, 0x2a, 0xb1, 0x6c, 0xb0, 0xae,
    0xbb, 0xf0, 0xba, 0x32, 0xb8, 0x74, 0xb9, 0xb6, 0xbc, 0xf8, 0xbd, 0x3a, 0xbf, 0x7c, 0xbe, 0xbe
};

// @Optimization - could be faster, but it's only called 256 times during initialization
static void aes128_gcm_multiply(u8 res[16], u8 a[16], u8 b[16])
{
    u8 z[16] = {};
    u8 v[16];
    memcpy(v, a, 16);
    for (u8 x = 0; x < 128; x++)
    {
        if (b[x >> 3] & (0x80 >> (x & 7)))
           for (u8 y = 0; y < 16; y++)
               z[y] ^= v[y];
        u8 poly = 0xE1 * (v[15] & 1);
        for (u8 y = 15; y > 0; y--)
            v[y] = (v[y] >> 1) | ((v[y - 1] << 7) & 0x80);
        v[0] >>= 1;
        v[0] ^= poly;
    }
    memcpy(res, z, 16);
}


struct AES128_GCM
{
    AES128 aes;
    u8 implicit_iv[AES128_GCM_IMPLICIT_IV_SIZE];

    alignas(16) u8 multiplier[16];

    union
    {
        alignas(16) u8      pc [16][256][16];
        alignas(16) u64     pcq[16][256][2];
#ifdef AESNI_ARCHITECTURE
        alignas(16) __m128i pcv[16][256];
#endif
    };
};

static inline void aes128_gcm_multiply(AES128_GCM* gcm, u8 accumulator[16])
{
#ifdef AESNI_ARCHITECTURE
    if (has_sse2)
    {
        __m128i x = _mm_load_si128(&gcm->pcv[0][accumulator[0]]);
        for (umm i = 1; i < 16; i++)
            x = _mm_xor_si128(x, _mm_load_si128(&gcm->pcv[i][accumulator[i]]));
        _mm_store_si128((__m128i*) &accumulator[0], x);
        return;
    }
#endif

    u64 x0 = gcm->pcq[0][accumulator[0]][0];
    u64 x1 = gcm->pcq[0][accumulator[0]][1];
    for (umm i = 1; i < 16; i++)
    {
        x0 ^= gcm->pcq[i][accumulator[i]][0];
        x1 ^= gcm->pcq[i][accumulator[i]][1];
    }
    store_u64(accumulator + 0, x0);
    store_u64(accumulator + 8, x1);
}

AES128_GCM* aes128_gcm_init(void* parent, u8 key[AES128_KEY_SIZE], u8 implicit_iv[AES128_GCM_IMPLICIT_IV_SIZE])
{
    AES128_GCM* gcm = halloc<AES128_GCM, false>(parent);
    aes128_init(&gcm->aes, key);
    memcpy(gcm->implicit_iv, implicit_iv, AES128_GCM_IMPLICIT_IV_SIZE);

    // multiplier = AES(0)
    memset(gcm->multiplier, 0, 16);
    aes128_encrypt(&gcm->aes, gcm->multiplier);

    // precompute acceleration table
    alignas(16) u8 block[16] = {};
    for (umm y = 0; y < 256; y++)
    {
        block[0] = y;
        aes128_gcm_multiply(gcm->pc[0][y], gcm->multiplier, block);
    }
    for (umm x = 1; x < 16; x++)
        for (umm y = 0; y < 256; y++)
        {
            for (umm z = 15; z > 0; z--)
                gcm->pc[x][y][z] = gcm->pc[x - 1][y][z - 1];
            u16 t = (u16)(gcm->pc[x - 1][y][15]) << 1;
            gcm->pc[x][y][0]  = AES128_GCM_SHIFT_TABLE[t];
            gcm->pc[x][y][1] ^= AES128_GCM_SHIFT_TABLE[t + 1];
        }

    return gcm;
}

void aes128_gcm(bool decrypt, AES128_GCM* gcm, u64 sequence_number,
                u8 header[AES128_GCM_AAD_LENGTH], String data, u8 tag[AES128_GCM_TAG_LENGTH])
{
    umm plain_text_length = data.length;

    // iv = implicit | explicit | counter
    u32 counter = 1;
    alignas(16) u8 iv[16];
    {
        CompileTimeAssert(AES128_GCM_IMPLICIT_IV_SIZE == 4);
        CompileTimeAssert(AES128_GCM_EXPLICIT_IV_SIZE == 8);
        memcpy(iv, gcm->implicit_iv, 4);
        if (decrypt)
        {
            memcpy(iv + 4, header + 5, 8);
        }
        else
        {
            entropy(iv + 4, 8);
            memcpy(header + 5, iv + 4, 8);
        }
        store_u32be(&iv[12], counter);
    }

    // aad = AEAD
    alignas(16) u8 accumulator[16] = {};
    {
        CompileTimeAssert(AES128_GCM_EXPLICIT_IV_SIZE == 8);
        store_u64be(&accumulator[0], sequence_number);
        accumulator[8]  = header[0];
        accumulator[9]  = header[1];
        accumulator[10] = header[2];
        accumulator[11] = plain_text_length >> 8;
        accumulator[12] = plain_text_length;
    }

    aes128_gcm_multiply(gcm, accumulator);
    counter++;

    alignas(16) u8 encrypted_counter[16] = {};
    memcpy(encrypted_counter, iv, 12);
    store_u32be(&encrypted_counter[12], counter);
    aes128_encrypt(&gcm->aes, encrypted_counter);

    // encrypt plain text
    for (umm i = 0; i < plain_text_length; i++)
    {
        if (i && !(i & 15))
        {
            aes128_gcm_multiply(gcm, accumulator);
            counter++;

            memcpy(encrypted_counter, iv, 12);
            store_u32be(&encrypted_counter[12], counter);
            aes128_encrypt(&gcm->aes, encrypted_counter);
        }

        u8 cipher_byte;
        if (decrypt)
        {
            cipher_byte = data.data[i];
            data.data[i] = cipher_byte ^ encrypted_counter[i & 15];
        }
        else
        {
            cipher_byte = data.data[i] ^ encrypted_counter[i & 15];
            data.data[i] = cipher_byte;
        }
        accumulator[i & 15] ^= cipher_byte;
    }
    if (plain_text_length)
        aes128_gcm_multiply(gcm, accumulator);

    // tag
    u64 aad_bits        = u64be(13 * 8);
    u64 plain_text_bits = u64be(plain_text_length * 8);
    for (umm i = 0; i < 8; i++) accumulator[i + 0] ^= ((u8*) &aad_bits       )[i];
    for (umm i = 0; i < 8; i++) accumulator[i + 8] ^= ((u8*) &plain_text_bits)[i];
    aes128_gcm_multiply(gcm, accumulator);

    aes128_encrypt(&gcm->aes, iv);
    for (umm i = 0; i < AES128_GCM_TAG_LENGTH; i++)
        tag[i] = iv[i] ^ accumulator[i];
}


////////////////////////////////////////////////////////////////////////////////
// ChaCha20
////////////////////////////////////////////////////////////////////////////////


#define ChaChaQuarterRound(a, b, c, d)                  \
    a += b; d ^= a; d = RotL32(d, 16);                  \
    c += d; b ^= c; b = RotL32(b, 12);                  \
    a += b; d ^= a; d = RotL32(d,  8);                  \
    c += d; b ^= c; b = RotL32(b,  7);

#define ChaChaBlock()                                   \
    u32 v0  = x0,  v1  = x1,  v2  = x2,  v3  = x3,      \
        v4  = x4,  v5  = x5,  v6  = x6,  v7  = x7,      \
        v8  = x8,  v9  = x9,  v10 = x10, v11 = x11,     \
        v12 = x12, v13 = x13, v14 = x14, v15 = x15;     \
    for (umm i = 0; i < 10; i++)                        \
    {                                                   \
        ChaChaQuarterRound(v0, v4, v8,  v12);           \
        ChaChaQuarterRound(v1, v5, v9,  v13);           \
        ChaChaQuarterRound(v2, v6, v10, v14);           \
        ChaChaQuarterRound(v3, v7, v11, v15);           \
        ChaChaQuarterRound(v0, v5, v10, v15);           \
        ChaChaQuarterRound(v1, v6, v11, v12);           \
        ChaChaQuarterRound(v2, v7, v8,  v13);           \
        ChaChaQuarterRound(v3, v4, v9,  v14);           \
    }                                                   \
    v0  += x0;  v1  += x1;  v2  += x2;  v3  += x3;      \
    v4  += x4;  v5  += x5;  v6  += x6;  v7  += x7;      \
    v8  += x8;  v9  += x9;  v10 += x10; v11 += x11;     \
    v12 += x12; v13 += x13; v14 += x14; v15 += x15;     \
    x12++;

u32 chacha20(u32 key[8], u32 nonce[3], u32 counter, String input, String output)
{
    assert(output.length >= input.length);
    umm full_blocks = (input.length >> 6);
    umm remaining   = (input.length & 0x3F);

    u32 x0  = 0x61707865, x1  = 0x3320646e, x2  = 0x79622d32, x3  = 0x6b206574,
        x4  = key[0],     x5  = key[1],     x6  = key[2],     x7  = key[3],
        x8  = key[4],     x9  = key[5],     x10 = key[6],     x11 = key[7],
        x12 = counter,    x13 = nonce[0],   x14 = nonce[1],   x15 = nonce[2];

    u32* read  = (u32*) input .data;
    u32* write = (u32*) output.data;
    for (umm i = 0; i < full_blocks; i++)
    {
        ChaChaBlock();
    #define W(x) store_u32le(write++, load_u32le(read++) ^ v##x);
        W(0) W(1) W(2) W(3) W(4) W(5) W(6) W(7) W(8) W(9) W(10) W(11) W(12) W(13) W(14) W(15)
    #undef W
    }

    if (remaining)
    {
        ChaChaBlock();
        u8* read8  = (u8*) read  + remaining;
        u8* write8 = (u8*) write + remaining;
        switch (remaining)
        {
    #define W(x)                                                        \
        case (x * 4 + 4): *(--write8) = *(--read8) ^ (u8)(v##x >> 24);  \
        case (x * 4 + 3): *(--write8) = *(--read8) ^ (u8)(v##x >> 16);  \
        case (x * 4 + 2): *(--write8) = *(--read8) ^ (u8)(v##x >>  8);  \
        case (x * 4 + 1): *(--write8) = *(--read8) ^ (u8)(v##x      );
        W(15) W(14) W(13) W(12) W(11) W(10) W(9) W(8) W(7) W(6) W(5) W(4) W(3) W(2) W(1) W(0)
    #undef W
        }
    }

    return x12;  // would-be next counter
}


////////////////////////////////////////////////////////////////////////////////
// Poly1305
////////////////////////////////////////////////////////////////////////////////


static void poly1305_block(Poly1305_Context* ctx, String data, u32 pad_bit)
{
#define ConstantTimeCarry(a, b) ((a ^ ((a ^ b) | ((a - b) ^ b))) >> (sizeof(a) * 8 - 1))

    u32 h0 = ctx->h[0], h1 = ctx->h[1], h2 = ctx->h[2], h3 = ctx->h[3], h4 = ctx->h[4];
    u32 r0 = ctx->r[0], r1 = ctx->r[1], r2 = ctx->r[2], r3 = ctx->r[3];
    u32 s1 = r1 + (r1 >> 2), s2 = r2 + (r2 >> 2), s3 = r3 + (r3 >> 2);
    u64 d0, d1, d2, d3;

    u32* cur     = (u32*) data.data;
    u32* cur_end = (u32*)(data.data + (data.length & ~(umm)(15)));
    while (cur < cur_end)
    {
        u32 i0 = load_u32le(cur); cur++;
        u32 i1 = load_u32le(cur); cur++;
        u32 i2 = load_u32le(cur); cur++;
        u32 i3 = load_u32le(cur); cur++;

        h0  = (u32)(d0 = (u64) h0 +              i0);
        h1  = (u32)(d1 = (u64) h1 + (d0 >> 32) + i1);
        h2  = (u32)(d2 = (u64) h2 + (d1 >> 32) + i2);
        h3  = (u32)(d3 = (u64) h3 + (d2 >> 32) + i3);
        h4 += (u32)(d3 >> 32) + pad_bit;

        d0 = ((u64) h0 * r0) + ((u64) h1 * s3) + ((u64) h2 * s2) + ((u64) h3 * s1);
        d1 = ((u64) h0 * r1) + ((u64) h1 * r0) + ((u64) h2 * s3) + ((u64) h3 * s2) + (h4 * s1);
        d2 = ((u64) h0 * r2) + ((u64) h1 * r1) + ((u64) h2 * r0) + ((u64) h3 * s3) + (h4 * s2);
        d3 = ((u64) h0 * r3) + ((u64) h1 * r2) + ((u64) h2 * r1) + ((u64) h3 * r0) + (h4 * s3);
        h4 = (h4 * r0);

        h0  = (u32) d0;
        h1  = (u32)(d1 += d0 >> 32);
        h2  = (u32)(d2 += d1 >> 32);
        h3  = (u32)(d3 += d2 >> 32);
        h4 += (u32)(d3 >> 32);

        u32 c = (h4 >> 2) + (h4 & ~3u);
        h4 &= 3;
        h0 +=  c;
        h1 += (c = ConstantTimeCarry(h0, c));
        h2 += (c = ConstantTimeCarry(h1, c));
        h3 += (c = ConstantTimeCarry(h2, c));
        h4 +=      ConstantTimeCarry(h3, c);
    }

    ctx->h[0] = h0, ctx->h[1] = h1, ctx->h[2] = h2, ctx->h[3] = h3, ctx->h[4] = h4;

#undef ConstantTimeCarry
}

void poly1305_init(Poly1305_Context* ctx, u32 key[8])
{
    ctx->h[0] = 0;
    ctx->h[1] = 0;
    ctx->h[2] = 0;
    ctx->h[3] = 0;
    ctx->h[4] = 0;

    ctx->r[0] = key[0] & 0x0fffffff;
    ctx->r[1] = key[1] & 0x0ffffffc;
    ctx->r[2] = key[2] & 0x0ffffffc;
    ctx->r[3] = key[3] & 0x0ffffffc;

    ctx->s[0] = key[4];
    ctx->s[1] = key[5];
    ctx->s[2] = key[6];
    ctx->s[3] = key[7];

    ctx->buffer_size = 0;
}

void poly1305_data(Poly1305_Context* ctx, String data)
{
    u32 buffer_size = ctx->buffer_size;
    if (buffer_size)
    {
        u32 remaining = 16 - buffer_size;
        if (data.length >= remaining)
        {
            memcpy(ctx->buffer + buffer_size, data.data, remaining);
            poly1305_block(ctx, { 16, ctx->buffer }, 1);
            consume(&data, remaining);
        }
        else
        {
            memcpy(ctx->buffer + buffer_size, data.data, data.length);
            ctx->buffer_size = buffer_size + data.length;
            return;
        }
    }

    u32 remaining = data.length & 15;
    memcpy(ctx->buffer, data.data + data.length - remaining, remaining);
    ctx->buffer_size = remaining;
    data.length -= remaining;

    poly1305_block(ctx, data, 1);
}

void poly1305_done(Poly1305_Context* ctx)
{
    u32 buffer_size = ctx->buffer_size;
    if (buffer_size)
    {
        ctx->buffer[buffer_size++] = 1;
        while (buffer_size < 16) ctx->buffer[buffer_size++] = 0;
        poly1305_block(ctx, { 16, ctx->buffer }, 0);
    }

    u32 h0 = ctx->h[0], h1 = ctx->h[1], h2 = ctx->h[2], h3 = ctx->h[3], h4 = ctx->h[4];

    u64 t;
    u32 g0 = (u32)(t = (u64) h0 +             5 );
    u32 g1 = (u32)(t = (u64) h1 +      (t >> 32));
    u32 g2 = (u32)(t = (u64) h2 +      (t >> 32));
    u32 g3 = (u32)(t = (u64) h3 +      (t >> 32));
    u32 g4 =                 h4 + (u32)(t >> 32);

    u32 mask = 0 - (g4 >> 2);
    g0  &=  mask;
    g1  &=  mask;
    g2  &=  mask;
    g3  &=  mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3;

    ctx->result.digest[0] = endian_swap32_le((u32)(t = (u64)h0 +             ctx->s[0]));
    ctx->result.digest[1] = endian_swap32_le((u32)(t = (u64)h1 + (t >> 32) + ctx->s[1]));
    ctx->result.digest[2] = endian_swap32_le((u32)(t = (u64)h2 + (t >> 32) + ctx->s[2]));
    ctx->result.digest[3] = endian_swap32_le((u32)(t = (u64)h3 + (t >> 32) + ctx->s[3]));
}

Poly1305 poly1305(u32 key[8], String data)
{
    Poly1305_Context ctx;
    poly1305_init(&ctx, key);
    poly1305_data(&ctx, data);
    poly1305_done(&ctx);
    return ctx.result;
}


void poly1305_chacha20_key_gen(u32 out_poly_key[8], u32 key[8], u32 nonce[3])
{
    u32 x0  = 0x61707865, x1  = 0x3320646e, x2  = 0x79622d32, x3  = 0x6b206574,
        x4  = key[0],     x5  = key[1],     x6  = key[2],     x7  = key[3],
        x8  = key[4],     x9  = key[5],     x10 = key[6],     x11 = key[7],
        x12 = 0,          x13 = nonce[0],   x14 = nonce[1],   x15 = nonce[2];
    ChaChaBlock();
    out_poly_key[0] = u32le(v0);
    out_poly_key[1] = u32le(v1);
    out_poly_key[2] = u32le(v2);
    out_poly_key[3] = u32le(v3);
    out_poly_key[4] = u32le(v4);
    out_poly_key[5] = u32le(v5);
    out_poly_key[6] = u32le(v6);
    out_poly_key[7] = u32le(v7);
}


////////////////////////////////////////////////////////////////////////////////
// Entropy
////////////////////////////////////////////////////////////////////////////////


static thread_local struct CSPRNG
{
    bool   initialized;
    umm    block_counter;
    u64    counter;
    AES128 aes;
} the_csprng;

void reseed_entropy()
{
    CSPRNG* rng = &the_csprng;

    static u32 reseed_counter;
    reseed_counter++;  // not thread-safe, but it doesn't matter (even better?)

    SHA256_Context sha256;
    sha256_init(&sha256);
    sha256_data(&sha256, &reseed_counter, sizeof(reseed_counter));
    sha256_data(&sha256, rng, sizeof(CSPRNG));
    entropy_source_callback(&sha256, [](void* sha256_ptr, byte* data, umm size)
    {
        sha256_data((SHA256_Context*) sha256_ptr, data, size);
    });
    sha256_done(&sha256);

    CompileTimeAssert(sizeof(sha256.result) >= AES128_KEY_SIZE);
    aes128_init(&rng->aes, &sha256.result);
}

void entropy(void* destination, umm size)
{
    CSPRNG* rng = &the_csprng;
    if (!rng->initialized)
    {
        rng->initialized   = true;
        rng->block_counter = 0;
        rng->counter       = 0;
        reseed_entropy();
    }

    u64* cursor = (u64*) destination;

    umm block_counter = rng->block_counter;
    u64 counter       = rng->counter;

    while (size >= AES128_BLOCK_SIZE)
    {
        size -= AES128_BLOCK_SIZE;

        CompileTimeAssert(2 * sizeof(u64) == AES128_BLOCK_SIZE);
        store_u64(cursor + 0, counter);  counter++;
        store_u64(cursor + 1, 0);
        aes128_encrypt(&rng->aes, cursor);
        cursor += 2;

        block_counter++;
        if (block_counter >= 65536)
        {
            block_counter = 0;
            reseed_entropy();
        }
    }

    if (size)
    {
        u64 block[2] = { counter++, 0 };
        CompileTimeAssert(sizeof(block) == AES128_BLOCK_SIZE);
        aes128_encrypt(&rng->aes, block);
        memcpy(cursor, block, size);

        block_counter++;
        if (block_counter >= 65536)
        {
            block_counter = 0;
            reseed_entropy();
        }
    }

    rng->block_counter = block_counter;
    rng->counter       = counter;
}


////////////////////////////////////////////////////////////////////////////////
// RSA
////////////////////////////////////////////////////////////////////////////////


// ASN1 Private Key and X.509 Certificate structures
// https://tls.mbed.org/kb/cryptography/asn1-key-structures-in-der-and-pem
// https://datatracker.ietf.org/doc/html/rfc5280#appendix-A


Array<String> parse_pem(String pem)
{
    Dynamic_Array<String> results = {};
    Defer(free_heap_array(&results));

    bool started = false;
    String base64 = {};
    while (String line = consume_line(&pem))
    {
        if (prefix_equals(line, "-----BEGIN "_s) && suffix_equals(line, "-----"_s))
        {
            started = true;
            base64 = {};
        }
        else if (prefix_equals(line, "-----END "_s) && suffix_equals(line, "-----"_s))
        {
            String decoded;
            if (decode_base64(base64, &decoded, temp) && decoded)
                add_item(&results, &decoded);
            started = false;
        }
        else if (started)
            append(&base64, temp, line);
    }

    return allocate_array(temp, &results);
}

static bool consume_asn1_field(String* der, u8 expected_type, String* out_field)
{
    if (der->length < 2) return false;
    u8 type = read_u8(der);
    if ((type & 0x1F) == 0x1F) return false;
    if (type != expected_type) return false;
    u32 length = read_u8(der);
    if (length >= 128)
    {
        u8 bytes = length & 0x7F;
        if (bytes < 1 || bytes > 3) return false;
        length = 0;
        for (umm i = 0; i < bytes; i++)
            length = (length << 8) | read_u8(der);
        if (!length) return false;
    }
    if (length > der->length) return false;
    *out_field = take(der, length);
    return true;
}

bool get_rsa_pkcs1_from_pkcs8(String der, String* out_pkcs1_der)
{
    String info_sequence;
    if (!consume_asn1_field(&der, 0x30, &info_sequence)) return false;  // SEQUENCE

    String version;
    String algorithm;
    String private_key;
    if (!consume_asn1_field(&info_sequence, 0x02, &version)   ||  // INTEGER
        !consume_asn1_field(&info_sequence, 0x30, &algorithm) ||  // SEQUENCE
        !consume_asn1_field(&info_sequence, 0x04, &private_key))  // OCTET STRING
        return false;

    String algorithm_id;
    if (!consume_asn1_field(&algorithm, 0x06, &algorithm_id))  // OBJECT IDENTIFIER
        return false;

    // RSAES-PKCS1-v1_5 encryption scheme
    u8 oid[9] = { 42, 134, 72, 134, 247, 13, 1, 1, 1 };  // 1.2.840.113549.1.1.1
    if (algorithm_id != static_array_as_string(oid))
        return false;

    *out_pkcs1_der = private_key;
    return true;
}

bool get_rsa_pkcs1_from_x509(String der, String* out_pkcs1_der)
{
    String certificate;
    if (!consume_asn1_field(&der, 0x30, &certificate)) return false;  // SEQUENCE

    String tbs_certificate;
    String algorithm;
    String signature;
    if (!consume_asn1_field(&certificate, 0x30, &tbs_certificate) ||       // SEQUENCE
        !consume_asn1_field(&certificate, 0x30, &algorithm)       ||       // SEQUENCE
        !consume_asn1_field(&certificate, 0x03, &signature)) return false; // BIT STRING

    String algorithm_id;
    if (!consume_asn1_field(&algorithm, 0x06, &algorithm_id))  // OBJECT IDENTIFIER
        return false;

    // Public-Key Cryptography Standards (PKCS) #1 version 1.5
    // signature algorithm with Secure Hash Algorithm 256 (SHA256)
    // and Rivest, Shamir and Adleman (RSA) encryption
    u8 oid_1[9] = { 42, 134, 72, 134, 247, 13, 1, 1, 11 };  // 1.2.840.113549.1.1.11
    // Rivest, Shamir and Adleman (RSA) with Secure Hash Algorithm (SHA-1) signature
    u8 oid_2[9] = { 42, 134, 72, 134, 247, 13, 1, 1, 5  };  // 1.2.840.113549.1.1.5
    if (algorithm_id != static_array_as_string(oid_1) &&
        algorithm_id != static_array_as_string(oid_2))
        return false;

    String version;
    String serial_number;
    String signature2;
    String issuer;
    String validity;
    String subject;
    String subject_public_key_info;
    if (!consume_asn1_field(&tbs_certificate, 0xA0, &version)       ||          // CONTEXT SPECIFIC
        !consume_asn1_field(&tbs_certificate, 0x02, &serial_number) ||          // INTEGER
        !consume_asn1_field(&tbs_certificate, 0x30, &signature2)    ||          // SEQUENCE
        !consume_asn1_field(&tbs_certificate, 0x30, &issuer)        ||          // SEQUENCE
        !consume_asn1_field(&tbs_certificate, 0x30, &validity)      ||          // SEQUENCE
        !consume_asn1_field(&tbs_certificate, 0x30, &subject)       ||          // SEQUENCE
        !consume_asn1_field(&tbs_certificate, 0x30, &subject_public_key_info))  // SEQUENCE
        return false;

    String algorithm2;
    String public_key;
    if (!consume_asn1_field(&subject_public_key_info, 0x30, &algorithm2) ||  // SEQUENCE
        !consume_asn1_field(&subject_public_key_info, 0x03, &public_key))    // BIT STRING
        return false;

    String algorithm_id2;
    if (!consume_asn1_field(&algorithm2, 0x06, &algorithm_id2))  // OBJECT IDENTIFIER
        return false;

    // RSAES-PKCS1-v1_5 encryption scheme
    u8 oid2[9] = { 42, 134, 72, 134, 247, 13, 1, 1, 1 };  // 1.2.840.113549.1.1.1
    if (algorithm_id2 != static_array_as_string(oid2))
        return false;

    if (!public_key.length)
        return false;

    *out_pkcs1_der = consume(public_key, 1);  // remove leading byte (unused bits indicator)
    return true;
}

bool check_certificate_is_rsa(String der)
{
    String certificate;
    if (!consume_asn1_field(&der, 0x30, &certificate)) return false;  // SEQUENCE

    String tbs_certificate;
    String algorithm;
    String signature;
    if (!consume_asn1_field(&certificate, 0x30, &tbs_certificate) ||       // SEQUENCE
        !consume_asn1_field(&certificate, 0x30, &algorithm)       ||       // SEQUENCE
        !consume_asn1_field(&certificate, 0x03, &signature)) return false; // BIT STRING

    String algorithm_id;
    if (!consume_asn1_field(&algorithm, 0x06, &algorithm_id))  // OBJECT IDENTIFIER
        return false;

    // Public-Key Cryptography Standards (PKCS) #1 version 1.5
    // signature algorithm with Secure Hash Algorithm 256 (SHA256)
    // and Rivest, Shamir and Adleman (RSA) encryption
    u8 oid[9] = { 42, 134, 72, 134, 247, 13, 1, 1, 11 };  // 1.2.840.113549.1.1.11
    if (algorithm_id != static_array_as_string(oid))
        return false;

    return true;
}


static bool consume_asn1_integer(String* der, Integer* integer)
{
    String data;
    if (!consume_asn1_field(der, 0x02, &data))  // INTEGER
        return false;

    int_set(integer, data, true /* big endian */);

    if (data && data[0] & 0x80)  // negative
    {
        Integer upper = {};
        int_set16(&upper, 1);
        int_shift_left(&upper, data.length * 8);
        int_sub(integer, &upper);
        int_free(&upper);
    }

    return true;
}


struct RSA_PKCS1v15_Private
{
    Integer N;
    Integer e;
    Integer d;
    Integer p;
    Integer q;
    Integer dP;
    Integer dQ;
    Integer qP;
};

void rsa_pkcs1v15_free(RSA_PKCS1v15_Private* rsa)
{
    if (!rsa) return;
    int_free(&rsa->N);
    int_free(&rsa->e);
    int_free(&rsa->d);
    int_free(&rsa->p);
    int_free(&rsa->q);
    int_free(&rsa->dP);
    int_free(&rsa->dQ);
    int_free(&rsa->qP);
    free(rsa);
}

RSA_PKCS1v15_Private* rsa_pkcs1v15_load_private(String der)
{
    String sequence;
    if (!consume_asn1_field(&der, 0x30, &sequence)) return NULL;  // SEQUENCE

    RSA_PKCS1v15_Private* rsa = alloc<RSA_PKCS1v15_Private>(NULL);

    String version;
    if (!consume_asn1_field  (&sequence, 0x02, &version) ||  // INTEGER
        !consume_asn1_integer(&sequence, &rsa->N)  ||
        !consume_asn1_integer(&sequence, &rsa->e)  ||
        !consume_asn1_integer(&sequence, &rsa->d)  ||
        !consume_asn1_integer(&sequence, &rsa->p)  ||
        !consume_asn1_integer(&sequence, &rsa->q)  ||
        !consume_asn1_integer(&sequence, &rsa->dP) ||
        !consume_asn1_integer(&sequence, &rsa->dQ) ||
        !consume_asn1_integer(&sequence, &rsa->qP))
    {
        rsa_pkcs1v15_free(rsa);
        return NULL;
    }

    return rsa;
}

String rsa_pkcs1v15_decrypt(RSA_PKCS1v15_Private* rsa, String in, Region* memory)
{
    if (!rsa) return {};

    // These are referenced with const pointers to check for thread safety!
    // They can't be modified in this function, because they could be used on other threads.
    Integer const* N  = &rsa->N;
    Integer const* p  = &rsa->p;
    Integer const* q  = &rsa->q;
    Integer const* dP = &rsa->dP;
    Integer const* dQ = &rsa->dQ;
    Integer const* qP = &rsa->qP;

    umm n_bytes = int_bytes(N);
    if (in.length != n_bytes)
        return {};

    Integer a = {};  Defer(int_free(&a));
    Integer b = {};  Defer(int_free(&b));
    Integer c = {};  Defer(int_free(&c));

    int_set(&a, in, true /* big endian */);
    if (int_compare(N, &a) < 0) return {};

    // b = a ^ dP (mod p)
    int_pow_mod(&b, &a, dP, p);

    // c = a ^ dQ (mod q)
    int_pow_mod(&c, &a, dQ, q);

    // a = (b - c) * qP (mod p)
    int_sub(&a, &b, &c);
    int_mul(&a, qP);
    int_mod(&a, p);
    if (int_sign(&a) < 0)
        int_add(&a, p);

    // a = q * a + c
    int_mul(&a, q);
    int_add(&a, &c);

    if (int_bytes(&a) > n_bytes) return {};
    String out = int_binary_abs(&a, memory, true /* big endian */);
    while (out.length < n_bytes)
        out = concatenate(memory, "\x00"_s, out);  // @Optimization

    if (out.length < 2 || out[0] != 0 || out[1] != 2)
        return {};

    umm separator;
    for (separator = 2; separator < out.length; separator++)
        if (out[separator] == 0x00)
            break;
    if (separator == out.length || separator < 10)
        return {};

    consume(&out, separator + 1);
    return out;
}


struct RSA_PKCS1v15_Public
{
    Integer N;
    Integer e;
};

void rsa_pkcs1v15_free(RSA_PKCS1v15_Public* rsa)
{
    if (!rsa) return;
    int_free(&rsa->N);
    int_free(&rsa->e);
    free(rsa);
}

RSA_PKCS1v15_Public* rsa_pkcs1v15_load_public(String der)
{
    String sequence;
    if (!consume_asn1_field(&der, 0x30, &sequence)) return NULL;  // SEQUENCE

    RSA_PKCS1v15_Public* rsa = alloc<RSA_PKCS1v15_Public>(NULL);

    if (!consume_asn1_integer(&sequence, &rsa->N) ||
        !consume_asn1_integer(&sequence, &rsa->e))
    {
        rsa_pkcs1v15_free(rsa);
        return NULL;
    }

    return rsa;
}

String rsa_pkcs1v15_encrypt(RSA_PKCS1v15_Public* rsa, String in, Region* memory)
{
    // These are referenced with const pointers to check for thread safety!
    // They can't be modified in this function, because they could be used on other threads.
    Integer const* N = &rsa->N;
    Integer const* e = &rsa->e;

    umm n_bytes = int_bytes(N);
    if ((in.length + 11) > n_bytes)
        return {};  // too long

    umm random_length = n_bytes - in.length - 3;
    String padded = allocate_zero_string(temp, n_bytes);
    padded[0] = 0;
    padded[1] = 2;
    entropy(&padded[2], random_length);
    for (umm i = 0; i < random_length; i++)
        while (padded[2 + i] == 0)
            entropy(&padded[2 + i], 1);  // has to be non-zero!

    padded[2 + random_length] = 0;
    memcpy(&padded[2 + random_length + 1], in.data, in.length);
    assert(padded.length == 2 + random_length + 1 + in.length);


    Integer a = {};
    Defer(int_free(&a));
    int_set(&a, padded, true /* big endian */);
    if (int_compare(N, &a) < 0)
        return {};

    // b = a ^ e (mod N)
    Integer b = {};
    Defer(int_free(&b));
    int_pow_mod(&b, &a, e, N);
    if (int_bytes(&b) > n_bytes) return {};

    String out = int_binary_abs(&b, memory, true /* big endian */);
    while (out.length < n_bytes)
        out = concatenate(memory, "\x00"_s, out);  // @Optimization

    return out;
}


static String emsa_pkcs1v15_encode_general_digest(String digest, String oid, umm encoded_length, Region* memory)
{
    umm asn1_length = 10 + oid.length + digest.length;
    if (encoded_length < asn1_length + 11)
        return {};

    umm padding_length = encoded_length - asn1_length - 3;
    assert(padding_length >= 8);

    umm i = 0;
    String result = allocate_uninitialized_string(memory, encoded_length);
    result[i++] = 0x00;
    result[i++] = 0x01;
    for (umm p = 0; p < padding_length; p++)
        result[i++] = 0xFF;
    result[i++] = 0x00;

    // DigestInfo ::= SEQUENCE {
    //     digestAlgorithm AlgorithmIdentifier,
    //     digest OCTET STRING
    // }
    result[i++] = 0x30;  // SEQUENCE (DigestInfo)
    result[i++] = 8 + oid.length + digest.length;
    {
        result[i++] = 0x30;  // SEQUENCE (AlgorithmIdentifier)
        result[i++] = 4 + oid.length;
        {
            result[i++] = 0x06;  // OBJECT IDENTIFIER
            result[i++] = oid.length;
            memcpy(&result[i], oid.data, oid.length);
            i += oid.length;

            result[i++] = 0x05;  // NULL (parameters)
            result[i++] = 0x00;
        }

        result[i++] = 0x04;  // OCTET STRING
        result[i++] = digest.length;
        memcpy(&result[i], digest.data, digest.length);
        i += digest.length;
    }

    assert(i == result.length);
    return result;
}

static String rsa_signature_general_digest(RSA_PKCS1v15_Private* rsa, String digest, String oid, Region* memory)
{
    if (!rsa) return {};

    // These are referenced with const pointers to check for thread safety!
    // They can't be modified in this function, because they could be used on other threads.
    Integer const* N  = &rsa->N;
    Integer const* p  = &rsa->p;
    Integer const* q  = &rsa->q;
    Integer const* dP = &rsa->dP;
    Integer const* dQ = &rsa->dQ;
    Integer const* qP = &rsa->qP;

    umm n_bytes = int_bytes(N);
    String our_hash = emsa_pkcs1v15_encode_general_digest(digest, oid, n_bytes, temp);
    if (!our_hash) return {};

    Integer a = {};  Defer(int_free(&a));
    Integer b = {};  Defer(int_free(&b));
    Integer c = {};  Defer(int_free(&c));
    int_set(&a, our_hash, true /* big endian */);
    // b = a ^ dP (mod p)
    int_pow_mod(&b, &a, dP, p);
    // c = a ^ dQ (mod q)
    int_pow_mod(&c, &a, dQ, q);
    // a = (b - c) * qP (mod p)
    int_sub(&a, &b, &c);
    int_mul(&a, qP);
    int_mod(&a, p);
    if (int_sign(&a) < 0)
        int_add(&a, p);
    // a = q * a + c
    int_mul(&a, q);
    int_add(&a, &c);

    String out = int_binary_abs(&a, memory, true /* big endian */);
    if (out.length > n_bytes) return {};
    while (out.length < n_bytes)
        out = concatenate(memory, "\x00"_s, out);  // @Optimization
    return out;
}

static bool rsa_signature_verify_general_digest(RSA_PKCS1v15_Public* rsa, String digest, String oid, String signature)
{
    // These are referenced with const pointers to check for thread safety!
    // They can't be modified in this function, because they could be used on other threads.
    Integer const* N = &rsa->N;
    Integer const* e = &rsa->e;

    umm n_bytes = int_bytes(N);
    if (signature.length != n_bytes) return false;
    String our_hash = emsa_pkcs1v15_encode_general_digest(digest, oid, n_bytes, temp);
    if (!our_hash) return false;

    Integer a = {};
    Integer b = {};
    Defer(int_free(&a));
    Defer(int_free(&b));
    int_set(&a, signature, true /* big endian */);
    int_pow_mod(&b, &a, e, N);

    String their_hash = int_binary_abs(&b, temp, true /* big endian */);
    if (their_hash.length > n_bytes)     return false;
    if (their_hash.length < n_bytes - 1) return false;  // there can be one leading zero, but not more
    if (their_hash.length == n_bytes - 1)
    {
        assert(our_hash[0] == 0x00);
        our_hash = consume(our_hash, 1);
    }
    return our_hash == their_hash;
}


String rsa_sha1_signature(RSA_PKCS1v15_Private* rsa, SHA1* hash, Region* memory)
{
    // Secure Hash Algorithm, revision 1 (SHA-1)
    u8 oid[5] = { 43, 14, 3, 2, 26 };  // 1.3.14.3.2.26
    return rsa_signature_general_digest(rsa, memory_as_string(hash), static_array_as_string(oid), temp);
}

String rsa_sha256_signature(RSA_PKCS1v15_Private* rsa, SHA256* hash, Region* memory)
{
    // Secure Hash Algorithm that uses a 256 bit key (SHA256)
    u8 oid[9] = { 96, 134, 72, 1, 101, 3, 4, 2, 1 };  // 2.16.840.1.101.3.4.2.1
    return rsa_signature_general_digest(rsa, memory_as_string(hash), static_array_as_string(oid), temp);
}


bool rsa_sha1_signature_verify(RSA_PKCS1v15_Public* rsa, SHA1* hash, String signature)
{
    // Secure Hash Algorithm, revision 1 (SHA-1)
    u8 oid[5] = { 43, 14, 3, 2, 26 };  // 1.3.14.3.2.26
    return rsa_signature_verify_general_digest(rsa, memory_as_string(hash), static_array_as_string(oid), signature);
}

bool rsa_sha256_signature_verify(RSA_PKCS1v15_Public* rsa, SHA256* hash, String signature)
{
    // Secure Hash Algorithm that uses a 256 bit key (SHA256)
    u8 oid[9] = { 96, 134, 72, 1, 101, 3, 4, 2, 1 };  // 2.16.840.1.101.3.4.2.1
    return rsa_signature_verify_general_digest(rsa, memory_as_string(hash), static_array_as_string(oid), signature);
}



////////////////////////////////////////////////////////////////////////////////
// Elliptic Curve Cryptography
////////////////////////////////////////////////////////////////////////////////



struct ECC_Point
{
    // Jacbobian format, such that (x,y,z) => (x/z^2, y/z^3, 1) when interpreted as affine
    Integer x;
    Integer y;
    Integer z;
};

static void ecc_free_point(ECC_Point* point)
{
    int_free(&point->x);
    int_free(&point->y);
    int_free(&point->z);
}


#define Add(r, ...) int_add(r, __VA_ARGS__); if (int_compare  (r, mod) >= 0) int_sub(r, mod);
#define Sub(r, ...) int_sub(r, __VA_ARGS__); if (int_compare16(r, 0  ) <  0) int_add(r, mod);
#define Mul(r, ...) int_mul(r, __VA_ARGS__); int_montgomery_reduce(r, mod, rho);
#define Sqr(r)      int_square(r);           int_montgomery_reduce(r, mod, rho);
#define Dbl(r)      int_shift_left(r, 1);    if (int_compare  (r, mod) >= 0) int_sub(r, mod);
#define Hlv(r)      if (int_is_odd(r)) int_add(r, mod); int_shift_right(r, 1);

// out = double p
// 'rho' is the output from int_montgomery_setup
static void ecc_double_point(ECC_Point* out, ECC_Point const* p, Integer const* mod, u32 rho)
{
    Integer a = {};
    Integer b = {};
    Defer(int_free(&a));
    Defer(int_free(&b));

    Integer x = int_clone(&p->x);
    Integer y = int_clone(&p->y);
    Integer z = int_clone(&p->z);

    Mul(&a, &z, &z);  // a = z * z
    Mul(&z, &y);      // z = y * z
    Dbl(&z);          // z = 2z
    Sub(&b, &x, &a);  // b = x - a
    Add(&a, &x);      // a = x + a
    Mul(&b, &a);      // b = a * b
    Add(&a, &b, &b);  // a = 2b
    Add(&a, &b);      // a = a + b
    Dbl(&y);          // y = 2y
    Sqr(&y);          // y = y * y
    Mul(&b, &y, &y);  // b = y * y
    Hlv(&b);          // b = b/2
    Mul(&y, &x);      // y = y * x
    Mul(&x, &a, &a);  // x  = a * a
    Sub(&x, &y);      // x = x - y
    Sub(&x, &y);      // x = x - y
    Sub(&y, &x);      // y = y - x
    Mul(&y, &a);      // y = y * a
    Sub(&y, &b);      // y = y - b

    int_move(&out->x, &x);
    int_move(&out->y, &y);
    int_move(&out->z, &z);
}

// out = p + q (mod)
// 'rho' is the output from int_montgomery_setup
static void ecc_add_point(ECC_Point* out, ECC_Point const* p, ECC_Point const* q, Integer const* mod, u32 rho)
{
    Integer a = {};
    Integer b = {};
    Defer(int_free(&a));
    Defer(int_free(&b));

    int_sub(&a, mod, &q->y);
    if (int_compare(&p->x, &q->x) == 0 && int_compare(&p->z, &q->z) == 0 &&
       (int_compare(&p->y, &q->y) == 0 || int_compare(&p->y, &a) == 0))
    {
        ecc_double_point(out, p, mod, rho);
        return;
    }

    Integer x = int_clone(&p->x);
    Integer y = int_clone(&p->y);
    Integer z = int_clone(&p->z);

    Mul(&a, &q->z, &q->z);   // a = z' * z'
    Mul(&x, &a);             // x = x * a
    Mul(&a, &q->z);          // a = z' * a
    Mul(&y, &a);             // y = y * a
    Mul(&a, &z, &z);         // a = z*z
    Mul(&b, &q->x, &a);      // b = x' * a
    Mul(&a, &z);             // a = z * a
    Mul(&a, &q->y);          // a = y' * a
    Sub(&y, &a);             // y = y - a
    Dbl(&a);                 // a = 2a
    Add(&a, &y);             // a = y + a
    Sub(&x, &b);             // x = x - b
    Dbl(&b);                 // b = 2b
    Add(&b, &x);             // b = x + b
    Mul(&z, &q->z);          // z = z * z'
    Mul(&z, &x);             // z = z * x
    Mul(&a, &x);             // a = a * x
    Sqr(&x);                 // x = x * x
    Mul(&b, &x);             // b = b * x
    Mul(&a, &x);             // a = a * x
    Mul(&x, &y, &y);         // x = y*y
    Sub(&x, &b);             // x = x - b
    Sub(&b, &x);             // b = b - x
    Sub(&b, &x);             // b = b - x
    Mul(&b, &y);             // b = b * y
    Sub(&y, &b, &a);         // y = b - a
    Hlv(&y);                 // y = y/2

    int_move(&out->x, &x);
    int_move(&out->y, &y);
    int_move(&out->z, &z);
}

// Jacobian point P is mapped to affine space.
static void ecc_map_to_affine(ECC_Point* p, Integer const* mod, u32 rho)
{
    Integer a = {};
    Integer b = {};
    Defer(int_free(&a));
    Defer(int_free(&b));

    int_montgomery_reduce(&p->z, mod, rho);
    int_mod_inverse(&a, &p->z, mod);  // a = 1/z
    int_mul(&b, &a, &a);              // b = (1/z)^2
    int_mod(&b, mod);
    int_mul(&a, &b);                  // a = (1/z)^3
    int_mod(&a, mod);

    Mul(&p->x, &b);
    Mul(&p->y, &a);
    int_set16(&p->z, 1);
}

static void int_montgomery_normalization(Integer* r, Integer const* x)
{
    umm bits = int_log2_abs(x) % DIGIT_BITS;
    if (x->size > 1)
    {
        int_set_bit(r, ((x->size - 1) * DIGIT_BITS) + bits - 1);
    }
    else
    {
        int_set16(r, 1);
        bits = 1;
    }

    for (umm i = bits - 1; i < DIGIT_BITS; i++)
    {
        int_shift_left(r, 1);
        if (int_compare(r, x) >= 0)
            int_sub(r, x);
    }
}

// out = kG (mod)
static bool ecc_mul_mod(ECC_Point* out, Integer const* k, ECC_Point const* g, Integer const* mod, bool map_to_affine)
{
    u32 rho;
    if (!int_montgomery_setup(mod, &rho))
        return false;

    Integer mu = {};
    Defer(int_free(&mu));
    int_montgomery_normalization(&mu, mod);

    ECC_Point tg = {};
    Defer(ecc_free_point(&tg));
    if (int_compare16(&mu, 1) == 0)
    {
        int_set(&tg.x, &g->x);
        int_set(&tg.y, &g->y);
        int_set(&tg.z, &g->z);
    }
    else
    {
        // @Optimization - should be mul_mod!
        int_mul(&tg.x, &g->x, &mu);
        int_mul(&tg.y, &g->y, &mu);
        int_mul(&tg.z, &g->z, &mu);
        int_mod(&tg.x, mod);
        int_mod(&tg.y, mod);
        int_mod(&tg.z, mod);
    }

    int_set16(&out->x, 0);
    int_set16(&out->y, 0);
    int_set16(&out->z, 0);

    bool first = true;
    umm k_log2 = int_log2_abs(k);
    for (umm i = 0; i <= k_log2; i++)
    {
        if (int_test_bit(k, i))
        {
            if (first)
            {
                int_set(&out->x, &tg.x);
                int_set(&out->y, &tg.y);
                int_set(&out->z, &tg.z);
                first = false;
            }
            else
            {
                ecc_add_point(out, out, &tg, mod, rho);
            }
        }
        ecc_double_point(&tg, &tg, mod, rho);
    }

    if (map_to_affine)
        ecc_map_to_affine(out, mod, rho);

    return true;
}

#undef Add
#undef Sub
#undef Mul
#undef Sqr
#undef Dbl
#undef Hlv



ECC_Domain_Parameters ecc_secp256r1 =
{
    32,                                                                    // key_size
    "secp256r1"_s,                                                         // name
    "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF"_s,  // P
    "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFC"_s,  // A
    "5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B"_s,  // B
    "6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296"_s,  // Gx
    "4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5"_s,  // Gy
    "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551"_s,  // n
};

struct ECC_Key
{
    ECC_Domain_Parameters* domain;
    ECC_Point public_key;
    Integer   private_key;
};

void ecc_free_key(ECC_Key** key_ptr)
{
    ECC_Key* key = *key_ptr;
    *key_ptr = NULL;
    if (!key) return;

    ecc_free_point(&key->public_key);
    int_free(&key->private_key);
    free(key);
}

ECC_Key* ecc_make_key(ECC_Domain_Parameters* domain)
{
    ECC_Key* key = alloc<ECC_Key>(NULL);
    Defer(ecc_free_key(&key));
    key->domain = domain;

    ECC_Point base = {};
    Integer prime = {};
    Integer order = {};
    Defer(ecc_free_point(&base));
    Defer(int_free(&prime));
    Defer(int_free(&order));

    int_set  (&prime,  bytes_from_hex(temp, domain->hex_P),  /* big_endian */ true);
    int_set  (&order,  bytes_from_hex(temp, domain->hex_n),  /* big_endian */ true);
    int_set  (&base.x, bytes_from_hex(temp, domain->hex_Gx), /* big_endian */ true);
    int_set  (&base.y, bytes_from_hex(temp, domain->hex_Gy), /* big_endian */ true);
    int_set16(&base.z, 1);

    String random_bytes = allocate_uninitialized_string(temp, domain->key_size);
    entropy(random_bytes.data, random_bytes.length);
    int_set(&key->private_key, random_bytes, /* big_endian */ true);

    // private_key must be smaller than the base point order
    int_mod(&key->private_key, &order);

    // make the public key
    if (!ecc_mul_mod(&key->public_key, &key->private_key, &base, &prime, /* map_to_affine */ true))
        return NULL;

    ECC_Key* result = key;
    key = NULL;  // so it doesn't get freed
    return result;
}

ECC_Key* ecc_ansi_x963_import(String data, ECC_Domain_Parameters* domain)
{
    ECC_Key* key = alloc<ECC_Key>(NULL);
    Defer(ecc_free_key(&key));
    key->domain = domain;

    if ((data.length & 1) == 0) return NULL;
    if (data[0] != 4 && data[0] != 6 && data[0] != 7) return NULL;

    umm size = (data.length - 1) >> 1;
    if (size != domain->key_size)
        return NULL;

    int_set  (&key->public_key.x, { size, data.data + 1        }, /* big_endian */ true);
    int_set  (&key->public_key.y, { size, data.data + size + 1 }, /* big_endian */ true);
    int_set16(&key->public_key.z, 1);

    ECC_Key* result = key;
    key = NULL;  // so it doesn't get freed
    return result;
}

String ecc_ansi_x963_export(ECC_Key* key, Region* memory)
{
    umm size = key->domain->key_size;
    String x = int_binary_abs(&key->public_key.x, temp, /* big_endian */ true);
    String y = int_binary_abs(&key->public_key.y, temp, /* big_endian */ true);

    String result = allocate_zero_string(memory, 1 + 2 * size);
    result[0] = 0x04;
    memcpy(result.data + 1 + 1 * size - x.length, x.data, x.length);
    memcpy(result.data + 1 + 2 * size - y.length, y.data, y.length);
    return result;
}

String ecc_shared_secret(ECC_Key* private_key, ECC_Key* public_key, Region* memory)
{
    if (!private_key || !public_key)
        return {};

    Integer prime = {};
    Defer(int_free(&prime));
    int_set(&prime, bytes_from_hex(temp, private_key->domain->hex_P), /* big_endian */ true);

    ECC_Point result = {};
    Defer(ecc_free_point(&result));
    if (!ecc_mul_mod(&result, &private_key->private_key, &public_key->public_key, &prime, /* map_to_affine */ true))
        return {};

    return int_binary_abs(&result.x, memory, /* big_endian */ true);
}



////////////////////////////////////////////////////////////////////////////////
// ID protection
////////////////////////////////////////////////////////////////////////////////


void init_id_security(ID_Security* security, String secret, String salt)
{
    // This isn't a great key derivation function, but this is a relatively low-security setting.
    SHA256 key      = sha256(secret);
    SHA256 key_salt = sha256(salt);
    for (umm i = 0; i < ArrayCount(key.digest); i++)
        key.digest[i] ^= key_salt.digest[i];

    aes128_init(&security->key, &key);
    security->lower_xor = (u64) key.digest[4] | ((u64) key.digest[5] << 32);
    security->upper_xor = (u64) key.digest[6] | ((u64) key.digest[7] << 32);
}

String encode_secure_id(ID_Security* security, u64 id, Region* memory)
{
    u64 in[2] = { id ^ security->lower_xor, security->upper_xor };
    aes128_encrypt(&security->key, in);
    return encode_base64(static_array_as_string(in), memory, /* add_padding */ false);
}

bool decode_secure_id(ID_Security* security, String cipher_id, u64* out_id)
{
    *out_id = U64_MAX;

    String decoded = {};
    if (cipher_id.length != 22) return false;
    if (!decode_base64(cipher_id, &decoded, temp)) return false;
    if (decoded.length != 16) return false;

    aes128_decrypt(&security->key, decoded.data);
    u64 low  = read_u64(&decoded);
    u64 high = read_u64(&decoded);
    if (high != security->upper_xor) return false;

    *out_id = low ^ security->lower_xor;
    return true;
}



ExitApplicationNamespace
