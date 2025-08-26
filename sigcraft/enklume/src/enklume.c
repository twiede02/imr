#include "enklume/enklume.h"
#include "enklume/nbt.h"
#include "support_private.h"

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdalign.h>

struct McWorld_ {
    Enkl_Allocator* allocator;
    const char* path;
};

McWorld* cunk_open_mcworld(const char* folder, Enkl_Allocator* allocator) {
    if (!enkl_folder_exists(folder))
        return NULL;
    const char* datfile_path = enkl_format_string("%s/level.dat", folder);
    if (!enkl_file_exists(datfile_path))
        return NULL;
    free((char*) datfile_path);

    McWorld* world = allocator->allocate_bytes(allocator, sizeof(McWorld), alignof(McWorld));
    *world = (McWorld) {
        .allocator = allocator,
        .path = enkl_copy_string(folder, allocator),
    };
    return world;
}

void cunk_close_mcworld(McWorld* w) {
    w->allocator->free_bytes(w->allocator, (void*) w->path);
    w->allocator->free_bytes(w->allocator, w);
}

typedef union {
    struct {
        unsigned offset: 24;
        unsigned sector_count: 8;
    };
    uint32_t word;
} ChunkLocation;

typedef uint32_t ChunkTimestamp;

typedef struct {
    ChunkLocation locations[32][32];
    ChunkTimestamp timestamps[32][32];
} McRegionHeader;

static_assert(sizeof(ChunkLocation) == sizeof(int32_t), "some unwanted padding made it in :/");

typedef enum {
    Compr_INVALID, Compr_GZip, Compr_Zlib, Compr_Uncompressed
} McChunkCompression;

typedef struct {
    uint32_t length;
    uint8_t compression_type;
    const char* compressed_data;
} McRegionPayload;

struct McRegion_ {
    McWorld* world;
    char* bytes;
    McRegionHeader decoded_header;
    McRegionPayload decoded_payloads[32][32];
};

McRegion* cunk_open_mcregion(McWorld* world, int x, int z) {
    const char* path = enkl_format_string("%s/region/r.%d.%d.mca", world->path, x, z);
    if (!enkl_file_exists(path))
        goto fail;

    size_t size;
    void* contents;
    if (!enkl_read_file(path, &size, (char**) &contents, world->allocator))
        goto fail;

    McRegion* region = world->allocator->allocate_bytes(world->allocator, sizeof(McRegion), alignof(McRegion));
    region->world = world;

    // decode the headers
    const McRegionHeader* big_endian_header = contents;
    for (int cz = 0; cz < 32; cz++) {
        for (int cx = 0; cx < 32; cx++) {
            // We need to swap the endianness of those
            ChunkLocation location = region->decoded_header.locations[cz][cx];
            location.offset = enkl_swap_endianness(3, big_endian_header->locations[cz][cx].offset & 0xFFFFFF);
            location.sector_count = enkl_swap_endianness(1, big_endian_header->locations[cz][cx].sector_count & 0xFF);
            region->decoded_header.locations[cz][cx] = location;
            // Likewise.
            region->decoded_header.timestamps[cz][cx] = enkl_swap_endianness(4, big_endian_header->timestamps[cz][cx]);

            assert(region->decoded_header.locations[cz][cx].offset * 4096 < size);
            McRegionPayload* payload = &region->decoded_payloads[cz][cx];

            if (location.sector_count > 0) {
                const McRegionPayload* big_endian_payload = contents + location.offset * 4096;
                payload->length = enkl_swap_endianness(4, big_endian_payload->length);
                // payload->compression_type = swap_endianness(4, big_endian_payload->compression_type);
                payload->compression_type = big_endian_payload->compression_type;
                assert((McChunkCompression) payload->compression_type <= Compr_Uncompressed);
                payload->compressed_data = (char *) big_endian_payload + 5;

                assert((size_t) (location.offset * 4096 + payload->length) <= size);
            } else {
                payload->length = 0;
                payload->compressed_data = NULL;
            }
        }
    }

    free((char*) path);
    region->bytes = contents;
    return region;

    fail:
    free((char*) path);
    return NULL;
}

void enkl_close_region(McRegion* r) {
    Enkl_Allocator* allocator = r->world->allocator;
    allocator->free_bytes(allocator, r->bytes);
    allocator->free_bytes(allocator, r);
}

struct McChunk_ {
    McRegion* region;
    NBT_Object* root;
};

McChunk* cunk_open_mcchunk(McRegion* region, unsigned int x, unsigned int z) {
    assert(x < 32 && z < 32);
    Enkl_Allocator* allocator = region->world->allocator;

    NBT_Object* root = NULL;
    McRegionPayload* payload = &region->decoded_payloads[z][x];
    if (payload->length == 0)
        return NULL;
    const char* nbt_data = payload->compressed_data;
    uint32_t nbt_data_size = payload->length;
    switch ((McChunkCompression) payload->compression_type) {
        case Compr_Zlib:
        case Compr_GZip: {
            ZLibMode zlib_mode = payload->compression_type == Compr_GZip ? ZLib_GZip : ZLib_Zlib;
            assert(payload->compression_type == 2);
            assert(zlib_mode == ZLib_Zlib);
            void* decompressed_nbt_data;
            size_t decompressed_size;
            enkl_inflate(zlib_mode, (size_t) nbt_data_size, nbt_data, &decompressed_size, &decompressed_nbt_data, allocator);
            root = cunk_decode_nbt(decompressed_size, decompressed_nbt_data, allocator);
            free((char*) decompressed_nbt_data);
            break;
        }
        case Compr_Uncompressed: {
            root = cunk_decode_nbt(nbt_data_size, nbt_data, allocator);
            break;
        }
    }

    assert(root);

    McChunk* chunk = calloc(1, sizeof(McChunk));
    *chunk = (McChunk) {
        .region = region,
        .root = root,
    };
    return chunk;
}

void enkl_close_chunk(McChunk* chunk) {
    Enkl_Allocator* allocator = chunk->region->world->allocator;
    enkl_free_nbt(chunk->root, allocator);
    free(chunk);
}

const NBT_Object* cunk_mcchunk_get_root(const McChunk* c) { return c->root; }

McDataVersion cunk_mcchunk_get_data_version(const McChunk* c) {
    const NBT_Object* o = cunk_nbt_compound_access(c->root, "DataVersion");
    if (o)
        return *cunk_nbt_extract_int(o);
    return 0;
}
