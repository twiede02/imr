#include "shader_private.h"

namespace imr {

struct DescriptorBindHelper::Impl {
    Device& device;
    PipelineLayout& layout;
    ReflectedLayout& reflected;
    VkPipelineBindPoint bind_point;

    unsigned nsets;
    VkDescriptorSet* sets;
    VkDescriptorPool pool;

    std::vector<std::function<void(void)>> cleanup;
    bool committed = false;

    Impl(Device& device, PipelineLayout& layout, ReflectedLayout& reflected, VkPipelineBindPoint bind_point) : device(device), layout(layout), reflected(reflected), bind_point(bind_point) {
        auto& vk = device.dispatch;
        nsets = reflected.set_bindings.size();

        std::unordered_map<VkDescriptorType, uint32_t> descriptor_counts;
        auto access_map = [&](VkDescriptorType key) -> uint32_t& {
            if (descriptor_counts.contains(key))
                return descriptor_counts[key];
            return descriptor_counts[key] = 0;
        };
        for (auto& [set, bindings] : reflected.set_bindings) {
            for (auto& binding : bindings) {
                access_map(binding.descriptorType) += binding.descriptorCount;
            }
        }

        std::vector<VkDescriptorPoolSize> pool_sizes;
        for (auto& [type, count] : descriptor_counts) {
            VkDescriptorPoolSize size = {
                .type = type,
                .descriptorCount = count,
            };
            pool_sizes.push_back(size);
        }

        vkCreateDescriptorPool(vk.device, tmpPtr<VkDescriptorPoolCreateInfo>({
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = static_cast<uint32_t>(layout.set_layouts.size()),
            .poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
            .pPoolSizes = pool_sizes.data(),
        }), nullptr, &pool);

        sets = reinterpret_cast<VkDescriptorSet*>(calloc(nsets, sizeof(VkDescriptorSet)));
    }

    // Lazily allocates the set if we need it
    VkDescriptorSet get_or_create_set(unsigned set) {
        if (sets[set] == 0) {
            CHECK_VK_THROW(vkAllocateDescriptorSets(device.device, tmpPtr<VkDescriptorSetAllocateInfo>({
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &layout.set_layouts[set],
            }), &sets[set]));
        }
        return sets[set];
    }

