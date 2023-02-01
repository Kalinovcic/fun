#include <stdio.h>
#include <math.h>

#include "common.h"
#include "crypto.h"

EnterApplicationNamespace


thread_local Region temporary_memory;
thread_local Region* temp = &temporary_memory;


static String line_ending_chars = "\n\r"_s;
static String whitespace_chars = " \t\n\r"_s;
static String slash_chars = "/\\"_s;



////////////////////////////////////////////////////////////////////////////////
// Hierarchical memory allocator.
////////////////////////////////////////////////////////////////////////////////


struct alignas(16) Allocation_Header
{
    Atomic32 reference_count;
    Atomic32 children_lock;
    Allocation_Header* parent;
    Allocation_Header* first_child;
    Allocation_Header* next_sibling;
    Allocation_Header* previous_sibling;
    void(*on_free)(void* memory);
};

#define GetAllocationHeader(memory) (((Allocation_Header*) memory) - 1)

void* halloc_(void* parent, umm size, bool zero)
{
    void* memory;
    if (zero)
        memory = alloc<byte, true>(NULL, sizeof(Allocation_Header) + size);
    else
        memory = alloc<byte, false>(NULL, sizeof(Allocation_Header) + size);
    assert(memory);

    Allocation_Header* header = (Allocation_Header*) memory;
    ZeroStruct(header);
    store(&header->reference_count, 1);

    if (parent)
    {
        Allocation_Header* parent_header = GetAllocationHeader(parent);
        header->parent = parent_header;

        atomic_lock(&parent_header->children_lock);
        Allocation_Header* head = parent_header->first_child;
        if (head)
        {
            header->next_sibling = head;
            head->previous_sibling = header;
        }
        parent_header->first_child = header;
        atomic_unlock(&parent_header->children_lock);
    }

    return header + 1;
}

void hfree(void* node)
{
    if (!node) return;

    Allocation_Header* header = GetAllocationHeader(node);
    if (decrement_and_return_new(&header->reference_count))
        return;  // non-zero, still referenced somewhere

    // Remove from parent's children list, so it doesn't free us.
    // :HierarchialFreeingRace If the parent is also being freed right now,
    // we could access invalid memory. So, you're not allowed to free nodes from
    // the same hierarchy from different threads (but you're allowed to dereference them,
    // if you know they or their parents won't get freed).
    Allocation_Header* parent = header->parent;
    if (parent)
    {
        atomic_lock(&parent->children_lock);

        Allocation_Header* prev = header->previous_sibling;
        Allocation_Header* next = header->next_sibling;
        if (next) next->previous_sibling = prev;
        if (prev) prev->next_sibling = next;
        else parent->first_child = next;

        atomic_unlock(&parent->children_lock);
    }

    // Free all of our children. We don't have to worry about locking here...
    // Nobody can possibly add more children, because there are no more references
    // to this node. However children could decide to remove themselves if they are
    // freed at the same time, which is not allowed. :HierarchialFreeingRace
    Allocation_Header* next = header->first_child;
    while (Allocation_Header* child = next)
    {
        next = child->next_sibling;
        child->parent           = NULL;  // Remove so the child doesn't waste time removing itself.
        child->previous_sibling = NULL;
        child->next_sibling     = NULL;
        hfree(child + 1);  // @Reconsider - potentially dangerous recursion here
    }

    // Bye.
    if (header->on_free)
        header->on_free(node);
    free(header);
}

void reference(void* node, u32 to_add)
{
    if (!node) return;

    Allocation_Header* header = GetAllocationHeader(node);
    add(&header->reference_count, to_add);
}

void on_free_(void* node, void(*callback)(void* memory))
{
    if (!node) return;

    Allocation_Header* header = GetAllocationHeader(node);
    header->on_free = callback;
}



////////////////////////////////////////////////////////////////////////////////
// 8-bit strings.
// When treated as text, UTF-8 encoding is assumed.
////////////////////////////////////////////////////////////////////////////////



String make_string(const char* c_string)
{
    umm length = strlen(c_string);

    String result;
    result.length = length;
    result.data = alloc<u8>(temp, length);

    memcpy(result.data, c_string, length);

    return result;
}

char* make_c_style_string(String string, Region* memory)
{
    char* result = alloc<char>(memory, string.length + 1);

    memcpy(result, string.data, string.length);
    result[string.length] = 0;

    return result;
}

char* make_c_style_string_on_heap(String string)
{
    char* result = alloc<char, true>(NULL, string.length + 1);
    memcpy(result, string.data, string.length);
    return result;
}

String wrap_string(const char* c_string)
{
    String result;
    result.length = strlen(c_string);
    result.data = (u8*) c_string;
    return result;
}


void copy_string_to_c_style_buffer(char* destination, umm size, String string)
{
    if (size == 0) return;
    if (string.length >= size)
        string.length = size - 1;
    memcpy(destination, string.data, string.length);
    destination[string.length] = 0;
}

void copy_string_to_c_style_buffer(u16* destination, umm size, String16 string)
{
    size /= 2;
    if (size == 0) return;
    if (string.length >= size)
        string.length = size - 1;
    memcpy(destination, string.data, string.length * sizeof(u16));
    destination[string.length] = 0;
}


String allocate_string(Region* memory, String string)
{
    String result;
    result.length = string.length;
    result.data = alloc<u8, false>(memory, string.length);
    memcpy(result.data, string.data, string.length);

    return result;
}


String allocate_zero_string(Region* memory, umm length)
{
    String result;
    result.length = length;
    result.data = alloc<u8>(memory, length);
    return result;
}

String allocate_uninitialized_string(Region* memory, umm length)
{
    String result;
    result.length = length;
    result.data = alloc<u8, false>(memory, length);
    return result;
}


String allocate_string_on_heap(String string)
{
    String result;
    result.length = string.length;
    result.data = alloc<u8, false>(NULL, string.length + 1);
    result.data[result.length] = 0;
    memcpy(result.data, string.data, string.length);
    return result;
}


String make_lowercase_copy(Region* memory, String string)
{
    String result = allocate_string(temp, string);
    for (umm i = 0; i < result.length; i++)
    {
        u8 c = result.data[i];
        if (c >= 'A' && c <= 'Z')
            result.data[i] = c - 'A' + 'a';
    }
    return result;
}

String make_uppercase_copy(Region* memory, String string)
{
    String result = allocate_string(temp, string);
    for (umm i = 0; i < result.length; i++)
    {
        u8 c = result.data[i];
        if (c >= 'a' && c <= 'z')
            result.data[i] = c - 'a' + 'A';
    }
    return result;
}


String concatenate(Region* memory, String first, String second, String third, String fourth, String fifth, String sixth)
{
    String result;
    result.length = first.length + second.length + third.length + fourth.length + fifth.length + sixth.length;
    result.data = alloc<u8, false>(memory, result.length);

    u8* write = result.data;

    memcpy(write, first.data, first.length);
    write += first.length;

    memcpy(write, second.data, second.length);
    write += second.length;

    memcpy(write, third.data, third.length);
    write += third.length;

    memcpy(write, fourth.data, fourth.length);
    write += fourth.length;

    memcpy(write, fifth.data, fifth.length);
    write += fifth.length;

    memcpy(write, sixth.data, sixth.length);
    write += sixth.length;

    return result;
}

String concatenate(String first, String second, String third, String fourth, String fifth, String sixth)
{
    return concatenate(temp, first, second, third, fourth, fifth, sixth);
}

String concatenate(Array<String>* strings, Region* memory)
{
    umm length = 0;
    For (*strings) length += it->length;

    String result;
    result.length = length;
    result.data   = alloc<u8, false>(memory, length);

    u8* cursor = result.data;
    For (*strings)
    {
        memcpy(cursor, it->data, it->length);
        cursor += it->length;
    }

    return result;
}


String substring(String string, umm start_index, umm length)
{
    assert(start_index <= string.length);
    assert((start_index + length) <= string.length);

    String result;
    result.length = length;
    result.data = string.data + start_index;

    return result;
}


bool operator==(String lhs, String rhs)
{
    if (lhs.length != rhs.length)
        return false;
    return memcmp(lhs.data, rhs.data, lhs.length) == 0;
}

bool operator!=(String lhs, String rhs)
{
    if (lhs.length != rhs.length)
        return true;
    return memcmp(lhs.data, rhs.data, lhs.length) != 0;
}


bool prefix_equals(String string, String prefix)
{
    if (string.length < prefix.length)
        return false;

    return memcmp(string.data, prefix.data, prefix.length) == 0;
}

bool suffix_equals(String string, String suffix)
{
    if (string.length < suffix.length)
        return false;

    u8* substring = string.data + string.length - suffix.length;
    return memcmp(substring, suffix.data, suffix.length) == 0;
}


bool compare_case_insensitive(const void* m1, const void* m2, umm length)
{
    byte* bytes1 = (byte*) m1;
    byte* bytes2 = (byte*) m2;
    for (umm i = 0; i < length; i++)
    {
        byte a = bytes1[i];
        byte b = bytes2[i];
        if (a == b) continue;

        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a == b) continue;

        return false;
    }

    return true;
}

bool compare_case_insensitive(String lhs, String rhs)
{
    if (lhs.length != rhs.length)
        return false;

    return compare_case_insensitive(lhs.data, rhs.data, lhs.length);
}

bool prefix_equals_case_insensitive(String string, String prefix)
{
    if (string.length < prefix.length)
        return false;

    return compare_case_insensitive(string.data, prefix.data, prefix.length);
}

bool suffix_equals_case_insensitive(String string, String suffix)
{
    if (string.length < suffix.length)
        return false;

    u8* substring = string.data + string.length - suffix.length;
    return compare_case_insensitive(substring, suffix.data, suffix.length);
}


int lexicographic_order(String a, String b)
{
    int order = memcmp(a.data, b.data, a.length < b.length ? a.length : b.length);
    if (order) return order;
    if (a.length < b.length) return -1;
    if (a.length > b.length) return  1;
    return 0;
}



umm find_first_occurance(String string, u8 of)
{
    for (smm i = 0; i < (smm) string.length; i++)
        if (string[i] == of)
            return i;

    return NOT_FOUND;
}

umm find_first_occurance(String string, String of)
{
    if (string.length < of.length)
        return NOT_FOUND;

    for (smm i = 0; i <= (smm)(string.length - of.length); i++)
        if (memcmp(string.data + i, of.data, of.length) == 0)
            return i;

    return NOT_FOUND;
}

umm find_first_occurance_case_insensitive(String string, String of)
{
    if (string.length < of.length)
        return NOT_FOUND;

    for (smm i = 0; i <= (smm)(string.length - of.length); i++)
        if (compare_case_insensitive(string.data + i, of.data, of.length))
            return i;

    return NOT_FOUND;
}

umm find_first_occurance_of_any(String string, String any_of)
{
    for (smm i = 0; i < (smm) string.length; i++)
    {
        u8 c = string[i];

        for (umm j = 0; j < any_of.length; j++)
        {
            u8 c2 = any_of[j];
            if (c == c2)
                return i;
        }
    }

    return NOT_FOUND;
}


umm find_last_occurance(String string, u8 of)
{
    for (smm i = (smm) string.length - 1; i >= 0; i--)
        if (string.data[i] == of)
            return i;

    return NOT_FOUND;
}

umm find_last_occurance(String string, String of)
{
    if (string.length < of.length)
        return NOT_FOUND;

    for (smm i = (smm)(string.length - of.length); i >= 0; i--)
        if (memcmp(string.data + i, of.data, of.length) == 0)
            return i;

    return NOT_FOUND;
}

umm find_last_occurance_of_any(String string, String any_of)
{
    for (smm i = (smm) string.length - 1; i >= 0; i--)
    {
        u8 c = string[i];

        for (umm j = 0; j < any_of.length; j++)
        {
            u8 c2 = any_of[j];
            if (c == c2)
                return i;
        }
    }

    return NOT_FOUND;
}


void replace_all_occurances(String string, u8 what, u8 with_what)
{
    for (smm i = (smm) string.length - 1; i >= 0; i--)
        if (string[i] == what)
            string[i] = with_what;
}


String replace_all_occurances(String string, String what, String with_what, Region* memory)
{
    String result = {};
    while (string)
    {
        umm remaining = string.length;
        String copy = consume_until(&string, what);
        append(&result, memory, copy);
        if (copy.length < remaining)
            append(&result, memory, with_what);
    }
    return result;
}

String replace_all_occurances_or_return_input(String string, String what, String with_what, Region* memory)
{
    String result = {};
    while (string)
    {
        umm remaining = string.length;
        String copy = consume_until(&string, what);
        if (copy.length == string.length)
            return string;  // 'what' doesn't appear in source, so don't copy

        append(&result, memory, copy);
        if (copy.length < remaining)
            append(&result, memory, with_what);
    }
    return result;
}


u32 compute_crc32(String data)
{
    u32 crc = U32_MAX;

    for (umm i = 0; i < data.length; i++)
    {
        crc = crc ^ data.data[i];
        crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
        crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
        crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
        crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
        crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
        crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
        crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
        crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
    }

    return ~crc;
}

String hex_from_bytes(Region* memory, String bytes)
{
    String hex;
    hex.length = bytes.length * 2;
    hex.data = alloc<u8, false>(memory, hex.length);

    const char* HEX_DIGITS = "0123456789abcdef";
    u8* write = hex.data;
    for (umm i = 0; i < bytes.length; i++)
    {
        *(write++) = HEX_DIGITS[bytes.data[i] >> 4];
        *(write++) = HEX_DIGITS[bytes.data[i] & 0xF];
    }

    return hex;
}

String bytes_from_hex(Region* memory, String hex)
{
    if (hex.length & 1) return {};

    String bytes;
    bytes.length = hex.length >> 1;
    bytes.data = alloc<u8, false>(memory, bytes.length);

    u8* read = hex.data;
    for (umm i = 0; i < bytes.length; i++)
    {
        u8 high = *(read++);
        if (high >= '0' && high <= '9')
            high = high - '0';
        else if (high >= 'a' && high <= 'f')
            high = high - 'a' + 10;
        else if (high >= 'A' && high <= 'F')
            high = high - 'A' + 10;
        else return {};

        u8 low = *(read++);
        if (low >= '0' && low <= '9')
            low = low - '0';
        else if (low >= 'a' && low <= 'f')
            low = low - 'a' + 10;
        else if (low >= 'A' && low <= 'F')
            low = low - 'A' + 10;
        else return {};

        bytes.data[i] = (high << 4) | low;
    }

    return bytes;
}


////////////////////////////////////////////////////////////////////////////////
// Text reading utilities.
////////////////////////////////////////////////////////////////////////////////



String consume(String string, umm amount)
{
    assert(amount <= string.length);
    string.data += amount;
    string.length -= amount;
    return string;
}

void consume(String* string, umm amount)
{
    assert(amount <= string->length);
    string->data += amount;
    string->length -= amount;
}

String take(String* string, umm amount)
{
    assert(amount <= string->length);
    String result = { amount, string->data };
    string->data += amount;
    string->length -= amount;
    return result;
}

void consume_whitespace(String* string)
{
    umm to_consume = 0;
    while (to_consume < string->length && IsSpaceByte(string->data[to_consume]))
        to_consume++;

    string->data   += to_consume;
    string->length -= to_consume;
}

String consume_line(String* string)
{
    consume_whitespace(string);

    umm line_length = 0;
    for (; line_length < string->length; line_length++)
    {
        u8 c = string->data[line_length];
        if (c == '\n' || c == '\r')
            break;
    }

    String line = substring(*string, 0, line_length);
    consume(string, line_length);

    return line;
}

