#ifndef ENKLUME_NBT_H
#define ENKLUME_NBT_H

#include "support.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef union NBT_Body_ NBT_Body;
typedef struct NBT_Object_ NBT_Object;

typedef struct { int32_t count; int8_t* arr; } NBT_ByteArray;
typedef struct { int32_t count; int32_t* arr; } NBT_IntArray;
typedef struct { int32_t count; int64_t* arr; } NBT_LongArray;

typedef int8_t NBT_Byte;
typedef int16_t NBT_Short;
typedef int32_t NBT_Int;
typedef int64_t NBT_Long;
typedef float NBT_Float;
typedef double NBT_Double;

typedef const char* NBT_String;

#define NBT_TAG_TYPES(T) \
T(Byte,      byte)      \
T(Short,     short)     \
T(Int,       int)       \
T(Long,      long)      \
T(Float,     float)     \
T(Double,    double)    \
T(ByteArray, byte_array) \
T(String,    string)    \
T(List,      list)      \
T(Compound,  compound)  \
T(IntArray,  int_array)  \
T(LongArray, long_array) \

typedef enum {
    NBT_Tag_End,
#define T(name, snake_name) NBT_Tag_##name,
    NBT_TAG_TYPES(T)
#undef T
} NBT_Tag;

typedef struct {
    NBT_Tag tag;
    int32_t count;
    NBT_Body* bodies;
} NBT_List;

typedef struct {
    int32_t count;
    NBT_Object** objects;
} NBT_Compound;

union NBT_Body_ {
#define T(name, snake_name) NBT_##name p_##snake_name;
NBT_TAG_TYPES(T)
#undef T
};

struct NBT_Object_ {
    NBT_Tag tag;
    const char* name;
    NBT_Body body;
};

NBT_Object* cunk_decode_nbt(size_t buffer_size, const char* buffer, Enkl_Allocator*);
void enkl_free_nbt(NBT_Object*, Enkl_Allocator* allocator);

void enkl_print_nbt(Enkl_Printer*, const NBT_Object*);

const NBT_Object* cunk_nbt_compound_direct_access(const NBT_Compound* c, const char* name);
const NBT_Object* cunk_nbt_compound_access(const NBT_Object*, const char*);

#define X(N, s) const NBT_##N* cunk_nbt_extract_##s(const NBT_Object*);
NBT_TAG_TYPES(X)
#undef X

#endif
