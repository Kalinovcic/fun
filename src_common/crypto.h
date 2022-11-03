EnterApplicationNamespace


////////////////////////////////////////////////////////////////////////////////
// Utilities
////////////////////////////////////////////////////////////////////////////////


// Timing-safe memory comparison, time depends on size but not on content.
bool8 crypto_compare(void const* a, void const* b, umm size);

// Use to erase a secret when it is no longer needed, to reduce chances leak.
void crypto_wipe(void* secret, umm size);


////////////////////////////////////////////////////////////////////////////////
// Base64
////////////////////////////////////////////////////////////////////////////////


String encode_base64(String string, Region* memory, bool add_padding = true);
bool decode_base64(String string, String* out_decoded, Region* memory);


////////////////////////////////////////////////////////////////////////////////
// Entropy
////////////////////////////////////////////////////////////////////////////////


// A cryptographically secure pseudo-random number generator.
//
// Data is filled from AES128 in counter mode, encrypting an incrementing 128-bit integer.
// However, AES128 will not generate any duplicate blocks in the first 2^128 increments,
// where a true entropy source would be expected to generate about 1 duplicate after 2^64 increments.
//
// Because of this, the key is changed after each 1 MB of generated entropy.
// This provides forward secrecy (a future compromised key doesn't endanger past data).
//
// Keys are generated from a SHA256 hash of the previous key and several sources of entropy.
// This includes process memory addresses (which are randomized by the OS),
// timings (which are hard to control), various OS state (such as CPU usage, memory usage,
// process information, etc). On x64 this also includes data coming from the RDRAND instruction.
//
// Each thread gets its own entropy generator, so no locking needs to happen, aside from some
// implicit OS-level locking for getting data required for reseeding.
void entropy(void* destination, umm size);

// Probably no reason to call this, unless you're paranoid and really insist on forward secrecy.
// This is the very expensive function call that hashes a bunch of entropy sources.
void reseed_entropy();


////////////////////////////////////////////////////////////////////////////////
// SHA1
////////////////////////////////////////////////////////////////////////////////


static constexpr umm SHA1_MAC_SIZE = 20;

struct SHA1 { u32 digest[5]; };
SHA1 sha1(String string);


struct SHA1_Context
{
    union
    {
        u32 h[5];
        SHA1 result;
    };
    u64 byte_count;
    u32 block_count;
    u8  block[64];
};

void sha1_block(SHA1_Context* ctx, u8* block);
void sha1_init(SHA1_Context* ctx);
void sha1_data(SHA1_Context* ctx, String data);
void sha1_done(SHA1_Context* ctx);

inline void sha1_u8   (SHA1_Context* ctx, u8  x) {               sha1_data(ctx, { sizeof(x), (u8*) &x }); };
inline void sha1_u16be(SHA1_Context* ctx, u16 x) { x = u16be(x); sha1_data(ctx, { sizeof(x), (u8*) &x }); };
inline void sha1_u32be(SHA1_Context* ctx, u32 x) { x = u32be(x); sha1_data(ctx, { sizeof(x), (u8*) &x }); };
inline void sha1_u64be(SHA1_Context* ctx, u64 x) { x = u64be(x); sha1_data(ctx, { sizeof(x), (u8*) &x }); };


struct HMAC_SHA1_Context
{
    SHA1_Context sha1;
    u8 secret[64];
};

void hmac_sha1_init(HMAC_SHA1_Context* ctx, String secret);
void hmac_sha1_data(HMAC_SHA1_Context* ctx, String data);
void hmac_sha1_done(HMAC_SHA1_Context* ctx);


////////////////////////////////////////////////////////////////////////////////
// SHA256
////////////////////////////////////////////////////////////////////////////////


static constexpr umm SHA256_MAC_SIZE = 32;

struct SHA256 { u32 digest[8]; };
inline bool operator==(SHA256 a, SHA256 b) { return memcmp(a.digest, b.digest, sizeof(SHA256)) == 0; }
inline bool operator!=(SHA256 a, SHA256 b) { return memcmp(a.digest, b.digest, sizeof(SHA256)) != 0; }

SHA256 sha256(String string);


struct SHA256_Context
{
    union
    {
        u32 h[8];
        SHA256 result;
    };
    u64 byte_count;
    u32 block_count;
    u8  block[64];
};