String consume_line_preserve_whitespace(String* string)
{
    umm line_length = find_first_occurance_of_any(*string, "\n\r"_s);
    if (line_length == NOT_FOUND)
        line_length = string->length;

    String line = substring(*string, 0, line_length);
    consume(string, line_length);

    // If we've found the line ending, consume it.
    if (*string)
    {
        umm ending_length = 1;
        if (string->length > 1)
        {
            // Handle two-byte line endings.
            u8 c1 = string->data[0];
            u8 c2 = string->data[1];
            if ((c1 == '\n' && c2 == '\r') ||
                (c1 == '\r' && c2 == '\n'))
                ending_length++;
        }
        consume(string, ending_length);
    }

    return line;
}

String consume_until(String* string, u8 until_what)
{
    consume_whitespace(string);

    umm left_length = 0;
    for (; left_length < string->length; left_length++)
        if (string->data[left_length] == until_what)
            break;

    String left = substring(*string, 0, left_length);
    consume(string, left_length);

    // If we've found the delimiter, consume it.
    if (*string)
        consume(string, 1);

    return left;
}

String consume_until_preserve_whitespace(String* string, u8 until_what)
{
    umm left_length = find_first_occurance(*string, until_what);
    if (left_length == NOT_FOUND)
        left_length = string->length;

    String left = substring(*string, 0, left_length);
    consume(string, left_length);

    // If we've found the delimiter, consume it.
    if (*string)
        consume(string, 1);

    return left;
}

String consume_until_preserve_whitespace(String* string, String until_what)
{
    umm left_length = find_first_occurance(*string, until_what);
    if (left_length == NOT_FOUND)
        left_length = string->length;

    String left = substring(*string, 0, left_length);
    consume(string, left_length);

    // If we've found the delimiter, consume it.
    if (*string)
        consume(string, until_what.length);

    return left;
}

String consume_until(String* string, String until_what)
{
    consume_whitespace(string);
    return consume_until_preserve_whitespace(string, until_what);
}

String consume_until_any(String* string, String until_what)
{
    consume_whitespace(string);

    umm left_length = find_first_occurance_of_any(*string, until_what);
    if (left_length == NOT_FOUND)
        left_length = string->length;

    String left = substring(*string, 0, left_length);
    consume(string, left_length);

    // If we've found the delimiter, consume it.
    if (*string)
        consume(string, 1);

    return left;
}

String consume_until_whitespace(String* string)
{
    consume_whitespace(string);

    umm left_length = 0;
    for (; left_length < string->length; left_length++)
        if (IsSpaceByte(string->data[left_length]))
            break;

    String left = substring(*string, 0, left_length);
    consume(string, left_length);

    // If we've found the delimiter, consume it.
    if (*string)
        consume(string, 1);

    return left;
}


String consume_until_last(String* string, u8 until_what)
{
    umm left_length = find_last_occurance(*string, until_what);
    if (left_length == NOT_FOUND)
        left_length = string->length;

    String left = substring(*string, 0, left_length);
    consume(string, left_length);

    // If we've found the delimiter, consume it.
    if (*string)
        consume(string, 1);

    return left;
}

String consume_until_last(String* string, String until_what)
{
    umm left_length = find_last_occurance(*string, until_what);
    if (left_length == NOT_FOUND)
        left_length = string->length;

    String left = substring(*string, 0, left_length);
    consume(string, left_length);

    // If we've found the delimiter, consume it.
    if (*string)
        consume(string, until_what.length);

    return left;
}

String consume_until_if_exists(String* string, String until_what)
{
    umm left_length = find_first_occurance(*string, until_what);
    if (left_length == NOT_FOUND)
        return {};
    String left = { left_length, string->data };
    consume(string, left_length + until_what.length);
    return left;
}

String consume_identifier(String* string)
{
    if (!string->length || !IsAlpha(string->data[0]))
        return {};

    umm length = 1;
    while (length < string->length && IsAlphaNumeric(string->data[length]))
        length++;

    return take(string, length);
}


String trim(String string)
{
    umm to_consume = 0;
    while (to_consume < string.length && IsSpaceByte(string.data[to_consume]))
        to_consume++;
    string.data   += to_consume;
    string.length -= to_consume;
    while (string.length && IsSpaceByte(string.data[string.length - 1]))
        string.length--;
    return string;
}

String trim_front(String string)
{
    umm to_consume = 0;
    while (to_consume < string.length && IsSpaceByte(string.data[to_consume]))
        to_consume++;
    string.data   += to_consume;
    string.length -= to_consume;
    return string;
}

String trim_back(String string)
{
    while (string.length && IsSpaceByte(string.data[string.length - 1]))
        string.length--;
    return string;
}

String trim_null_back(String string)
{
    while (string.length && string.data[string.length - 1] == 0)
        string.length--;
    return string;
}


String collapse_whitespace(String string, Region* memory)
{
    string = trim(string);
    String_Concatenator cat = {};
    bool last_white = false;
    for (umm i = 0; i < string.length; i++)
    {
        u8 c = string.data[i];
        bool white = IsSpaceByte(c);
        if (white)
        {
            if (last_white) continue;
            c = ' ';
        }
        last_white = white;
        add(&cat, &c, 1);
    }
    return resolve_to_string_and_free(&cat, temp);
}


struct Integer_From_String
{
    bool is_negative;
    u64 absolute_value;
};

static Integer_From_String consume_integer(String* string, u32 base)
{
    Integer_From_String result = {};

    consume_whitespace(string);

    if (*string)
    {
        if (string->data[0] == '-')
        {
            consume(string, 1);
            result.is_negative = true;
        }
        else if (string->data[0] == '+')
        {
            consume(string, 1);
            result.is_negative = true;
        }
    }

    while (*string)
    {
        char c = string->data[0];

        u32 digit = 0;
             if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
        else break;

        if (digit >= base) break;

        result.absolute_value = result.absolute_value * base + digit;
        consume(string, 1);
    }

    return result;
}


u32 consume_u32(String* string, u32 base)
{
    Integer_From_String integer = consume_integer(string, base);
    return (u32) integer.absolute_value;
}

u64 consume_u64(String* string, u32 base)
{
    Integer_From_String integer = consume_integer(string, base);
    return integer.absolute_value;
}

s32 consume_s32(String* string, u32 base)
{
    Integer_From_String integer = consume_integer(string, base);
    s32 absolute = (s32) integer.absolute_value;
    return integer.is_negative ? -absolute : absolute;
}

s64 consume_s64(String* string, u32 base)
{
    Integer_From_String integer = consume_integer(string, base);
    s64 absolute = (s64) integer.absolute_value;
    return integer.is_negative ? -absolute : absolute;
}

f32 consume_f32(String* string)
{
    char* c_string = make_c_style_string(*string);
    char* end = c_string;
    float result = strtof(c_string, &end);
    consume(string, end - c_string);
    return result;
}

f64 consume_f64(String* string)
{
    char* c_string = make_c_style_string(*string);
    char* end = c_string;
    double result = strtod(c_string, &end);
    consume(string, end - c_string);
    return result;
}


u32 u32_from_string(String string, u32 base)
{
    return consume_u32(&string, base);
}

u64 u64_from_string(String string, u32 base)
{
    return consume_u64(&string, base);
}

s32 s32_from_string(String string, u32 base)
{
    return consume_s32(&string, base);
}

s64 s64_from_string(String string, u32 base)
{
    return consume_s64(&string, base);
}

f32 f32_from_string(String string)
{
    return consume_f32(&string);
}

f64 f64_from_string(String string)
{
    return consume_f64(&string);
}



////////////////////////////////////////////////////////////////////////////////
// Binary reading utilities.
////////////////////////////////////////////////////////////////////////////////



bool read_bytes(String* string, void* result, umm count)
{
    if (string->length < count)
    {
        string->length = 0;
        return false;
    }

    memcpy(result, string->data, count);
    consume(string, count);
    return true;
}


bool read_u8 (String* string, u8*  result) { return read_bytes(string, result, 1); }
bool read_u16(String* string, u16* result) { return read_bytes(string, result, 2); }
bool read_u32(String* string, u32* result) { return read_bytes(string, result, 4); }
bool read_u64(String* string, u64* result) { return read_bytes(string, result, 8); }

bool read_s8 (String* string, s8*  result) { return read_bytes(string, result, 1); }
bool read_s16(String* string, s16* result) { return read_bytes(string, result, 2); }
bool read_s32(String* string, s32* result) { return read_bytes(string, result, 4); }
bool read_s64(String* string, s64* result) { return read_bytes(string, result, 8); }

bool read_f32(String* string, f32* result) { return read_bytes(string, result, 4); }
bool read_f64(String* string, f64* result) { return read_bytes(string, result, 8); }

bool read_string(String* string, String* result)
{
    u64 length;
    if (!read_u64(string, &length)) return false;
    if (string->length < length) return false;
    *result = take(string, length);
    return true;
}


u8  read_u8   (String* string) { u8  result = 0;  read_u8 (string, &result);  return result; }
u16 read_u16  (String* string) { u16 result = 0;  read_u16(string, &result);  return result; }
u32 read_u32  (String* string) { u32 result = 0;  read_u32(string, &result);  return result; }
u64 read_u64  (String* string) { u64 result = 0;  read_u64(string, &result);  return result; }
s8  read_s8   (String* string) { s8  result = 0;  read_s8 (string, &result);  return result; }
s16 read_s16  (String* string) { s16 result = 0;  read_s16(string, &result);  return result; }
s32 read_s32  (String* string) { s32 result = 0;  read_s32(string, &result);  return result; }
s64 read_s64  (String* string) { s64 result = 0;  read_s64(string, &result);  return result; }
f32 read_f32  (String* string) { f32 result = 0;  read_f32(string, &result);  return result; }
f64 read_f64  (String* string) { f64 result = 0;  read_f64(string, &result);  return result; }

String read_string(String* string)
{
    String result;
    if (!read_string(string, &result)) return {};
    return result;
}


////////////////////////////////////////////////////////////////////////////////
// File path utilities.
////////////////////////////////////////////////////////////////////////////////


#define IsPathSeparator(x) (((x) == '\\') || ((x) == '/'))


String get_file_name(String path)
{
    umm last_slash_index = find_last_occurance_of_any(path, slash_chars);
    if (last_slash_index == NOT_FOUND)
        return path;

    umm start_index = last_slash_index + 1;
    umm length = path.length - start_index;

    String file_name = substring(path, start_index, length);
    return file_name;
}


String get_file_name_without_extension(String path)
{
    umm last_slash_index = find_last_occurance_of_any(path, slash_chars);
    if (last_slash_index != NOT_FOUND)
        consume(&path, last_slash_index + 1);

    umm last_dot_index = find_last_occurance(path, '.');
    if (last_dot_index != NOT_FOUND)
        path.length = last_dot_index;

    return path;
}


String get_file_extension(String path)
{
    umm last_slash_index = find_last_occurance_of_any(path, slash_chars);
    if (last_slash_index != NOT_FOUND)
        consume(&path, last_slash_index + 1);

    umm last_dot_index = find_last_occurance(path, '.');
    if (last_dot_index == NOT_FOUND)
        return ""_s;

    consume(&path, last_dot_index + 1);
    return path;
}


String get_parent_directory_path(String path)
{
    if (!path) return {};

    // @Reconsider - this doesn't really works for weird Windows paths,
    // or for any paths containing . and ..

    umm last_slash_index = find_last_occurance_of_any(path, slash_chars);
    // @Incomplete if (last_slash_index == NOT_FOUND)

    String parent = substring(path, 0, last_slash_index);
    return parent;
}


bool is_path_absolute(String path)
{
    // IMPORTANT NOTE! We treat drive-relative paths as absolute, ex. "C:path" or "\path"
    // *Relative* in this codebase means that the app gets to choose what it's relative to.
    // If the user enters a drive-relative path, they don't want us to choose a root, they chose it,
    // so it's kinda absolute. This means we shouldn't change the working directory, ever.

    if (path.length && IsPathSeparator(path.data[0]))
    {
        // Might be a UNC path, ex. "\\host\share\path"
        // or a current-drive-relative path, ex. "\path"
        // or an NT path. ex "\??\C:\path"
        return true;
    }

    // Might start with a drive name, ex. "C:\path" or "C:path"
    for (u8* cursor = path.data; path.length--; cursor++)
    {
        if (*cursor == ':') return true;
        if (IsPathSeparator(*cursor)) break;
    }

    // Otherwise, it's relative (in our interpretation of what relative means; see comment above).
    return false;
}


bool is_path_relative(String path)
{
    return !is_path_absolute(path);
}


String concatenate_path(Region* memory, String a, String b)
{
    if (suffix_equals(a, "/"_s))
        return concatenate(memory, a, b);
    else
        return concatenate(memory, a, "/"_s, b);
}


////////////////////////////////////////////////////////////////////////////////
// 16-bit strings. These mostly exist for interfacing with Windows.
// That's why conversion routines return null terminated strings, and why
// we don't really support any operations on them.
////////////////////////////////////////////////////////////////////////////////



static inline bool is_legal_code_point(u32 code_point)
{
    if (code_point > 0x10FFFF) return false;
    if (code_point >= 0xD800 && code_point <= 0xDFFF) return false;
    return true;
}


u32 get_utf8_sequence_length(u32 code_point)
{
    if (code_point <      0x80) { return 1; }
    if (code_point <     0x800) { return 2; }
    if (code_point <   0x10000) { return 3; }
    if (code_point <  0x200000) { return 4; }
    if (code_point < 0x4000000) { return 5; }
                                { return 6; }
}

void encode_utf8_sequence(u32 code_point, u8* target, u32 length)
{
    switch (length)
    {
    case 6:  target[5] = (u8)((code_point | 0x80) & 0xBF); code_point >>= 6;  // fall-through
    case 5:  target[4] = (u8)((code_point | 0x80) & 0xBF); code_point >>= 6;  // fall-through
    case 4:  target[3] = (u8)((code_point | 0x80) & 0xBF); code_point >>= 6;  // fall-through
    case 3:  target[2] = (u8)((code_point | 0x80) & 0xBF); code_point >>= 6;  // fall-through
    case 2:  target[1] = (u8)((code_point | 0x80) & 0xBF); code_point >>= 6;  // fall-through
    }

    static constexpr u8 FIRST_MASK[7] = { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };
    target[0] = (u8)(code_point | FIRST_MASK[length]);
}


bool decode_utf8_sequence(String* string, u32* out_code_point, String* out_consumed)
{
    umm length = string->length;
    u8* data = string->data;
    u32 consume_count = 0;
    assert(length);

    Defer(if (out_consumed)
    {
        out_consumed->length = consume_count;
        out_consumed->data = data;
    });

    u8 unit1 = data[consume_count++];
    consume(string, 1);

    if (unit1 < 0x80)
    {
        *out_code_point = unit1;
        return true;
    }

    if (unit1 >= 0x80 && unit1 <= 0xBF)
        return false;


    u32 code_point = unit1;

    #define HandleUnit                                  \
    {                                                   \
        if (!--length)  return false;                   \
        u8 unit = data[consume_count++];                \
        if (unit < 0x80 || unit > 0xBF)  return false;  \
        code_point = (code_point << 6) + unit;          \
    }

    if (unit1 >= 0xFC) HandleUnit  // 6 units
    if (unit1 >= 0xF8) HandleUnit  // 5 units
    if (unit1 >= 0xF0) HandleUnit  // 4 units
    if (unit1 >= 0xE0) HandleUnit  // 3 units
    HandleUnit                     // 2 units

    #undef HandleUnit


    static constexpr u32 DECODING_MAGIC[] =
    {
        0x00000000,
        0x00000000, 0x00003080, 0x000E2080,
        0x03C82080, 0xFA082080, 0x82082080
    };

    code_point -= DECODING_MAGIC[consume_count];
    consume(string, consume_count - 1);

    *out_code_point = code_point;
    return true;
}


