add_library(imr src/context.cpp src/swapchain.cpp src/buffer.cpp src/image.cpp src/vma.cpp src/util.c)
target_include_directories(imr PUBLIC "include")
target_link_libraries(imr PUBLIC glfw Vulkan::Vulkan vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator)

find_program(GLSLANG_EXE glslang glslangValidator REQUIRED)