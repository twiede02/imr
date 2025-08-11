#include "shader_private.h"

#include "imr/imr.h"
#include "imr/util.h"

namespace imr {

    RayTracingPipeline::RayTracingPipeline(imr::Device& d, std::vector<ShaderEntryPoint*>&& stages) {
        _impl = std::make_unique<Impl>(d, std::move(stages));
    }

    RayTracingPipeline::Impl::Impl(imr::Device& device, std::vector<ShaderEntryPoint*>&& stages)
        : device_(device)
    {
        // take ownership of the entry points
        for (auto* ep : stages) {
            entry_points.emplace_back(ep);  // transfer ownership (if safe)
        }

        // Step 1: Create layout (shared from any shader's reflection)
        layout = std::make_unique<PipelineLayout>(device, *entry_points[0]->_impl->reflected);

        // Step 2: Collect shader stages and groups
        std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;

        for (size_t i = 0; i < entry_points.size(); ++i) {
            auto& ep = *entry_points[i];

            VkPipelineShaderStageCreateInfo stage_info{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = ep.stage(),
                    .module = ep._impl->module.vk_shader_module(),
                    .pName = ep.name().c_str()
            };
            shader_stages.push_back(stage_info);

            VkRayTracingShaderGroupCreateInfoKHR group_info{
                .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
                    .generalShader = VK_SHADER_UNUSED_KHR,
                    .closestHitShader = VK_SHADER_UNUSED_KHR,
                    .anyHitShader = VK_SHADER_UNUSED_KHR,
                    .intersectionShader = VK_SHADER_UNUSED_KHR
            };

            switch (ep.stage()) {
                case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
                case VK_SHADER_STAGE_MISS_BIT_KHR:
                    group_info.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                    group_info.generalShader = static_cast<uint32_t>(shader_stages.size() - 1);
                    break;
                case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
                    group_info.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
                    group_info.closestHitShader = static_cast<uint32_t>(shader_stages.size() - 1);
                    break;
                default:
                    throw std::runtime_error("Unsupported shader stage in RT pipeline");
            }

            shader_groups.push_back(group_info);
        }

        // Step 3: Create the pipeline
        VkRayTracingPipelineCreateInfoKHR pipeline_info{
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
                .stageCount = static_cast<uint32_t>(shader_stages.size()),
                .pStages = shader_stages.data(),
                .groupCount = static_cast<uint32_t>(shader_groups.size()),
                .pGroups = shader_groups.data(),
                .maxPipelineRayRecursionDepth = 1,
                .layout = layout->pipeline_layout,
        };


        auto vkCreateRayTracingPipelinesKHR_fn =
            (PFN_vkCreateRayTracingPipelinesKHR) vkGetDeviceProcAddr(device.device, "vkCreateRayTracingPipelinesKHR");

        CHECK_VK_THROW(vkCreateRayTracingPipelinesKHR_fn(
                    device.device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline));

        // Step 4: SBT setup will come next
        create_shader_binding_table();
    }

    static inline size_t align_up(size_t v, size_t align) {
        return (v + align - 1) & ~(align - 1);
    }



    void RayTracingPipeline::Impl::create_shader_binding_table() {
        // 1) Query ray tracing pipeline properties
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR
        };
        VkPhysicalDeviceProperties2 props2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        props2.pNext = &rt_props;
        vkGetPhysicalDeviceProperties2(device_.physical_device, &props2);

        uint32_t handle_size = rt_props.shaderGroupHandleSize;
        uint32_t handle_alignment = rt_props.shaderGroupHandleAlignment;
        uint32_t base_alignment = rt_props.shaderGroupBaseAlignment;

        // Align handle size to base alignment
        auto align_up = [](uint32_t value, uint32_t alignment) -> uint32_t {
            return (value + alignment - 1) & ~(alignment - 1);
        };
        uint32_t stride = align_up(handle_size, base_alignment);

        // 2) Get group count from pipeline properties
        // Here, you *should* track the number of groups at pipeline creation.
        // For now, assume group count = entry_points.size()
        uint32_t group_count = static_cast<uint32_t>(entry_points.size());
        if (group_count == 0) {
            throw std::runtime_error("No shader groups found for SBT");
        }

        // 3) Load vkGetRayTracingShaderGroupHandlesKHR dynamically
        auto fpGetHandles = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(
                vkGetDeviceProcAddr(device_.device, "vkGetRayTracingShaderGroupHandlesKHR")
                );
        if (!fpGetHandles) {
            throw std::runtime_error("Failed to load vkGetRayTracingShaderGroupHandlesKHR");
        }

        // 4) Allocate storage for all shader group handles
        std::vector<uint8_t> shader_handle_storage(group_count * handle_size);

        VkResult res = fpGetHandles(
                device_.device,
                pipeline,
                0,
                group_count,
                static_cast<uint32_t>(shader_handle_storage.size()),
                shader_handle_storage.data()
                );
        if (res != VK_SUCCESS) {
            throw std::runtime_error("vkGetRayTracingShaderGroupHandlesKHR failed");
        }

        // 5) Create the SBT buffer
        size_t sbt_size = stride * group_count;

        VkBufferUsageFlags usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        VkMemoryPropertyFlags mem_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        sbt_buffer = std::make_unique<Buffer>(
                device_, sbt_size, usage, mem_flags
                );

        // 6) Map the SBT buffer memory
        void* mapped = nullptr;
        VkResult map_res = vkMapMemory(
                device_.device,
                sbt_buffer->memory,
                sbt_buffer->memory_offset,
                sbt_buffer->size,
                0,
                &mapped
                );
        if (map_res != VK_SUCCESS || !mapped) {
            throw std::runtime_error("Failed to map SBT buffer memory");
        }

        uint8_t* dst = reinterpret_cast<uint8_t*>(mapped);

        // 7) Copy all group handles consecutively, padding each to stride
        for (uint32_t i = 0; i < group_count; ++i) {
            const uint8_t* src = shader_handle_storage.data() + i * handle_size;
            memcpy(dst + i * stride, src, handle_size);
            if (stride > handle_size) {
                memset(dst + i * stride + handle_size, 0, stride - handle_size);
            }
        }

        vkUnmapMemory(device_.device, sbt_buffer->memory);

        // 8) Get device address of SBT buffer
        VkBufferDeviceAddressInfo addr_info{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        addr_info.buffer = sbt_buffer->handle;
        VkDeviceAddress sbt_addr = vkGetBufferDeviceAddress(device_.device, &addr_info);

        // 9) Set up StridedDeviceAddressRegion for whole SBT (fallback: all groups in one region)
        raygen_region = { sbt_addr, stride, stride * group_count };
        miss_region = { 0, 0, 0 };    // TODO: fill these properly later
        hit_region = { 0, 0, 0 };     // TODO: fill these properly later
        callable_region = { 0, 0, 0 }; // not used for now
    }

    RayTracingPipeline::Impl::~Impl() {
        vkDestroyPipeline(device_.device, pipeline, nullptr);
    }

}