String utf8_safe_truncate(String string, umm maximum_bytes)
{
    if (string.length <= maximum_bytes)
        return string;

    string.length = maximum_bytes;
    for (umm i = maximum_bytes; i; i--)
    {
        u8 unit = string.data[i - 1];
        umm continuations;
             if (unit >= 0b11111100) continuations = 5;  // leading unit of a 6-unit sequence
        else if (unit >= 0b11111000) continuations = 4;  // leading unit of a 5-unit sequence
        else if (unit >= 0b11110000) continuations = 3;  // leading unit of a 4-unit sequence
        else if (unit >= 0b11100000) continuations = 2;  // leading unit of a 3-unit sequence
        else if (unit >= 0b11000000) continuations = 1;  // leading unit of a 2-unit sequence
        else if (unit >= 0b10000000) continue;           // continuation unit
        else                         break;              // ascii
        if (continuations > maximum_bytes - i)
            string.length = i - 1;
        break;
    }
    return string;
}


static void encode_utf16_sequence(u32 code_point, u16* target, u32 length)
{
    if (length == 2)
    {
        code_point -= 0x10000;
        target[0] = 0xD800 + (u16)(code_point >> 10);
        target[1] = 0xDC00 + (u16)(code_point & 0x3FF);
        return;
    }

    target[0] = (u16) code_point;
}


// If this function finds an unpaired surrogate, it will decode it as a
// code point equal to that surrogate. It is up to the caller to check if
// the returned value is a legal Unicode code point, if relevant.
static u32 decode_utf16_sequence(String16* string)
{
    assert(string->length);
    u32 unit1 = string->data[0];
    string->data++;
    string->length--;

    if (unit1 >= 0xD800 && unit1 <= 0xDBFF)
    {
        if (!string->length)
            return unit1;  // Unpaired high surrogate.

        u32 unit2 = string->data[0];
        if (unit2 < 0xDC00 || unit2 > 0xDFFF)
            return unit1;  // Unpaired high surrogate.

        string->data++;
        string->length--;
        return (((unit1 - 0xD800) << 10) | (unit2 - 0xDC00)) + 0x10000;
    }

    // Might be an unpaired low surrogate, but it makes no difference.
    return unit1;
}


umm convert_utf8_to_utf16(String16 target, String source)
{
    umm utf16_length = 0;

    while (source)
    {
        u32 code_point;
        if (!decode_utf8_sequence(&source, &code_point))
            continue;

        u32 sequence_length = (code_point < 0x10000) ? 1 : 2;
        if (target)
        {
            u16* sequence = target.data + utf16_length;
            encode_utf16_sequence(code_point, sequence, sequence_length);
        }

        utf16_length += sequence_length;
    }

    return utf16_length;
}


umm convert_utf16_to_utf8(String target, String16 source)
{
    umm utf8_length = 0;

    while (source)
    {
        u32 code_point = decode_utf16_sequence(&source);

        u32 sequence_length = get_utf8_sequence_length(code_point);
        if (target)
        {
            u8* sequence = target.data + utf8_length;
            encode_utf8_sequence(code_point, sequence, sequence_length);
        }

        utf8_length += sequence_length;
    }

    return utf8_length;
}


// The returned string is null terminated.
String16 make_string16(const u16* c_string)
{
    umm length = 0;
    while (c_string[length]) length++;

    String16 result;
    result.length = length;
    result.data = alloc<u16>(temp, length + 1);

    memcpy(result.data, c_string, 2 * (length + 1));

    return result;
}

String16 wrap_string16(u16* c_string)
{
    umm length = 0;
    while (c_string[length]) length++;

    String16 result;
    result.length = length;
    result.data = c_string;
    return result;
}

// The returned string is null terminated.
String16 convert_utf8_to_utf16(String string, Region* memory)
{
    umm length = convert_utf8_to_utf16({}, string);

    String16 string16;
    string16.length = length;
    string16.data = alloc<u16, false>(memory, length + 1);
    string16.data[length] = 0;

    length = convert_utf8_to_utf16(string16, string);
    assert(string16.length == length);

    return string16;
}

// The returned string is null terminated.
String convert_utf16_to_utf8(String16 string, Region* memory)
{
    umm length = convert_utf16_to_utf8({}, string);

    String string8;
    string8.length = length;
    string8.data = alloc<u8, false>(memory, length + 1);
    string8.data[length] = 0;

    length = convert_utf16_to_utf8(string8, string);
    assert(string8.length == length);

    return string8;
}



String16 allocate_string(Region* memory, String16 string)
{
    String16 result;
    result.length = string.length;
    result.data = alloc<u16>(memory, string.length);
    memcpy(result.data, string.data, string.length * 2);
    return result;
}

String16 allocate_string_on_heap(String16 string)
{
    String16 result;
    result.length = string.length;
    result.data = alloc<u16, false>(NULL, string.length);
    memcpy(result.data, string.data, string.length * 2);
    return result;
}

String16 concatenate(Region* memory, String16 first, String16 second, String16 third, String16 fourth, String16 fifth, String16 sixth)
{
    String16 result;
    result.length = first.length + second.length + third.length + fourth.length + fifth.length + sixth.length;
    result.data = alloc<u16, false>(memory, result.length);

    u16* write = result.data;

    memcpy(write, first.data, first.length * 2);
    write += first.length;

    memcpy(write, second.data, second.length * 2);
    write += second.length;

    memcpy(write, third.data, third.length * 2);
    write += third.length;

    memcpy(write, fourth.data, fourth.length * 2);
    write += fourth.length;

    memcpy(write, fifth.data, fifth.length * 2);
    write += fifth.length;

    memcpy(write, sixth.data, sixth.length * 2);
    write += sixth.length;

    return result;
}

String16 concatenate(String16 first, String16 second, String16 third, String16 fourth, String16 fifth, String16 sixth)
{
    return concatenate(temp, first, second, third, fourth, fifth, sixth);
}

String16 substring(String16 string, umm start_index, umm length)
{
    assert(start_index <= string.length);
    assert((start_index + length) <= string.length);

    String16 result;
    result.length = length;
    result.data = string.data + start_index;

    return result;
}


bool operator==(String16 lhs, String16 rhs)
{
    if (lhs.length != rhs.length)
        return false;
    return memcmp(lhs.data, rhs.data, lhs.length * 2) == 0;
}


bool prefix_equals(String16 string, String16 prefix)
{
    if (string.length < prefix.length)
        return false;

    return memcmp(string.data, prefix.data, prefix.length * 2) == 0;
}

bool suffix_equals(String16 string, String16 suffix)
{
    if (string.length < suffix.length)
        return false;

    u16* substring = string.data + string.length - suffix.length;
    return memcmp(substring, suffix.data, suffix.length * 2) == 0;
}


umm find_first_occurance(String16 string, u16 of)
{
    for (smm i = 0; i < string.length; i++)
        if (string.data[i] == of)
            return i;

    return NOT_FOUND;
}

umm find_first_occurance(String16 string, String16 of)
{
    if (string.length < of.length)
        return NOT_FOUND;

    for (smm i = 0; i <= string.length - of.length; i++)
        if (memcmp(string.data + i, of.data, of.length * 2) == 0)
            return i;

    return NOT_FOUND;
}

umm find_first_occurance_of_any(String16 string, String16 any_of)
{
    for (smm i = 0; i < string.length; i++)
    {
        u16 c = string[i];

        for (umm j = 0; j < any_of.length; j++)
        {
            u16 c2 = any_of[j];
            if (c == c2)
                return i;
        }
    }

    return NOT_FOUND;
}


umm find_last_occurance(String16 string, u16 of)
{
    for (smm i = string.length - 1; i >= 0; i--)
        if (string[i] == of)
            return i;

    return NOT_FOUND;
}

umm find_last_occurance(String16 string, String16 of)
{
    if (string.length < of.length)
        return NOT_FOUND;

    for (smm i = string.length - of.length; i >= 0; i--)
        if (memcmp(string.data + i, of.data, of.length * 2) == 0)
            return i;

    return NOT_FOUND;
}

umm find_last_occurance_of_any(String16 string, String16 any_of)
{
    for (smm i = string.length - 1; i >= 0; i--)
    {
        u16 c = string[i];

        for (umm j = 0; j < any_of.length; j++)
        {
            u16 c2 = any_of[j];
            if (c == c2)
                return i;
        }
    }

    return NOT_FOUND;
}

bool match_wildcard_string(String wildcard, String string)
{
    bool first_substring = true;
    bool ends_with_wildcard = suffix_equals(wildcard, "*"_s);
    while (wildcard)
    {
        String substring = consume_until(&wildcard, '*');
        if (first_substring)
        {
            if (!prefix_equals(string, substring)) return false;
            consume(&string, substring.length);
            first_substring = false;
        }
        else
        {
            umm at = find_first_occurance(string, substring);
            if (at == NOT_FOUND) return false;
            consume(&string, at + substring.length);
        }

        while (wildcard && wildcard.data[0] == '*')
            consume(&wildcard, 1);
    }
    return ends_with_wildcard || string.length == 0;
}


wchar_t* make_windows_path(String path)
{
    return (wchar_t*) make_windows_path_string16(path).data;
}

String16 make_windows_path_string16(String path)
{
    String16 result = convert_utf8_to_utf16(path, temp);
    for (umm i = 0; i < result.length; i++)
        if (result.data[i] == '/')
            result.data[i] = '\\';
    return result;
}

String make_utf8_path(wchar_t* path)
{
    return make_utf8_path(wrap_string16((u16*) path));
}

String make_utf8_path(String16 path)
{
    String result = convert_utf16_to_utf8(path, temp);
    for (umm i = 0; i < result.length; i++)
        if (result.data[i] == '\\')
            result.data[i] = '/';
    return result;
}



////////////////////////////////////////////////////////////////////////////////
// 32-bit strings.
// UTF-32 encoding is assumed.
////////////////////////////////////////////////////////////////////////////////



String32 allocate_string32(Region* memory, String32 string)
{
    String32 result;
    result.length = string.length;
    result.data = alloc<u32, false>(memory, string.length);
    memcpy(result.data, string.data, string.length * 4);
    return result;
}

String32 allocate_zero_string32(Region* memory, umm length)
{
    String32 result;
    result.length = length;
    result.data = alloc<u32>(memory, length);
    return result;
}

String32 allocate_uninitialized_string32(Region* memory, umm length)
{
    String32 result;
    result.length = length;
    result.data = alloc<u32, false>(memory, length);
    return result;
}



umm convert_utf8_to_utf32(String32 target, String source)
{
    umm utf32_length = 0;
    while (source)
    {
        u32 code_point;
        if (!decode_utf8_sequence(&source, &code_point)) continue;
        if (target)
            target.data[utf32_length] = code_point;
        utf32_length++;
    }
    return utf32_length;
}

umm convert_utf32_to_utf8(String target, String32 source)
{
    umm utf8_length = 0;
    for (umm i = 0; i < source.length; i++)
    {
        u32 code_point = source.data[i];
        u32 sequence_length = get_utf8_sequence_length(code_point);
        if (target)
            encode_utf8_sequence(code_point, target.data + utf8_length, sequence_length);
        utf8_length += sequence_length;
    }
    return utf8_length;
}

String32 convert_utf8_to_utf32(String string, Region* memory)
{
    umm length = convert_utf8_to_utf32({}, string);

    String32 string32;
    string32.length = length;
    string32.data = alloc<u32, false>(memory, length + 1);
    string32.data[length] = 0;

    length = convert_utf8_to_utf32(string32, string);
    assert(string32.length == length);
    return string32;
}

String convert_utf32_to_utf8(String32 string, Region* memory)
{
    umm length = convert_utf32_to_utf8({}, string);

    String string8;
    string8.length = length;
    string8.data = alloc<u8, false>(memory, length + 1);
    string8.data[length] = 0;

    length = convert_utf32_to_utf8(string8, string);
    assert(string8.length == length);
    return string8;
}



////////////////////////////////////////////////////////////////////////////////
// Region-based string building.
////////////////////////////////////////////////////////////////////////////////



static inline bool region_builder_grow(Region* memory, void** base, umm size, umm size_to_add, umm alignment)
{
    Region_Cursor cursor;
    lk_region_cursor(memory, &cursor);
    LeakcheckNextAllocationType(u8);
    void* allocated = lk_region_alloc(memory, size_to_add, alignment);

    void* end = (byte*) *base + size;
    if (size && (allocated != end))
    {
        // The allocation isn't contiguous. Rewind and reallocate.
        lk_region_rewind(memory, &cursor);

        LeakcheckNextAllocationType(u8);
        *base = lk_region_alloc(memory, size + size_to_add, alignment);
        return true;
    }

    if (!size)
        *base = allocated;

    return false;
}

void* region_builder_reserve(Region* memory, void** base, umm size, umm reservation_size, umm alignment)
{
    void* old_base = *base;
    if (region_builder_grow(memory, base, size, reservation_size, alignment))
        memcpy(*base, old_base, size);
    return (byte*) *base + size;
}

void region_builder_append(Region* memory, void** base, umm size, const void* data, umm data_size, umm alignment)
{
    void* old_base = *base;
    if (region_builder_grow(memory, base, size, data_size, alignment))
        memcpy(*base, old_base, size);
    memcpy((byte*) *base + size, data, data_size);
}

void region_builder_insert(Region* memory, void** base, umm size, umm offset, const void* data, umm data_size, umm alignment)
{
    byte* old_base = (byte*) *base;
    if (region_builder_grow(memory, base, size, data_size, alignment))
    {
        byte* new_base = (byte*) *base;
        memcpy(new_base, old_base, offset);
        new_base += offset;
        old_base += offset;

        memcpy(new_base, data, data_size);
        new_base += data_size;

        memcpy(new_base, old_base, size - offset);
    }
    else
    {
        byte* spot = old_base + offset;
        memmove(spot + data_size, spot, size - offset);
        memcpy(spot, data, data_size);
    }
}

// String building functions

void append(String* string, Region* memory, const void* data, umm length)
{
    region_builder_append(memory, (void**) &string->data, string->length, data, length, 1);
    string->length += length;
}

void append(String* string, Region* memory, String to_append)
{
    append(string, memory, to_append.data, to_append.length);
}

void append(String* string, Region* memory, const char* c_string)
{
    append(string, memory, c_string, strlen(c_string));
}

void insert(String* string, Region* memory, umm at_offset, const void* data, umm length)
{
    region_builder_insert(memory, (void**) &string->data, string->length, at_offset, data, length, 1);
    string->length += length;
}

void insert(String* string, Region* memory, umm at_offset, String to_insert)
{
    insert(string, memory, at_offset, to_insert.data, to_insert.length);
}

void insert(String* string, Region* memory, umm at_offset, const char* c_string)
{
    insert(string, memory, at_offset, c_string, strlen(c_string));
}



////////////////////////////////////////////////////////////////////////////////
// Input buffer
////////////////////////////////////////////////////////////////////////////////


void skip(Input_Buffer* source, umm size)
{
    while (!source->error)
    {
        umm remaining = source->end - source->cursor;
        if (size <= remaining)
        {
            source->cursor += size;
            break;
        }
        size -= remaining;
        source->cursor = source->end;
        refill(source);
    }
}

void copy_from_buffer(void* target, Input_Buffer* source, umm size)
{
    byte* bytes = (byte*) target;
    while (true)
    {
        umm remaining = source->end - source->cursor;
        if (size <= remaining)
        {
            memcpy(bytes, source->cursor, size);
            source->cursor += size;
            break;
        }

        memcpy(bytes, source->cursor, remaining);
        bytes += remaining;
        size -= remaining;

        refill(source);
    }
}


String read_entire_buffer(Input_Buffer* source, Region* memory)
{
    String_Concatenator cat = {};
    while (!source->error)
    {
        add(&cat, source->cursor, source->end - source->cursor);
        refill(source);
    }
    return resolve_to_string_and_free(&cat, memory);
}

void skip_entire_buffer(Input_Buffer* source)
{
    while (!source->error)
    {
        source->cursor = source->end;
        refill(source);
    }
}


