#include "enklume/enklume.h"
#include "enklume/nbt.h"
#include "support_private.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <limits.h>

int main(int argc, char** argv) {
    Enkl_FilePrinter p = enkl_get_default_printer();
    Enkl_Allocator allocator = enkl_get_malloc_free_allocator();

    McWorld* w = cunk_open_mcworld(argv[1], &allocator);
    assert(w);

    McRegion* r = cunk_open_mcregion(w, 0, 0);
    assert(r);
    McChunk* c = cunk_open_mcchunk(r, 0, 0);
    assert(c);

    // cunk_print_nbt(p, cunk_mcchunk_get_root(c));
    const NBT_Object* o = cunk_mcchunk_get_root(c);
    assert(o);
    o = cunk_nbt_compound_access(o, "Level");
    assert(o);
    o = cunk_nbt_compound_access(o, "Sections");
    assert(o);
    const NBT_List* sections = cunk_nbt_extract_list(o);
    assert(sections);
    assert(sections->tag == NBT_Tag_Compound);
    for (size_t i = 0; i < sections->count; i++) {
        const NBT_Compound* section = &sections->bodies[i].p_compound;
        int8_t y = *cunk_nbt_extract_byte(cunk_nbt_compound_direct_access(section, "Y"));
        printf("Y: %d\n", y);
        const NBT_Object* block_states = cunk_nbt_compound_direct_access(section, "BlockStates");
        const NBT_Object* palette = cunk_nbt_compound_direct_access(section, "Palette");
        if (!(block_states && palette))
            continue;
        enkl_print_nbt(&p.base, palette);
        assert(block_states->tag == NBT_Tag_LongArray && palette->tag == NBT_Tag_List);
        const NBT_LongArray* block_state_arr = cunk_nbt_extract_long_array(block_states);
        int palette_size = palette->body.p_list.count;

        int bits = enkl_needed_bits(palette_size);
        int longbits = block_state_arr->count * sizeof(int64_t) * CHAR_BIT;
        printf("%d %d %d %d\n", palette_size, bits, longbits, bits * 16 * 16 * 16);
        // cunk_print_nbt(p, block_states);
    }

    return 0;
}
