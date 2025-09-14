#include "shader_private.h"

#include "imr/imr.h"
#include "imr/util.h"
#include <cstdint>
#include <memory>
#include <shady/driver.h>
#include <vulkan/vulkan_core.h>

namespace imr {

    RayTracingPipeline::RayTracingPipeline(Device& d, ShaderEntryPoint* raygen, std::vector<HitShadersTriple> hit_shaders, std::vector<ShaderEntryPoint*> miss_shaders, std::vector<ShaderEntryPoint*> callables) {
        _impl = std::make_unique<Impl>(d, raygen, hit_shaders, miss_shaders, callables);
    }

    RayTracingPipeline::Impl::Impl(imr::Device& device, ShaderEntryPoint* raygen, std::vector<HitShadersTriple> hit_shaders, std::vector<ShaderEntryPoint*> miss_shaders, std::vector<ShaderEntryPoint*> callables)
        : device(device) {

        VkPhysicalDeviceProperties2 physicalDeviceProperties2 {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &rayTracingPipelineProperties,
        };
        vkGetPhysicalDeviceProperties2(device.physical_device, &physicalDeviceProperties2);

        createRayTracingPipeline(raygen, hit_shaders, miss_shaders, callables);
        createShaderBindingTable(raygen, hit_shaders, miss_shaders, callables);
    }

    void RayTracingPipeline::traceRays(VkCommandBuffer c, uint16_t w, uint16_t h, uint16_t d) {
        _impl->traceRays(c, w, h, d);
    }

