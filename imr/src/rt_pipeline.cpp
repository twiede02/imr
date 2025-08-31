#include "shader_private.h"

#include "imr/imr.h"
#include "imr/util.h"

namespace imr {

    RayTracingPipeline::RayTracingPipeline(Device& d) {
        _impl = std::make_unique<Impl>(d);
    }

    RayTracingPipeline::Impl::Impl(imr::Device& device)
        : device(device) {

        VkPhysicalDeviceProperties2 dont_care {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &rayTracingPipelineProperties,
        };
        vkGetPhysicalDeviceProperties2(device.physical_device, &dont_care);

        pipeline = VK_NULL_HANDLE;

        createRayTracingPipeline();
        createShaderBindingTable();
    }

    // RayTracingPipeline getters

    uint32_t RayTracingPipeline::getHandleSizeAligned() const
    {
        return (_impl->rayTracingPipelineProperties.shaderGroupHandleSize + _impl->rayTracingPipelineProperties.shaderGroupHandleAlignment - 1) & ~(_impl->rayTracingPipelineProperties.shaderGroupHandleAlignment - 1);
    }

    VkPipeline RayTracingPipeline::pipeline() const
    {
        return _impl->pipeline;
    }

    VkPipelineLayout RayTracingPipeline::layout() const {
        return _impl->layout->pipeline_layout;
    }

    Buffer* RayTracingPipeline::raygenShaderBindingTable() const {
        return _impl->raygenShaderBindingTable.get();
    }

    Buffer* RayTracingPipeline::missShaderBindingTable() const {
        return _impl->missShaderBindingTable.get();
    }

    Buffer* RayTracingPipeline::hitShaderBindingTable() const {
        return _impl->hitShaderBindingTable.get();
    }

    // private functions

    void RayTracingPipeline::Impl::createRayTracingPipeline() {
        auto& vk = device.dispatch;

        /*
           Setup ray tracing shader groups
           */
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        std::optional<ReflectedLayout> merged_layout;

        auto loadShader = [&](const char* name, VkShaderStageFlagBits stage) {
            auto& sm = *shader_modules.emplace_back(std::make_unique<imr::ShaderModule>(device, name));
            auto& ept = *entry_pts.emplace_back(std::make_unique<imr::ShaderEntryPoint>(sm, stage, "main"));
            if (!merged_layout)
                merged_layout = *ept._impl->reflected;
            else
                merged_layout = ReflectedLayout(*merged_layout, *ept._impl->reflected);

            return (VkPipelineShaderStageCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = stage,
                    .module = sm.vk_shader_module(),
                    .pName = "main"
            };
        };

        // Ray generation group
        {
            shaderStages.push_back(loadShader("raygen.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR));
            VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
            shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
            shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
            shaderGroups.push_back(shaderGroup);
        }

        // Miss group
        {
            shaderStages.push_back(loadShader("miss.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR));
            VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
            shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
            shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
            shaderGroups.push_back(shaderGroup);
        }

        // Closest hit group
        {
            shaderStages.push_back(loadShader("closesthit.rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));
            VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
            shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.closestHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
            shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
            shaderGroups.push_back(shaderGroup);
        }

        assert(merged_layout);
        layout = std::make_unique<PipelineLayout>(device, *merged_layout);
        final_layout = *merged_layout;

        /*
           Create the ray tracing pipeline
           */
        VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI{};
        rayTracingPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        rayTracingPipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
        rayTracingPipelineCI.pStages = shaderStages.data();
        rayTracingPipelineCI.groupCount = static_cast<uint32_t>(shaderGroups.size());
        rayTracingPipelineCI.pGroups = shaderGroups.data();
        rayTracingPipelineCI.maxPipelineRayRecursionDepth = 1;
        rayTracingPipelineCI.layout = layout->pipeline_layout;
        vk.createRayTracingPipelinesKHR(VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, nullptr, &pipeline);

    }

    void RayTracingPipeline::Impl::createShaderBindingTable() {
        auto& vk = device.dispatch;
        const uint32_t handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
        const uint32_t handleSizeAligned = (rayTracingPipelineProperties.shaderGroupHandleSize + rayTracingPipelineProperties.shaderGroupHandleAlignment-1) & ~(rayTracingPipelineProperties.shaderGroupHandleAlignment - 1);
        const uint32_t groupCount = static_cast<uint32_t>(shaderGroups.size());
        const uint32_t sbtSize = groupCount * handleSizeAligned;

        std::vector<uint8_t> shaderHandleStorage(sbtSize);
        vk.getRayTracingShaderGroupHandlesKHR(pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data());

        const VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        const VkMemoryPropertyFlags memoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        raygenShaderBindingTable = std::make_unique<imr::Buffer>(device, handleSize, bufferUsageFlags, memoryPropertyFlags);
        missShaderBindingTable = std::make_unique<imr::Buffer>(device, handleSize, bufferUsageFlags, memoryPropertyFlags);
        hitShaderBindingTable = std::make_unique<imr::Buffer>(device, handleSize, bufferUsageFlags, memoryPropertyFlags);

        // Copy handles
        raygenShaderBindingTable->uploadDataSync(0, handleSize, shaderHandleStorage.data());
        missShaderBindingTable->uploadDataSync(0, handleSize, shaderHandleStorage.data() + handleSizeAligned);
        hitShaderBindingTable->uploadDataSync(0, handleSize, shaderHandleStorage.data() + handleSizeAligned * 2);
    }

    RayTracingPipeline::Impl::~Impl() {
        vkDestroyPipeline(device.device, pipeline, nullptr);
    }

    RayTracingPipeline::~RayTracingPipeline() {}

}