static bool zero_buffer_refill(Input_Buffer* buffer)
{
    static byte zeroes[256];
    buffer->start  = zeroes;
    buffer->cursor = zeroes;
    buffer->end    = zeroes + sizeof(zeroes);
    return !buffer->error;
}

bool fail(Input_Buffer* buffer, const char* error)
{
    assert(error);
    buffer->error = error;
    buffer->refill = zero_buffer_refill;
    return refill(buffer);
}

static const char* eof_error_string = "Reading past the end of the buffer.";

bool fail_eof(Input_Buffer* buffer)
{
    return fail(buffer, eof_error_string);
}

bool failed_eof(Input_Buffer* buffer)
{
    return buffer->error == eof_error_string;
}


void make_zero_input_buffer(Input_Buffer* buffer)
{
    ZeroStruct(buffer);
    buffer->refill = zero_buffer_refill;
    refill(buffer);
}

void make_memory_input_buffer(Input_Buffer* buffer, void* base, umm size)
{
    ZeroStruct(buffer);

    byte* bytes = (byte*) base;
    buffer->start  = bytes;
    buffer->cursor = bytes;
    buffer->end    = bytes + size;
    buffer->refill = fail_eof;
}

Input_Buffer make_memory_input_buffer(String data)
{
    Input_Buffer result;
    make_memory_input_buffer(&result, data.data, data.length);
    return result;
}



void make_substring_input_buffer(Substring_Input_Buffer* buffer, Input_Buffer* source, umm length)
{
    ZeroStruct(buffer);
    buffer->remaining = length;
    buffer->source    = source;
    buffer->refill = [](Input_Buffer* buffer_ptr)
    {
        Substring_Input_Buffer* buffer = (Substring_Input_Buffer*) buffer_ptr;
        if (!buffer->remaining)
            return fail_eof(buffer);

        Input_Buffer* source = buffer->source;
        if (source->cursor == source->end)
            if (!refill(source))
                return fail(buffer, source->error);

        buffer->start  = source->cursor;
        buffer->cursor = source->cursor;
        buffer->end    = source->cursor + buffer->remaining;
        if (buffer->end > source->end)
            buffer->end = source->end;

        umm consumed = buffer->end - buffer->cursor;
        source->cursor += consumed;
        buffer->remaining -= consumed;
        return true;
    };
    refill(buffer);
}



void make_concatenator_input_buffer(Concatenator_Input_Buffer* buffer, Array<String> strings)
{
    buffer->strings = strings;
    buffer->next_string_index = 0;
    buffer->refill = [](Input_Buffer* buffer_ptr)
    {
        Concatenator_Input_Buffer* buffer = (Concatenator_Input_Buffer*) buffer_ptr;
        while (true)
        {
            if (buffer->next_string_index >= buffer->strings.count)
                return fail_eof(buffer);

            String string = buffer->strings[buffer->next_string_index++];
            if (!string) continue;

            buffer->start  = string.data;
            buffer->cursor = string.data;
            buffer->end    = string.data + string.length;
            return true;
        }
    };
    refill(buffer);
}


bool consume_until(Input_Buffer* in, Concatenator_Input_Buffer* prefix, String until_what,
                   Region* memory, bool copy_strings, umm seek_limit)
{
    ZeroStruct(prefix);
    Dynamic_Array<String> strings = {};
    Defer(free_heap_array(&strings));
#define LastString (strings[strings.count - 1])

    umm matched = 0;
    while (!in->error)
    {
        *reserve_item(&strings) = { (umm)(in->end - in->cursor), (u8*) in->cursor };
        while (in->cursor < in->end)
        {
            if (seek_limit-- == 0)
                return false;

            umm remaining = in->end - in->cursor;
            umm to_match = until_what.length - matched;
            if (to_match > remaining)
                to_match = remaining;

            if (memcmp(in->cursor, until_what.data + matched, to_match) == 0)
            {
                matched += to_match;
                in->cursor += to_match;
                if (matched != until_what.length)
                    continue;

                LastString.length -= in->end - in->cursor;
                if (copy_strings) LastString = allocate_string(memory, LastString);

                for (umm to_remove = until_what.length; to_remove;)
                {
                    if (LastString.length > to_remove)
                    {
                        LastString.length -= to_remove;
                        break;
                    }

                    to_remove -= LastString.length;
                    if (copy_strings && !memory) free(LastString.data);
                    strings.count--;
                }

                if (memory)
                {
                    make_concatenator_input_buffer(prefix, allocate_array(memory, &strings));
                }
                else
                {
                    make_concatenator_input_buffer(prefix, strings);
                    strings = {};
                }
                return true;
            }
            else
            {
                matched = 0;
                in->cursor++;
            }
        }

        if (copy_strings) LastString = allocate_string(memory, LastString);
        refill(in);
    }

#undef LastString
    return false;
}



////////////////////////////////////////////////////////////////////////////////
// Output buffer
////////////////////////////////////////////////////////////////////////////////


void copy_to_buffer(Output_Buffer* target, const void* source, umm size)
{
    byte* bytes = (byte*) source;
    while (true)
    {
        umm remaining = target->end - target->cursor;
        if (size <= remaining)
        {
            memcpy(target->cursor, bytes, size);
            target->cursor += size;
            break;
        }

        memcpy(target->cursor, bytes, remaining);
        bytes += remaining;
        size -= remaining;

        commit(target);
    }
}

static bool void_buffer_commit(Output_Buffer* buffer)
{
    static byte junk[256];
    buffer->start  = junk;
    buffer->cursor = junk;
    buffer->end    = junk + sizeof(junk);
    return !buffer->error;
}

bool fail(Output_Buffer* buffer, const char* error)
{
    assert(error);
    buffer->error = error;
    buffer->commit = void_buffer_commit;
    return commit(buffer);
}


void make_void_output_buffer(Output_Buffer* buffer)
{
    ZeroStruct(buffer);
    buffer->commit = void_buffer_commit;
    commit(buffer);
}

void make_memory_output_buffer(Output_Buffer* buffer, void* base, umm size)
{
    ZeroStruct(buffer);

    byte* bytes = (byte*) base;
    buffer->start  = bytes;
    buffer->cursor = bytes;
    buffer->end    = bytes + size;
    buffer->commit = [](Output_Buffer* buffer)
    {
        return fail(buffer, "Writing past the end of the memory range.");
    };
}


void make_string_concatenator_output_buffer(String_Concatenator_Output_Buffer* buffer)
{
    ZeroStruct(buffer);
    buffer->commit = [](Output_Buffer* buffer_ptr) -> bool
    {
        String_Concatenator_Output_Buffer* buffer = (String_Concatenator_Output_Buffer*) buffer_ptr;
        typedef String_Concatenator_Output_Buffer::Chunk Chunk;

        umm     next_size = 128;
        Chunk** next_link = &buffer->head;

        if (buffer->start)
        {
            Chunk* previous = ((Chunk*) buffer->start) - 1;
            previous->size = buffer->end - buffer->start;
            next_size = (buffer->end - buffer->start) * 2;
            next_link = &previous->next;
            buffer->total_size += previous->size;
        }

        Chunk* next_chunk = (Chunk*) alloc<byte, false>(NULL, sizeof(Chunk) + next_size);
        next_chunk->next = NULL;
        next_chunk->size = 0;
        *next_link = next_chunk;

        buffer->start = buffer->cursor = (byte*)(next_chunk + 1);
        buffer->end = buffer->start + next_size;
        return true;
    };
    commit(buffer);
}

String resolve_to_string_and_free(String_Concatenator_Output_Buffer* buffer, Region* memory)
{
    typedef String_Concatenator_Output_Buffer::Chunk Chunk;

    if (buffer->start)
    {
        Chunk* previous = ((Chunk*) buffer->start) - 1;
        previous->size = buffer->cursor - buffer->start;
        buffer->total_size += previous->size;
    }

    String result;
    result.length = buffer->total_size;
    result.data = alloc<u8, false>(memory, result.length);

    umm write = 0;
    Chunk* next = buffer->head;
    while (Chunk* chunk = next)
    {
        next = chunk->next;
        memcpy(result.data + write, chunk + 1, chunk->size);
        write += chunk->size;
        free(chunk);
    }

    return result;
}



////////////////////////////////////////////////////////////////////////////////
// Speculative input/output stream
////////////////////////////////////////////////////////////////////////////////


static SPSC_Buffered_Stream::Buffer* allocate_buffer(u32 buffer_size)
{
    typedef SPSC_Buffered_Stream::Buffer Buffer;
    Buffer* buffer = (Buffer*) alloc<byte, false>(NULL, sizeof(Buffer) + buffer_size);
    store(&buffer->read_cursor, 0);
    store(&buffer->write_cursor, 0);
    store(&buffer->next, (Buffer*) NULL);
    return buffer;
}

void make_spsc_buffered_stream(SPSC_Buffered_Stream* stream, umm buffer_size)
{
    assert(buffer_size && IsPowerOfTwo(buffer_size));
    stream->buffer_size = buffer_size;
    SPSC_Buffered_Stream::Buffer* buffer = allocate_buffer(buffer_size);
    store(&stream->head, buffer);
    store(&stream->tail, buffer);
}

void free_spsc_buffered_stream(SPSC_Buffered_Stream* stream)
{
    SPSC_Buffered_Stream::Buffer* buffer = load(&stream->head);
    while (buffer)
    {
        SPSC_Buffered_Stream::Buffer* next = load(&buffer->next);
        free(buffer);
        buffer = next;
    }
}

static bool set_buffer(SPSC_Buffered_Stream_Input* input, SPSC_Buffered_Stream::Buffer* buffer)
{
    while (true)
    {
        if (!buffer)
        {
            fail_eof(input);
            return false;
        }

        input->buffer = buffer;
        input->next = load(&buffer->next);
        fence();  // must read next before write_cursor
        input->read_cursor  = load(&buffer->read_cursor);
        input->write_cursor = load(&buffer->write_cursor);
        if (input->read_cursor != input->write_cursor)
            return true;

        buffer = input->next;
    }
}

void begin_input(SPSC_Buffered_Stream* stream, SPSC_Buffered_Stream_Input* input)
{
    ZeroStruct(input);

    input->buffer_size = stream->buffer_size;
    input->refill = [](Input_Buffer* input_ptr) -> bool
    {
        SPSC_Buffered_Stream_Input* input = (SPSC_Buffered_Stream_Input*) input_ptr;
        input->total_length += (input->end - input->start);
        input->read_cursor += (input->end - input->start);

        if (input->read_cursor == input->write_cursor)
            if (!set_buffer(input, input->next))
                return false;

        u32 available = input->write_cursor - input->read_cursor;
        assert(available);

        SPSC_Buffered_Stream::Buffer* buffer = input->buffer;
        input->start = buffer->data + (input->read_cursor & (input->buffer_size - 1));
        input->cursor = input->start;
        input->end = input->start + available;
        if (input->end > buffer->data + input->buffer_size)
            input->end = buffer->data + input->buffer_size;

        assert(input->start < input->end);
        return true;
    };

    SPSC_Buffered_Stream::Buffer* head = load(&stream->head);
    assert(head);
    if (set_buffer(input, head))
        input->refill(input);
    assert(input->buffer);
}

void commit_input(SPSC_Buffered_Stream* stream, SPSC_Buffered_Stream_Input* input)
{
    if (!input->error)
    {
        input->total_length += input->cursor - input->start;
        input->read_cursor += input->cursor - input->start;
    }

    assert(input->buffer);
    SPSC_Buffered_Stream::Buffer* head = load(&stream->head);
    store(&stream->head, input->buffer);
    store(&input->buffer->read_cursor, input->read_cursor);
    add(&stream->available, -input->total_length);

    while (head != input->buffer)
    {
        SPSC_Buffered_Stream::Buffer* next = load(&head->next);
        free(head);
        head = next;
    }
}

void begin_output(SPSC_Buffered_Stream* stream, SPSC_Buffered_Stream_Output* output)
{
    ZeroStruct(output);
    output->buffer = load(&stream->tail);
    assert(output->buffer);
    output->read_cursor  = load(&output->buffer->read_cursor);
    output->write_cursor = load(&output->buffer->write_cursor);

    output->buffer_size = stream->buffer_size;
    output->commit = [](Output_Buffer* output_ptr) -> bool
    {
        SPSC_Buffered_Stream_Output* output = (SPSC_Buffered_Stream_Output*) output_ptr;
        output->total_length += (output->end - output->start);
        output->write_cursor += (output->end - output->start);

        if (output->write_cursor == output->read_cursor + output->buffer_size)
        {
            SPSC_Buffered_Stream::Buffer* new_buffer = allocate_buffer(output->buffer_size);
            if (output->tail_next)
            {
                store(&output->buffer->next, new_buffer);
                store(&output->buffer->write_cursor, output->write_cursor);
            }
            else
            {
                output->tail_next = new_buffer;
                output->tail_write_cursor = output->write_cursor;
            }
            output->buffer       = new_buffer;
            output->read_cursor  = load(&new_buffer->read_cursor);
            output->write_cursor = load(&new_buffer->write_cursor);
        }

        u32 available = (output->read_cursor + output->buffer_size) - output->write_cursor;
        assert(available);

        SPSC_Buffered_Stream::Buffer* buffer = output->buffer;
        output->start = buffer->data + (output->write_cursor & (output->buffer_size - 1));
        output->cursor = output->start;
        output->end = output->start + available;
        if (output->end > buffer->data + output->buffer_size)
            output->end = buffer->data + output->buffer_size;

        assert(output->start < output->end);
        return true;
    };

    output->commit(output);
}

void commit_output(SPSC_Buffered_Stream* stream, SPSC_Buffered_Stream_Output* output)
{
    output->total_length += (output->cursor - output->start);
    output->write_cursor += (output->cursor - output->start);
    if (!output->tail_next)
        output->tail_write_cursor = output->write_cursor;
    else
        store(&output->buffer->write_cursor, output->write_cursor);

    SPSC_Buffered_Stream::Buffer* tail = load(&stream->tail);
    store(&tail->write_cursor, output->tail_write_cursor);
    fence();  // must write write_cursor before setting next
    store(&tail->next, output->tail_next);
    add(&stream->available, output->total_length);

    if (output->tail_next)
        store(&stream->tail, output->tail_next);
}

void drop_output(SPSC_Buffered_Stream* stream, SPSC_Buffered_Stream_Output* output)
{
    SPSC_Buffered_Stream::Buffer* buffer = output->tail_next;
    while (buffer)
    {
        SPSC_Buffered_Stream::Buffer* next = load(&buffer->next);
        free(buffer);
        buffer = next;
    }
}



////////////////////////////////////////////////////////////////////////////////
// Text formatting utilities.
////////////////////////////////////////////////////////////////////////////////




