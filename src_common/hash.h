EnterApplicationNamespace


inline u32 hash_u32(u32 key)
{
    key = ((key >> 16) ^ key) * 0x45d9f3b;
    key = ((key >> 16) ^ key) * 0x45d9f3b;
    key =  (key >> 16) ^ key;
    return key;
}

inline u64 hash_u64(u64 key)
{
    key = ~key + (key << 21);
    key =  key ^ (key >> 24);
    key = (key + (key << 3)) + (key << 8);
    key =  key ^ (key >> 14);
    key = (key + (key << 2)) + (key << 4);
    key =  key ^ (key >> 28);
    key =  key + (key << 31);
    return key;
}

inline u64 hash_u16(u16 key)   // shitty hash
{
    return hash_u64((u64) key);
}

inline u64 hash_s64(s64 key)
{
    return hash_u64((u64) key);
}

inline u64 hash_s32(s32 key)
{
    return hash_u64((u64) key); // shitty hash
}

inline u64 hash64(const void* data, u64 length)
{
    u64 hash = 1315423911;
    byte* bytes = (byte*) data;
    for (umm i = 0; i < length; i++)
      hash ^= (hash << 5) + bytes[i] + (hash >> 2);
    return hash;
}

#if ARCHITECTURE_X86
CompileTimeAssert(sizeof(u32) == sizeof(void*));
inline umm hash_pointer(const void* p) { return hash_u32((u32) p); }
#else
CompileTimeAssert(sizeof(u64) == sizeof(void*));
inline umm hash_pointer(const void* p) { return hash_u64((u64) p); }
#endif

inline u64 hash_string(const void* data, umm length)
{
    return hash64((void*) data, length);
}

inline u64 hash_string(String string)
{
    return hash64(string.data, string.length);
}

template <typename T>
inline u64 hash_structure(const T& t)
{
    return hash64(&t, sizeof(T));
}


////////////////////////////////////////////////////////////////////////////////
// Hash table
////////////////////////////////////////////////////////////////////////////////


//
//  Table(Key, Value, hash_function) table = {};
//
//    hash_function is <integer>(*)(Key)
//
//  void free_table(Table* table);
//  void set(Table* table, Key* key, Value* value);
//  bool set(Table* table, Key* key, Value* value, Value* out_old_value);
//  bool try_set(Table* table, Key* key, Value* value);
//  bool try_set(Table* table, Key* key, Value* value, Value* out_current_value);
//  bool get(Table* table, Key* key, Value* out_value);
//  Value get(Table* table, Key* key);  // returns the value, or {} if not found
//  bool remove(Table* table, Key* key);
//  Value get_and_remove(Table* table, const K* key)
//


template <typename K, typename V, typename Hash, typename Compare>
struct Table_
{
    struct Item
    {
        Item* next;
        bool occupied;
        K key;
        V value;
    };

    Item*  base;
    Item*  free;
    Item** buckets;
    umm    count;
    umm    capacity;  // must be pow2
};

#define Table Table_<K, V, Hash, Compare>

template <typename K, typename V, typename Hash, typename Compare>
void ensure_capacity(Table* table);

template <typename K, typename V, typename Hash, typename Compare>
void free_table(Table* table)
{
    free(table->base);
    free(table->buckets);
    ZeroStruct(table);
}

template <typename K, typename V, typename Hash, typename Compare>
V* get_address(Table* table, const K* key)
{
    typedef typename Table::Item Item;

    if (!table->count)
        return false;

    umm bucket_index = (umm)(Hash::hash(key) & (table->capacity - 1));
    Item** it = &table->buckets[bucket_index];
    while (*it)
    {
        Item* item = *it;
        if (Compare::compare(&item->key, key))
        {
            return &item->value;
        }

        it = &item->next;
    }

    return NULL;
}

template <typename K, typename V, typename Hash, typename Compare>
bool get(Table* table, const K* key, V* value)
{
    V* address = get_address(table, key);
    if (address && value)
        *value = *address;
    return address;
}

template <typename K, typename V, typename Hash, typename Compare>
bool exists(Table* table, const K* key)
{
    return get_address(table, key);
}

