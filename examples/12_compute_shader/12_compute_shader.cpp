#include "imr/imr.h"
#include "imr/util.h"

#include "VkBootstrap.h"

#include <ctime>
#include <memory>
#include <filesystem>

struct vec2 { float x, y; };

struct Tri { vec2 v0, v1, v2; };

struct push_constants {
    Tri tri = {
        { -0.5, 0.5 },
        { 0.5, -0.5 },
        { 0.5, 0.5}
    };
    float time;
} push_constants;

int main() {
    imr::Context context;
    imr::Device device(context);
    auto& vk = device.dispatch;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 1024, "Example", nullptr, nullptr);
    imr::Swapchain swapchain(device, window);

    imr::FpsCounter fps_counter;

    size_t spirv_bytes_count;
    uint32_t* spirv_bytes;
    if (!imr_read_file((std::filesystem::path(imr_get_executable_location()).parent_path().string() + "/12_compute_shader.spv").c_str(), &spirv_bytes_count, (unsigned char**) &spirv_bytes))
        abort();

    VkShaderModule module;
    CHECK_VK(vk.createShaderModule(tmp((VkShaderModuleCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .flags = 0,
        .codeSize = spirv_bytes_count,
        .pCode = spirv_bytes,
    }), nullptr, &module), abort());

    VkDescriptorSetLayout set0_layout;
    CHECK_VK(vkCreateDescriptorSetLayout(vk.device, tmp((VkDescriptorSetLayoutCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = tmp((VkDescriptorSetLayoutBinding) {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
        }),
    }), nullptr, &set0_layout), abort());

    VkPipelineLayout layout;
    CHECK_VK(vkCreatePipelineLayout(device.device, tmp((VkPipelineLayoutCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &set0_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = tmp((VkPushConstantRange) {
            .stageFlags = VK_SHADER_STAGE_ALL,
            .size = sizeof(push_constants),
        })
    }), nullptr, &layout), abort());

    VkPipeline pipeline;
    CHECK_VK(vkCreateComputePipelines(device.device, VK_NULL_HANDLE, 1, tmp((VkComputePipelineCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .flags = 0,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .flags = 0,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = module,
            .pName = "main",
        },
        .layout = layout,
    }), nullptr, &pipeline), abort());

    VkDescriptorPool pool;
    vkCreateDescriptorPool(vk.device, tmp((VkDescriptorPoolCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 256,
        .poolSizeCount = 1,
        .pPoolSizes = tmp((VkDescriptorPoolSize) {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 256,
        }),
    }), nullptr, &pool);

    while (!glfwWindowShouldClose(window)) {
        uint64_t now = imr_get_time_nano();
        fps_counter.tick();
        fps_counter.updateGlfwWindowTitle(window);

        swapchain.beginFrame([&](imr::Swapchain::Frame& frame) {
            auto image = frame.swapchain_image;

            VkImageView view;
            vkCreateImageView(device.device, tmp((VkImageViewCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = swapchain.format(),
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            }), nullptr, &view);

            VkDescriptorSet set;
            vkAllocateDescriptorSets(device.device, tmp((VkDescriptorSetAllocateInfo) {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &set0_layout,
            }), &set);

            vkUpdateDescriptorSets(device.device, 1, tmp((VkWriteDescriptorSet) {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = set,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = tmp((VkDescriptorImageInfo) {
                    .sampler = VK_NULL_HANDLE,
                    .imageView = view,
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                }),
            }), 0, nullptr);

            VkCommandBuffer cmdbuf;
            vkAllocateCommandBuffers(device.device, tmp((VkCommandBufferAllocateInfo) {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = device.pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            }), &cmdbuf);

            vkBeginCommandBuffer(cmdbuf, tmp((VkCommandBufferBeginInfo) {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            }));

            vk.cmdPipelineBarrier2KHR(cmdbuf, tmp((VkDependencyInfo) {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .dependencyFlags = 0,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = tmp((VkImageMemoryBarrier2) {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                    .srcAccessMask = VK_ACCESS_2_NONE,
                    .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .image = image,
                    .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .levelCount = 1,
                        .layerCount = 1,
                    }
                }),
            }));

            vk.cmdClearColorImage(cmdbuf, image, VK_IMAGE_LAYOUT_GENERAL, tmp((VkClearColorValue) {
                .float32 = { 0.0f, 0.0f, 0.0f, 1.0f},
            }), 1, tmp((VkImageSubresourceRange) {
                .aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }));

            vk.cmdPipelineBarrier2KHR(cmdbuf, tmp((VkDependencyInfo) {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .dependencyFlags = 0,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = tmp((VkImageMemoryBarrier2) {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .image = image,
                    .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .levelCount = 1,
                        .layerCount = 1,
                    }
                }),
            }));

            push_constants.time = ((imr_get_time_nano() / 1000) % 10000000000) / 1000000.0f;

            vk.cmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vk.cmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);

            push_constants.tri = {
                { -0.5, 0.5 },
                { 0.0, -0.5 },
                { 0.5, 0.5 }
            };
            vk.cmdPushConstants(cmdbuf, layout, VK_SHADER_STAGE_ALL, 0, sizeof(push_constants), &push_constants);
            vk.cmdDispatch(cmdbuf, (frame.width + 31) / 32, (frame.height + 31) / 32, 1);

            vk.cmdPipelineBarrier2KHR(cmdbuf, tmp((VkDependencyInfo) {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .dependencyFlags = 0,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = tmp((VkImageMemoryBarrier2) {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    .image = image,
                    .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .levelCount = 1,
                        .layerCount = 1,
                    }
                }),
            }));

            vkEndCommandBuffer(cmdbuf);
            vk.queueSubmit(device.main_queue, 1, tmp((VkSubmitInfo) {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &frame.swapchain_image_available,
                .pWaitDstStageMask = tmp((VkPipelineStageFlags) { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT }),
                .commandBufferCount = 1,
                .pCommandBuffers = &cmdbuf,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &frame.signal_when_ready,
            }), VK_NULL_HANDLE);

            frame.add_to_delete_queue(std::nullopt, [=, &device]() {
                vkDestroyImageView(device.device, view, nullptr);
                vkFreeDescriptorSets(device.device, pool, 1, &set);
                vkFreeCommandBuffers(device.device, device.pool, 1, &cmdbuf);
            });

            frame.present();
        });

        glfwPollEvents();
    }

    swapchain.drain();

    vkDestroyPipeline(device.device, pipeline, nullptr);
    vkDestroyShaderModule(device.device, module, nullptr);
    vkDestroyDescriptorPool(device.device, pool, nullptr);
    vkDestroyPipelineLayout(device.device, layout, nullptr);
    vkDestroyDescriptorSetLayout(device.device, set0_layout, nullptr);

    return 0;
}
