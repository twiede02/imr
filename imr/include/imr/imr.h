#ifndef HAG_H
#define HAG_H

#include "vulkan/vulkan_core.h"
#include "GLFW/glfw3.h"
#include "VkBootstrap.h"

#include <functional>
#include <memory>
#include <optional>

#include <cstdio>

#define CHECK_VK(op, else) if (op != VK_SUCCESS) { printf("Check failed at %s\n", #op); else; }

inline auto tmp(auto&& t) { return &t; }

namespace imr {

    struct Buffer {
        VkBuffer handle;
        VkDeviceAddress device_address;

        struct Impl;
        std::unique_ptr<Impl> _impl;
    };

    struct Context {
        Context();
        Context(Context&) = delete;
        ~Context();

        VkInstance instance;
        VkPhysicalDevice physical_device;
        VkDevice device;

        VkQueue main_queue;
        uint32_t main_queue_idx;

        VkCommandPool pool;

        struct {
            vkb::InstanceDispatchTable instance;
            vkb::DispatchTable device;
        } dispatch_tables;

        class Impl;
        std::unique_ptr<Impl> _impl;
    };

    struct Image {
        VkImageType dim;
        VkExtent3D size;
        VkFormat format;
        VkImageUsageFlagBits usage;

        VkImage handle;

        Image(Context&, VkImageType dim, VkExtent3D size, VkFormat format, VkImageUsageFlagBits usage);
        Image(Image&) = delete;
        ~Image();

        struct Impl;
        std::unique_ptr<Impl> _impl;
    };

    struct ImageState {
        Image& image;
        VkImageLayout layout;
    };

    struct Swapchain {
        Swapchain(Context&, GLFWwindow* window);
        ~Swapchain();

        void add_to_delete_queue(std::function<void(void)>&& fn);
        void present(VkImage image, VkFence signal_when_reusable, std::optional<VkSemaphore> wait_for, VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL, std::optional<VkExtent2D> image_size = std::nullopt);

        class Impl;
        std::unique_ptr<Impl> _impl;
    };
}

#endif