// digits MUST be equal to digits_base10_u64(value),
// and destination MUST have at least that many bytes.
void write_base10_u64(u8* destination, umm digits, u64 value)
{
    // K = 2 * ceil(log2(mod))
    // M = 2^K / mod

    constexpr u32 K_100       = 14;         // 2 * 7;
    constexpr u32 M_100       = 163;        // (1ull << K_100) / 100;
    constexpr u32 K_10000     = 28;         // 2 * 14;
    constexpr u64 M_10000     = 26843;      // (1ull << K_10000) / 10000;
    constexpr u64 K_10_POW_8  = 54;                   // 2 * 27
    constexpr u64 M_10_POW_8  = 180143985;            // (1ull << K_10_POW_8) / 10^8;
    constexpr u64 K_10_POW_16 = 108;                  // 2 * 54;
    constexpr u64 M_10_POW_16 = 32451855365842672ull; // 2^K_10_POW_16 / 10^16

    IgnoreUnused(K_10_POW_8);
    IgnoreUnused(M_10_POW_8);
    IgnoreUnused(K_10_POW_16);
    IgnoreUnused(M_10_POW_16);

    CacheAlign char const LUT_literal[] =
        "00010203040506070809101112131415161718192021222324"
        "25262728293031323334353637383940414243444546474849"
        "50515253545556575859606162636465666768697071727374"
        "75767778798081828384858687888990919293949596979899"
        // repeat again, because Barrett reduction returns result < 2*mod
        "00010203040506070809101112131415161718192021222324"
        "25262728293031323334353637383940414243444546474849"
        "50515253545556575859606162636465666768697071727374"
        "75767778798081828384858687888990919293949596979899";
    u16* LUT = (u16*) LUT_literal;

    u8* cursor = (u8*) destination;

#define OneToEightDigits(v)                                               \
    {                                                                     \
        /* v4 * 10000 + v3 */                                             \
        u32 v4 = (v * (u64) M_10000) >> K_10000;                          \
        u32 v3 = v - v4 * 10000;                                          \
        /* v8 * 100^3 + v7 * 100^2 + v6 * 100 + v5 */                     \
        switch (digits & 7)                                               \
        {                                                                 \
        case 1:                                                           \
        {                                                                 \
            u32 v6 = (v3 * M_100) >> K_100;                               \
            u32 v5 = v3 - v6 * 100;                                       \
            *cursor = LUT[v5] >> 8;        cursor += 1;                   \
        } break;                                                          \
        case 2:                                                           \
        {                                                                 \
            u32 v6 = (v3 * M_100) >> K_100;                               \
            u32 v5 = v3 - v6 * 100;                                       \
            store_u16(cursor, LUT[v5]);    cursor += 2;                   \
        } break;                                                          \
        case 3:                                                           \
        {                                                                 \
            u32 v6 = (v3 * M_100) >> K_100;                               \
            u32 v5 = v3 - v6 * 100;                                       \
            v6 += (v5 >= 100);                                            \
            *cursor = LUT[v6] >> 8;        cursor += 1;                   \
            store_u16(cursor, LUT[v5]);    cursor += 2;                   \
        } break;                                                          \
        case 4:                                                           \
        {                                                                 \
            u32 v6 = (v3 * M_100) >> K_100;                               \
            u32 v5 = v3 - v6 * 100;                                       \
            v6 += (v5 >= 100);                                            \
            store_u16(cursor, LUT[v6]);    cursor += 2;                   \
            store_u16(cursor, LUT[v5]);    cursor += 2;                   \
        } break;                                                          \
        case 5:                                                           \
        {                                                                 \
            v4 += (v3 >= 10000);                                          \
            u32 v8 = (v4 * M_100) >> K_100;                               \
            u32 v6 = (v3 * M_100) >> K_100;                               \
            u32 v7 = v4 - v8 * 100;                                       \
            u32 v5 = v3 - v6 * 100;                                       \
            v6 += (v5 >= 100);                                            \
            *cursor = LUT[v7] >> 8;        cursor += 1;                   \
            store_u16(cursor, LUT[v6]);    cursor += 2;                   \
            store_u16(cursor, LUT[v5]);    cursor += 2;                   \
        } break;                                                          \
        case 6:                                                           \
        {                                                                 \
            v4 += (v3 >= 10000);                                          \
            u32 v8 = (v4 * M_100) >> K_100;                               \
            u32 v6 = (v3 * M_100) >> K_100;                               \
            u32 v7 = v4 - v8 * 100;                                       \
            u32 v5 = v3 - v6 * 100;                                       \
            v6 += (v5 >= 100);                                            \
            store_u16(cursor, LUT[v7]);    cursor += 2;                   \
            store_u16(cursor, LUT[v6]);    cursor += 2;                   \
            store_u16(cursor, LUT[v5]);    cursor += 2;                   \
        } break;                                                          \
        case 7:                                                           \
        {                                                                 \
            v4 += (v3 >= 10000);                                          \
            u32 v8 = (v4 * M_100) >> K_100;                               \
            u32 v6 = (v3 * M_100) >> K_100;                               \
            u32 v7 = v4 - v8 * 100;                                       \
            u32 v5 = v3 - v6 * 100;                                       \
            v8 += (v7 >= 100);                                            \
            v6 += (v5 >= 100);                                            \
            *cursor = LUT[v8] >> 8;        cursor += 1;                   \
            store_u16(cursor, LUT[v7]);    cursor += 2;                   \
            store_u16(cursor, LUT[v6]);    cursor += 2;                   \
            store_u16(cursor, LUT[v5]);    cursor += 2;                   \
        } break;                                                          \
        case 0: /* 8 */                                                   \
        {                                                                 \
            v4 += (v3 >= 10000);                                          \
            u32 v8 = (v4 * M_100) >> K_100;                               \
            u32 v6 = (v3 * M_100) >> K_100;                               \
            u32 v7 = v4 - v8 * 100;                                       \
            u32 v5 = v3 - v6 * 100;                                       \
            v8 += (v7 >= 100);                                            \
            v6 += (v5 >= 100);                                            \
            store_u16(cursor, LUT[v8]);    cursor += 2;                   \
            store_u16(cursor, LUT[v7]);    cursor += 2;                   \
            store_u16(cursor, LUT[v6]);    cursor += 2;                   \
            store_u16(cursor, LUT[v5]);    cursor += 2;                   \
        } break;                                                          \
        }                                                                 \
    }

#define EightDigits(v)                                                    \
    {                                                                     \
        /* v4 * 10000 + v3 */                                             \
        u32 v4 = (v * (u64) M_10000) >> K_10000;                          \
        u32 v3 = v - v4 * 10000;                                          \
        v4 += (v3 >= 10000);                                              \
        /* v8 * 100^3 + v7 * 100^2 + v6 * 100 + v5 */                     \
        u32 v8 = (v4 * M_100) >> K_100;                                   \
        u32 v6 = (v3 * M_100) >> K_100;                                   \
        u32 v7 = v4 - v8 * 100;                                           \
        u32 v5 = v3 - v6 * 100;                                           \
        v8 += (v7 >= 100);                                                \
        v6 += (v5 >= 100);                                                \
        store_u16(cursor, LUT[v8]); cursor += 2;                          \
        store_u16(cursor, LUT[v7]); cursor += 2;                          \
        store_u16(cursor, LUT[v6]); cursor += 2;                          \
        store_u16(cursor, LUT[v5]); cursor += 2;                          \
    }

    // v2 * 100000000^2 + v1 * 100000000 + v0
    u32 v0, v1, v2;
    if (value < 100000000)
    {
        v0 = value;
        OneToEightDigits(v0);
    }
    else if (value < 10000000000000000ull)
    {
#if defined(COMPILER_MSVC)
    #if defined(ARCHITECTURE_X64)
        u64 qhi;
        u64 qlo = _umul128(value, M_10_POW_8, &qhi);
        u64 q = (qlo >> K_10_POW_8) | (qhi << (64 - K_10_POW_8));
        v0 = value - q * 100000000ull;
        v1 = q + (v0 >= 100000000);
    #else
        v1 = _udiv64(value, 100000000, &v0);
    #endif
#elif defined(COMPILER_GCC) || defined(COMPILER_CLANG)
    #if defined(ARCHITECTURE_X64) || defined(ARCHITECTURE_ARM64)
        u64 q = (u64)(((__uint128_t) value * (__uint128_t) M_10_POW_8) >> K_10_POW_8);
        v0 = value - q * 100000000ull;
        v1 = q + (v0 >= 100000000);
    #else
        v1 = value / 100000000;
        v0 = value % 100000000;
    #endif
#else
#error "Unsupported"
#endif

        OneToEightDigits(v1);
        EightDigits(v0);
    }
    else
    {
#if defined(COMPILER_MSVC)
    #if defined(ARCHITECTURE_X64)
        u64 qhi2;
        u64 qlo2 = _umul128(value, M_10_POW_16, &qhi2);
        u64 q2 = qhi2 >> (K_10_POW_16 - 64);
        value -= q2 * 10000000000000000ull;
        v2 = q2 + (value >= 10000000000000000ull);

        u64 qhi;
        u64 qlo = _umul128(value, M_10_POW_8, &qhi);
        u64 q = (qlo >> K_10_POW_8) | (qhi << (64 - K_10_POW_8));
        v0 = value - q * 100000000ull;
        v1 = q + (v0 >= 100000000);
    #else
        v2 = value / 10000000000000000ull;
        v1 = _udiv64(value - v2 * 10000000000000000ull, 100000000, &v0);
    #endif
#elif defined(COMPILER_GCC) || defined(COMPILER_CLANG)
    #if defined(ARCHITECTURE_X64) || defined(ARCHITECTURE_ARM64)
        u64 q2 = (u64)(((__uint128_t) value * (__uint128_t) M_10_POW_16) >> K_10_POW_16);
        value -= q2 * 10000000000000000ull;
        v2 = q2 + (value >= 10000000000000000ull);

        u64 q = (u64)(((__uint128_t) value * (__uint128_t) M_10_POW_8) >> K_10_POW_8);
        v0 = value - q * 100000000ull;
        v1 = q + (v0 >= 100000000);
    #else
        v2 = value / 10000000000000000ull;
        u64 v1v0 = value - v2 * 10000000000000000ull;
        v1 = v1v0 / 100000000;
        v0 = v1v0 % 100000000;
    #endif
#else
#error "Unsupported"
#endif

        OneToEightDigits(v2);
        EightDigits(v1);
        EightDigits(v0);
    }

#undef OneToEightDigits
#undef EightDigits
}



////////////////////////////////////////////////////////////////////////////////
// Safe text formatting.
////////////////////////////////////////////////////////////////////////////////


#define SignedIntegerFormatFunctions(T)                                 \
    umm format_item_length(T v)                                         \
    {                                                                   \
        return v < 0                                                    \
             ? digits_base10_u64((u64) -v) + 1                          \
             : digits_base10_u64((u64)  v);                             \
    }                                                                   \
                                                                        \
    void format_item(Output_Buffer* buffer, T v)                        \
    {                                                                   \
        u8 temp[21];                                                    \
        bool direct = (buffer->end - buffer->cursor >= sizeof(temp));   \
        u8* cursor = direct ? buffer->cursor : temp;                    \
                                                                        \
        u64 abs = (u64) v;                                              \
        if (v < 0)                                                      \
        {                                                               \
            *(cursor++) = '-';                                          \
            abs = (u64) -v;                                             \
        }                                                               \
        umm digits = digits_base10_u64(abs);                            \
        write_base10_u64(cursor, digits, abs);                          \
        cursor += digits;                                               \
                                                                        \
        if (direct) buffer->cursor = cursor;                            \
        else copy_to_buffer(buffer, temp, cursor - temp);               \
    }
SignedIntegerFormatFunctions(signed char)
SignedIntegerFormatFunctions(signed short)
SignedIntegerFormatFunctions(signed int)
SignedIntegerFormatFunctions(signed long)
SignedIntegerFormatFunctions(signed long long)
#undef SignedIntegerFormatFunctions

#define UnsignedIntegerFormatFunctions(T)                               \
    umm format_item_length(T v)                                         \
    {                                                                   \
        return digits_base10_u64(v);                                    \
    }                                                                   \
                                                                        \
    void format_item(Output_Buffer* buffer, T v)                        \
    {                                                                   \
        u8 temp[20];                                                    \
        bool direct = (buffer->end - buffer->cursor >= sizeof(temp));   \
        u8* cursor = direct ? buffer->cursor : temp;                    \
                                                                        \
        umm digits = digits_base10_u64(v);                              \
        write_base10_u64(cursor, digits, v);                            \
        cursor += digits;                                               \
                                                                        \
        if (direct) buffer->cursor = cursor;                            \
        else copy_to_buffer(buffer, temp, cursor - temp);               \
    }
UnsignedIntegerFormatFunctions(unsigned char)
UnsignedIntegerFormatFunctions(unsigned short)
UnsignedIntegerFormatFunctions(unsigned int)
UnsignedIntegerFormatFunctions(unsigned long)
UnsignedIntegerFormatFunctions(unsigned long long)
#undef UnsignedIntegerFormatFunctions


#include "common_float_to_string.inl"

#define DEFAULT_DOUBLE_PRECISION 6

umm format_item_length(double v)
{
    return f64_format_item_length(v, DEFAULT_DOUBLE_PRECISION);
}

void format_item(Output_Buffer* buffer, double v)
{
    f64_format_item(buffer, v, DEFAULT_DOUBLE_PRECISION);
}


umm format_item_length(bool v) { return v ? 4 : 5; }
void format_item(Output_Buffer* buffer, bool v) { write(buffer, v ? "true"_s : "false"_s); }

umm format_item_length(String v) { return v.length; }
void format_item(Output_Buffer* buffer, String v) { write(buffer, v); }


umm format_item_length(String16 v)
{
    umm length = 0;
    while (v)
        length += get_utf8_sequence_length(decode_utf16_sequence(&v));
    return length;
}

void format_item(Output_Buffer* buffer, String16 v)
{
    while (v)
    {
        u32 code_point = decode_utf16_sequence(&v);

        u32 sequence_length = get_utf8_sequence_length(code_point);
        if (buffer->end - buffer->cursor >= sequence_length)
        {
            encode_utf8_sequence(code_point, buffer->cursor, sequence_length);
            buffer->cursor += sequence_length;
        }
        else
        {
            u8 temp[MAX_UTF8_SEQUENCE_LENGTH];
            encode_utf8_sequence(code_point, temp, sequence_length);
            copy_to_buffer(buffer, temp, sequence_length);
        }
    }
}


umm format_item_length(String32 v)
{
    umm length = 0;
    for (umm i = 0; i < v.length; i++)
        length += get_utf8_sequence_length(v.data[i]);
    return length;
}

void format_item(Output_Buffer* buffer, String32 v)
{
    for (umm i = 0; i < v.length; i++)
    {
        u32 code_point = v.data[i];
        u32 sequence_length = get_utf8_sequence_length(code_point);
        if (buffer->end - buffer->cursor >= sequence_length)
        {
            encode_utf8_sequence(code_point, buffer->cursor, sequence_length);
            buffer->cursor += sequence_length;
        }
        else
        {
            u8 temp[MAX_UTF8_SEQUENCE_LENGTH];
            encode_utf8_sequence(code_point, temp, sequence_length);
            copy_to_buffer(buffer, temp, sequence_length);
        }
    }
}


umm format_item_length(const char* v) { return v ? strlen(v) : 6; }
void format_item(Output_Buffer* buffer, const char* v) { if (!v) v = "(null)"; copy_to_buffer(buffer, v, strlen(v)); }


umm format_item_length(const void* v) { return sizeof(umm) * 2; }

void format_item(Output_Buffer* buffer, const void* v)
{
    const char* alphabet = "0123456789abcdef";
    umm integer = (umm) v;
    for (umm i = 0; i < sizeof(umm); i++)
    {
        umm offset = (sizeof(umm) - i - 1) * 8;
        write_u8(buffer, alphabet[(integer >> (offset + 4)) & 15]);
        write_u8(buffer, alphabet[(integer >> (offset + 0)) & 15]);
    }
}


umm format_item_length(Hexadecimal_Format v)
{
    umm digits = digits_base16_u64(v.value);
    if (digits < v.digits)
        digits = v.digits;
    return digits;
}

void format_item(Output_Buffer* buffer, Hexadecimal_Format v)
{
    umm digits = digits_base16_u64(v.value);
    if (digits < v.digits)
        digits = v.digits;

    // Handled separately, because shifting by 64 or more is undefined behavior
    // (and the behavior is not what we want on Intel, it's truncated to 6 bits)
    while (digits > 16)
    {
        write_u8(buffer, '0');
        digits--;
    }

    const char* alphabet = v.capital ? "0123456789ABCDEF" : "0123456789abcdef";
    for (umm offset = digits * 4; offset;)
    {
        offset -= 4;
        write_u8(buffer, alphabet[(v.value >> offset) & 15]);
    }
}

