#include "imr/imr.h"
#include "imr/util.h"

#include "VkBootstrap.h"

#include <ctime>
#include <memory>
#include <filesystem>

struct vec2 { float x, y; };

struct Tri { vec2 v0, v1, v2; };

int main() {
    imr::Context context;
    imr::Device device(context);
    auto& vk = device.dispatch;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 1024, "Example", nullptr, nullptr);
    imr::Swapchain swapchain(device, window);

    imr::FpsCounter fps_counter;

    imr::ComputeShader shader(device, "12_compute_shader.spv");

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
            auto& image = frame.image();

            VkImageView view;
            vkCreateImageView(device.device, tmp((VkImageViewCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = image.handle(),
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
                .pSetLayouts = tmp(shader.set_layout(0)),
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
                    .image = image.handle(),
                    .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .levelCount = 1,
                        .layerCount = 1,
                    }
                }),
            }));

            vk.cmdClearColorImage(cmdbuf, image.handle(), VK_IMAGE_LAYOUT_GENERAL, tmp((VkClearColorValue) {
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
                    .image = image.handle(),
                    .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .levelCount = 1,
                        .layerCount = 1,
                    }
                }),
            }));

            vk.cmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, shader.pipeline());
            vk.cmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, shader.layout(), 0, 1, &set, 0, nullptr);

            vk.cmdDispatch(cmdbuf, (image.size().width + 31) / 32, (image.size().height + 31) / 32, 1);

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
                    .image = image.handle(),
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

    vkDestroyDescriptorPool(device.device, pool, nullptr);

    return 0;
}