template <typename K, typename V, typename Hash, typename Compare>
void set(Table* table, const K* key, const V* value)
{
    typedef typename Table::Item Item;

    ensure_capacity(table);

    umm bucket_index = (umm)(Hash::hash(key) & (table->capacity - 1));
    Item** it = &table->buckets[bucket_index];
    while (*it)
    {
        Item* item = *it;
        if (Compare::compare(&item->key, key)) break;
        it = &item->next;
    }

    if (!*it)
    {
        Item* item = table->free;
        table->free = item->next;
        table->count++;

        item->next = NULL;
        item->occupied = true;
        item->key = *key;
        *it = item;
    }

    Item* item = *it;
    item->value = *value;
}

template <typename K, typename V, typename Hash, typename Compare>
bool remove(Table* table, const K* key)
{
    typedef typename Table::Item Item;

    if (!table->count)
        return false;

    umm bucket_index = (umm)(Hash::hash(key) & (table->capacity - 1));
    Item** it = &table->buckets[bucket_index];
    while (*it)
    {
        Item* item = *it;
        if (Compare::compare(&item->key, key))
        {
            *it = item->next;
            item->occupied = false;
            item->next = table->free;
            table->free = item;
            table->count--;
            return true;
        }

        it = &item->next;
    }

    return false;
}

template <typename K, typename V, typename Hash, typename Compare>
void ensure_capacity(Table* table)
{
    typedef typename Table::Item Item;

    if (table->count < table->capacity) return;
    assert(table->count == table->capacity);

    umm new_capacity = table->capacity * 2;
    if (!new_capacity) new_capacity = 64;

    Item* storage = alloc<Item>(NULL, new_capacity);
    for (umm i = 1; i < new_capacity; i++)
        storage[i - 1].next = &storage[i];

    Table new_table = {};
    new_table.base     = storage;
    new_table.free     = storage;
    new_table.buckets  = alloc<Item*>(NULL, new_capacity);
    new_table.count    = 0;
    new_table.capacity = new_capacity;

    for (umm i = 0; i < table->capacity; i++)
    {
        K* key   = &table->base[i].key;
        V* value = &table->base[i].value;
        set(&new_table, key, value);
    }

    free_table(table);
    *table = new_table;
}

template <typename K, typename V, typename Hash, typename Compare>
V get(Table* table, const K* key)
{
    V result = {};
    get(table, key, &result);
    return result;
}

template <typename K, typename V, typename Hash, typename Compare>
bool set(Table* table, const K* key, const V* value, V* out_current_value)
{
    bool existed = get(table, key, out_current_value);
    set(table, key, value);
    return existed;
}

template <typename K, typename V, typename Hash, typename Compare>
bool try_set(Table* table, const K* key, const V* value)
{
    if (get(table, key, (V*) NULL))
        return false;
    set(table, key, value);
    return true;
}

template <typename K, typename V, typename Hash, typename Compare>
bool try_set(Table* table, const K* key, const V* value, V* out_current_value)
{
    if (get(table, key, out_current_value))
        return false;
    set(table, key, value);
    return true;
}

template <typename K, typename V, typename Hash, typename Compare>
V get_and_remove(Table* table, const K* key)
{
    V result = {};
    get(table, key, &result);
    remove(table, key);
    return result;
}


template <typename K, typename V, typename Hash, typename Compare>
struct Table_Iterator
{
    typedef typename Table::Item Item;
    Item* array;
    umm capacity;
    umm i;

    inline bool operator!=(Table_Iterator<K, V, Hash, Compare> other)
    {
        return i != other.i || array != other.array;
    }

    inline void operator++()
    {
        while (++i < capacity)
            if (array[i].occupied)
                break;
    }

    inline Item* operator*()
    {
        return &array[i];
    }
};

template <typename K, typename V, typename Hash, typename Compare>
inline Table_Iterator<K, V, Hash, Compare> begin(const Table& t)
{
    Table_Iterator<K, V, Hash, Compare> it = { t.base, t.capacity, (umm)-1 };
    ++it;
    return it;
}

template <typename K, typename V, typename Hash, typename Compare>
inline Table_Iterator<K, V, Hash, Compare> end(const Table& t) { return { t.base, t.capacity, t.capacity }; }



#undef Table


#define DeclareTable(Name, Key, Value, Hash_Function)                                             \
    struct Name: Table_<Key, Value, Name, Name>                                                   \
    {                                                                                             \
        static inline umm hash(const Key* key) { return (umm) Hash_Function(*(Key*) key);       } \
        static inline bool compare(const Key* a, const Key* b) { return *(Key*) a == *(Key*) b; } \
    }

