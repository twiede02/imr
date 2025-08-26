#include "enklume/enklume.h"
#include "enklume/nbt.h"
#include "support_private.h"

#include "enklume/block_data.h"

#include <string.h>
#include <assert.h>
#include <stdlib.h>

#define MC_1_18_DATA_VERSION 2825

static BlockData decode_pre_flattening_id(uint8_t id) {
    if (id == 0)
        return BlockAir;
    if (id == 1)
        return BlockStone;
    if (id == 2)
        return BlockGrass;
    if (id == 3)
        return BlockDirt;
    if (id == 4)
        return BlockStone;
    if (id == 5)
        return BlockPlanks;
    if (id == 8 || id == 9)
        return BlockWater;
    if (id == 10 || id == 11)
        return BlockLava;
    if (id == 12)
        return BlockSand;
    if (id == 13)
        return BlockGravel;
    if (id == 17)
        return BlockWood;
    if (id == 18 || id == 161)
        return BlockLeaves;
    if (id == 31)
        return BlockTallGrass;
    if (id == 78 || id == 80)
        return BlockSnow;
    return BlockUnknown;
}

static BlockData decode_flattened_id(const char* id) {
    if (strcmp("minecraft:air", id) == 0)
        return BlockAir;
    if (strcmp("minecraft:stone", id) == 0)
        return BlockStone;
    if (strcmp("minecraft:grass", id) == 0)
        return BlockGrass;
    if (strcmp("minecraft:dirt", id) == 0)
        return BlockDirt;
    if (strcmp("minecraft:cobblestone", id) == 0)
        return BlockStone;
    if (strcmp("minecraft:planks", id) == 0)
        return BlockPlanks;
    if ((strcmp("minecraft:water", id) == 0) || (strcmp("minecraft:flowing_water", id) == 0))
        return BlockWater;
    if ((strcmp("minecraft:lava", id) == 0) || (strcmp("minecraft:flowing_lava", id) == 0))
        return BlockLava;
    if (strcmp("minecraft:sand", id) == 0)
        return BlockSand;
    if (strcmp("minecraft:gravel", id) == 0)
        return BlockGravel;
    if (strcmp("minecraft:log", id) == 0)
        return BlockWood;
    if ((strcmp("minecraft:leaves", id) == 0) || strcmp("minecraft:leaves2", id) == 0)
        return BlockLeaves;
    if (strcmp("minecraft:tallgrass", id) == 0)
        return BlockTallGrass;
    if ((strcmp("minecraft:snow_layer", id) == 0) || strcmp("minecraft:snow", id) == 0)
        return BlockSnow;
    return BlockUnknown;
}

static void decode_pre_flattening(ChunkData* dst_chunk, int section_y, const NBT_Object* blocks_data) {
    if (!blocks_data)
        return;
    const NBT_ByteArray* arr = cunk_nbt_extract_byte_array(blocks_data);
    for (int pos = 0; pos < 16 * 16 * 16; pos++) {
        int x = (pos >> 4) & 15;
        int y = (pos >> 8) & 15;
        int z = (pos >> 0) & 15;

        int8_t block_state = arr->arr[pos];
        assert(pos < arr->count);

        chunk_set_block_data(dst_chunk, z, y + section_y * 16, x, decode_pre_flattening_id(block_state));
    }
}

static void decode_post_flattening(ChunkData* dst_chunk, int section_y, const NBT_Object* block_states, const NBT_Object* palette, bool can_straddle_boundary) {
    if (!(block_states && palette))
        return;
    // cunk_print_nbt(p, palette);
    assert(block_states->tag == NBT_Tag_LongArray && palette->tag == NBT_Tag_List);
    const NBT_LongArray* block_state_arr = cunk_nbt_extract_long_array(block_states);
    int palette_size = palette->body.p_list.count;

    BlockData decoded[palette_size];
    for (size_t j = 0; j < palette_size; j++) {
        const NBT_Compound* color = &palette->body.p_list.bodies[j].p_compound;
        const char* name = *cunk_nbt_extract_string(cunk_nbt_compound_direct_access(color, "Name"));
        assert(name);
        decoded[j] = decode_flattened_id(name);
    }

    int bits = enkl_needed_bits(palette_size);
    if (bits < 4)
        bits = 4;

    int aligned_bitpos = 0;
    for (int pos = 0; pos < 16 * 16 * 16; pos++) {
        int x = (pos >> 4) & 15;
        int y = (pos >> 8) & 15;
        int z = (pos >> 0) & 15;

        uint64_t block_state = enkl_fetch_bits_long_arr(block_state_arr->arr, true, aligned_bitpos, bits);
        aligned_bitpos += bits;
        // "Since 1.16, the indices are not packed across multiple elements of the array, meaning that if there is no more space in a given 64-bit integer for the next index, it starts instead at the first (lowest) bit of the next 64-bit element."
        // https://minecraft.fandom.com/wiki/Chunk_format#NBT_structure
        if (!can_straddle_boundary) {
            int starting_long = aligned_bitpos / 64;
            int finishing_long = (aligned_bitpos + bits - 1) / 64;
            if (starting_long != finishing_long)
                aligned_bitpos = finishing_long * 64;
        }

        assert(block_state < palette_size);
        block_state %= palette_size;
        chunk_set_block_data(dst_chunk, z, y + section_y * 16, x, decoded[block_state]);
    }
}

