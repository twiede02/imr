#include "support_private.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <limits.h>

unsigned int enkl_needed_bits(unsigned int n) {
    if (n == 0)
        return 0;
    int bits = 1;
    int maxn = 2;
    while (n > maxn) {
        bits++;
        maxn *= 2;
    }
    return bits;
}

uint64_t enkl_fetch_bits(const void* buf, size_t bit_pos, unsigned int width) {
    const char* cbuf = buf;
    int64_t acc = 0;
    size_t pos = SIZE_MAX;
    char last_fetch;
    for (size_t bit = 0; bit < width; bit++) {
        size_t new_pos = (bit_pos + bit) / CHAR_BIT;
        if (new_pos != pos) {
            pos = new_pos;
            last_fetch = cbuf[pos];
        }
        int64_t b = (last_fetch >> ((bit_pos + bit) & 0x7)) & 0x1;
        acc |= (b << bit);
    }
    return acc;
}

uint64_t enkl_fetch_bits_long_arr(const void* buf, bool big_endian, size_t bit_pos, unsigned int width) {
    const int64_t* lbuf = buf;
#define LONG_BITS (CHAR_BIT * sizeof(int64_t))
    int64_t acc = 0;

    size_t pos = SIZE_MAX;
    uint64_t last_fetch;
    for (size_t bit = 0; bit < width; bit++) {
        size_t new_pos = (bit_pos + bit) / LONG_BITS;
        if (new_pos != pos) {
            pos = new_pos;
            last_fetch = lbuf[pos];
            if (big_endian)
                last_fetch = enkl_swap_endianness(8, last_fetch);
        }
        uint64_t b = (last_fetch >> ((bit_pos + bit) & (LONG_BITS - 1))) & 0x1;
        acc |= (b << bit);
    }
    return acc;
#undef LONG_BITS
}

bool enkl_string_ends_with(const char* string, const char* suffix) {
    size_t len = strlen(string);
    size_t slen = strlen(suffix);
    if (len < slen)
        return false;
    for (size_t i = 0; i < slen; i++) {
        if (string[len - 1 - i] != suffix[slen - 1 - i])
            return false;
    }
    return true;
}

static const char* sanitize_path(const char* path) {
    size_t pathlen = strlen(path);
    bool windows_path = false;
    if (pathlen > 2) {
        if (path[1] == ':')
            windows_path = true;
    }

    if (windows_path) {
        return enkl_replace_string(path, "/", "\\");
    }

    return enkl_format_string("%s", path);
}

bool enkl_read_file(const char* filename, size_t* out_size, char** out_buffer, Enkl_Allocator* allocator) {
    char* buffer = NULL;
    long length;
    FILE* f = fopen(filename, "rb");

    if (f) {
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        fseek(f, 0, SEEK_SET);

        buffer = *out_buffer = allocator->allocate_bytes(allocator, length + 1, 0);
        if (buffer) {
            buffer[length] = '\0';
            fread(buffer, 1, length, f);
            *out_size = (size_t) length;
            fclose(f);
            return true;
        }
    }
    return false;
}

// this does not work on non-POSIX compliant systems
// Cygwin/MINGW works though.
#include "sys/stat.h"

bool enkl_folder_exists(const char* filename) {
    struct stat s = { 0 };
    if (stat(filename, &s) == 0) {
        return S_ISDIR(s.st_mode);
    }
    return false;
}

bool enkl_file_exists(const char* filename) {
    const char* sanitized = sanitize_path(filename);
    struct stat s = { 0 };
    if (stat(filename, &s) == 0) {
        free((char*) sanitized);
        return true;
    }
    free((char*) sanitized);
    return false;
}

int64_t enkl_swap_endianness(int bytes, int64_t i) {
    int64_t acc = 0;
    for (int byte = 0; byte < bytes; byte++)
        acc |= ((i >> byte * 8) & 0xFF) << (bytes - 1 - byte) * 8;
    return acc;
}

enum {
    ThreadLocalStaticBufferSize = 256
};

static char static_buffer[ThreadLocalStaticBufferSize];

static void format_string_internal(const char* str, va_list args, void* uptr, void final_allocator(void*, size_t, char*)) {
    size_t buffer_size = ThreadLocalStaticBufferSize;
    int len;
    char* tmp;
    while (true) {
        if (buffer_size == ThreadLocalStaticBufferSize) {
            tmp = static_buffer;
        } else {
            tmp = malloc(buffer_size);
        }
        va_list args_copy;
        va_copy(args_copy, args);
        len = vsnprintf(tmp, buffer_size, str, args_copy);
        if (len < 0 || len >= (int) buffer_size - 1) {
            buffer_size *= 2;
            if (tmp != static_buffer)
                free(tmp);
            continue;
        } else {
            final_allocator(uptr, len, tmp);
            if (tmp != static_buffer)
                free(tmp);
            return;
        }
    }
}