void sha256_block(SHA256_Context* ctx, u8* block);
void sha256_init(SHA256_Context* ctx);
void sha256_data(SHA256_Context* ctx, String data);
void sha256_done(SHA256_Context* ctx);

inline void sha256_u8   (SHA256_Context* ctx, u8  x) {               sha256_data(ctx, { sizeof(x), (u8*) &x }); };
inline void sha256_u16be(SHA256_Context* ctx, u16 x) { x = u16be(x); sha256_data(ctx, { sizeof(x), (u8*) &x }); };
inline void sha256_u32be(SHA256_Context* ctx, u32 x) { x = u32be(x); sha256_data(ctx, { sizeof(x), (u8*) &x }); };
inline void sha256_u64be(SHA256_Context* ctx, u64 x) { x = u64be(x); sha256_data(ctx, { sizeof(x), (u8*) &x }); };

inline void sha256_data(SHA256_Context* ctx, void const* data, umm length)
{
    sha256_data(ctx, { length, (byte*) data });
}


struct HMAC_SHA256_Context
{
    SHA256_Context sha256;
    u8 secret[64];
};

void hmac_sha256_init(HMAC_SHA256_Context* ctx, String secret);
void hmac_sha256_data(HMAC_SHA256_Context* ctx, String data);
void hmac_sha256_done(HMAC_SHA256_Context* ctx);


void prf_sha256(String destination, String secret, String label, String seed);

void hkdf_sha256_extract(SHA256* out_prk, String salt, String input_keying_material);
void hkdf_sha256_expand(void* out_keying_material, String prk, String info, umm length);

struct HKDF_Label_Buffer
{
    u8 data[2 +          // uint16 length
          + 1 + 255      // label<7..255>
          + 1 + 255];    // context<0..255>
};

// Returns a substring of 'buffer' memory.
String hkdf_label(HKDF_Label_Buffer* buffer, String label, String context, u16 length, String label_prefix = {});

void hkdf_sha256_derive_secret(SHA256* out, SHA256* secret, String label, SHA256* transcript);


////////////////////////////////////////////////////////////////////////////////
// AES128
////////////////////////////////////////////////////////////////////////////////


static constexpr umm AES128_BLOCK_SIZE = 16;
static constexpr umm AES128_KEY_SIZE   = 16;
static constexpr umm AES128_IV_SIZE    = 16;

struct AES128
{
    alignas(16)
    u32 round_key[44];
};

void aes128_init   (AES128* aes, void const* key /* AES128_KEY_SIZE */);
void aes128_encrypt(AES128* aes, void* block /* AES128_BLOCK_SIZE */);
void aes128_decrypt(AES128* aes, void* block /* AES128_BLOCK_SIZE */);


struct AES128_CBC
{
    AES128 context;
    alignas(16)
    u8 iv[AES128_IV_SIZE];
};

void aes128_cbc_init   (AES128_CBC* aes, void const* key /* AES128_KEY_SIZE */, void const* iv /* AES128_IV_SIZE */);
bool aes128_cbc_decrypt(AES128_CBC* aes, String data);
bool aes128_cbc_encrypt(AES128_CBC* aes, String data);


static constexpr umm AES128_GCM_TAG_LENGTH       = 16;
static constexpr umm AES128_GCM_AAD_LENGTH       = 13;
static constexpr umm AES128_GCM_IMPLICIT_IV_SIZE = 4;
static constexpr umm AES128_GCM_EXPLICIT_IV_SIZE = 8;

struct AES128_GCM;  // opaque, because size varies depending on architecture
AES128_GCM* aes128_gcm_init(void* parent, u8 key[AES128_KEY_SIZE], u8 implicit_iv[AES128_GCM_IMPLICIT_IV_SIZE]);
void aes128_gcm(bool decrypt, AES128_GCM* gcm, u64 sequence_number,
                u8 header[AES128_GCM_AAD_LENGTH], String data, u8 tag[AES128_GCM_TAG_LENGTH]);


////////////////////////////////////////////////////////////////////////////////
// ChaCha20
////////////////////////////////////////////////////////////////////////////////


static constexpr umm CHACHA20_KEY_SIZE = 32;
static constexpr umm CHACHA20_IV_SIZE  = 12;

