#ifndef HAG_H
#define HAG_H

#include "vulkan/vulkan_core.h"
#include "GLFW/glfw3.h"
#include "VkBootstrap.h"

#include <functional>
#include <memory>

#define CHECK_VK(op, else) if (op != VK_SUCCESS) { else; }

inline auto tmp(auto&& t) { return &t; }

namespace hag {
    struct Context {
        Context();
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

        VkImage make_image2d(uint32_t width, uint32_t height, VkFormat, VkImageUsageFlags usage);
    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

    struct Swapchain {
        Swapchain(Context&, GLFWwindow* window);
        ~Swapchain();

        void add_to_delete_queue(std::function<void(void)>&& fn);
        void present(VkImage image, VkFence signal_when_reusable);
    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };
}

#endif
