#ifndef ENKL_SUPPORT_PRIVATE_H
#define ENKL_SUPPORT_PRIVATE_H

#include "enklume/support.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

unsigned int enkl_needed_bits(unsigned int n);
uint64_t enkl_fetch_bits(const void* buf, size_t bit_pos, unsigned int width);
uint64_t enkl_fetch_bits_long_arr(const void* buf, bool big_endian, size_t bit_pos, unsigned int width);
int64_t enkl_swap_endianness(int bytes, int64_t i);
bool enkl_folder_exists(const char* filename);
bool enkl_file_exists(const char* filename);
bool enkl_string_ends_with(const char* string, const char* suffix);
char* enkl_format_string(const char* str, ...);
char* enkl_format_string_valist(const char* str, va_list args);
const char* enkl_replace_string(const char* source, const char* match, const char* replace_with);
char* enkl_copy_string(const char*, Enkl_Allocator*);
bool enkl_read_file(const char* filename, size_t* out_size, char** out_buffer, Enkl_Allocator* allocator);

void* enkl_append_bytes_resize_helper(void* dst, size_t* dst_offset, size_t* dst_capacity, const void* src, size_t size, Enkl_Allocator* allocator);

typedef enum {
    ZLib_Deflate, ZLib_Zlib, ZLib_GZip
} ZLibMode;

bool enkl_inflate(ZLibMode, size_t src_size, const void* input_data, size_t* output_size, void** output, Enkl_Allocator* allocator);

#endif
