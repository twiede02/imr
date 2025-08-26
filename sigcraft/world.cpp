#include "world.h"

World::World(const char* filename) {
    allocator = enkl_get_malloc_free_allocator();
    enkl_world = cunk_open_mcworld(filename, &allocator);
}

World::~World() {
    regions.clear();
    cunk_close_mcworld(enkl_world);
}

std::vector<Chunk*> World::loaded_chunks() {
    std::vector<Chunk*> list;
    for (auto& [_, region] : regions) {
        assert(region);
        for (auto& [_, chunk] : region->chunks) {
            list.push_back(&*chunk);
        }
    }
    return list;
}

Region* World::get_loaded_region(int rx, int rz) {
    if (auto found = regions.find({ rx, rz}); found != regions.end()) {
        return &*found->second;
    }
    return nullptr;
}

Region* World::load_region(int rx, int rz) {
    assert(!get_loaded_region(rx, rz));
    Int2 pos = {rx, rz};
    auto& r = regions[pos] = std::make_unique<Region>(*this, rx, rz);
    assert(!r->loaded);
    assert(!r->unloaded);
    r->loaded = true;
    return &*r;
}

static int to_region_coordinate(int c) {
    if (c < 0)
        return (c - 31) / 32;
    return c / 32;
}

static std::tuple<int, int> to_region_coordinates(int cx, int cz) {
    int rx = to_region_coordinate(cx);
    int rz = to_region_coordinate(cz);
    return { rx, rz };
}

Chunk* World::get_loaded_chunk(int cx, int cz) {
    auto [rx, rz] = to_region_coordinates(cx, cz);
    auto found = get_loaded_region(rx, rz);
    if (found)
        return found->get_chunk(cx & 0x1f, cz & 0x1f);
    return nullptr;
}

Chunk* World::load_chunk(int cx, int cz) {
    auto [rx, rz] = to_region_coordinates(cx, cz);
    Region* r = get_loaded_region(rx, rz);
    if (!r)
        r = load_region(rx, rz);
    return r->load_chunk(cx, cz);
}

void World::unload_chunk(Chunk* chunk) {
    Region* region = &chunk->region;
    region->unload_chunk(chunk);
    if (region->chunks.size() == 0)
        unload_region(region);
}

void World::unload_region(Region* region) {
    assert(region->loaded);
    assert(!region->unloaded);
    region->unloaded = true;
    int rx = region->rx;
    int rz = region->rz;
    Int2 pos = {rx, rz};
    regions.erase(pos);
}

Region::Region(World& w, int rx, int rz) : world(w), rx(rx), rz(rz) {
    //printf("! %d %d\n", rx, rz);
    enkl_region = cunk_open_mcregion(w.enkl_world, rx, rz);
}

Region::~Region() {
    //printf("~ %d %d %zu\n", rx, rz, (size_t) enkl_region);
    chunks.clear();
    if (enkl_region)
        enkl_close_region(enkl_region);
}

Chunk* Region::get_chunk(unsigned int rcx, unsigned int rcz) {
    assert(rcx >= 0 && rcz >= 0);
    assert(rcx < 32 && rcz < 32);
    auto found = chunks.find({(int) rcx, (int) rcz});
    if (found != chunks.end())
        return &*found->second;
    return nullptr;
}

Chunk* Region::load_chunk(int cx, int cz) {
    unsigned rcx = cx & 0x1f;
    unsigned rcz = cz & 0x1f;
    assert(rcx < 32 && rcz < 32);
    assert(!get_chunk(rcx, rcz));
    Int2 pos = {(int)rcx, (int)rcz};
    auto& r = chunks[pos] = std::make_unique<Chunk>(*this, cx, cz);
    return &*r;
}

void Region::unload_chunk(Chunk* chunk) {
    unsigned rcx = chunk->cx & 0x1f;
    unsigned rcz = chunk->cz & 0x1f;
    Int2 pos = {(int)rcx, (int)rcz};
    chunks.erase(pos);
}

Chunk::Chunk(Region& r, int cx, int cz) : region(r), cx(cx), cz(cz) {
    unsigned rcx = cx & 0x1f;
    unsigned rcz = cz & 0x1f;
    //printf("! %d %d\n", cx, cz);
    if (r.enkl_region) {
        enkl_chunk = cunk_open_mcchunk(r.enkl_region, rcx, rcz);
        if (enkl_chunk) {
            load_from_mcchunk(&data, enkl_chunk);
        }
    }
}

Chunk::~Chunk() {
    //printf("~ %d %d\n", cx, cz);
    enkl_destroy_chunk_data(&data);
    if (enkl_chunk)
        enkl_close_chunk(enkl_chunk);
}
