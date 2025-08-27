#include "shader_private.h"

#include "imr/imr.h"
#include "imr/util.h"

namespace imr {

    RayTracingPipeline::RayTracingPipeline(Device& d,
            Swapchain& s, uint16_t w, uint16_t h,
            AccelerationStructure& t) {
        _impl = std::make_unique<Impl>(d, s, w, h, t);
    }

    RayTracingPipeline::Impl::Impl(imr::Device& device, Swapchain& swapchain, uint16_t width, uint16_t height, AccelerationStructure& topLevelAS)
        : device(device) {

        VkPhysicalDeviceProperties2 dont_care {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &rayTracingPipelineProperties,
        };
        vkGetPhysicalDeviceProperties2(device.physical_device, &dont_care);

        pipeline = VK_NULL_HANDLE;

        ubo = std::make_unique<imr::Buffer>(device, sizeof(uniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

        createStorageImage(swapchain, width, height);
        createRayTracingPipeline();
        createShaderBindingTable();
        createDescriptorSets(topLevelAS);
    }

    // RayTracingPipeline getters

    Buffer* RayTracingPipeline::ubo() const {
        return _impl->ubo.get();
    }

    VkPipeline* RayTracingPipeline::pipeline() const {
        return &_impl->pipeline;
    }

    VkPipelineLayout* RayTracingPipeline::layout() const {
        return &_impl->layout;
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

    VkDescriptorSet* RayTracingPipeline::descriptorSet() const {
        return &_impl->descriptorSet;
    }

    RayTracingPipeline::StorageImage* RayTracingPipeline::storageImage() const {
        return &_impl->storageImage;
    }

    // private functions

    void RayTracingPipeline::Impl::createStorageImage(Swapchain& swapchain, uint16_t width, uint16_t height) {
        storageImage.image = std::make_unique<imr::Image>(device, VK_IMAGE_TYPE_2D, (VkExtent3D) {width, height, 1}, swapchain.format(), (VkImageUsageFlagBits) (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));

        VkImageViewCreateInfo colorImageView = VkImageViewCreateInfo();
        colorImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        colorImageView.format = swapchain.format();
        colorImageView.subresourceRange = {};
        colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorImageView.subresourceRange.baseMipLevel = 0;
        colorImageView.subresourceRange.levelCount = 1;
        colorImageView.subresourceRange.baseArrayLayer = 0;
        colorImageView.subresourceRange.layerCount = 1;
        colorImageView.image = storageImage.image->handle();
        vkCreateImageView(device.device, &colorImageView, nullptr, &storageImage.view);
    }

    void RayTracingPipeline::Impl::createRayTracingPipeline() {
        auto& vk = device.dispatch;

        VkDescriptorSetLayoutBinding accelerationStructureLayoutBinding{};
        accelerationStructureLayoutBinding.binding = 0;
        accelerationStructureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        accelerationStructureLayoutBinding.descriptorCount = 1;
        accelerationStructureLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        VkDescriptorSetLayoutBinding resultImageLayoutBinding{};
        resultImageLayoutBinding.binding = 1;
        resultImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        resultImageLayoutBinding.descriptorCount = 1;
        resultImageLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        VkDescriptorSetLayoutBinding uniformBufferBinding{};
        uniformBufferBinding.binding = 2;
        uniformBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformBufferBinding.descriptorCount = 1;
        uniformBufferBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        std::vector<VkDescriptorSetLayoutBinding> bindings({
                accelerationStructureLayoutBinding,
                resultImageLayoutBinding,
                uniformBufferBinding
                });

        VkDescriptorSetLayoutCreateInfo descriptorSetlayoutCI{};
        descriptorSetlayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetlayoutCI.bindingCount = static_cast<uint32_t>(bindings.size());
        descriptorSetlayoutCI.pBindings = bindings.data();
        vkCreateDescriptorSetLayout(device.device, &descriptorSetlayoutCI, nullptr, &descriptorSetLayout);

        VkPipelineLayoutCreateInfo pipelineLayoutCI{};
        pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCI.setLayoutCount = 1;
        pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
        vkCreatePipelineLayout(device.device, &pipelineLayoutCI, nullptr, &layout);

        /*
           Setup ray tracing shader groups
           */
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

        auto loadShader = [&](const char* name, VkShaderStageFlagBits stage) {
            auto sm = new imr::ShaderModule(device, name);
            return (VkPipelineShaderStageCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,

                    .stage = stage,
                    .module = sm->vk_shader_module(),
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
        rayTracingPipelineCI.layout = layout;
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

    void RayTracingPipeline::Impl::createDescriptorSets(AccelerationStructure& topLevelAS) {
        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }
        };

        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{};
        descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCreateInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        descriptorPoolCreateInfo.pPoolSizes = poolSizes.data();
        descriptorPoolCreateInfo.maxSets = 1;
        vkCreateDescriptorPool(device.device, &descriptorPoolCreateInfo, nullptr, &descriptorPool);

        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo {};
        descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.descriptorPool = descriptorPool;
        descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;
        descriptorSetAllocateInfo.descriptorSetCount = 1;
        vkAllocateDescriptorSets(device.device, &descriptorSetAllocateInfo, &descriptorSet);

        VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo{};
        descriptorAccelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
        auto lmao = topLevelAS.handle();
        descriptorAccelerationStructureInfo.pAccelerationStructures = &lmao;

        VkWriteDescriptorSet accelerationStructureWrite{};
        accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        // The specialized acceleration structure descriptor has to be chained
        accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
        accelerationStructureWrite.dstSet = descriptorSet;
        accelerationStructureWrite.dstBinding = 0;
        accelerationStructureWrite.descriptorCount = 1;
        accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

        VkDescriptorImageInfo storageImageDescriptor{};
        storageImageDescriptor.imageView = storageImage.view;
        storageImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet resultImageWrite = {};
        resultImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        resultImageWrite.dstSet = descriptorSet;
        resultImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        resultImageWrite.dstBinding = 1;
        resultImageWrite.pImageInfo = &storageImageDescriptor;
        resultImageWrite.descriptorCount = 1;


        VkDescriptorBufferInfo dbi = {
            .buffer = ubo->handle,
            .offset = 0,
            .range = ubo->size,
        };

        VkWriteDescriptorSet uniformBufferWrite = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 2,

            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &dbi
        };

        std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
            accelerationStructureWrite,
            resultImageWrite,
            uniformBufferWrite
        };
        vkUpdateDescriptorSets(device.device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);

    }

    RayTracingPipeline::Impl::~Impl() {
        vkDestroyPipeline(device.device, pipeline, nullptr);
    }

    RayTracingPipeline::~RayTracingPipeline() {}

}

