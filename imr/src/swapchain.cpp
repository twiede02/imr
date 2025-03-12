#include "imr_private.h"

#include "VkBootstrap.h"

#include <functional>
#include <vector>

namespace imr {

struct PerImage {
    VkSemaphore image_acquired;
    VkSemaphore ready2present;

    std::vector<std::function<void(void)>> cleanup_queue;
};

struct Swapchain::Impl {
    Context& context;
    GLFWwindow* window = nullptr;
    VkSurfaceKHR surface;
    vkb::Swapchain swapchain;

    std::vector<PerImage> in_flight;
    int in_flight_counter;
};

#define CHECK_VK_THROW(do) CHECK_VK(do, throw std::exception())

Swapchain::Swapchain(Context& context, GLFWwindow* window) {
    _impl = std::make_unique<Swapchain::Impl>(context);
    CHECK_VK_THROW(glfwCreateWindowSurface(context.instance, window, nullptr, &_impl->surface));

    uint32_t surface_formats_count;
    CHECK_VK_THROW(vkGetPhysicalDeviceSurfaceFormatsKHR(context.physical_device, _impl->surface, &surface_formats_count, nullptr));

    std::vector<VkSurfaceFormatKHR> formats;
    formats.resize(surface_formats_count);
    CHECK_VK_THROW(vkGetPhysicalDeviceSurfaceFormatsKHR(context.physical_device, _impl->surface, &surface_formats_count, formats.data()));

    std::optional<VkSurfaceFormatKHR> preferred;
    for (auto format : formats) {
        if (format.format == VK_FORMAT_R8G8B8A8_SRGB)
            preferred = format;
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB)
            preferred = format;
    }

    if (!preferred) {
        fprintf(stderr, "Swapchain format is not 8-bit RGBA or BGRA");
    }

    if (auto built = vkb::SwapchainBuilder(context.physical_device, context.device, _impl->surface)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .set_desired_format(*preferred)
        .build(); built.has_value())
    {
        _impl->swapchain = built.value();
    } else { throw std::exception(); }

    format = _impl->swapchain.image_format;

    _impl->in_flight.resize(_impl->swapchain.image_count);
    for (int i = 0; i < _impl->swapchain.image_count; i++) {
        CHECK_VK_THROW(vkCreateSemaphore(context.device, tmp((VkSemaphoreCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        }), nullptr, &_impl->in_flight[i].image_acquired));

        CHECK_VK_THROW(vkCreateSemaphore(context.device, tmp((VkSemaphoreCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        }), nullptr, &_impl->in_flight[i].ready2present));
    }
}

void Swapchain::add_to_delete_queue(std::function<void()>&& fn) {
    auto& per_image = _impl->in_flight[_impl->in_flight_counter];
    per_image.cleanup_queue.push_back(std::move(fn));
}

void Swapchain::presentFromBuffer(VkBuffer buffer, VkFence signal_when_reusable, std::optional<VkSemaphore> sem) {
    _impl->in_flight_counter = (_impl->in_flight_counter + 1) % _impl->swapchain.image_count;

    auto& per_image = _impl->in_flight[_impl->in_flight_counter];
    auto& context = _impl->context;
    auto& vk = context.dispatch_tables.device;

    CHECK_VK_THROW(vkResetFences(context.device, 1, &signal_when_reusable));

    for (auto& fn : per_image.cleanup_queue) {
        fn();
    }
    per_image.cleanup_queue.clear();

    uint32_t image_index;
    _impl->context.dispatch_tables.device.acquireNextImageKHR(_impl->swapchain, UINT64_MAX, per_image.image_acquired, VK_NULL_HANDLE, &image_index);

    VkCommandBuffer cmdbuf;
    CHECK_VK_THROW(vkAllocateCommandBuffers(context.device, tmp((VkCommandBufferAllocateInfo) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = context.pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    }), &cmdbuf));

    CHECK_VK_THROW(vkBeginCommandBuffer(cmdbuf, tmp((VkCommandBufferBeginInfo) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    })));

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
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = _impl->swapchain.get_images().value()[image_index],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            }
        }),
    }));
    VkExtent2D src_size = _impl->swapchain.extent;
    vk.cmdCopyBufferToImage(cmdbuf, buffer,  _impl->swapchain.get_images().value()[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, tmp((VkBufferImageCopy) {
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .imageExtent = {
            .width = _impl->swapchain.extent.width,
            .height =  _impl->swapchain.extent.height,
            .depth = 1
        }
    }));
    vk.cmdPipelineBarrier2KHR(cmdbuf, tmp((VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .dependencyFlags = 0,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = tmp((VkImageMemoryBarrier2) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = _impl->swapchain.get_images().value()[image_index],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            }
        }),
    }));

    uint32_t semaphores_count = 1;
    VkSemaphore semaphores[2] = { per_image.image_acquired };
    VkPipelineStageFlags stage_flags[2] = { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
    if (sem) {
        stage_flags[semaphores_count] = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        semaphores[semaphores_count++] = *sem;
    }

    vkEndCommandBuffer(cmdbuf);
    vk.queueSubmit(context.main_queue, 1, tmp((VkSubmitInfo) {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = semaphores_count,
        .pWaitSemaphores = semaphores,
        .pWaitDstStageMask = stage_flags,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdbuf,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &per_image.ready2present,
    }), signal_when_reusable);

    add_to_delete_queue([=, &context]() {
        vkFreeCommandBuffers(context.device, context.pool, 1, &cmdbuf);
    });

    vk.queuePresentKHR(context.main_queue, tmp((VkPresentInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &per_image.ready2present,
        .swapchainCount = 1,
        .pSwapchains = &_impl->swapchain.swapchain,
        .pImageIndices = tmp(image_index),
    }));
}