umm format_item_length(U64_Format v)
{
    umm digits = digits_base10_u64(v.value);
    if (digits < v.digits)
        digits = v.digits;
    return digits;
}

void format_item(Output_Buffer* buffer, U64_Format v)
{
    umm digits = digits_base10_u64(v.value);
    if (digits < v.digits)
    {
        umm padding = v.digits - digits;
        for (umm i = 0; i < padding; i++)
            write_u8(buffer, '0');
    }

    if (buffer->end - buffer->cursor >= digits)
    {
        write_base10_u64(buffer->cursor, digits, v.value);
        buffer->cursor += digits;
    }
    else
    {
        u8 data[32];
        write_base10_u64(data, digits, v.value);

        String string;
        string.length = digits;
        string.data   = data;
        write(buffer, string);
    }
}

umm format_item_length(S64_Format v)
{
    u64 value_u64  = (u64) v.value;
    umm characters = 0;
    if (v.value < 0)
    {
        value_u64  = (u64) -v.value;
        characters = 1;
    }
    else if (v.force_sign)
    {
        characters = 1;
    }

    characters += digits_base10_u64(value_u64);

    if (characters < v.characters)
        characters = v.characters;
    return characters;
}

void format_item(Output_Buffer* buffer, S64_Format v)
{
    u64 value_u64  = (u64) v.value;
    umm characters = 0;
    if (v.value < 0)
    {
        value_u64  = (u64) -v.value;
        characters = 1;
    }
    else if (v.force_sign)
    {
        characters = 1;
    }

    umm digits  = digits_base10_u64(value_u64);
    characters += digits;

    if (characters < v.characters)
    {
        umm padding = v.characters - characters;
        for (umm i = 0; i < padding; i++)
            write_u8(buffer, ' ');
    }

    if (v.value < 0)
        write_u8(buffer, '-');
    else if (v.force_sign)
        write_u8(buffer, '+');

    if (buffer->end - buffer->cursor >= digits)
    {
        write_base10_u64(buffer->cursor, digits, value_u64);
        buffer->cursor += digits;
    }
    else
    {
        u8 data[32];
        write_base10_u64(data, digits, value_u64);

        String string;
        string.length = digits;
        string.data   = data;
        write(buffer, string);
    }
}

umm format_item_length(F64_Format v)
{
    return f64_format_item_length(v.value, (s32) v.precision);
}

void format_item(Output_Buffer* buffer, F64_Format v)
{
    f64_format_item(buffer, v.value, (u32) v.precision);
}

umm format_item_length(String_Format v)
{
    umm length = v.value.length;
    if (length < v.desired_length)
        length = v.desired_length;
    return length;
}

void format_item(Output_Buffer* buffer, String_Format v)
{
    if (v.desired_length > v.value.length)
    {
        umm padding = v.desired_length - v.value.length;
        for (umm i = 0; i < padding; i++)
            write_u8(buffer, ' ');
    }

    write(buffer, v.value);
}


umm format_item_length(Plural_Format v)
{
    if (!v.is_plural)
        return v.singular_form.length;
    if (v.plural_form)
        return v.plural_form.length;
    return v.singular_form.length + 1;
}

void format_item(Output_Buffer* buffer, Plural_Format v)
{
    if (!v.is_plural)
        write(buffer, v.singular_form);
    else if (v.plural_form)
        write(buffer, v.plural_form);
    else
    {
        write(buffer, v.singular_form);
        write_u8(buffer, 's');
    }
}


void format_next_item(Output_Buffer* buffer, u8** read, umm* remaining_literal)
{
    u8* cursor = *read;
    if (*remaining_literal <= buffer->end - buffer->cursor)
    {
        // Fast path: literal part fits in available buffer, so no refill checks are needed
        u8* write = buffer->cursor;
        u8* write_start = write;
        while (u8 c = *cursor)
        {
            if (c == '%')
            {
                cursor++;
                u8 next = *cursor;
                if (next != '%')
                {
                    buffer->cursor = write;
                    *remaining_literal -= write - write_start;
                    *read = cursor + (next == '~');
                    return;
                }
            }
            *(write++) = c;
            cursor++;
        }

        buffer->cursor = write;
        *remaining_literal = 0;
        *read = cursor;
    }
    else
    {
        u8* start = cursor;
        while (u8 c = *cursor)
        {
            if (c == '%')
            {
                umm length = cursor - start;
                copy_to_buffer(buffer, start, cursor - start);
                *remaining_literal -= length;

                cursor++;
                u8 next = *cursor;
                if (next != '%')
                {
                    *read = cursor + (next == '~');
                    return;
                }

                write_u8(buffer, '%');
                (*remaining_literal)--;
                start = cursor + 1;
            }
            cursor++;
        }

        copy_to_buffer(buffer, start, cursor - start);
        *read = cursor;
        *remaining_literal = 0;
    }
}



////////////////////////////////////////////////////////////////////////////////
// Logging.
////////////////////////////////////////////////////////////////////////////////


static Lock log_lock;
static Dynamic_Array<void(*)(String, String, String)> log_handlers;

static inline void maybe_make_log_lock()
{
    OnlyOnce make_lock(&log_lock);
}

void add_log_handler(void(*handler)(String, String, String))
{
    maybe_make_log_lock();
    acquire(&log_lock);
    add_item(&log_handlers, &handler);
    release(&log_lock);
}

String LOG_DEBUG = "DEBUG"_s;
String LOG_INFO  = "INFO"_s;
String LOG_WARN  = "WARN"_s;
String LOG_ERROR = "ERROR"_s;

String format_log_message_as_line(Region* memory, String severity, String subsystem, String message, File_Time timestamp)
{
    Date date;
    utc_date_from_filetime(timestamp, &date);

    if (subsystem)
    {
        return Format(memory, "%/%/% %:%:%.% % - [%] %\n",
            u64_format(date.year, 4), u64_format(date.month,  2), u64_format(date.day,    2),
            u64_format(date.hour, 2), u64_format(date.minute, 2), u64_format(date.second, 2),
            u64_format(date.nanosecond / 1000000, 3), string_format(severity, 5), subsystem, message);
    }
    else
    {
        return Format(memory, "%/%/% %:%:%.% % - %\n",
            u64_format(date.year, 4), u64_format(date.month,  2), u64_format(date.day,    2),
            u64_format(date.hour, 2), u64_format(date.minute, 2), u64_format(date.second, 2),
            u64_format(date.nanosecond / 1000000, 3), string_format(severity, 5), message);
    }
}

void log(String severity, String subsystem, String message)
{
    maybe_make_log_lock();

    String line;
#if defined(OS_ANDROID)
    if (subsystem)
        line = Format(temp, "[NDK %] %\n", subsystem, message);
    else
        line = Format(temp, "[NDK] %\n", message);
#else
    line = format_log_message_as_line(temp, severity, subsystem, message, current_filetime());
#endif

    acquire(&log_lock);
    For (log_handlers) (*it)(severity, subsystem, line);
    release(&log_lock);
}



////////////////////////////////////////////////////////////////////////////////
// Sorting.
////////////////////////////////////////////////////////////////////////////////


static inline void swap(void* a, void* b, umm size)
{
    if (a == b) return;

    constexpr umm STACK_COPY_THRESHOLD = 1024;
    if (size <= STACK_COPY_THRESHOLD)
    {
        byte t[STACK_COPY_THRESHOLD];
        memcpy(t, a, size);
        memcpy(a, b, size);
        memcpy(b, t, size);
    }
    else
    {
        byte* bytes_a = (byte*) a;
        byte* bytes_b = (byte*) b;
        for (umm i = 0; i < size; i++)
            swap<byte>(&bytes_a[i], &bytes_b[i]);
    }
}

static void radix_sort(byte* base, byte* key, u32 count, umm size, umm remaining, u32 cursor[256])
{
    u32 end[256] = {};
    for (u32 i = 0; i < count; i++)
        end[key[i * size]]++;

    cursor[0] = 0;
    for (u32 i = 1; i < 256; i++)
    {
        end[i] += end[i - 1];
        cursor[i] = end[i - 1];
    }

    for (u32 i = 0; i < 256; i++)
    {
        u32 src = cursor[i];
        while (src < end[i])
        {
            u32 dest = cursor[key[src * size]]++;
            if (dest == src) src++;
            else swap(&base[src * size], &base[dest * size], size);
        }
    }

    if (!remaining--) return;
    u32 start = 0;
    for (u32 i = 0; i < 256; i++)
    {
        u32 count = end[i] - start;
        if (count > 1)
        {
            byte* next_base = base + start * size;
            byte* next_key = key + start * size - 1;
            radix_sort(next_base, next_key, count, size, remaining, cursor);
        }
        start += count;
    }
}

void radix_sort(void* address, umm count, umm size, umm key_offset, umm key_size)
{
    if (count <= 1 || !key_size) return;

    u32 cursor[256];
    byte* base = (byte*) address;
    byte* key = base + key_offset + key_size - 1;
    radix_sort(base, key, (u32)(count), size, key_size - 1, cursor);
}




////////////////////////////////////////////////////////////////////////////////
// File system
////////////////////////////////////////////////////////////////////////////////


static String subsystem_files = "files"_s;


bool create_directory_recursive(String path)
{
    while (path)
    {
        if (check_if_directory_exists(path))
            return true;
        if (!create_directory_recursive(get_parent_directory_path(path)))
            return false;
        if (!create_directory(path))
            return false;
    }
    return true;
}


bool read_file(String path, u64 offset, umm size, void* destination)
{
    assert(size != UMM_MAX && destination != NULL);
    String unused;
    return read_file(&unused,               // output string
                     path, offset,          // path and offset
                     size, size,            // min and max size
                     destination, NULL);    // buffer and allocator
}

bool read_file(String path, u64 offset, umm size, String* content, Region* memory)
{
    return read_file(content,               // output string
                     path, offset,          // path and offset
                     size, size,            // min and max size
                     NULL, memory);         // buffer and allocator
}

bool read_file(String path, u64 offset, umm destination_size, void* destination, umm* bytes_read)
{
    if (!destination_size) return true;
    assert(destination);
    String content = {};
    bool ok = read_file(&content,              // output string
                        path, offset,          // path and offset
                        0, destination_size,   // min and max size
                        destination, NULL);    // buffer and allocator
    *bytes_read = content.length;
    return ok;
}

bool read_entire_file(String path, String* content, Region* memory)
{
    return read_file(content,               // output string
                     path, 0,               // path and offset
                     0, UMM_MAX,            // min and max size
                     NULL, memory);         // buffer and allocator
}

String read_entire_file(String path, Region* memory)
{
    String content = {};
    if (read_entire_file(path, &content, memory))
        return content;
    return {};
}

bool write_to_file(String path, u64 offset, umm size, void* content, bool must_exist)
{
    String string = { size, (u8*) content };
    return write_to_file(path, offset, string, must_exist);
}

bool delete_directory_with_contents(String path)
{
    return delete_directory_conditional(path, true, [](String, String, bool, void*) { return DELETE_FILE_OR_DIRECTORY; }, NULL);
}



////////////////////////////////////////////////////////////////////////////////
// Transactional file utilities
////////////////////////////////////////////////////////////////////////////////


Safe_Filesystem the_sfs;

struct File
{
    Lock   lock;
    void*  handle;
    u64    size;
    String journal_path;
    String voucher_path;
};

struct Journal_Header
{
    u64    magic;
    SHA256 hash;
    u64    journal_size;
    u64    file_size;
};
CompileTimeAssert(sizeof(Journal_Header) == 56);

struct Journal_Content_Header
{
    u64 offset;
    u64 end_offset;
};
CompileTimeAssert(sizeof(Journal_Content_Header) == 16);

static constexpr u32 BLOCK_SIZE = 512;

File* open_exclusive(String path, bool share_read, bool report_open_failures)
{
    u64 size;
    bool success = false;
    void* handle = the_sfs.open(&success, &size, path, share_read, report_open_failures);
    if (!success) return NULL;

    File* file = alloc<File>(NULL);
    make_lock(&file->lock);
    file->handle       = handle;
    file->size         = size;
    file->journal_path = concatenate(NULL, path, "-journal"_s);
    file->voucher_path = concatenate(NULL, path, "-voucher"_s);

    // @Reconsider - handle case of failure to check
    if (check_if_file_exists(file->journal_path))
    {
        Journal_Header header;

        u64 journal_size = 0;
        void* journal = the_sfs.open(NULL, &journal_size, file->journal_path);
        Defer(the_sfs.close(journal));

        if (journal_size < sizeof(Journal_Header)) goto legacy_recover;
        the_sfs.read(journal, sizeof(Journal_Header), &header);
        if (header.magic != U64_MAX) goto legacy_recover;
        if (header.journal_size != journal_size) goto bad_journal;

        String original = allocate_uninitialized_string(NULL, journal_size - sizeof(Journal_Header));
        Defer(free_heap_string(&original));
        the_sfs.read(journal, original.length, original.data);

        // validate hash and structure
        SHA256_Context expected = {};
        sha256_init(&expected);
        sha256_data(&expected, &header.journal_size, sizeof(Journal_Header) - MemberOffset(Journal_Header, journal_size));
        sha256_data(&expected, original.data, original.length);
        sha256_done(&expected);
        if (memcmp(&header.hash, &expected.result, sizeof(SHA256)) != 0)
            goto bad_journal;

        for (String cursor = original; cursor;)
        {
            if (cursor.length < sizeof(Journal_Content_Header)) goto bad_journal;
            Journal_Content_Header* content_header = (Journal_Content_Header*) cursor.data;
            consume(&cursor, sizeof(Journal_Content_Header));

            u64 data_size = content_header->end_offset - content_header->offset;
            if (cursor.length < data_size) goto bad_journal;
            consume(&cursor, data_size);
        }

        // write again
        for (String cursor = original; cursor;)
        {
            assert(cursor.length >= sizeof(Journal_Content_Header));
            Journal_Content_Header* content_header = (Journal_Content_Header*) cursor.data;
            consume(&cursor, sizeof(Journal_Content_Header));

            u64 data_size = content_header->end_offset - content_header->offset;
            assert(cursor.length >= data_size);
            String content = take(&cursor, data_size);

            the_sfs.seek(file->handle, content_header->offset);
            the_sfs.write(file->handle, content.length, content.data);
        }

        the_sfs.trim(file->handle);
        the_sfs.flush(file->handle);
        file->size = header.file_size;
        the_sfs.erase(file->journal_path);
    }

    if (false)
    {
        // Legacy mode is never written again, but we need to be able to open
        // files that were written at the moment we switched to the new version.
        // With legacy files, there was an issue where a zero-sized journal would
        // get written. This code treats that case as if there was no journal,
        // and assumes the file is correct.

legacy_recover:
        // @Reconsider - handle case of failure to check
        if (check_if_file_exists(file->voucher_path))
        {
            u64 journal_size = 0;
            void* journal = the_sfs.open(NULL, &journal_size, file->journal_path);
            Defer(the_sfs.close(journal));


            if (journal_size > 0)
            {
                // read journal
                struct
                {
                    u64 file_size;
                    u64 offset;
                    u64 end_offset;
                } header;
                the_sfs.read(journal, sizeof(header), &header);

                String original = allocate_zero_string(NULL, header.end_offset - header.offset);
                Defer(free(original.data));

                the_sfs.read(journal, original.length, original.data);

                // write again
                the_sfs.seek(file->handle, header.offset);
                the_sfs.write(file->handle, original.length, original.data);
                the_sfs.seek(file->handle, header.file_size);
                the_sfs.trim(file->handle);
                the_sfs.flush(file->handle);
                file->size = header.file_size;
            }

            // delete voucher
            the_sfs.erase(file->voucher_path);
        }
        the_sfs.erase(file->journal_path);
    }
bad_journal:

    return file;
}

