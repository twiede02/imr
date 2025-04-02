#include "imr_private.h"

#include "VkBootstrap.h"

#include <functional>
#include <vector>

namespace imr {

#define CHECK_VK_THROW(do) CHECK_VK(do, throw std::exception())

struct SwapchainSlot;

struct Swapchain::Impl {
    Context& context;
    GLFWwindow* window = nullptr;
    VkSurfaceKHR surface;
    vkb::Swapchain swapchain;

    std::vector<std::unique_ptr<SwapchainSlot>> slots;

    //std::vector<std::unique_ptr<Frame>> prev_frames;
};

struct SwapchainSlot {
    Swapchain& swapchain;
    SwapchainSlot(Swapchain& s) : swapchain(s) {
        auto& context = s._impl->context;
        auto& vk = context.dispatch_tables.device;

        CHECK_VK_THROW(vkCreateSemaphore(context.device, tmp((VkSemaphoreCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        }), nullptr, &copy_done));

        vk.setDebugUtilsObjectNameEXT(tmp((VkDebugUtilsObjectNameInfoEXT) {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .objectType = VK_OBJECT_TYPE_SEMAPHORE,
            .objectHandle = reinterpret_cast<uint64_t>(copy_done),
            .pObjectName = "SwapchainSlot::copy_done"
        }));

        CHECK_VK_THROW(vkCreateFence(context.device, tmp((VkFenceCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        }), nullptr, &wait_for_previous_present));
    }
    SwapchainSlot(SwapchainSlot&) = delete;

    VkImage image;
    uint32_t image_index;

    VkSemaphore copy_done;
    VkFence wait_for_previous_present = VK_NULL_HANDLE;

    std::unique_ptr<Swapchain::Frame> frame = nullptr;

    ~SwapchainSlot();
};

SwapchainSlot::~SwapchainSlot() {
    auto& context = swapchain._impl->context;
    vkDestroySemaphore(context.device, copy_done, nullptr);
    vkDestroyFence(context.device, wait_for_previous_present, nullptr);
}

struct Swapchain::Frame::Impl {
    Context& context;
    SwapchainSlot& slot;

    std::vector<VkFence> cleanup_fences;
    std::vector<std::function<void(void)>> cleanup_queue;
};

Swapchain::Swapchain(Context& context, GLFWwindow* window) {
    auto& vk = context.dispatch_tables.device;

    _impl = std::make_unique<Swapchain::Impl>(context);
    CHECK_VK_THROW(glfwCreateWindowSurface(context.instance, window, nullptr, &_impl->surface));

    uint32_t surface_formats_count;
    CHECK_VK_THROW(vkGetPhysicalDeviceSurfaceFormatsKHR(context.physical_device, _impl->surface, &surface_formats_count, nullptr));

    std::vector<VkSurfaceFormatKHR> formats;
    formats.resize(surface_formats_count);
    CHECK_VK_THROW(vkGetPhysicalDeviceSurfaceFormatsKHR(context.physical_device, _impl->surface, &surface_formats_count, formats.data()));

    std::optional<VkSurfaceFormatKHR> preferred;
    for (auto format : formats) {
        if (format.format == VK_FORMAT_R8G8B8A8_UNORM)
            preferred = format;
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM)
            preferred = format;
    }

    if (!preferred) {
        fprintf(stderr, "Swapchain format is not 8-bit RGBA or BGRA");
    }

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    auto builder = vkb::SwapchainBuilder(context.physical_device, context.device, _impl->surface);
    builder.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    builder.set_desired_extent(width, height);
    builder.set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR);
    if (preferred)
        builder.set_desired_format(*preferred);

    if (auto built = builder.build(); built.has_value()) {
        _impl->swapchain = built.value();
    } else {
        fprintf(stderr, "Failed to build a swapchain (size=%d,%d, error=%d).\n", width, height, built.vk_result());
        throw std::exception();
    }

    format = _impl->swapchain.image_format;

    for (int i = 0; i < _impl->swapchain.image_count; i++) {
        _impl->slots.emplace_back(std::make_unique<SwapchainSlot>(*this));
    }
}

