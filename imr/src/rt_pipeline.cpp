#include "shader_private.h"

#include "imr/imr.h"
#include "imr/util.h"
#include <cstdint>
#include <memory>
#include <shady/driver.h>
#include <vulkan/vulkan_core.h>

namespace imr {

    RayTracingPipeline::RayTracingPipeline(Device& d, std::vector<RT_Shader> s) {
        _impl = std::make_unique<Impl>(d, s);
    }

    RayTracingPipeline::Impl::Impl(imr::Device& device, std::vector<RT_Shader> s)
        : device(device), shaders(s) {

        VkPhysicalDeviceProperties2 physicalDeviceProperties2 {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &rayTracingPipelineProperties,
        };
        vkGetPhysicalDeviceProperties2(device.physical_device, &physicalDeviceProperties2);

        createRayTracingPipeline(s);
        createShaderBindingTable(s);
    }

    void RayTracingPipeline::traceRays(VkCommandBuffer c, uint16_t w, uint16_t h, uint16_t d) {
        _impl->traceRays(c, w, h, d);
    }

    void RayTracingPipeline::Impl::traceRays(VkCommandBuffer cmdbuf, uint16_t width, uint16_t height, uint16_t maxRayRecursionDepth) {

        assert(SBT.size() >= 3);
        auto& vk = device.dispatch;

        const uint32_t handleSizeAligned = (rayTracingPipelineProperties.shaderGroupHandleSize + rayTracingPipelineProperties.shaderGroupHandleAlignment - 1) & ~(rayTracingPipelineProperties.shaderGroupHandleAlignment - 1);

        VkStridedDeviceAddressRegionKHR raygenShaderSbtEntry{};
        raygenShaderSbtEntry.deviceAddress = SBT[0]->device_address();
        raygenShaderSbtEntry.stride = handleSizeAligned;
        raygenShaderSbtEntry.size = handleSizeAligned;

        VkStridedDeviceAddressRegionKHR missShaderSbtEntry{};
        missShaderSbtEntry.deviceAddress = SBT[1]->device_address();
        missShaderSbtEntry.stride = handleSizeAligned;
        missShaderSbtEntry.size = handleSizeAligned;

        VkStridedDeviceAddressRegionKHR hitShaderSbtEntry{};
        hitShaderSbtEntry.deviceAddress = SBT[2]->device_address();
        hitShaderSbtEntry.stride = handleSizeAligned;
        hitShaderSbtEntry.size = handleSizeAligned;

        VkStridedDeviceAddressRegionKHR callableShaderSbtEntry{};
        if (SBT.size() > 3) {
            callableShaderSbtEntry.deviceAddress = SBT[3]->device_address();
            callableShaderSbtEntry.stride = handleSizeAligned;
            callableShaderSbtEntry.size = handleSizeAligned;
        }

        vk.cmdTraceRaysKHR(
            cmdbuf,
            &raygenShaderSbtEntry,
            &missShaderSbtEntry,
            &hitShaderSbtEntry,
            &callableShaderSbtEntry,
            width,
            height,
            maxRayRecursionDepth);

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

    // private functions
   
    void RayTracingPipeline::Impl::createRayTracingPipeline(std::vector<RT_Shader> shader) {
        auto& vk = device.dispatch;

        /*
           Setup ray tracing shader groups
           */
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        std::optional<ReflectedLayout> merged_layout;

        auto loadShader = [&](const char* name, VkShaderStageFlagBits stage, std::string entryPoint) {
            auto& sm = *shader_modules.emplace_back(std::make_unique<imr::ShaderModule>(device, name));
            auto& ept = *entry_pts.emplace_back(std::make_unique<imr::ShaderEntryPoint>(sm, stage, entryPoint));
            if (!merged_layout)
                merged_layout = *ept._impl->reflected;
            else
                merged_layout = ReflectedLayout(*merged_layout, *ept._impl->reflected);

            return (VkPipelineShaderStageCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = stage,
                    .module = sm.vk_shader_module(),
                    .pName = entryPoint.c_str()
            };
        };

        auto matchShaderStageCreateInfoBit = [&](ShaderType t) {
            switch (t) {
                case raygen:
                    return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
                case miss:
                    return VK_SHADER_STAGE_MISS_BIT_KHR;
                case closestHit:
                    return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
                case anyHit:
                    return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
                case intersection:
                    return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
                case callable:
                    return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
                }
        };

        auto isGeneral = [&](ShaderType t) {
            return t == ShaderType::raygen || t == ShaderType::miss || t == ShaderType::callable;
        };

        for (auto t : shaders) {
            std::string filename = t.filename + ".spv";
            shaderStages.push_back(loadShader(filename.c_str(), matchShaderStageCreateInfoBit(t.type), t.entrypoint_name));
            VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
            shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            shaderGroup.type = t.type == ShaderType::closestHit || t.type == ShaderType::closestHit ? VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR : VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            shaderGroup.generalShader = isGeneral(t.type) ? static_cast<uint32_t>(shaderStages.size()) - 1 : VK_SHADER_UNUSED_KHR;
            shaderGroup.closestHitShader = t.type == ShaderType::closestHit ? static_cast<uint32_t>(shaderStages.size()) - 1 : VK_SHADER_UNUSED_KHR;
            shaderGroup.anyHitShader = t.type == ShaderType::anyHit ? static_cast<uint32_t>(shaderStages.size()) - 1 : VK_SHADER_UNUSED_KHR;
            shaderGroup.intersectionShader = t.type == ShaderType::intersection ? static_cast<uint32_t>(shaderStages.size()) - 1 : VK_SHADER_UNUSED_KHR;
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

    void RayTracingPipeline::Impl::createShaderBindingTable(std::vector<RT_Shader> shader) {
        auto& vk = device.dispatch;
        const uint32_t handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
        const uint32_t handleSizeAligned = (rayTracingPipelineProperties.shaderGroupHandleSize + rayTracingPipelineProperties.shaderGroupHandleAlignment-1) & ~(rayTracingPipelineProperties.shaderGroupHandleAlignment - 1);
        const uint32_t groupCount = static_cast<uint32_t>(shaderGroups.size());
        const uint32_t sbtSize = groupCount * handleSizeAligned;

        std::vector<uint8_t> shaderHandleStorage(sbtSize);
        vk.getRayTracingShaderGroupHandlesKHR(pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data());

        const VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        const VkMemoryPropertyFlags memoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        for (auto s : shaders) {
            SBT.push_back(std::make_unique<imr::Buffer>(device, handleSize, bufferUsageFlags, memoryPropertyFlags));
        }

        // Copy handles
        for (int i = 0; i < SBT.size(); i++) {
            SBT[i]->uploadDataSync(0, handleSize, shaderHandleStorage.data() + handleSizeAligned * i);
        }

    }

    RayTracingPipeline::Impl::~Impl() {
        vkDestroyPipeline(device.device, pipeline, nullptr);
    }

    RayTracingPipeline::~RayTracingPipeline() {}

}

