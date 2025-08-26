#ifndef ENKLUME_SUPPORT_H
#define ENKLUME_SUPPORT_H

#include <stddef.h>
#include <stdio.h>

typedef struct Enkl_Allocator_ {
    void* (*allocate_bytes)(struct Enkl_Allocator_*, size_t size, size_t alignment);
    void* (*grow_allocation)(struct Enkl_Allocator_*, void* old, size_t alignment, size_t old_size, size_t new_size);
    void (*free_bytes)(struct Enkl_Allocator_*, void*);
} Enkl_Allocator;

/// Default allocator
Enkl_Allocator enkl_get_malloc_free_allocator(void);

typedef struct Enkl_Printer_ {
    void (*newline)(struct Enkl_Printer_*);
    void (*indent)(struct Enkl_Printer_*);
    void (*deindent)(struct Enkl_Printer_*);
    void (*append)(struct Enkl_Printer_*, const char*, size_t);
} Enkl_Printer;

typedef struct {
    Enkl_Printer base;
    FILE* f;
    unsigned indent;
} Enkl_FilePrinter;

Enkl_FilePrinter enkl_make_file_printer(FILE* f);
Enkl_FilePrinter enkl_get_default_printer(void);

#endif