/// Acquires the next image
static std::tuple<SwapchainSlot&, VkSemaphore> nextSwapchainSlot(Swapchain::Impl* _impl) {
    auto& context = _impl->context;
    auto& vk = context.dispatch_tables.device;

    uint32_t image_index;

    VkSemaphore image_acquired_semaphore;
    CHECK_VK_THROW(vkCreateSemaphore(context.device, tmp((VkSemaphoreCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    }), nullptr, &image_acquired_semaphore));

    vk.setDebugUtilsObjectNameEXT(tmp((VkDebugUtilsObjectNameInfoEXT) {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = VK_OBJECT_TYPE_SEMAPHORE,
        .objectHandle = reinterpret_cast<uint64_t>(image_acquired_semaphore),
        .pObjectName = "SwapchainSlot::image_acquired"
    }));

    VkResult acquire_result = context.dispatch_tables.device.acquireNextImageKHR(_impl->swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &image_index);
    switch (acquire_result) {
        case VK_SUCCESS:
        case VK_SUBOPTIMAL_KHR: break;
        case VK_ERROR_OUT_OF_DATE_KHR: {
            fprintf(stderr, "Acquire failed. We need to resize!\n");
            throw std::exception();
        }
        default:
            fprintf(stderr, "Acquire result was: %d\n", acquire_result);
            throw std::exception();
    }

    // We know the next image !
    SwapchainSlot& slot = *_impl->slots[image_index];
    printf("Image acquired: %d\n", image_index);
    slot.image = _impl->swapchain.get_images().value()[image_index];
    slot.image_index = image_index;

    // let's recycle the resources that last slot used...
    // First make sure the _previous_ present is finished.
    // We could also set and wait on an acquire fence, but the validation layers are apparently not convinced this is sufficiently safe...
    CHECK_VK_THROW(vkWaitForFences(context.device, 1, &slot.wait_for_previous_present, true, UINT64_MAX));
    printf("Waited for %llx\n", (uint64_t) slot.wait_for_previous_present);

    slot.frame.reset();

    return std::tie<SwapchainSlot&, VkSemaphore>(slot, image_acquired_semaphore);
}

void Swapchain::Frame::add_to_delete_queue(std::optional<VkFence> fence, std::function<void()>&& fn) {
    if (fence)
        _impl->cleanup_fences.push_back(*fence);
    _impl->cleanup_queue.push_back(std::move(fn));
}

Swapchain::Frame::Frame(Impl&& impl) {
    _impl = std::make_unique<Frame::Impl>(impl);
    swapchain_image = _impl->slot.image;
}

void Swapchain::beginFrame(std::function<void(Swapchain::Frame&)>&& fn) {
    auto& context = _impl->context;
    auto [slot, acquired] = nextSwapchainSlot(&*_impl);
    slot.frame = std::make_unique<Frame>(std::move(Frame::Impl(context, slot)));
    slot.frame->swapchain_image_available = acquired;
    assert(acquired);
    slot.frame->_impl->cleanup_queue.emplace_back([=, &context]() {
        vkDestroySemaphore(context.device, acquired, nullptr);
    });

    fn(*slot.frame);
}

Swapchain::Frame::~Frame() {
    printf("Recycling frame in slot %d\n", _impl->slot.image_index);
    // Before we can cleanup the resources we need to wait on the relevant fences
    // for now let's just wait on ALL of them at once
    if (!_impl->cleanup_fences.empty()) {
        CHECK_VK_THROW(vkWaitForFences(_impl->context.device, _impl->cleanup_fences.size(), _impl->cleanup_fences.data(), true, UINT64_MAX));
        _impl->cleanup_fences.clear();
    }

    // We want to iterate over the queue in a FIFO manner
    std::reverse(_impl->cleanup_queue.begin(), _impl->cleanup_queue.end());
    for (auto& fn : _impl->cleanup_queue) {
        fn();
    }
    _impl->cleanup_queue.clear();
}

void Swapchain::Frame::presentFromBuffer(VkBuffer buffer, VkFence signal_when_reusable, std::optional<VkSemaphore> sem) {
    auto& slot = _impl->slot;
    auto& swapchain = slot.swapchain;
    auto& context = _impl->context;
    auto& vk = context.dispatch_tables.device;

    assert(signal_when_reusable != VK_NULL_HANDLE);

    std::vector<VkSemaphore> semaphores;
    semaphores.push_back(swapchain_image_available);
    if (sem)
        semaphores.push_back(*sem);

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
            .image = slot.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            }
        }),
    }));
    VkExtent2D src_size = swapchain._impl->swapchain.extent;
    vkCmdCopyBufferToImage(cmdbuf, buffer, slot.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, tmp((VkBufferImageCopy) {
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .imageExtent = {
            .width = swapchain._impl->swapchain.extent.width,
            .height = swapchain._impl->swapchain.extent.height,
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
            .image = slot.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            }
        }),
    }));

    std::vector<VkPipelineStageFlags> stage_flags;
    for (auto& sem : semaphores)
        stage_flags.emplace_back(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

    vkEndCommandBuffer(cmdbuf);
    vkQueueSubmit(context.main_queue, 1, tmp((VkSubmitInfo) {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = static_cast<uint32_t>(semaphores.size()),
        .pWaitSemaphores = semaphores.data(),
        .pWaitDstStageMask = stage_flags.data(),
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdbuf,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &slot.copy_done,
    }), signal_when_reusable);

    add_to_delete_queue(std::nullopt, [=, &context]() {
        vkFreeCommandBuffers(context.device, context.pool, 1, &cmdbuf);
    });

    present(slot.copy_done);
}