    ~Impl() {
        free(sets);
        vkDestroyDescriptorPool(device.device, pool, nullptr);

        for (auto& fn : cleanup) {
            fn();
        }
    }
};

DescriptorBindHelper::DescriptorBindHelper(std::unique_ptr<imr::DescriptorBindHelper::Impl>&& impl) {
    _impl = std::move(impl);
}

DescriptorBindHelper::~DescriptorBindHelper() {}

DescriptorBindHelper* ComputePipeline::create_bind_helper() {
    auto impl = std::make_unique<DescriptorBindHelper::Impl>(_impl->device, *_impl->layout, *_impl->entry_point->_impl->reflected, VK_PIPELINE_BIND_POINT_COMPUTE);
    return new DescriptorBindHelper(std::move(impl));
}

DescriptorBindHelper* GraphicsPipeline::create_bind_helper() {
    auto impl = std::make_unique<DescriptorBindHelper::Impl>(_impl->device_, *_impl->layout, _impl->final_layout, VK_PIPELINE_BIND_POINT_GRAPHICS);
    return new DescriptorBindHelper(std::move(impl));
}

DescriptorBindHelper* RayTracingPipeline::create_bind_helper() {
    auto impl = std::make_unique<DescriptorBindHelper::Impl>(_impl->device, *_impl->layout, _impl->final_layout, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR);
    return new DescriptorBindHelper(std::move(impl));
}

void DescriptorBindHelper::set_storage_image(uint32_t set, uint32_t binding, VkImageView view, uint32_t array_element) {
    assert(!_impl->committed);
    auto& device = _impl->device;

    assert(view != VK_NULL_HANDLE);

    if(!_impl->reflected.find_binding(set, binding)) {
        return;
    }

    vkUpdateDescriptorSets(device.device, 1, tmpPtr<VkWriteDescriptorSet>({
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = _impl->get_or_create_set(set),
        .dstBinding = binding,
        .dstArrayElement = array_element,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = tmpPtr<VkDescriptorImageInfo>({
            .sampler = VK_NULL_HANDLE,
            .imageView = view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        }),
    }), 0, nullptr);
    auto deviceHandle = device.device.device;
}

void DescriptorBindHelper::set_sampler(uint32_t set, uint32_t binding, VkSampler sampler, uint32_t array_element) {
    assert(!_impl->committed);
    auto& device = _impl->device;

    if(!_impl->reflected.find_binding(set, binding)) {
        return;
    }

    vkUpdateDescriptorSets(device.device, 1, tmpPtr<VkWriteDescriptorSet>({
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = _impl->get_or_create_set(set),
        .dstBinding = binding,
        .dstArrayElement = array_element,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .pImageInfo = tmpPtr<VkDescriptorImageInfo>({
            .sampler = sampler,
        }),
    }), 0, nullptr);
}

void DescriptorBindHelper::set_texture_image(uint32_t set, uint32_t binding, VkImageView view, uint32_t array_element) {
    assert(!_impl->committed);
    auto& device = _impl->device;

    if(!_impl->reflected.find_binding(set, binding)) {
        return;
    }

    vkUpdateDescriptorSets(device.device, 1, tmpPtr<VkWriteDescriptorSet>({
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = _impl->get_or_create_set(set),
        .dstBinding = binding,
        .dstArrayElement = array_element,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = tmpPtr<VkDescriptorImageInfo>({
            .sampler = VK_NULL_HANDLE,
            .imageView = view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        }),
    }), 0, nullptr);
}

void DescriptorBindHelper::set_uniform_buffer(uint32_t set, uint32_t binding, imr::Buffer& buffer, uint64_t offset) {
    auto& device = _impl->device;

    if(!_impl->reflected.find_binding(set, binding)) {
        return;
    }
    VkDescriptorBufferInfo dbi = {
        .buffer = buffer.handle,
        .offset = offset,
        .range = buffer.size,
    };

    VkWriteDescriptorSet uniformBufferWrite = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = _impl->get_or_create_set(set),
        .dstBinding = binding,

        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &dbi
    };

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
        uniformBufferWrite
    };
    vkUpdateDescriptorSets(device.device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
}

void DescriptorBindHelper::set_storage_buffer(uint32_t set, uint32_t binding, imr::Buffer& buffer, uint64_t offset) {
    auto& device = _impl->device;

    if(!_impl->reflected.find_binding(set, binding)) {
        return;
    }
    VkDescriptorBufferInfo dbi = {
        .buffer = buffer.handle,
        .offset = offset,
        .range = buffer.size,
    };

    VkWriteDescriptorSet storageBufferWrite = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = _impl->get_or_create_set(set),
        .dstBinding = binding,

        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &dbi
    };

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
        storageBufferWrite
    };

    vkUpdateDescriptorSets(device.device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);

}

void DescriptorBindHelper::set_acceleration_structure(uint32_t set, uint32_t binding, imr::AccelerationStructure& as) {
    auto& device = _impl->device;
    if(!_impl->reflected.find_binding(set, binding)) {
        return;
    }
    VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo{};
    descriptorAccelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
    VkAccelerationStructureKHR lmao = as.handle();
    descriptorAccelerationStructureInfo.pAccelerationStructures = &lmao;

    VkWriteDescriptorSet accelerationStructureWrite{};
    accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    // The specialized acceleration structure descriptor has to be chained
    accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
    accelerationStructureWrite.dstSet = _impl->get_or_create_set(set);
    accelerationStructureWrite.dstBinding = binding;
    accelerationStructureWrite.descriptorCount = 1;
    accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
        accelerationStructureWrite,
    };
    vkUpdateDescriptorSets(device.device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
}

void DescriptorBindHelper::commit(VkCommandBuffer cmdbuf) {
    assert(!_impl->committed);
    for (unsigned set = 0; set < _impl->nsets; set++) {
        if (_impl->sets[set])
            vkCmdBindDescriptorSets(cmdbuf, _impl->bind_point, _impl->layout.pipeline_layout, set, 1, &_impl->sets[set], 0, nullptr);
    }
    _impl->committed = true;
}

}