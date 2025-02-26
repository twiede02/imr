add_library(hag src/hag.cpp src/swapchain.cpp src/context.cpp src/vma.cpp)
target_include_directories(hag PUBLIC "include")
target_link_libraries(hag PUBLIC glfw Vulkan::Vulkan vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator)