#define Table(Key, Value, Hash_Function) DeclareTable(UniqueIdentifier(Table), Key, Value, Hash_Function)




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Boolean array.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <umm N>
struct Bool_Array
{
    u32 buckets[(N + 31) / 32];

    inline u32 operator[](umm index)
    {
        assert(index < N);
        return buckets[index >> 5] & (1u << (index & 31));
    }

    inline void clear(umm index)
    {
        assert(index < N);
        buckets[index >> 5] &= ~(1u << (index & 31));
    }

    inline void set(umm index)
    {
        assert(index < N);
        buckets[index >> 5] |= (1u << (index & 31));
    }
};


////////////////////////////////////////////////////////////////////////////////
// CacheTable
////////////////////////////////////////////////////////////////////////////////

//
//  CacheTable(Buckets, Ways, Key, Value, hash_function) table = {};
//
//    hash_function is <integer>(*)(Key)
//
//  bool get_cached(Cache_Table* table, Key* key, Value* out_value);
//  void cache_value(Cache_Table* table, Key* key, Value* value);
//


#define DeclareCacheTable(Name, Buckets, Ways, Key, Value, Hash_Function)           \
    struct Name: Cache_Table<Buckets, Ways, Key, Value, Name, Name>                 \
    {                                                                               \
        static inline umm hash(Key* key) { return (umm) Hash_Function(*key); } \
        static inline bool compare(Key* a, Key* b) { return *a == *b; }        \
    }

#define CacheTable(Buckets, Ways, Key, Value, Hash_Function) \
    DeclareCacheTable(UniqueIdentifier(Cache_Table), Buckets, Ways, Key, Value, Hash_Function)

template <umm Buckets, umm Ways, typename Key, typename Value, typename Hash, typename Compare>
struct Cache_Table
{
    struct Entry
    {
        Key   key;
        Value value;
    };

    Bool_Array<Buckets * Ways> filled;
    Entry entry[Buckets * Ways];
    umm insertion_index;
};

#define Cache_Table Cache_Table<Buckets, Ways, Key, Value, Hash, Compare>
#define Entry typename Cache_Table::Entry


template <umm Buckets, umm Ways, typename Key, typename Value, typename Hash, typename Compare>
bool get_cached(Cache_Table* table, Key* key, Value* out_value)
{
    umm i = (Hash::hash(key) % Buckets) * Ways;
    for (umm way = 0; way < Ways; way++, i++)
    {
        if (!table->filled[i]) continue;
        if (!Compare::compare(key, &table->entry[i].key)) continue;
        *out_value = table->entry[i].value;
        return true;
    }
    return false;
}

template <umm Buckets, umm Ways, typename Key, typename Value, typename Hash, typename Compare>
bool get_and_remove_cached(Cache_Table* table, Key* key, Value* out_value)
{
    umm i = (Hash::hash(key) % Buckets) * Ways;
    for (umm way = 0; way < Ways; way++, i++)
    {
        if (!table->filled[i]) continue;
        if (!Compare::compare(key, &table->entry[i].key)) continue;
        *out_value = table->entry[i].value;
        table->filled.clear(i);
        ZeroStruct(&table->entry[i]);
        return true;
    }
    return false;
}

template <umm Buckets, umm Ways, typename Key, typename Value, typename Hash, typename Compare>
void cache_value(Cache_Table* table, Key* key, Value* value, Value* out_previous = NULL)
{
    umm bucket = (Hash::hash(key) % Buckets) * Ways;
    umm free_index = UMM_MAX;
    umm i = bucket;
    for (umm way = 0; way < Ways; way++, i++)
    {
        if (!table->filled[i]) { free_index = i; continue; }
        if (!Compare::compare(key, &table->entry[i].key)) continue;

        if (out_previous) *out_previous = table->entry[i].value;
        table->entry[i].value = *value;
        return;
    }

    if (free_index == UMM_MAX)
        free_index = bucket + (table->insertion_index++ % Ways);

    if (out_previous) *out_previous = table->entry[free_index].value;

    table->filled.set(free_index);
    table->entry[free_index].key   = *key;
    table->entry[free_index].value = *value;
}


#undef Cache_Table
#undef Entry


ExitApplicationNamespace
