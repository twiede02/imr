#include "imr/imr.h"

#include "vk_mem_alloc.h"

namespace imr {

struct Device::Impl {
    VmaAllocator allocator;

    //std::vector<std::unique_ptr<Buffer>> buffers;
    std::vector<std::unique_ptr<Image>> images;
};

}