typedef struct { char** result; } PutNewPayload;

static void malloc_allocator(PutNewPayload* uptr, size_t len, char* tmp) {
    char* allocated = (char*) malloc(len + 1);
    strncpy(allocated, tmp, len);
    allocated[len] = '\0';
    *uptr->result = allocated;
}

char* enkl_format_string_valist(const char* str, va_list args) {
    char* result = NULL;
    PutNewPayload p = { .result = &result };
    format_string_internal(str, args, &p, (void (*)(void*, size_t, char*)) malloc_allocator);
    return result;
}

char* enkl_format_string(const char* str, ...) {
    char* result = NULL;
    PutNewPayload p = { .result = &result };
    va_list args;
    va_start(args, str);
    format_string_internal(str, args, &p, (void (*)(void*, size_t, char*)) malloc_allocator);
    va_end(args);
    return result;
}

static void* append_bytes_resize_helper(void* dst, size_t* dst_offset, size_t* dst_capacity, const void* src, size_t size) {
    while (*dst_offset + size >= *dst_capacity) {
        *dst_capacity *= 2;
        dst = realloc(dst, *dst_capacity);
        assert(dst);
    }
    memcpy((uint8_t*) dst + *dst_offset, src, size);
    *dst_offset += size;
    return dst;
}

void* enkl_append_bytes_resize_helper(void* dst, size_t* dst_offset, size_t* dst_capacity, const void* src, size_t size, Enkl_Allocator* allocator) {
    while (*dst_offset + size >= *dst_capacity) {
        size_t old_capacity = *dst_capacity;
        *dst_capacity *= 2;
        dst = allocator->grow_allocation(allocator, dst, 1, old_capacity, *dst_capacity);
        assert(dst);
    }
    memcpy((uint8_t*) dst + *dst_offset, src, size);
    *dst_offset += size;
    return dst;
}

const char* enkl_replace_string(const char* source, const char* match, const char* replace_with) {
    size_t space = 16, size = 0;
    char* result = malloc(space);

    size_t match_len = strlen(match);
    size_t replace_len = strlen(replace_with);
    const char* next_match = strstr(source, match);
    while (next_match != NULL) {
        size_t diff = next_match - source;
        result = append_bytes_resize_helper(result, &size, &space, source, diff);
        result = append_bytes_resize_helper(result, &size, &space, replace_with, replace_len);
        source = next_match + match_len;
        next_match = strstr(source, match);
    }
    result = append_bytes_resize_helper(result, &size, &space, source, strlen(source));
    char zero = '\0';
    result = append_bytes_resize_helper(result, &size, &space, &zero, 1);
    return result;
}

char* enkl_copy_string(const char* s, Enkl_Allocator* allocator) {
    size_t len = strlen(s);
    char* copy = allocator->allocate_bytes(allocator, len + 1, 0);
    copy[len] = '\0';
    memcpy(copy, s, len);
    return copy;
}

void enkl_append_stdout(Enkl_FilePrinter* p, const char* str, size_t size) {
    fprintf(p->f, "%s", str);
}

void enkl_indent_stdout(Enkl_FilePrinter* p) {
    p->indent++;
}

void enkl_deindent_stdout(Enkl_FilePrinter* p) {
    p->indent--;
}

void enkl_newline_stdout(Enkl_FilePrinter* p) {
    fprintf(p->f, "\n");
    for (unsigned i = 0; i < p->indent; i++)
        fprintf(p->f, " ");
}

Enkl_FilePrinter enkl_make_file_printer(FILE* f) {
    return (Enkl_FilePrinter) {
        .base = {
            .append = (void (*)(Enkl_Printer*, const char*, size_t)) enkl_append_stdout,
            .newline = (void (*)(Enkl_Printer*)) enkl_newline_stdout,
            .indent = (void (*)(Enkl_Printer*)) enkl_indent_stdout,
            .deindent = (void (*)(Enkl_Printer*)) enkl_deindent_stdout,
        },
        .f = f,
        .indent = 0,
    };
}

Enkl_FilePrinter enkl_get_default_printer(void) {
    return enkl_make_file_printer(stdout);
}

void* enkl_allocate_malloc(Enkl_Allocator* unused, size_t size, size_t align) {
    return malloc(size);
}

void* enkl_grow_allocation_malloc(Enkl_Allocator* unused, void* ptr, size_t align, size_t old_size, size_t size) {
    return realloc(ptr, size);
}

void enkl_free_malloc(Enkl_Allocator* unused, void* ptr) {
    free(ptr);
}

Enkl_Allocator enkl_get_malloc_free_allocator(void) {
    return (Enkl_Allocator) {
        .allocate_bytes = enkl_allocate_malloc,
        .grow_allocation = enkl_grow_allocation_malloc,
        .free_bytes = enkl_free_malloc,
    };
}