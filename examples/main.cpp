#include "hag/hag.h"
#include "hag/util.h"

#include "VkBootstrap.h"

#include <ctime>
#include <memory>

struct vec2 { float x, y; };

struct push_constants {
    vec2 tri[3] = {
        { -0.5, 0.5 },
        { 0, -0.5 },
        { 0.5, 0.5}
    };
    float time;
} push_constants;

int main() {
    hag::Context context;
    auto& vk = context.dispatch_tables.device;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 1024, "HAG", nullptr, nullptr);
    hag::Swapchain swapchain(context, window);

    VkFence last_fence;
    vkCreateFence(context.device, tmp((VkFenceCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    }), nullptr, &last_fence);

    VkFence next_fence;
    vkCreateFence(context.device, tmp((VkFenceCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = 0,
    }), nullptr, &next_fence);

    uint64_t last_epoch = shd_get_time_nano();
    int frames_since_last_epoch = 0;

    size_t spirv_bytes_count;
    uint32_t* spirv_bytes;
    if (!shd_read_file("checkerboard.spv", &spirv_bytes_count, (unsigned char**) &spirv_bytes))
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
    CHECK_VK(vkCreatePipelineLayout(context.device, tmp((VkPipelineLayoutCreateInfo) {
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
    CHECK_VK(vkCreateComputePipelines(context.device, VK_NULL_HANDLE, 1, tmp((VkComputePipelineCreateInfo) {
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
        uint64_t now = shd_get_time_nano();
        uint64_t delta = now - last_epoch;
        if (delta > 1000000000 /* 1 second */) {
            last_epoch = now;
            if (frames_since_last_epoch > 0) {
                int fps = frames_since_last_epoch;
                float avg_frametime = (delta / 1000000.0f /* scale to ms */) / frames_since_last_epoch;
                std::string str = "Fps: ";
                str.append(std::to_string(fps));
                str.append(", Avg frametime: ");
                str.append(std::to_string(avg_frametime));
                str.append("ms");
                glfwSetWindowTitle(window, str.c_str());
            }
            frames_since_last_epoch = 0;
        }

        vkWaitForFences(context.device, 1, &last_fence, VK_TRUE, UINT64_MAX);

        int width, height;
        glfwGetWindowSize(window, &width, &height);
        VkExtent3D extents = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        auto image = new hag::Image (context, VK_IMAGE_TYPE_2D, extents, VK_FORMAT_R8G8B8A8_UNORM, (VkImageUsageFlagBits) (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));

        VkCommandBuffer cmdbuf;
        vkAllocateCommandBuffers(context.device, tmp((VkCommandBufferAllocateInfo) {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = context.pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        }), &cmdbuf);

        vkBeginCommandBuffer(cmdbuf, tmp((VkCommandBufferBeginInfo) {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        }));

        VkSemaphore sem;
        vkCreateSemaphore(context.device, tmp((VkSemaphoreCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        }), nullptr, &sem);
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
                .image = image->handle,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                }
            }),
        }));

        vk.cmdClearColorImage(cmdbuf, image->handle, VK_IMAGE_LAYOUT_GENERAL, tmp((VkClearColorValue) {
            .float32 = { 1.0f, 0.0f, 0.0f, 1.0f},
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
                .image = image->handle,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                }
            }),
        }));

        VkDescriptorSet set;
        vkAllocateDescriptorSets(context.device, tmp((VkDescriptorSetAllocateInfo) {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &set0_layout,
        }), &set);

        VkImageView view;
        vkCreateImageView(context.device, tmp((VkImageViewCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image->handle,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = image->format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        }), nullptr, &view);

        vkUpdateDescriptorSets(context.device, 1, tmp((VkWriteDescriptorSet) {
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

        push_constants.time = ((shd_get_time_nano() / 1000) % 10000000000) / 1000000.0f;

        vk.cmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vk.cmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
        vk.cmdPushConstants(cmdbuf, layout, VK_SHADER_STAGE_ALL, 0, sizeof(push_constants), &push_constants);
        vk.cmdDispatch(cmdbuf, (image->size.width + 31) / 32, (image->size.height + 31) / 32, 1);
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
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .image = image->handle,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                }
            }),
        }));

        vkEndCommandBuffer(cmdbuf);
        vk.queueSubmit(context.main_queue, 1, tmp((VkSubmitInfo) {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 0,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmdbuf,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &sem,
        }), VK_NULL_HANDLE);

        // vkTransitionImageLayoutEXT(context.device, 1, tmp((VkHostImageLayoutTransitionInfoEXT) {
        //
        // }))

        swapchain.add_to_delete_queue([=, &context]() {
            delete image;
            vkDestroySemaphore(context.device, sem, nullptr);
            vkFreeDescriptorSets(context.device, pool, 1, &set);
            vkDestroyImageView(context.device, view, nullptr);
        });
        swapchain.present(image->handle, next_fence, { sem }, VK_IMAGE_LAYOUT_GENERAL, std::make_optional<VkExtent2D>(image->size.width, image->size.height));

        frames_since_last_epoch++;
        std::swap(last_fence, next_fence);
        glfwPollEvents();
    }

    vkDeviceWaitIdle(context.device);

    vkDestroyFence(context.device, next_fence, nullptr);
    vkDestroyFence(context.device, last_fence, nullptr);

    return 0;
}
