#include "enklume/nbt.h"
#include "support_private.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

int main(int argc, char** argv) {
    Enkl_FilePrinter p = enkl_get_default_printer();
    Enkl_Allocator allocator = enkl_get_malloc_free_allocator();

    void* buf;
    size_t buf_size;
    printf("Using test file %s\n", argv[1]);
    if (!enkl_read_file(argv[1], &buf_size, (char**) &buf, &allocator))
        return 1;

    if (enkl_string_ends_with(argv[1], ".dat")) {
        printf(".dat file detected, it needs decompression\n");
        printf("compressed size: %zu\n", buf_size);
        void* decompressed;
        size_t decompressed_size;
        enkl_inflate(ZLib_GZip, buf_size, buf, &decompressed_size, &decompressed, &allocator);
        free(buf);
        printf("Decompression successful\n");
    }

    NBT_Object* o = cunk_decode_nbt(buf_size, buf, &allocator);
    assert(o);
    enkl_print_nbt(&p.base, o);
    //enkl_print(p, "\nSize of NBT arena: ");
    //enkl_print_size_suffix(p, cunk_arena_size(arena), 3);
    //cunk_print(p, "\n");
    free(buf);
    return 0;
}
