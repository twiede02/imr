#include "imr/imr.h"
#include "imr/util.h"

int main() {
    glfwInit();
    glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 1024, "Example", nullptr, nullptr);

    imr::Context context;
    imr::Device device(context);
    imr::Swapchain swapchain(device, window);
    imr::FpsCounter fps_counter;

    while (!glfwWindowShouldClose(window)) {
        swapchain.renderFrameSimplified([&](imr::Swapchain::SimplifiedRenderContext& context) {
            auto& image = context.image();
            auto cmdbuf = context.cmdbuf();

            vkCmdClearColorImage(cmdbuf, image.handle(), VK_IMAGE_LAYOUT_GENERAL, tmp((VkClearColorValue) {
                .float32 = { 1.0f, 0.0f, 0.0f, 1.0f},
            }), 1, tmp(image.whole_image_subresource_range()));
        });

        fps_counter.tick();
        fps_counter.updateGlfwWindowTitle(window);
        glfwPollEvents();
    }

    vkDeviceWaitIdle(device.device);

    return 0;
}