void close(File* file)
{
    if (!file) return;
    the_sfs.close(file->handle);
    free_lock(&file->lock);
    free(file->journal_path.data);
    free(file->voucher_path.data);
    free(file);
}

u64 size(File* file)
{
    LockedScope(&file->lock);
    return file->size;
}

void resize(File* file, u64 new_size)
{
    LockedScope(&file->lock);
    file->size = new_size;
    the_sfs.seek(file->handle, new_size);
    the_sfs.trim(file->handle);
}

void read(File* file, u64 offset, umm size, void* data)
{
    LockedScope(&file->lock);
    if (offset >= file->size) return;
    if (offset + size > file->size)
        size = file->size - offset;
    the_sfs.seek(file->handle, offset);
    the_sfs.read(file->handle, size, data);
}

void write(File* file, u64 offset, umm size, void const* data, bool truncate_after_written_data)
{
    LockedScope(&file->lock);

    // read original
    u64 copy_start = offset / BLOCK_SIZE * BLOCK_SIZE;
    u64 copy_end = (offset + size + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;
    if (copy_start > file->size) copy_start = file->size;
    if (copy_end   > file->size) copy_end   = file->size;
    if (truncate_after_written_data)
        copy_end = file->size;

    u64 copy_size = copy_end - copy_start;
    byte* copy = alloc<byte>(NULL, copy_size);
    the_sfs.seek(file->handle, copy_start);
    the_sfs.read(file->handle, copy_size, copy);

    // write journal
    struct
    {
        Journal_Header header;
        Journal_Content_Header content_header;
    } header_and_content_header;
    CompileTimeAssert(sizeof(header_and_content_header) == 72);

    header_and_content_header.header.magic        = U64_MAX;
    header_and_content_header.header.journal_size = sizeof(header_and_content_header) + copy_size;
    header_and_content_header.header.file_size    = file->size;

    header_and_content_header.content_header.offset     = copy_start;
    header_and_content_header.content_header.end_offset = copy_end;

    SHA256_Context hash = {};
    sha256_init(&hash);
    sha256_data(&hash, &header_and_content_header.header.journal_size, sizeof(Journal_Header) - MemberOffset(Journal_Header, journal_size));
    sha256_data(&hash, &header_and_content_header.content_header,      sizeof(Journal_Content_Header));
    sha256_data(&hash, copy, copy_size);
    sha256_done(&hash);
    header_and_content_header.header.hash = hash.result;

    void* journal = the_sfs.open(NULL, NULL, file->journal_path);
    the_sfs.write(journal, sizeof(header_and_content_header), &header_and_content_header);
    the_sfs.write(journal, copy_size, copy);
    the_sfs.flush(journal);
    the_sfs.close(journal);
    free(copy);

    // edit original
    the_sfs.seek(file->handle, offset);
    the_sfs.write(file->handle, size, (byte const*) data);

    if (truncate_after_written_data)
    {
        the_sfs.trim(file->handle);
        file->size = offset + size;
    }

    the_sfs.flush(file->handle);

    if (offset + size > file->size)
        file->size = offset + size;

    // delete journal
    the_sfs.erase(file->journal_path);
}

void write(File* file, Array<Data_To_Write> data)
{
    LockedScope(&file->lock);

    void* journal = the_sfs.open(NULL, NULL, file->journal_path);

    Journal_Header header;
    header.magic        = U64_MAX;
    header.journal_size = sizeof(Journal_Header);
    header.file_size    = file->size;
    For (data)
    {
        u64 copy_start = it->offset / BLOCK_SIZE * BLOCK_SIZE;
        u64 copy_end = (it->offset + it->size + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;
        if (copy_start > file->size) copy_start = file->size;
        if (copy_end   > file->size) copy_end   = file->size;
        header.journal_size += sizeof(Journal_Content_Header) + (copy_end - copy_start);
    }


    SHA256_Context hash = {};
    sha256_init(&hash);
    sha256_data(&hash, &header.journal_size, sizeof(Journal_Header) - MemberOffset(Journal_Header, journal_size));

    the_sfs.seek(journal, sizeof(Journal_Header));
    For (data)
    {
        // read original
        u64 copy_start = it->offset / BLOCK_SIZE * BLOCK_SIZE;
        u64 copy_end = (it->offset + it->size + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;
        if (copy_start > file->size) copy_start = file->size;
        if (copy_end   > file->size) copy_end   = file->size;

        u64 copy_size = copy_end - copy_start;
        byte* copy = alloc<byte>(NULL, copy_size);
        the_sfs.seek(file->handle, copy_start);
        the_sfs.read(file->handle, copy_size, copy);

        // write journal
        Journal_Content_Header content_header;
        content_header.offset     = copy_start;
        content_header.end_offset = copy_end;

        sha256_data(&hash, &content_header, sizeof(content_header));
        sha256_data(&hash, copy, copy_size);

        the_sfs.write(journal, sizeof(content_header), &content_header);
        the_sfs.write(journal, copy_size, copy);
        free(copy);
    }

    // write header
    sha256_done(&hash);
    header.hash = hash.result;
    the_sfs.seek(journal, 0);
    the_sfs.write(journal, sizeof(header), &header);
    the_sfs.flush(journal);
    the_sfs.close(journal);

    // edit original
    For (data)
    {
        the_sfs.seek(file->handle, it->offset);
        the_sfs.write(file->handle, it->size, (byte const*) it->data);

        if (it->offset + it->size > file->size)
            file->size = it->offset + it->size;
    }

    the_sfs.flush(file->handle);

    // delete journal
    the_sfs.erase(file->journal_path);
}

bool safe_read_file(String path, String* content, Region* memory)
{
    File* file = open_exclusive(path);
    if (!file)
        return false;

    *content = read(file, 0, size(file), memory);
    close(file);
    return true;
}

void safe_write_file(String path, String content)
{
    File* file;
    while (!(file = open_exclusive(path)))
        continue;

    write(file, 0, content.length, content.data, /* truncate_after_written_data */ true);
    close(file);
}



////////////////////////////////////////////////////////////////////////////////
// Log files
////////////////////////////////////////////////////////////////////////////////



struct Log_File
{
    Lock   lock;
    void*  handle;

    String    base_path;
    File_Time next_day;

    umm days_to_keep;

    QPC flush_qpc;
};

static void flush_under_lock(Log_File* file)
{
    if (!file->handle) return;
    the_sfs.flush(file->handle);
    file->flush_qpc = current_qpc();
}

static void check_time(Log_File* file)
{
    File_Time now = current_filetime();
    if (now < file->next_day)
        return;

    constexpr File_Time DAY = 864000000000ull;
    File_Time current_day = now / DAY * DAY;
    file->next_day = current_day + DAY;

    String path = Format(temp, "%-%.txt", file->base_path, format_timestamp(temp, "YYMMDD"_s, now));

    // delete older files
    String directory = get_parent_directory_path(path);
    For (list_files(directory, "txt"_s))
    {
        if (!prefix_equals(*it, get_file_name(file->base_path))) continue;
        String path = concatenate_path(temp, directory, *it);

        File_Time write_time;
        if (!get_file_time(path, NULL, &write_time)) continue;
        if (write_time > current_day - file->days_to_keep * DAY) continue;
        delete_file(path);
    }

    flush_under_lock(file);
    if (file->handle)
        the_sfs.close(file->handle);

    bool success;
    u64 size;
    file->handle = the_sfs.open(&success, &size, path);
    if (success)
        the_sfs.seek(file->handle, size);
}

Log_File* open_log_file(String path, umm days_to_keep)
{
    Log_File* file = alloc<Log_File>(NULL);
    make_lock(&file->lock);
    file->base_path = allocate_string_on_heap(path);
    file->days_to_keep = days_to_keep;
    file->flush_qpc = current_qpc();

    check_time(file);
    return file;
}

void close(Log_File* file)
{
    if (!file) return;
    flush_log(file);
    if (file->handle)
        the_sfs.close(file->handle);
    free(file->base_path.data);
    free_lock(&file->lock);
    free(file);
}

void append(Log_File* file, String data, bool flush)
{
    if (!file) return;
    if (!file->handle) return;

    LockedScope(&file->lock);

    check_time(file);
    if (!file->handle) return;

    the_sfs.write(file->handle, data.length, data.data);
    if (flush || seconds_from_qpc(current_qpc() - file->flush_qpc) > 5)
        flush_under_lock(file);
}

void flush_log(Log_File* file)
{
    if (!file) return;
    if (!file->handle) return;
    LockedScope(&file->lock);
    flush_under_lock(file);
}



////////////////////////////////////////////////////////////////////////////////
// Time
////////////////////////////////////////////////////////////////////////////////



double seconds_from_filetime(File_Time filetime)
{
    return (double)(s64) filetime / 10000000.0;
}

File_Time filetime_from_seconds(double seconds)
{
    return (File_Time)(seconds * 10000000.0 + 0.5);
}

File_Time filetime_from_integer_seconds(s64 seconds)
{
    return (File_Time)(seconds * 10000000);
}

File_Time filetime_from_unix(u64 unix_time)
{
    return unix_time * 10000000 + UNIX_EPOCH;
}

u64 unix_from_filetime(File_Time filetime)
{
    return (filetime - UNIX_EPOCH) / 10000000;
}


void utc_date_from_filetime(File_Time filetime, Date* out_date)
{
    constexpr u32 DAYS_PER_400Y = 365 * 400 + 97;
    constexpr u32 DAYS_PER_100Y = 365 * 100 + 24;
    constexpr u32 DAYS_PER_4Y   = 365 *   4 +  1;
    constexpr u32 DAYS_PER_1Y   = 365;
    constexpr u32 DAYS_IN_MONTH[12] = {31, 30, 31, 30, 31, 31, 30, 31, 30, 31, 31, 29};  // [0] is March

    out_date->nanosecond = (filetime % 10000000) * 100;  filetime /= 10000000;
    out_date->second     = filetime % 60;                filetime /= 60;
    out_date->minute     = filetime % 60;                filetime /= 60;
    out_date->hour       = filetime % 24;                filetime /= 24;

    u32 days = (u32) filetime + 306;  // full days since 1600-03-01 (mod 400 year, day after Feb 29)
    out_date->week_day = (days + 3) % 7;  // +3 because 1600-03-01 is a Wednesday

    u32 cycles400 = days / DAYS_PER_400Y;
    days -= cycles400 * DAYS_PER_400Y;

    u32 cycles100 = days / DAYS_PER_100Y;
    if (cycles100 == 4) cycles100--;
    days -= cycles100 * DAYS_PER_100Y;

    u32 cycles4 = days / DAYS_PER_4Y;
    if (cycles4 == 25) cycles4--;
    days -= cycles4 * DAYS_PER_4Y;

    u32 years = days / DAYS_PER_1Y;
    if (years == 4) years--;
    days -= years * DAYS_PER_1Y;

    u32 month = 0;
    while (DAYS_IN_MONTH[month] <= days)
        days -= DAYS_IN_MONTH[month++];
    month += 2;
    if (month >= 12)
    {
        month -= 12;
        years++;
    }

    out_date->year  = 1600 + years + 4 * cycles4 + 100 * cycles100 + 400 * cycles400;
    out_date->month = month + 1;
    out_date->day   = days + 1;
}

bool maybe_filetime_from_utc_date(Date* date, File_Time* out_filetime)
{
    constexpr int            DAYS_IN_MONTH[12] = {31, 29, 31, 30,  31,  30,  31,  31,  30,  31,  30,  31};
    constexpr int CUMULATIVE_DAYS_IN_MONTH[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

    *out_filetime = 0;
    if (date->year < 1601 || date->year > 60056) return false;
    if (date->month < 1 || date->month > 12) return false;
    if (date->day < 1 || date->day > DAYS_IN_MONTH[date->month - 1]) return false;

    File_Time subday = (u64)(date->nanosecond /      100ull)
                     + (u64)(date->second *     10000000ull)
                     + (u64)(date->minute *    600000000ull)
                     + (u64)(date->hour   *  36000000000ull);

    u32 month = date->month - 1;
    u32 year = date->year - 1601;
    u32 cycles4   = year / 4;
    u32 cycles100 = year / 100;
    u32 cycles400 = year / 400;
    u32 days = year * 365 + cycles4 - cycles100 + cycles400
             + CUMULATIVE_DAYS_IN_MONTH[month]
             + (date->day - 1)
             + (u32)((year + 1) % 4 == 0 && ((year + 1) % 100 != 0 || (year + 1) % 400 == 0) && month >= 2);
    if (days > 21350398 || (days == 21350398 && subday > 201709551615))
        return false;

    *out_filetime = subday + (u64)(days * 864000000000ull);
    return true;
}

File_Time filetime_from_utc_date(Date* date)
{
    File_Time result = 0;
    bool ok = maybe_filetime_from_utc_date(date, &result);
    assert(ok);
    return result;
}

File_Time filetime_from_utc_date_parts(u32 year, u32 month, u32 day, u32 hour, u32 minute, u32 second, u32 nanosecond)
{
    Date date;
    date.year       = year;
    date.month      = month;
    date.day        = day;
    date.hour       = hour;
    date.minute     = minute;
    date.second     = second;
    date.nanosecond = nanosecond;
    return filetime_from_utc_date(&date);
}

File_Time filetime_from_ISO_8601_date_string(String date)
{
    Date d = {};
    d.year   = u32_from_string(consume_until(&date, '-'));
    d.month  = u32_from_string(consume_until(&date, '-'));
    d.day    = u32_from_string(consume_until(&date, 'T'));
    d.hour   = u32_from_string(consume_until(&date, ':'));
    d.minute = consume_u32(&date);
    if (!d.month) d.month = 1;
    if (!d.day)   d.day   = 1;

    s32 offset_sign = (find_first_occurance(date, '-') == NOT_FOUND) ? -1 : 1;

    if (date && date[0] == ':')
    {
        take(&date, 1);

        d.second = consume_u32(&date);
        if (date && date[0] == '.')
        {
            take(&date, 1);

            umm before = date.length;
            u32 milliseconds = consume_u32(&date);
            umm decimal_digits = before - date.length;
            for (umm i = decimal_digits; i < 3; i++)
                milliseconds *= 10;

            d.nanosecond = milliseconds * 1'000'000;
        }
    }

    if (consume_until_any(&date, "+-"_s))
        return 0;

    s64 offset_hour   = u32_from_string(consume_until(&date, ':'));
    s64 offset_minute = consume_u32(&date);

    if (date) return 0;

    File_Time filetime;
    if (!maybe_filetime_from_utc_date(&d, &filetime))
        return 0;

    filetime += (File_Time)(offset_sign * offset_hour   * 3600 * FILETIME_FREQUENCY);
    filetime += (File_Time)(offset_sign * offset_minute *   60 * FILETIME_FREQUENCY);

    return filetime;
}

String ISO_8601_date_string_from_filetime(File_Time filetime, Region* memory)
{
    Date date = {};
    utc_date_from_filetime(filetime, &date);
    u64 milliseconds = ((u64)date.nanosecond + 500'000) / 1'000'000;
    return Format(memory, "%-%-%T%:%:%.%+00:00",
        u64_format(date.year, 4), u64_format(date.month,  2), u64_format(date.day,    2),
        u64_format(date.hour, 2), u64_format(date.minute, 2), u64_format(date.second, 2),
        u64_format(milliseconds, 3));
}

String human_readable_timestamp(Region* memory, File_Time filetime)
{
    Date date = {};
    utc_date_from_filetime(filetime, &date);
    return Format(memory, "%-%-% %:%:%",
                          u64_format(date.year, 4), u64_format(date.month,  2), u64_format(date.day,    2),
                          u64_format(date.hour, 2), u64_format(date.minute, 2), u64_format(date.second, 2));
}



String the_english_abbreviated_month_names[12] = {
    "Jan"_s, "Feb"_s, "Mar"_s, "Apr"_s, "May"_s, "Jun"_s, "Jul"_s, "Aug"_s, "Sep"_s, "Oct"_s, "Nov"_s, "Dec"_s
};
String the_english_full_month_names[12] = {
    "Jaunary"_s, "February"_s, "March"_s,     "April"_s,   "May"_s,      "June"_s,
    "July"_s,    "August"_s,   "September"_s, "October"_s, "November"_s, "December"_s
};

String the_english_abbreviated_weekday_names[7] = {
    "Sun"_s, "Mon"_s, "Tue"_s, "Wed"_s, "Thu"_s, "Fri"_s, "Sat"_s
};
String the_english_full_weekday_names[7] {
    "Sunday"_s, "Monday"_s, "Tuesday"_s, "Wednesday"_s, "Thursday"_s, "Friday"_s, "Saturday"_s
};

String format_date(Region* memory, String format, Date* date, String months[12], String weekdays[7])
{
    bool twelve_hour = false;
    for (umm i = 0; i < format.length; i++)
    {
        u8 c = format.data[i];
        if (c != 'a' && c != 'A') continue;
        twelve_hour = true;
        break;
    }

    String_Concatenator cat = {};
    for (umm i = 0; i < format.length; i++)
    {
        u8 c = format.data[i];
        switch (c)
        {

        u32 number;
        case 'Y': number = date->year;       goto format_number;
        case 'M': number = date->month;      goto format_number;
        case 'D': number = date->day;        goto format_number;
        case 'h':
            number = date->hour;
            if (twelve_hour && number >= 12) number -= 12;
            goto format_number;
        case 'm': number = date->minute;     goto format_number;
        case 's': number = date->second;     goto format_number;
        case 'f': number = date->nanosecond; goto format_number;

        format_number:
        {
            umm run_length = 1;
            while (i + 1 < format.length && format.data[i + 1] == c)
            {
                run_length++;
                i++;
            }

            if (c == 'M' && run_length == 3)
            {
                u32 month = date->month - 1;
                add(&cat, month < 12 ? months[month] : "<invalid month>"_s);
            }
            else if (c == 'f')
            {
                U64_Format u64f = {};
                switch (run_length)
                {
                case 1:  u64f = u64_format((number + 50000000) / 100000000, 1); break;
                case 2:  u64f = u64_format((number +  5000000) /  10000000, 2); break;
                case 3:  u64f = u64_format((number +   500000) /   1000000, 3); break;
                case 4:  u64f = u64_format((number +    50000) /    100000, 4); break;
                case 5:  u64f = u64_format((number +     5000) /     10000, 5); break;
                case 6:  u64f = u64_format((number +      500) /      1000, 6); break;
                case 7:  u64f = u64_format((number +       50) /       100, 7); break;
                case 8:  u64f = u64_format((number +        5) /        10, 8); break;
                default: u64f = u64_format( number,                         9); break;
                }
                FormatAdd(&cat, "%", u64f);
                while (run_length-- > 9)
                    add(&cat, "0"_s);
            }
            else if (run_length == 1)
            {
                add_base10_u64(&cat, number);
            }
            else
            {
                u64 mod = 1;
                for (umm i = 0; i < run_length; i++)
                    mod *= 10;
                FormatAdd(&cat, "%", u64_format(number % mod, run_length));
            }
        } break;

        case 'w': add(&cat, date->week_day < 7 ? weekdays[date->week_day] : "<invalid weekday>"_s); break;
        case 'a': add(&cat, (date->hour < 12) ? "am"_s : "pm"_s); break;
        case 'A': add(&cat, (date->hour < 12) ? "AM"_s : "PM"_s); break;
        default:  add(&cat, &c, 1);                               break;

        }
    }
    return resolve_to_string_and_free(&cat, memory);
}

String format_timestamp(Region* memory, String format, File_Time filetime, String months[12], String weekdays[7])
{
    Date date;
    utc_date_from_filetime(filetime, &date);
    return format_date(memory, format, &date, months, weekdays);
}


bool consume_date(Date* out_date, String format, String* string, String months[12])
{
    out_date->year       = 1601;
    out_date->month      = 1;
    out_date->day        = 1;
    out_date->hour       = 0;
    out_date->minute     = 0;
    out_date->second     = 0;
    out_date->nanosecond = 0;
    out_date->week_day   = 0;

    bool is_pm = false;

    for (umm i = 0; i < format.length; i++) Loop(format_loop)
    {
        u8 c = format.data[i];
        switch (c)
        {

        u32* number_result;
        case 'Y': number_result = &out_date->year;       goto parse_number;
        case 'M': number_result = &out_date->month;      goto parse_number;
        case 'D': number_result = &out_date->day;        goto parse_number;
        case 'h': number_result = &out_date->hour;       goto parse_number;
        case 'm': number_result = &out_date->minute;     goto parse_number;
        case 's': number_result = &out_date->second;     goto parse_number;
        case 'f': number_result = &out_date->nanosecond; goto parse_number;
        case '*': number_result = NULL;                  goto parse_number;

        parse_number:
        {
            umm run_length = 1;
            while (i + 1 < format.length && format.data[i + 1] == c)
            {
                run_length++;
                i++;
            }

            if (c == 'M' && run_length == 3)
            {
                while (true)
                {
                    if (!string->length) return false;
                    for (umm month = 0; month < 12; month++)
                    {
                        if (!prefix_equals_case_insensitive(*string, months[month])) continue;
                        *number_result = month + 1;
                        ContinueLoop(format_loop);
                    }
                    string->length--;
                    string->data++;
                }
            }
            else
            {
                // skip non-digits
                while (string->length && (*string->data < '0' || *string->data > '9'))
                {
                    string->length--;
                    string->data++;
                }

                // must get at least one digit
                if (!string->length) return false;

                u32 number = 0;
                u32 digits_parsed = 0;
                if (run_length == 1)
                {
                    // parse however many digits there are
                    while (string->length && (*string->data >= '0' && *string->data <= '9'))
                    {
                        u32 new_number = number * 10 + (*string->data - '0');
                        if (new_number < number) return false;  // overflow
                        number = new_number;
                        digits_parsed++;

                        string->length--;
                        string->data++;
                    }
                }
                else
                {
                    // parse exactly run_length digits
                    for (u32 i = 0; i < run_length; i++)
                    {
                        if (!string->length || (*string->data < '0' || *string->data > '9')) return false;

                        u32 new_number = number * 10 + (*string->data - '0');
                        if (new_number < number) return false;  // overflow
                        number = new_number;

                        string->length--;
                        string->data++;
                    }
                    digits_parsed = run_length;
                }

                if (number_result)
                {
                    if (c == 'f')
                    {
                        while (digits_parsed++ < 9)
                            number *= 10;
                    }
                    *number_result = number;
                }
            }
        } break;

        case 'a': case 'A':
        {
            while (true)
            {
                if (!string->length) return false;
                if (prefix_equals_case_insensitive(*string, "am"_s)) { is_pm = false; break; }
                if (prefix_equals_case_insensitive(*string, "pm"_s)) { is_pm =  true; break; }
                string->length--;
                string->data++;
            }
        } break;

        case '>':
        {
            if (i + 1 >= format.length) return false;
            u8 until_what = format[i + 1];
            i++;
            consume_until(string, until_what);
        } break;

        default:
        {
            if (!string->length || *string->data != c) return false;
            string->length--;
            string->data++;
        } break;

        }
    }

    if (is_pm)
        out_date->hour += 12;

    return true;
}

bool consume_filetime(File_Time* out_filetime, String format, String* string, String months[12])
{
    Date date = {};
    return consume_date(&date, format, string, months)
        && maybe_filetime_from_utc_date(&date, out_filetime);
}


bool parse_date(Date* out_date, String format, String string, String months[12])
{
    return consume_date(out_date, format, &string, months);
}

bool parse_filetime(File_Time* out_filetime, String format, String string, String months[12])
{
    return consume_filetime(out_filetime, format, &string, months);
}



////////////////////////////////////////////////////////////////////////////////
// Profiling helper
////////////////////////////////////////////////////////////////////////////////


extern "C" double pow(double x, double y);
extern "C" double sqrt(double x);

String profile_statistics(Profile_Statistics* prof, String title, double scale, umm precision, umm top_discard)
{
    Defer(free_heap_array(&prof->durations));
    Array<QPC> durations = prof->durations;

    radix_sort(durations.address, durations.count, sizeof(QPC), 0, sizeof(QPC));
    if (durations.count < top_discard)
        durations.count = 0;
    else
        durations.count -= top_discard;

    double sum = 0;
    For (durations) sum += seconds_from_qpc(*it) * scale;
    double mean = sum / (double) durations.count;
    double stdev = 0;
    For (durations) stdev += pow(seconds_from_qpc(*it) * scale - mean, 2);
    stdev = sqrt(stdev / (double) durations.count);


    String_Concatenator cat = {};
    FormatAdd(&cat, "\n%\n", title);
    FormatAdd(&cat, "    N ");
    for (int i = -2; i <= 10; i++)
    {
             if (i == -2) FormatAdd(&cat, "     Avg ");
        else if (i == -1) FormatAdd(&cat, "   StDev ");
        else if (i ==  0) FormatAdd(&cat, "     Min ");
        else if (i == 10) FormatAdd(&cat, "     Max ");
        else              FormatAdd(&cat, "    %th ", i * 10);
    }
    FormatAdd(&cat, "\n% ", s64_format(durations.count, 5));
    for (int i = -2; i <= 10; i++)
    {
        double value;
             if (i == -2) value = mean;
        else if (i == -1) value = stdev;
        else value = seconds_from_qpc(durations[(durations.count - 1) * i / 10]) * scale;
        FormatAdd(&cat, "% ", string_format(Format(temp, "%", f64_format(value, precision)), 8));
    }
    FormatAdd(&cat, "\n");

    String result = resolve_to_string_and_free(&cat, temp);
    printf("%.*s\n", StringArgs(result));
    Debug("%", result);
    return result;
}



////////////////////////////////////////////////////////////////////////////////
// Process utilities
////////////////////////////////////////////////////////////////////////////////


String get_executable_name()
{
    return get_file_name(get_executable_path());
}

String get_executable_directory()
{
    return get_parent_directory_path(get_executable_path());
}


bool get_command_line_bool(String name)
{
    String prefix = concatenate("-"_s, name);
    For (command_line_arguments())
        if (*it == prefix)
            return true;
    return false;
}

s64 get_command_line_integer(String name)
{
    String prefix = concatenate("-"_s, name, ":"_s);
    For (command_line_arguments())
    {
        String arg = *it;
        if (!prefix_equals(arg, prefix)) continue;
        consume(&arg, prefix.length);
        return s64_from_string(arg);
    }
    return 0;
}

String get_command_line_string(String name)
{
    String prefix = concatenate("-"_s, name, ":"_s);
    For (command_line_arguments())
    {
        String arg = *it;
        if (!prefix_equals(arg, prefix)) continue;
        consume(&arg, prefix.length);
        return arg;
    }
    return {};
}



////////////////////////////////////////////////////////////////////////////////
// Random
////////////////////////////////////////////////////////////////////////////////



inline static void step(Random* random)
{
    random->state = random->state * 6364136223846793005ull + random->increment;
}

void seed(Random* random, u64 seed1, u64 seed2)
{
    random->state = 0;
    random->increment = (seed2 << 1) | 1;
    step(random);
    random->state += seed1;
    step(random);
}

void seed(Random* random)
{
    u64 seeds[2];
    entropy(seeds, sizeof(seeds));
    seed(random, seeds[0], seeds[1]);
}

u32 next_u32(Random* random)
{
    u64 state = random->state;
    step(random);
    return rotr32((u32)(((state >> 18) ^ state) >> 27), (u32)(state >> 59));
}

#define RandomRangeAlgorithm(N)          \
    u##N bound = high - low;             \
    u##N threshold = -bound % bound;     \
    while (true)                         \
    {                                    \
        u##N value = next_u##N(random);  \
        if (value < threshold) continue; \
        return low + (value % bound);    \
    }

u64 next_u64(Random* random) { return ((u64) next_u32(random) << 32) | (u64) next_u32(random); }

u32 next_u32(Random* random, u32 low, u32 high) { RandomRangeAlgorithm(32) }
u64 next_u64(Random* random, u64 low, u64 high) { RandomRangeAlgorithm(64) }

s32 next_s32(Random* random)                    { return (s32) next_u32(random);                        }
s64 next_s64(Random* random)                    { return (s64) next_u64(random);                        }
s32 next_s32(Random* random, s32 low, s32 high) { return (s32) next_u32(random, (u32) low, (u32) high); }
s64 next_s64(Random* random, s64 low, s64 high) { return (s64) next_u64(random, (u64) low, (u64) high); }

float next_float    (Random* random) { return (float) next_u32(random) / 4294967296.0f; }
float next_float_pm1(Random* random) { return (float) next_s32(random) / 2147483648.0f; }
float next_float    (Random* random, float low, float high) { return lerp(low, high, next_float(random)); }
bool  next_chance   (Random* random, float chance) { return next_float(random) < chance; }

double next_double    (Random* random) { return (double) next_u64(random) / 18446744073709551616.0; }
double next_double_pm1(Random* random) { return (double) next_s64(random) /  9223372036854775808.0; }
double next_double    (Random* random, double low, double high) { return lerp(low, high, next_double(random)); }



GUID generate_guid()
{
    GUID id;
    entropy(&id, sizeof(id));
    id.d3    = 0x4000 | (id.d3    & 0x0FFF);  // Version 4 (entropy GUID)
    id.d4[0] =   0x80 | (id.d4[0] &   0x3F);  // Variant 1
    return id;
}

static bool is_hexadecimal_digit(u8 c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

static u32 consume_hexadecimal(String* str, u32 max_digits)
{
    u32 number = 0;
    while (str->length && max_digits--)
    {
        u8 c = *str->data;
             if (c >= '0' && c <= '9') number = (number << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f') number = (number << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') number = (number << 4) | (c - 'A' + 10);
        else return number;
        consume(str, 1);
    }
    return number;
}

GUID parse_guid(String string)
{
    GUID id;
    id.d1    = consume_hexadecimal(&string, 8); consume_until(&string, '-');
    id.d2    = consume_hexadecimal(&string, 4); consume_until(&string, '-');
    id.d3    = consume_hexadecimal(&string, 4); consume_until(&string, '-');
    id.d4[0] = consume_hexadecimal(&string, 2);
    id.d4[1] = consume_hexadecimal(&string, 2); consume_until(&string, '-');
    for (int i = 2; i < 8; i++)
        id.d4[i] = consume_hexadecimal(&string, 2);
    return id;
}

bool parse_guid_checked(String string, GUID* out_guid)
{
    for (umm i = 0; i < string.length; i++)
    {
        u8   byte      = string[i];
        bool separator = (i == 8 || i == 13 || i == 18 || i == 23);
        if (separator ? (byte != '-') : !is_hexadecimal_digit(byte))
            return false;
    }

    *out_guid = parse_guid(string);
    return true;
}

String print_guid(GUID id)
{
    return Format(temp, "%-%-%-%~%-%~%~%~%~%~%",
        hex_format(id.d1, 8), hex_format(id.d2, 4), hex_format(id.d3, 4),
        hex_format(id.d4[0], 2), hex_format(id.d4[1], 2), hex_format(id.d4[2], 2), hex_format(id.d4[3], 2),
        hex_format(id.d4[4], 2), hex_format(id.d4[5], 2), hex_format(id.d4[6], 2), hex_format(id.d4[7], 2));
}


ExitApplicationNamespace


#if defined(OS_WINDOWS)
#include "common_windows.inl"
#elif defined(OS_LINUX)
#include "common_linux.inl"
#else
#error "Unsupported"
#endif