void Swapchain::Frame::presentFromImage(VkImage image, VkFence signal_when_reusable, std::optional<VkSemaphore> sem, VkImageLayout src_layout, std::optional<VkExtent2D> image_size) {
    auto& slot = _impl->slot;
    auto& swapchain = slot.swapchain;
    auto& context = _impl->context;
    auto& vk = context.dispatch_tables.device;

    std::vector<VkSemaphore> semaphores;
    semaphores.push_back(swapchain_image_available);
    if (sem)
        semaphores.push_back(*sem);

    assert(image != slot.image);
    assert(signal_when_reusable != VK_NULL_HANDLE);

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
            .image = slot.image,
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
        src_size = swapchain._impl->swapchain.extent;
    vkCmdBlitImage(cmdbuf, image, src_layout, slot.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, tmp((VkImageBlit) {
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
                .x = (int32_t) swapchain._impl->swapchain.extent.width,
                .y = (int32_t) swapchain._impl->swapchain.extent.height,
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
            .image = slot.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            }
        }),
    }));

    std::vector<VkPipelineStageFlags> stage_flags;
    for (auto& sem : semaphores)
        stage_flags.emplace_back(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

    vkEndCommandBuffer(cmdbuf);
    vkQueueSubmit(context.main_queue, 1, tmp((VkSubmitInfo) {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = static_cast<uint32_t>(semaphores.size()),
        .pWaitSemaphores = semaphores.data(),
        .pWaitDstStageMask = stage_flags.data(),
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdbuf,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &slot.copy_done,
    }), signal_when_reusable);

    // abort();

    semaphores.clear();
    semaphores.emplace_back(slot.copy_done);

    add_to_delete_queue(std::nullopt, [=, &context]() {
        vkFreeCommandBuffers(context.device, context.pool, 1, &cmdbuf);
    });

    present(slot.copy_done);
}

void Swapchain::Frame::present(std::optional<VkSemaphore> sem) {
    auto& slot = _impl->slot;
    auto& swapchain = slot.swapchain;
    auto& context = _impl->context;
    printf("Presenting in slot: %d\n", slot.image_index);

    std::vector<VkSemaphore> semaphores;
    if (sem)
        semaphores.push_back(*sem);

    CHECK_VK_THROW(vkResetFences(context.device, 1, &slot.wait_for_previous_present));
    VkSwapchainPresentFenceInfoEXT swapchain_present_fence_info_ext = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,
        .swapchainCount = 1,
        .pFences = &slot.wait_for_previous_present
    };

    VkResult present_result = vkQueuePresentKHR(context.main_queue, tmp((VkPresentInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = &swapchain_present_fence_info_ext,
        .waitSemaphoreCount = static_cast<uint32_t>(semaphores.size()),
        .pWaitSemaphores = semaphores.data(),
        .swapchainCount = 1,
        .pSwapchains = &swapchain._impl->swapchain.swapchain,
        .pImageIndices = tmp(slot.image_index),
    }));
    printf("Queued presentation, will signal %llx\n", (uint64_t) slot.wait_for_previous_present);
    switch (present_result) {
        case VK_SUCCESS:
        case VK_SUBOPTIMAL_KHR: break;
        case VK_ERROR_OUT_OF_DATE_KHR: {
            fprintf(stderr, "Present failed. We need to resize!\n");
            break;
        }
        default: throw std::exception();
    }
}

void Swapchain::drain() {
    auto& context = _impl->context;
    vkDeviceWaitIdle(context.device);

    for (auto& slot : _impl->slots)
        slot->frame.reset();
    //_impl->prev_frames.clear();
}

Swapchain::~Swapchain() {
    drain();

    auto& context = _impl->context;
    vkDeviceWaitIdle(context.device);
    VkSurfaceKHR surface = _impl->surface;

    _impl->slots.clear();

    vkb::destroy_swapchain(_impl->swapchain);
    _impl.reset();
    vkDestroySurfaceKHR(context.instance, surface, nullptr);
}

}