void load_from_mcchunk(ChunkData* dst_chunk, McChunk* chunk) {
    McDataVersion ver = cunk_mcchunk_get_data_version(chunk);
    bool post_1_18 = ver > MC_1_18_DATA_VERSION;

    const NBT_Object *o = cunk_mcchunk_get_root(chunk);
    assert(o);

    const NBT_Object *level = cunk_nbt_compound_access(o, "Level");
    if (level)
        o = level;
    else
        assert(post_1_18);
    assert(o);
    // Iterate over sections
    o = cunk_nbt_compound_access(o, post_1_18 ? "sections" : "Sections");
    assert(o);
    const NBT_List* sections = cunk_nbt_extract_list(o);
    assert(sections);
    assert(sections->tag == NBT_Tag_Compound);
    for (size_t i = 0; i < sections->count; i++) {
        const NBT_Compound* section = &sections->bodies[i].p_compound;
        int8_t section_y = *cunk_nbt_extract_byte(cunk_nbt_compound_direct_access(section, "Y"));
        if (section_y < 0)
            continue;

        const NBT_Object* blocks_data = cunk_nbt_compound_direct_access(section, "Blocks");
        if (blocks_data) {
            decode_pre_flattening(dst_chunk, section_y, blocks_data);
        } else {
            // Starting with 1.18, block data for a chunk section is stored in a 'block_states' compound
            const NBT_Compound* container = section;
            const NBT_Object* block_states_container = cunk_nbt_compound_direct_access(section, "block_states");
            if (post_1_18 && block_states_container)
                container = cunk_nbt_extract_compound(block_states_container);

            const NBT_Object* block_states = cunk_nbt_compound_direct_access(container, post_1_18 ? "data" : "BlockStates");
            const NBT_Object* palette = cunk_nbt_compound_direct_access(container, post_1_18 ? "palette" : "Palette");
            decode_post_flattening(dst_chunk, section_y, block_states, palette, ver < 2504);
        }
    }
}

void enkl_destroy_chunk_data(ChunkData* chunk) {
    for (int section = 0; section < (CUNK_CHUNK_MAX_HEIGHT / CUNK_CHUNK_SIZE); section++) {
        if (chunk->sections[section])
            free(chunk->sections[section]);
    }
}

BlockData chunk_get_block_data(const ChunkData* chunk, unsigned x, unsigned y, unsigned z) {
    assert(x < CUNK_CHUNK_SIZE && z < CUNK_CHUNK_SIZE && y < CUNK_CHUNK_MAX_HEIGHT);
    const ChunkSection* section = chunk->sections[y / CUNK_CHUNK_SIZE];
    y %= CUNK_CHUNK_SIZE;
    if (section)
        return section->block_data[y][z][x];
    return air_data;
}

void chunk_set_block_data(ChunkData* chunk, unsigned x, unsigned y, unsigned z, BlockData data) {
    assert(x < CUNK_CHUNK_SIZE && z < CUNK_CHUNK_SIZE && y < CUNK_CHUNK_MAX_HEIGHT);
    unsigned sid = y / CUNK_CHUNK_SIZE;
    ChunkSection* section = chunk->sections[sid];
    y %= CUNK_CHUNK_SIZE;
    if (!section)
        section = chunk->sections[sid] = calloc(CUNK_CHUNK_SIZE * CUNK_CHUNK_SIZE * CUNK_CHUNK_SIZE, sizeof(BlockData));
    section->block_data[y][z][x] = data;
}
