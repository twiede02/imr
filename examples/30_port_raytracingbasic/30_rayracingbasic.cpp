#include "imr/imr.h"
#include "imr/util.h"

#include "VkBootstrap.h"

#include <ctime>
#include <memory>
#include <filesystem>
#include <cmath>
#include <iostream>
#include "nasl/nasl.h"
#include "nasl/nasl_mat.h"
#include "../common/camera.h"

#define VK_CHECK_RESULT(do) CHECK_VK((do), throw std::runtime_error(#do))

void camera_update(GLFWwindow*, CameraInput* input);

using namespace nasl;

struct Tri { vec3 v0, v1, v2; vec3 color; };

class VulkanExample
{
public:
    GLFWwindow* window;
    
    imr::Context context;
    std::unique_ptr<imr::Device> device;
    std::unique_ptr<imr::Swapchain> swapchain;

    Camera camera {
        .position = {0, 0, -2.5f},
        .rotation = {0},
        .fov = 60.0,
    };
    CameraFreelookState camera_state = {
        .fly_speed = 1.0f,
        .mouse_sensitivity = 1,
    };
    CameraInput camera_input;
    bool resized = false;
    bool viewUpdated = false;
    uint16_t width = 1024;
    uint16_t height = 1024;
    
    // Descriptor set pool
    VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };
    // Handle to the device graphics queue that command buffers are submitted to
    VkQueue queue{ VK_NULL_HANDLE };

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR  rayTracingPipelineProperties{};
    //VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};

    VkPhysicalDeviceBufferDeviceAddressFeatures enabledBufferDeviceAddresFeatures{};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR enabledRayTracingPipelineFeatures{};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR enabledAccelerationStructureFeatures{};

    std::unique_ptr<imr::AccelerationStructure> bottomLevelAS;
    std::unique_ptr<imr::AccelerationStructure> topLevelAS;

    std::unique_ptr<imr::Buffer> vertexBuffer;
    std::unique_ptr<imr::Buffer> indexBuffer;
    uint32_t indexCount;
    std::unique_ptr<imr::Buffer> transformBuffer;
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups{};
    std::unique_ptr<imr::Buffer> raygenShaderBindingTable;
    std::unique_ptr<imr::Buffer> missShaderBindingTable;
    std::unique_ptr<imr::Buffer> hitShaderBindingTable;

    struct Settings {
        /** @brief Activates validation layers (and message output) when set to true */
        bool validation = false;
        /** @brief Set to true if fullscreen mode has been requested via command line */
        bool fullscreen = false;
        /** @brief Set to true if v-sync will be forced for the swapchain */
        bool vsync = false;
        /** @brief Enable UI overlay */
        bool overlay = true;
    } settings;

    struct StorageImage {
        std::unique_ptr<imr::Image> image;
        VkImageView view;
    } storageImage;

    struct UniformData {
        mat4 viewInverse;
        mat4 projInverse;
    } uniformData;
    std::unique_ptr<imr::Buffer> ubo;

    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    VkDescriptorSet descriptorSet;
    VkDescriptorSetLayout descriptorSetLayout;

    VulkanExample() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(1024, 1024, "Example", nullptr, nullptr);

        //glfwSetKeyCallback(window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
        //  if (key == GLFW_KEY_R && (mods & GLFW_MOD_CONTROL))
        //      reload_shaders = true;
        //});

        device = std::make_unique<imr::Device>(context, [&](vkb::PhysicalDeviceSelector& selector) {
            selector.add_required_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            selector.add_required_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);

            // Required by VK_KHR_acceleration_structure
            selector.add_required_extension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
            selector.add_required_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            selector.add_required_extension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);

            // Required for VK_KHR_ray_tracing_pipeline
            selector.add_required_extension(VK_KHR_SPIRV_1_4_EXTENSION_NAME);

            // Required by VK_KHR_spirv_1_4
            selector.add_required_extension(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);

            enabledBufferDeviceAddresFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
            enabledBufferDeviceAddresFeatures.bufferDeviceAddress = VK_TRUE;
            selector.add_required_extension_features(enabledBufferDeviceAddresFeatures);

            enabledRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
            enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
            selector.add_required_extension_features(enabledRayTracingPipelineFeatures);

            enabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
            enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
            //enabledAccelerationStructureFeatures.accelerationStructureHostCommands = VK_TRUE;
            selector.add_required_extension_features(enabledAccelerationStructureFeatures);
        });
        
        swapchain = std::make_unique<imr::Swapchain>(*device, window);
        imr::FpsCounter fps_counter;

        prepare();
    }
    
    /*  
        Create a scratch buffer to hold temporary data for a ray tracing acceleration structure
    */
    std::unique_ptr<imr::Buffer> createScratchBuffer(VkDeviceSize size) {
        return std::make_unique<imr::Buffer>(*device, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    }

    /*
        Set up a storage image that the ray generation shader will be writing to
    */
    void createStorageImage() {
        storageImage.image = std::make_unique<imr::Image>(*device, VK_IMAGE_TYPE_2D, (VkExtent3D) {width, height, 1}, swapchain->format(), (VkImageUsageFlagBits) (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));

        VkImageViewCreateInfo colorImageView = VkImageViewCreateInfo();
        colorImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        colorImageView.format = swapchain->format();
        colorImageView.subresourceRange = {};
        colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorImageView.subresourceRange.baseMipLevel = 0;
        colorImageView.subresourceRange.levelCount = 1;
        colorImageView.subresourceRange.baseArrayLayer = 0;
        colorImageView.subresourceRange.layerCount = 1;
        colorImageView.image = storageImage.image->handle();
        VK_CHECK_RESULT(vkCreateImageView(device->device, &colorImageView, nullptr, &storageImage.view));
    }

    /*
        Create the geometry and prepare for building the bottom level acceleration structure
    */
    void prepareGeometry() {

        // Setup vertices for a single triangle
        struct Vertex {
            float pos[3];
        };
        std::vector<Vertex> vertices = {
            { {  1.0f,  1.0f, 0.0f } },
            { { -1.0f,  1.0f, 0.0f } },
            { {  0.0f, -1.0f, 0.0f } }
        };

        // Setup indices
        std::vector<uint32_t> indices = { 0, 1, 2 };
        indexCount = static_cast<uint32_t>(indices.size());

        // Setup identity transform matrix
        VkTransformMatrixKHR transformMatrix = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f
        };

        // Create buffers
        // For the sake of simplicity we won't stage the vertex data to the GPU memory
        // Vertex buffer
        vertexBuffer = std::make_unique<imr::Buffer>(*device, vertices.size() * sizeof(Vertex),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vertices.data());
        // Index buffer
        indexBuffer = std::make_unique<imr::Buffer>(*device, indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            indices.data());
        // Transform buffer
        transformBuffer = std::make_unique<imr::Buffer>(*device, sizeof(VkTransformMatrixKHR),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &transformMatrix);
    }

    /*
        Create the Shader Binding Tables that binds the programs and top-level acceleration structure

        SBT Layout used in this sample:

            /-----------\
            | raygen    |
            |-----------|
            | miss      |
            |-----------|
            | hit       |
            \-----------/

    */
    void createShaderBindingTable() {
        auto& vk = device->dispatch;
        const uint32_t handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
        const uint32_t handleSizeAligned = (rayTracingPipelineProperties.shaderGroupHandleSize + rayTracingPipelineProperties.shaderGroupHandleAlignment-1) & ~(rayTracingPipelineProperties.shaderGroupHandleAlignment - 1);
        const uint32_t groupCount = static_cast<uint32_t>(shaderGroups.size());
        const uint32_t sbtSize = groupCount * handleSizeAligned;

        std::vector<uint8_t> shaderHandleStorage(sbtSize);
        VK_CHECK_RESULT(vk.getRayTracingShaderGroupHandlesKHR(pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data()));

        const VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        const VkMemoryPropertyFlags memoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        raygenShaderBindingTable = std::make_unique<imr::Buffer>(*device, handleSize, bufferUsageFlags, memoryPropertyFlags);
        missShaderBindingTable = std::make_unique<imr::Buffer>(*device, handleSize, bufferUsageFlags, memoryPropertyFlags);
        hitShaderBindingTable = std::make_unique<imr::Buffer>(*device, handleSize, bufferUsageFlags, memoryPropertyFlags);

        // Copy handles
        raygenShaderBindingTable->uploadDataSync(0, handleSize, shaderHandleStorage.data());
        missShaderBindingTable->uploadDataSync(0, handleSize, shaderHandleStorage.data() + handleSizeAligned);
        hitShaderBindingTable->uploadDataSync(0, handleSize, shaderHandleStorage.data() + handleSizeAligned * 2);
    }

    /*
        Create the descriptor sets used for the ray tracing dispatch
    */
    void createDescriptorSets()
    {
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
        VK_CHECK_RESULT(vkCreateDescriptorPool(device->device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));

        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo {};
        descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.descriptorPool = descriptorPool;
        descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;
        descriptorSetAllocateInfo.descriptorSetCount = 1;
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device->device, &descriptorSetAllocateInfo, &descriptorSet));

        VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo{};
        descriptorAccelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
        auto lmao = topLevelAS->handle();
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
        vkUpdateDescriptorSets(device->device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
    }

    /*
        Create our ray tracing pipeline
    */
    void createRayTracingPipeline()
    {
        auto& vk = device->dispatch;

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
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->device, &descriptorSetlayoutCI, nullptr, &descriptorSetLayout));

        VkPipelineLayoutCreateInfo pipelineLayoutCI{};
        pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCI.setLayoutCount = 1;
        pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device->device, &pipelineLayoutCI, nullptr, &pipelineLayout));

        /*
            Setup ray tracing shader groups
        */
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

        auto loadShader = [&](const char* name, VkShaderStageFlagBits stage) {
            auto sm = new imr::ShaderModule(*device, name);
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
        rayTracingPipelineCI.layout = pipelineLayout;
        VK_CHECK_RESULT(vk.createRayTracingPipelinesKHR(VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, nullptr, &pipeline));
    }

    /*
        Create the uniform buffer used to pass matrices to the ray tracing ray generation shader
    */
    void createUniformBuffer() {
        ubo = std::make_unique<imr::Buffer>(*device, sizeof(uniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }
    
    void draw(imr::Swapchain::SimplifiedRenderContext& context) {
        auto& vk = device->dispatch;

        auto& image = context.image();
        auto cmdbuf = context.cmdbuf();

        VkCommandBufferBeginInfo cmdBufInfo {};
        cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        
        /*
            Setup the buffer regions pointing to the shaders in our shader binding table
        */

        const uint32_t handleSizeAligned = (rayTracingPipelineProperties.shaderGroupHandleSize + rayTracingPipelineProperties.shaderGroupHandleAlignment - 1) & ~(rayTracingPipelineProperties.shaderGroupHandleAlignment - 1);

        VkStridedDeviceAddressRegionKHR raygenShaderSbtEntry{};
        raygenShaderSbtEntry.deviceAddress = raygenShaderBindingTable->device_address();
        raygenShaderSbtEntry.stride = handleSizeAligned;
        raygenShaderSbtEntry.size = handleSizeAligned;

        VkStridedDeviceAddressRegionKHR missShaderSbtEntry{};
        missShaderSbtEntry.deviceAddress = missShaderBindingTable->device_address();
        missShaderSbtEntry.stride = handleSizeAligned;
        missShaderSbtEntry.size = handleSizeAligned;

        VkStridedDeviceAddressRegionKHR hitShaderSbtEntry{};
        hitShaderSbtEntry.deviceAddress = hitShaderBindingTable->device_address();
        hitShaderSbtEntry.stride = handleSizeAligned;
        hitShaderSbtEntry.size = handleSizeAligned;

        VkStridedDeviceAddressRegionKHR callableShaderSbtEntry{};

        /*
            Dispatch the ray tracing commands
        */
        vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);
        vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout, 0, 1, &descriptorSet, 0, 0);

        auto setImageLayout = [&](imr::Image& image, VkImageLayout new_layout, VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED) {
            vkCmdPipelineBarrier2(cmdbuf, tmpPtr((VkDependencyInfo) {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .dependencyFlags = 0,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = tmpPtr((VkImageMemoryBarrier2) {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                    .dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                    .oldLayout = old_layout,
                    .newLayout = new_layout,
                    .image = image.handle(),
                    .subresourceRange = image.whole_image_subresource_range()
                })
            }));
        };


        // Transition ray tracing output image back to general layout
        setImageLayout(
            *storageImage.image,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        vk.cmdTraceRaysKHR(
            cmdbuf,
            &raygenShaderSbtEntry,
            &missShaderSbtEntry,
            &hitShaderSbtEntry,
            &callableShaderSbtEntry,
            width,
            height,
            1);

        /*
            Copy ray tracing output to swap chain image
        */

        // Prepare current swap chain image as transfer destination
        setImageLayout(context.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Prepare ray tracing output image as transfer source
        setImageLayout(*storageImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

        VkImageCopy copyRegion{};
        copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.srcOffset = { 0, 0, 0 };
        copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.dstOffset = { 0, 0, 0 };
        copyRegion.extent = { width, height, 1 };
        vkCmdCopyImage(cmdbuf, storageImage.image->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        // Transition swap chain image back for presentation
        setImageLayout(
            context.image(),
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    }

    void updateUniformBuffers() {
        uniformData.projInverse = invert_mat4(camera_get_proj_mat4(&camera, width, height));
        uniformData.viewInverse = invert_mat4(camera_get_pure_view_mat4(&camera));
        ubo->uploadDataSync(0, sizeof(uniformData), &uniformData);
    }

    void prepare() {
        // Get ray tracing pipeline properties, which will be used later on in the sample
        rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        VkPhysicalDeviceProperties2 deviceProperties2{};
        deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        deviceProperties2.pNext = &rayTracingPipelineProperties;
        vkGetPhysicalDeviceProperties2(device->physical_device, &deviceProperties2);

        // Create the acceleration structures used to render the ray traced scene
        prepareGeometry();
        bottomLevelAS = std::make_unique<imr::AccelerationStructure>(*device);
        bottomLevelAS->createBottomLevelAccelerationStructure(*device, *vertexBuffer, *indexBuffer, *transformBuffer);
        topLevelAS = std::make_unique<imr::AccelerationStructure>(*device);

        std::vector<std::tuple<VkTransformMatrixKHR, imr::AccelerationStructure*>> instances;
        for(int i = 0; i<3; i++){
            VkTransformMatrixKHR transformMatrix = {
                1.0f, 0.0f, 0.0f, i * 6.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
            };
            instances.emplace_back(transformMatrix, &*bottomLevelAS);
        }
        topLevelAS->createTopLevelAccelerationStructure(*device, instances);

        createStorageImage();
        createUniformBuffer();
        createRayTracingPipeline();
        createShaderBindingTable();
        createDescriptorSets();
    }

    void have() {
        imr::FpsCounter fps_counter;

        auto prev_frame = imr_get_time_nano();
        float delta = 0;
        
        while (!glfwWindowShouldClose(window)) {
            fps_counter.tick();
            fps_counter.updateGlfwWindowTitle(window);

            updateUniformBuffers();

            swapchain->renderFrameSimplified([&](imr::Swapchain::SimplifiedRenderContext& context) {
                camera_update(window, &camera_input);
                camera_move_freelook(&camera, &camera_input, &camera_state, delta);

                draw(context);
                
                auto now = imr_get_time_nano();
                delta = ((float) ((now - prev_frame) / 1000L)) / 1000000.0f;
                prev_frame = now;

                glfwPollEvents();
            });
        }

        swapchain->drain();
    }
};

int main() {
    VulkanExample sex{};
    sex.have();
    return 0;
}