struct Chacha20_Context
{
    u32 key[8];
    u32 nonce[3];
    u32 counter;
};

// 'input' and 'output' can be the same string for an in-place encoding/decoding
u32 chacha20(u32 key[8], u32 nonce[3], u32 counter, String input, String output);


////////////////////////////////////////////////////////////////////////////////
// Poly1305
////////////////////////////////////////////////////////////////////////////////


static constexpr umm POLY1305_KEY_SIZE = 32;
static constexpr umm POLY1305_TAG_SIZE = 16;

struct Poly1305 { u32 digest[4]; };

struct Poly1305_Context
{
    u32 h[5];
    u32 r[4];
    u32 s[4];

    u8  buffer[16];
    u32 buffer_size;

    Poly1305 result;
};

void poly1305_init(Poly1305_Context* ctx, u32 key[8]);
void poly1305_data(Poly1305_Context* ctx, String data);
void poly1305_done(Poly1305_Context* ctx);

Poly1305 poly1305(u32 key[8], String data);

void poly1305_chacha20_key_gen(u32 out_poly_key[8], u32 key[8], u32 nonce[3]);


////////////////////////////////////////////////////////////////////////////////
// RSA
////////////////////////////////////////////////////////////////////////////////


Array<String> parse_pem(String pem);

bool get_rsa_pkcs1_from_pkcs8(String der, String* out_pkcs1_der);
bool get_rsa_pkcs1_from_x509(String der, String* out_pkcs1_der);
bool check_certificate_is_rsa(String der);


struct RSA_PKCS1v15_Private;  // opaque, so users don't have to include integer.h

RSA_PKCS1v15_Private* rsa_pkcs1v15_load_private(String der);  // NULL on error
String rsa_pkcs1v15_decrypt(RSA_PKCS1v15_Private* rsa, String in, Region* memory);
void rsa_pkcs1v15_free(RSA_PKCS1v15_Private* rsa);

struct RSA_PKCS1v15_Public;   // opaque, so users don't have to include integer.h

RSA_PKCS1v15_Public* rsa_pkcs1v15_load_public(String der);  // NULL on error
String rsa_pkcs1v15_encrypt(RSA_PKCS1v15_Public* rsa, String in, Region* memory);
void rsa_pkcs1v15_free(RSA_PKCS1v15_Public* rsa);


String rsa_sha1_signature         (RSA_PKCS1v15_Private* rsa, SHA1*   hash, Region* memory);
String rsa_sha256_signature       (RSA_PKCS1v15_Private* rsa, SHA256* hash, Region* memory);

bool   rsa_sha1_signature_verify  (RSA_PKCS1v15_Public*  rsa, SHA1*   hash, String signature);
bool   rsa_sha256_signature_verify(RSA_PKCS1v15_Public*  rsa, SHA256* hash, String signature);



////////////////////////////////////////////////////////////////////////////////
// Elliptic Curve Cryptography
////////////////////////////////////////////////////////////////////////////////


struct ECC_Domain_Parameters
{
    umm    key_size;
    String name;
    String hex_P;
    String hex_A;
    String hex_B;
    String hex_Gx;
    String hex_Gy;
    String hex_n;
};

extern ECC_Domain_Parameters ecc_secp256r1;


struct ECC_Key;   // opaque, so users don't have to include integer.h

ECC_Key* ecc_make_key(ECC_Domain_Parameters* domain);  // NULL on error
ECC_Key* ecc_ansi_x963_import(String data, ECC_Domain_Parameters* domain);
String   ecc_ansi_x963_export(ECC_Key* key, Region* memory);
void     ecc_free_key(ECC_Key** key_ptr);

// returns EC/DH ANSI x9.63 compilant shared secret, or empty string on error
String ecc_shared_secret(ECC_Key* private_key, ECC_Key* public_key, Region* memory);



////////////////////////////////////////////////////////////////////////////////
// ID protection
////////////////////////////////////////////////////////////////////////////////


struct ID_Security
{
    AES128 key;
    u64    lower_xor;
    u64    upper_xor;
};

void   init_id_security(ID_Security* security, String secret, String salt = {});
String encode_secure_id(ID_Security* security, u64 id, Region* memory);
bool   decode_secure_id(ID_Security* security, String cipher_id, u64* out_id);




ExitApplicationNamespace
