#ifndef CUNK_CHUNK
#define CUNK_CHUNK

#include <stdint.h>
#include <stddef.h>

#include "enklume.h"

#define CUNK_CHUNK_SIZE 16
#define CUNK_CHUNK_MAX_HEIGHT 384
#define CUNK_CHUNK_SECTIONS_COUNT CUNK_CHUNK_MAX_HEIGHT / CUNK_CHUNK_SIZE

typedef uint32_t BlockData;

#define BLOCK_TYPES(B) \
B(Air, 0, 0, 0) \
B(Stone, 0.5, 0.5, 0.5) \
B(Dirt, 0.25, 0.25, 0) \
B(Grass, 0.2, 0.8, 0.1) \
B(TallGrass, 0.2, 0.9, 0.1) \
B(Sand, 0.8, 0.8, 0) \
B(Gravel, 0.9, 0.9, 0.9) \
B(Planks, 0.8, 0.5, 0.0) \
B(Water, 0.0, 0.2, 0.8) \
B(Leaves, 0.1, 0.4, 0.1) \
B(Wood, 0.3, 0.1, 0.0) \
B(Snow, 1.0, 1.0, 1.0) \
B(Lava, 1.0, 0.2, 0.0) \
B(Unknown, 1.0, 0.0, 1.0)

enum BlockId {
#define B(name, r, g, b) Block##name,
BLOCK_TYPES(B)
#undef B
};

static struct { float r, g, b; } block_colors[] = {
#define B(name, r, g, b) { r, g, b},
    BLOCK_TYPES(B)
#undef B
};

static BlockData air_data = 0;

typedef struct {
    BlockData block_data[CUNK_CHUNK_SIZE][CUNK_CHUNK_SIZE][CUNK_CHUNK_SIZE];
} ChunkSection;

typedef struct {
    ChunkSection* sections[CUNK_CHUNK_SECTIONS_COUNT];
} ChunkData;

BlockData chunk_get_block_data(const ChunkData*, unsigned x, unsigned y, unsigned z);
void chunk_set_block_data(ChunkData*, unsigned x, unsigned y, unsigned z, BlockData);

void load_from_mcchunk(ChunkData* dst_chunk, McChunk* chunk);
void enkl_destroy_chunk_data(ChunkData*);

#endif