void Swapchain::presentFromImage(VkImage image, VkFence signal_when_reusable, std::optional<VkSemaphore> sem, VkImageLayout src_layout, std::optional<VkExtent2D> image_size) {
    _impl->in_flight_counter = (_impl->in_flight_counter + 1) % _impl->swapchain.image_count;

    auto& per_image = _impl->in_flight[_impl->in_flight_counter];
    auto& context = _impl->context;
    auto& vk = context.dispatch_tables.device;

    vkResetFences(context.device, 1, &signal_when_reusable);

    for (auto& fn : per_image.cleanup_queue) {
        fn();
    }
    per_image.cleanup_queue.clear();

    uint32_t image_index;
    _impl->context.dispatch_tables.device.acquireNextImageKHR(_impl->swapchain, UINT64_MAX, per_image.image_acquired, VK_NULL_HANDLE, &image_index);

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
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = _impl->swapchain.get_images().value()[image_index],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            }
        }),
    }));
    VkExtent2D src_size;
    if (image_size)
        src_size = *image_size;
    else
        src_size = _impl->swapchain.extent;
    vk.cmdBlitImage(cmdbuf, image, src_layout, _impl->swapchain.get_images().value()[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, tmp((VkImageBlit) {
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .srcOffsets = {
            {},
            {
                .x = (int32_t) src_size.width,
                .y = (int32_t) src_size.height,
                .z = 1,
            }
        },
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .dstOffsets = {
            {},
            {
                .x = (int32_t) _impl->swapchain.extent.width,
                .y = (int32_t) _impl->swapchain.extent.height,
                .z = 1,
            },
        }
    }), VK_FILTER_LINEAR);
    vk.cmdPipelineBarrier2KHR(cmdbuf, tmp((VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .dependencyFlags = 0,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = tmp((VkImageMemoryBarrier2) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = _impl->swapchain.get_images().value()[image_index],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            }
        }),
    }));

    uint32_t semaphores_count = 1;
    VkSemaphore semaphores[2] = { per_image.image_acquired };
    VkPipelineStageFlags stage_flags[2] = { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
    if (sem) {
        stage_flags[semaphores_count] = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        semaphores[semaphores_count++] = *sem;
    }

    vkEndCommandBuffer(cmdbuf);
    vk.queueSubmit(context.main_queue, 1, tmp((VkSubmitInfo) {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = semaphores_count,
        .pWaitSemaphores = semaphores,
        .pWaitDstStageMask = stage_flags,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdbuf,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &per_image.ready2present,
    }), signal_when_reusable);

    add_to_delete_queue([=, &context]() {
        vkFreeCommandBuffers(context.device, context.pool, 1, &cmdbuf);
    });

    vk.queuePresentKHR(context.main_queue, tmp((VkPresentInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &per_image.ready2present,
        .swapchainCount = 1,
        .pSwapchains = &_impl->swapchain.swapchain,
        .pImageIndices = tmp(image_index),
    }));
}

Swapchain::~Swapchain() {
    auto& context = _impl->context;
    vkDeviceWaitIdle(context.device);
    VkSurfaceKHR surface = _impl->surface;

    for (auto& per_image : _impl->in_flight) {
        vkDestroySemaphore(context.device, per_image.image_acquired, nullptr);
        vkDestroySemaphore(context.device, per_image.ready2present, nullptr);

        for (auto& fn : per_image.cleanup_queue) {
            fn();
        }
        per_image.cleanup_queue.clear();
    }
    vkb::destroy_swapchain(_impl->swapchain);
    _impl.reset();
    vkDestroySurfaceKHR(context.instance, surface, nullptr);
}

}