    void RayTracingPipeline::Impl::traceRays(VkCommandBuffer cmdbuf, uint16_t width, uint16_t height, uint16_t maxRayRecursionDepth) {
        auto& vk = device.dispatch;

        const uint32_t handleSizeAligned = (rayTracingPipelineProperties.shaderGroupHandleSize + rayTracingPipelineProperties.shaderGroupHandleAlignment - 1) & ~(rayTracingPipelineProperties.shaderGroupHandleAlignment - 1);

        VkStridedDeviceAddressRegionKHR raygenShaderSbtEntry{};
        raygenShaderSbtEntry.deviceAddress = raygen_sbt->device_address();
        raygenShaderSbtEntry.stride = handleSizeAligned;
        raygenShaderSbtEntry.size = handleSizeAligned;

        VkStridedDeviceAddressRegionKHR missShaderSbtEntry{};
        missShaderSbtEntry.deviceAddress = miss_sbt->device_address();
        missShaderSbtEntry.stride = handleSizeAligned;
        missShaderSbtEntry.size = handleSizeAligned;

        VkStridedDeviceAddressRegionKHR hitShaderSbtEntry{};
        hitShaderSbtEntry.deviceAddress = hit_sbt->device_address();
        hitShaderSbtEntry.stride = handleSizeAligned;
        hitShaderSbtEntry.size = handleSizeAligned;

        VkStridedDeviceAddressRegionKHR callableShaderSbtEntry{};
        if (callable_sbt) {
            callableShaderSbtEntry.deviceAddress = callable_sbt->device_address();
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
   
    void RayTracingPipeline::Impl::createRayTracingPipeline(ShaderEntryPoint* raygen, std::vector<HitShadersTriple> hit_shaders, std::vector<ShaderEntryPoint*> miss_shaders, std::vector<ShaderEntryPoint*> callables) {
        auto& vk = device.dispatch;

        /*
           Setup ray tracing shader groups
           */
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
        std::optional<ReflectedLayout> merged_layout;

        auto prepareShaderStage = [&](ShaderEntryPoint* ept) -> int {
            if (!ept)
                return VK_SHADER_UNUSED_KHR;

            if (!merged_layout)
                merged_layout = *ept->_impl->reflected;
            else
                merged_layout = ReflectedLayout(*merged_layout, *ept->_impl->reflected);

            VkPipelineShaderStageCreateInfo stage_create_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = ept->stage(),
                .module = ept->module().vk_shader_module(),
                .pName = ept->name().c_str()
            };
            shaderStages.push_back(stage_create_info);
            return shaderStages.size() - 1;
        };

        // create raygen shader group
        VkRayTracingShaderGroupCreateInfoKHR rayGenShaderGroup = {
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
            .generalShader = prepareShaderStage(raygen),
            .closestHitShader = VK_SHADER_UNUSED_KHR,
            .anyHitShader = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR,
        };
        shaderGroups.push_back(rayGenShaderGroup);

        // create hit shader groups
        for (auto [closest, any, intersection] : hit_shaders) {
            VkRayTracingShaderGroupCreateInfoKHR rayGenShaderGroup = {
                .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
                .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
                .generalShader = VK_SHADER_UNUSED_KHR,
                .closestHitShader = prepareShaderStage(closest),
                .anyHitShader = prepareShaderStage(any),
                .intersectionShader = prepareShaderStage(intersection),
            };
            shaderGroups.push_back(rayGenShaderGroup);
        }

        // create miss shader groups
        for (auto missShader : miss_shaders) {
            VkRayTracingShaderGroupCreateInfoKHR rayGenShaderGroup = {
                .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
                .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
                .generalShader = prepareShaderStage(missShader),
                .closestHitShader = VK_SHADER_UNUSED_KHR,
                .anyHitShader = VK_SHADER_UNUSED_KHR,
                .intersectionShader = VK_SHADER_UNUSED_KHR,
            };
            shaderGroups.push_back(rayGenShaderGroup);
        }

        // create callable shader groups
        for (auto callable : callables) {
            VkRayTracingShaderGroupCreateInfoKHR rayGenShaderGroup = {
                .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
                .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
                .generalShader = prepareShaderStage(callable),
                .closestHitShader = VK_SHADER_UNUSED_KHR,
                .anyHitShader = VK_SHADER_UNUSED_KHR,
                .intersectionShader = VK_SHADER_UNUSED_KHR,
            };
            shaderGroups.push_back(rayGenShaderGroup);
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

    void RayTracingPipeline::Impl::createShaderBindingTable(ShaderEntryPoint* raygen, std::vector<HitShadersTriple> hit_shaders, std::vector<ShaderEntryPoint*> miss_shaders, std::vector<ShaderEntryPoint*> callables) {
        auto& vk = device.dispatch;
        const uint32_t handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
        const uint32_t handleSizeAligned = (rayTracingPipelineProperties.shaderGroupHandleSize + rayTracingPipelineProperties.shaderGroupHandleAlignment-1) & ~(rayTracingPipelineProperties.shaderGroupHandleAlignment - 1);
        const uint32_t groupCount = static_cast<uint32_t>(1 + hit_shaders.size() + miss_shaders.size() + callables.size());
        const uint32_t sbtSize = groupCount * handleSizeAligned;

        std::vector<uint8_t> shaderHandleStorage(sbtSize);
        vk.getRayTracingShaderGroupHandlesKHR(pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data());

        const VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        const VkMemoryPropertyFlags memoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;


        raygen_sbt = std::make_unique<imr::Buffer>(device, handleSize, bufferUsageFlags, memoryPropertyFlags);
        hit_sbt = std::make_unique<imr::Buffer>(device, handleSize, bufferUsageFlags, memoryPropertyFlags);
        miss_sbt = std::make_unique<imr::Buffer>(device, handleSize, bufferUsageFlags, memoryPropertyFlags);
        callable_sbt = std::make_unique<imr::Buffer>(device, handleSize, bufferUsageFlags, memoryPropertyFlags);

        raygen_sbt->uploadDataSync(0, handleSize, shaderHandleStorage.data());
        hit_sbt->uploadDataSync(0, handleSize, shaderHandleStorage.data() + handleSizeAligned);
        miss_sbt->uploadDataSync(0, handleSize, shaderHandleStorage.data() + handleSizeAligned * (1 + hit_shaders.size()));
        callable_sbt->uploadDataSync(0, handleSize, shaderHandleStorage.data() + handleSizeAligned * (1 + hit_shaders.size() + miss_shaders.size()));
    }

    RayTracingPipeline::Impl::~Impl() {
        vkDestroyPipeline(device.device, pipeline, nullptr);
    }

    RayTracingPipeline::~RayTracingPipeline() {}

}

